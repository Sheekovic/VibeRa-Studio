// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <limits>
#include <set>

static PluginParameterCatalogue parameterCatalogue(PluginHost &host, bool refresh = false)
{
    const unsigned request = refresh ? host.requestParameterCatalogue() : 0;
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    PluginParameterCatalogue catalogue;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (host.readParameterCatalogue(catalogue) && catalogue.serial >= request)
        {
            require(catalogue.error == 0, "provider native parameter catalogue error " +
                                              std::to_string(catalogue.error) + ": " +
                                              catalogue.diagnostic.toStdString());
            require(catalogue.generation > 0 && catalogue.reportedCount == int(catalogue.parameters.size()),
                    "complete versioned native catalogue");
            return catalogue;
        }
        juce::Thread::sleep(2);
    }
    require(false, "bounded parameter catalogue response");
    return {};
}
static PluginParameterResult parameterResult(PluginHost &host, unsigned serial)
{
    require(serial != 0, "parameter request accepted");
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    PluginParameterResult result;
    while (!host.readParameterResult(serial, result) && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(2);
    require(result.serial == serial, "matching parameter acknowledgement");
    return result;
}
static const PluginParameter &nativeParameter(const PluginParameterCatalogue &catalogue, std::uint32_t id)
{
    const auto it = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                 [id](const auto &p) { return p.id == id; });
    require(it != catalogue.parameters.end(), "native ID exists " + std::to_string(id));
    return *it;
}
static PluginSpec parameterSpec(const juce::File &binary)
{
    std::vector<juce::PluginDescription> descriptions;
    const auto scan = PluginCatalog::scanOne(binary, descriptions);
    require(scan.wasOk() && !descriptions.empty(),
            "isolated parameter provider scan: " + scan.getErrorMessage().toStdString());
    const auto &type = descriptions.front();
    return {1, type.name, type.createXml()->toString(), {}, type.isInstrument, false};
}
#include "PluginAutomationNativeTests.h"
#include "PluginAutomationStreamTests.h"
#include "PluginAutomationPreparationTests.h"
#include "PluginClipGraphTests.h"
#include "PluginStartupCancellationTests.h"
#include "PluginGestureNativeTests.h"
#include "PluginControlOwnershipTests.h"
#include "PluginSchemaNotificationTests.h"
#include "PluginRecoveryTests.h"
#include "PluginInitialBaselineTests.h"
// Original SDK only: nonoffline startup owns an acknowledged native capture.
// No audio packets/editor/native GUI or arbitrary provider state assumptions.
static void pluginReusableCaptureTests(const juce::File &folder, const juce::File &binary)
{
    const auto spec = parameterSpec(binary);
    require(spec.name == "VibeRa Parameter Fixture" && !spec.instrument,
            "Reusable cache assertions require the actual original SDK effect fixture");
    PluginHost host;
    require(host.start(spec, 48000, 256, false).wasOk(), "Reusable baseline live SDK worker starts");
    require(host.stateCaptureSerial() > 0 && host.recoveryState() == spec.state && spec.state.isEmpty(),
            "Live startup really captured native state but preserves supplied recovery bytes before reuse");
    const auto instance = host.runtimeIdentity();
    juce::String trace;
    const auto record = [&](const char *phase)
    {
        trace += juce::String(phase) + " serial=" + juce::String(host.stateCaptureSerial()) +
                 " revision=" + juce::String(host.parameterRevision()) + "\n";
        require(folder.getChildFile("reusable-captured-state.txt").replaceWithText(trace),
                "Retain actual capture/cache authority before assertions");
    };
    const auto reusable = [&](const juce::String &expected)
    {
        const auto serial = host.stateCaptureSerial();
        juce::String state;
        unsigned revision = std::numeric_limits<unsigned>::max();
        const bool reused = host.reuseCapturedState(state, revision);
        record("reuse");
        require(reused && state.isNotEmpty() && (expected.isEmpty() || state == expected) &&
                    revision == host.parameterRevision() && host.stateCaptureSerial() == serial &&
                    host.recoveryState() == state,
                "Reuse promotes actual acknowledged native bytes without another state request");
        return state;
    };
    const auto refused = [&]
    {
        const auto serial = host.stateCaptureSerial();
        juce::String output = "original-caller-sentinel";
        unsigned revision = 987654;
        const bool reused = host.reuseCapturedState(output, revision);
        record("refused");
        require(!reused && output == "original-caller-sentinel" && revision == 987654 &&
                    host.stateCaptureSerial() == serial,
                "Unavailable cache preserves caller outputs and never launches a hidden capture");
    };
    const auto fresh = [&]
    {
        const auto before = host.stateCaptureSerial();
        const auto serial = host.requestState();
        record("fresh-request");
        require(serial && serial == before + 1 && host.stateCaptureSerial() == serial,
                "Explicit fresh policy sends exactly one actual state request");
        juce::String oldState = "old-receipt-sentinel";
        unsigned oldRevision = 654321;
        require(!host.readState(before, oldState, &oldRevision) && oldState == "old-receipt-sentinel" &&
                    oldRevision == 654321,
                "Superseded capture cannot read mutable shared state or change caller outputs");
        refused(); // Also rejects a completed-but-unread reply; no worker timing assumption.
        juce::String state;
        unsigned revision = std::numeric_limits<unsigned>::max();
        const auto began = juce::Time::getMillisecondCounter();
        bool captured = false;
        while (!(captured = host.readState(serial, state, &revision)) &&
               juce::Time::getMillisecondCounter() - began < 10000)
            juce::Thread::sleep(2); // CONTROL only, no new audio packet.
        record("fresh-ack");
        require(captured && state.isNotEmpty() && revision == host.parameterRevision(),
                "Actual matching fresh state acknowledgement restores revision authority");
        require(reusable(state) == state, "Fresh acknowledgement revalidates exact durable cache bytes");
        return state;
    };
    const auto startupSerial = host.stateCaptureSerial();
    const auto startup = reusable({});
    require(reusable(startup) == startup && reusable(startup) == startup &&
                host.stateCaptureSerial() == startupSerial,
            "Repeated structural reuse neither consumes its receipt nor increments native state requests");
    auto catalogue = parameterCatalogue(host);
    require(nativeParameter(catalogue, 0).value == .5f,
            "Original SDK actual startup value is independently 0.5");
    const auto control = host.setParameter(0, .25f, catalogue.generation);
    require(control != 0, "Original SDK native control accepted");
    refused(); // An unread request belongs to its parameter caller even if native work already finished.
    require(parameterResult(host, control).result == 0, "Original SDK gain control actually acknowledged");
    refused(); // Changed native revision cannot reuse the startup state after receipt consumption.
    const auto edited = fresh();
    require(edited != startup, "Original SDK changed gain produces different real native state bytes");
    require(fresh() == edited, "Even an unchanged explicit fresh capture requires its own acknowledgement");
    require(parameterResult(host, host.setParameter(42, 1, catalogue.generation)).result == 0,
            "Original SDK real schema-mode change acknowledged");
    juce::Thread::sleep(60); // Existing fixture's CONTROL metadata notification interval.
    const auto changed = parameterCatalogue(host, true);
    record("schema-changed");
    require(changed.generation > catalogue.generation && !nativeParameter(changed, 0xfedcba98u).writable(),
            "Actual schema generation/capability changed; no fabricated catalogue invalidation");
    refused();
    const auto schemaState = fresh();
    require(schemaState != edited, "Fresh original SDK schema-mode capture retains its actual changed state");
    require(parameterResult(host, host.setParameter(42, 0, changed.generation)).result == 0,
            "Original SDK schema mode restored before state roundtrip");
    juce::Thread::sleep(60);
    catalogue = parameterCatalogue(host, true);
    require(catalogue.generation > changed.generation && nativeParameter(catalogue, 0xfedcba98u).writable(),
            "Actual restored schema advances generation with writable sparse target");
    refused();
    const auto saved = fresh();
    auto restoredSpec = spec;
    restoredSpec.state = saved;
    PluginHost restored;
    require(restored.start(restoredSpec, 48000, 256, false).wasOk() &&
                restored.runtimeIdentity() != instance && restored.stateCaptureSerial() > 0 &&
                restored.recoveryState() == saved,
            "Distinct live restored Host acknowledges its own startup capture and preserves supplied exact "
            "state");
    const auto restoredCatalogue = parameterCatalogue(restored);
    require(nativeParameter(restoredCatalogue, 0).value == .25f &&
                nativeParameter(restoredCatalogue, 42).value == 0 &&
                nativeParameter(restoredCatalogue, 0xfedcba98u).writable(),
            "Actual original SDK edited gain and restored schema survive new native worker startup");
    const auto restoredSerial = restored.stateCaptureSerial();
    juce::String restoredState;
    unsigned restoredRevision = std::numeric_limits<unsigned>::max();
    require(restored.reuseCapturedState(restoredState, restoredRevision) && restoredState == saved &&
                restored.recoveryState() == saved && restoredRevision == restored.parameterRevision() &&
                restored.stateCaptureSerial() == restoredSerial,
            "Original deterministic SDK restored opaque bytes remain exact on actual acknowledged reuse");
    require(host.statistics().submitted == 0 && host.statistics().processed == 0 &&
                restored.statistics().submitted == 0 && restored.statistics().processed == 0,
            "Startup/control/schema/fresh-state cache tests never advance the audio packet cursor");
    record("PASS-original-SDK-live-cache-and-restore-only");
}
static void pluginStartupControlReadinessTests(const juce::File &folder, const PluginSpec &spec)
{
    require(spec.name == "VibeRa Parameter Fixture", "startup probe uses the original SDK fixture");
    struct ScopedProbeDelay
    {
        const wchar_t *name = L"VIBERA_FIXTURE_EDITOR_PROBE_DELAY_MS";
        juce::String previous = juce::SystemStats::getEnvironmentVariable(juce::String(name), {});
        ScopedProbeDelay()
        {
            require(_wputenv_s(name, L"3000") == 0, "arm actual slow native view capability probe");
        }
        ~ScopedProbeDelay()
        {
            _wputenv_s(name, previous.toWideCharPointer());
        }
    } delay;
    PluginHost host;
    const auto began = juce::Time::getMillisecondCounterHiRes();
    require(host.start(spec, 48000, 256, true).wasOk(), "slow native probe completes worker preparation");
    const auto readyMs = juce::Time::getMillisecondCounterHiRes() - began;
    const auto pid = host.processId();
    const auto catalogue = parameterCatalogue(host);
    const auto controlBegan = juce::Time::getMillisecondCounterHiRes();
    const auto serial = host.setParameter(0, .25f, catalogue.generation);
    const auto result = parameterResult(host, serial);
    const auto controlMs = juce::Time::getMillisecondCounterHiRes() - controlBegan;
    require(folder.getChildFile("startup-control-readiness.txt")
                .replaceWithText("readyMs=" + juce::String(readyMs, 3) +
                                 "; controlMs=" + juce::String(controlMs, 3) +
                                 "; result=" + juce::String(result.result) + "\n"),
            "retain slow-probe control timing before asserting outcome");
    require(result.result == 0 && result.id == 0 && result.value == .25f,
            "first native control after readiness meets unchanged host deadline despite slow editor probe");
    require(readyMs >= 2500 && host.alive() && host.processId() == pid && host.statistics().submitted == 0,
            "native view probe belongs to startup, without audio packets or worker replacement");
}
static void isolatedParameterTests(const juce::File &folder, const juce::File &binary, bool fixture)
{
    require(folder.createDirectory().wasOk(), "parameter evidence directory");
    auto spec = parameterSpec(binary);
    if (fixture)
        pluginStartupControlReadinessTests(folder, spec);
    juce::String report;
    for (const double rate : fixture ? std::vector<double>{44100, 48000, 96000} : std::vector<double>{48000})
        for (const int frames : fixture ? std::vector<int>{64, 256, 512} : std::vector<int>{256})
        {
            PluginHost host;
            require(host.start(spec, rate, frames, true).wasOk(), "parameter worker start");
            if (fixture)
            {
                auto invalidState = spec;
                invalidState.state = "16777217.";
                PluginHost refused;
                require(refused.start(invalidState, rate, frames, true).failed() && refused.processId() == 0,
                        "oversized declared state refused before launching any native worker");
            }
            auto catalogue = parameterCatalogue(host);
            if (fixture)
            {
                const auto pid = host.processId();
                PluginParameterCatalogue inspector, preparation;
                require(host.readParameterCatalogue(inspector) && host.readParameterCatalogue(preparation) &&
                            inspector.serial == catalogue.serial &&
                            preparation.generation == catalogue.generation &&
                            inspector.parameters.size() == preparation.parameters.size(),
                        "independent metadata readers see the same snapshot without stealing responses");
                require(!host.readParameterCatalogue(inspector),
                        "unchanged reader cursor does not recopy the entire parameter catalogue");
                const auto &gain = nativeParameter(preparation, 0);
                const PluginBinding proposed{gain.id,    gain.flags, gain.steps,
                                             gain.value, gain.name,  gain.label};
                const auto admission = host.validateAutomationBindings({&proposed, 1});
                require(admission.status == PluginBindingStatus::ready &&
                            admission.generation == catalogue.generation && host.processId() == pid &&
                            host.statistics().submitted == 0,
                        "runtime target admission uses current cached metadata without DSP packets/restart");
                const auto refresh = host.requestParameterCatalogue();
                const auto refreshDeadline = juce::Time::getMillisecondCounter() + 10000;
                while (preparation.serial < refresh && juce::Time::getMillisecondCounter() < refreshDeadline)
                {
                    host.readParameterCatalogue(preparation);
                    juce::Thread::sleep(2);
                }
                require(preparation.serial >= refresh && host.readParameterCatalogue(inspector) &&
                            inspector.serial == preparation.serial,
                        "preparation draining a refresh leaves the same response available to the inspector");
            }
            std::set<std::uint32_t> unique;
            for (const auto &p : catalogue.parameters)
            {
                require(unique.insert(p.id).second, "unique native IDs independent of order");
                require((!fixture || p.name.isNotEmpty()) && p.steps >= 0,
                        "fixture names and native step metadata; provider names may be blank");
                report += juce::String(juce::int64(p.id)) + " | " + p.name +
                          " | flags=" + juce::String(juce::int64(p.flags)) +
                          " | steps=" + juce::String(p.steps) + " | unit=" + juce::String(p.unit) +
                          " | availability=" + juce::String(p.availability) + "\n";
            }
            require(
                host.setParameter(0, std::numeric_limits<float>::quiet_NaN(), catalogue.generation) == 0 &&
                    host.setParameter(0, -1, catalogue.generation) == 0 &&
                    host.setParameter(0, 2, catalogue.generation) == 0 && host.setParameter(0, .5f, 0) == 0,
                "malformed control rejected before mailbox");
            auto chosen = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                       [](const auto &p) { return p.automatable(); });
            require(chosen != catalogue.parameters.end(), "writable automatable provider parameter");
            const auto id = fixture ? 0u : chosen->id;
            auto original = nativeParameter(catalogue, id).value;
            require(folder
                        .getChildFile("catalogue-" + juce::String(int(rate)) + "-" + juce::String(frames) +
                                      ".txt")
                        .replaceWithText(report),
                    "native catalogue diagnostic retained before control");
            const auto serial = host.setParameter(id, fixture ? .25f : original, catalogue.generation);
            require(host.setParameter(id, .9f, catalogue.generation) == 0,
                    "busy control cannot overwrite pending or unread acknowledgement");
            auto ack = parameterResult(host, serial);
            if (!fixture && ack.result == 1)
            {
                report += "Initial native generation " + juce::String(catalogue.generation) +
                          " became stale during provider startup; request explicitly refused.\n";
                const auto previousGeneration = catalogue.generation;
                catalogue = parameterCatalogue(host, true);
                require(catalogue.generation > previousGeneration &&
                            nativeParameter(catalogue, id).automatable(),
                        "provider startup schema refresh preserves the requested native ID and capability");
                original = nativeParameter(catalogue, id).value;
                ack = parameterResult(host, host.setParameter(id, original, catalogue.generation));
                require(folder.getChildFile("startup-schema-refresh.txt").replaceWithText(report),
                        "stale provider generation and bounded fresh retry retained");
            }
            require(ack.result == 0 && ack.id == id &&
                        std::abs(ack.value - (fixture ? .25f : original)) < .00001f,
                    "native ID control acknowledged; id=" + std::to_string(id) +
                        ", result=" + std::to_string(ack.result) + ", requested=" +
                        std::to_string(fixture ? .25f : original) + ", actual=" + std::to_string(ack.value) +
                        ", generation=" + std::to_string(catalogue.generation));
            if (!fixture)
            {
                // Same-value native sample-time event avoids changing a user's installed provider settings.
                juce::AudioBuffer<float> block(2, frames);
                ipc::ParameterEvent event{id, frames / 2, original};
                for (int i = 0; i < 4; ++i)
                {
                    block.clear();
                    host.process(block, 0, frames, nullptr, 0, i * frames / rate, 120, true, false, 4, 4,
                                 i == 0 ? &event : nullptr, i == 0 ? 1 : 0, catalogue.generation);
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < frames; ++n)
                            require(std::isfinite(block.getSample(c, n)), "provider finite parameter PCM");
                }
                require(host.statistics().parametersApplied == 1 &&
                            host.statistics().parametersRejected == 0 && host.statistics().missed == 0,
                        "actual provider native sample-offset acceptance");
                AutomationDocument constant;
                constant.initialValues = {{{AutomationParameter::pluginNormalized, spec.id, id}, original}};
                AutomationProgram prepared;
                std::string preparationError;
                require(AutomationProgram::compile(constant, true, prepared, preparationError),
                        "actual provider constant native target prepared");
                const auto workload = PluginAutomationBlock::admit(prepared, spec.id, 2 / rate, frames);
                require(workload.result == PluginAutomationBlock::Result::ok && workload.maximumEvents == 1,
                        "actual provider constant workload admitted before event generation");
                auto generated = std::make_unique<PluginAutomationBlock>();
                require(generated->render(prepared, spec.id, 0, 2 / rate, frames, true) ==
                                PluginAutomationBlock::Result::ok &&
                            generated->events().size() == 1,
                        "actual provider native ID selected by prepared event emitter");
                const auto preparedEvents = generated->events();
                for (int i = 0; i < 2; ++i)
                {
                    block.clear();
                    host.process(block, 0, frames, nullptr, 0, (i + 4) * frames / rate, 120, true, false, 4,
                                 4, i == 0 ? preparedEvents.data() : nullptr,
                                 i == 0 ? int(preparedEvents.size()) : 0, catalogue.generation);
                }
                require(host.statistics().parametersApplied == 2 && host.statistics().parametersRejected == 0,
                        "actual provider accepted generated same-value target without changing settings");
                report += "Prepared native-ID emitter -> actual provider constant same-value event PASS; "
                          "no changing-curve audio response oracle.\n";
                // Real-provider state flush with no new audio packet after the manual edit.
                const auto drainDeadline = juce::Time::getMillisecondCounter() + 10000;
                while (host.statistics().processed != host.statistics().submitted &&
                       juce::Time::getMillisecondCounter() < drainDeadline)
                    juce::Thread::sleep(2);
                require(host.statistics().processed == host.statistics().submitted,
                        "real-provider prior audio packets drained before stopped edit");
                catalogue = parameterCatalogue(host, true);
                const auto edited = parameterResult(host, host.setParameter(id, .33f, catalogue.generation));
                require(edited.result == 0 && edited.id == id,
                        "real-provider stopped manual parameter acknowledged");
                const auto beforeState = host.statistics();
                juce::String providerState;
                const auto stateSerial = host.requestState();
                const auto stateDeadline = juce::Time::getMillisecondCounter() + 10000;
                while (!host.readState(stateSerial, providerState) &&
                       juce::Time::getMillisecondCounter() < stateDeadline)
                    juce::Thread::sleep(2);
                require(providerState.isNotEmpty() && host.statistics().submitted == beforeState.submitted &&
                            host.statistics().processed == beforeState.processed,
                        "real-provider parameter-only capture without audio packet/cursor advancement");
                auto restoredSpec = spec;
                restoredSpec.state = providerState;
                juce::MemoryBlock encodedState;
                require(encodedState.fromBase64Encoding(providerState), "provider state envelope decodes");
                if (auto xml = juce::AudioProcessor::getXmlFromBinary(encodedState.getData(),
                                                                      int(encodedState.getSize())))
                    if (auto *values = xml->getChildByName("VibeRaParameterValues"))
                        require(
                            folder.getChildFile("host-normalized-values.xml")
                                .replaceWithText(values->toString()),
                            "host normalized snapshot diagnostic retained; native opaque streams excluded");
                PluginHost restored;
                require(restored.start(restoredSpec, rate, frames, true).wasOk(),
                        "real-provider stopped state restored to a new owned worker");
                const auto restoredValues = parameterCatalogue(restored);
                require(std::abs(nativeParameter(restoredValues, id).value - edited.value) < .00001f,
                        "real-provider edited native value persists without another audio packet; id=" +
                            std::to_string(id) + ", acknowledged=" + std::to_string(edited.value) +
                            ", restored=" + std::to_string(nativeParameter(restoredValues, id).value));
                auto savedProject = Project::empty();
                auto savedPlugin = restoredSpec;
                savedPlugin.id = savedProject.allocate();
                if (savedPlugin.instrument)
                    savedProject.channels.front().instrument = savedPlugin;
                else
                    savedProject.mixer[1].effects.push_back(savedPlugin);
                Controller declarations;
                declarations.replace(savedProject);
                const auto &native = nativeParameter(restoredValues, id);
                CommandArgs binding;
                binding.target = savedPlugin.id;
                binding.binding = PluginBinding{native.id,    native.flags, native.steps,
                                                native.value, native.name,  native.label};
                require(declarations.execute("plugin.binding.declare", binding).wasOk(),
                        "actual native metadata stored through canonical declaration");
                const auto privateProject = folder.getChildFile("declared-plugin-target.vibra");
                require(saveProject(declarations.project, privateProject).wasOk(),
                        "actual provider declaration and opaque state saved locally");
                Project diskProject;
                require(loadProject(privateProject, diskProject).wasOk() &&
                            diskProject.state().isEquivalentTo(declarations.project.state()),
                        "actual provider schema9 metadata/state exact disk roundtrip");
                PluginHost diskRestored;
                require(diskRestored.start(*diskProject.plugin(savedPlugin.id), rate, frames, true).wasOk(),
                        "actual provider restored from declared-target project");
                require(std::abs(nativeParameter(parameterCatalogue(diskRestored), id).value - edited.value) <
                            .00001f,
                        "native value survives declared-target project restoration");
                const auto admissionDeadline = juce::Time::getMillisecondCounter() + 2000;
                auto admission = diskRestored.validateAutomationBindings(
                    diskProject.plugin(savedPlugin.id)->automationBindings);
                while ((admission.status == PluginBindingStatus::catalogueUnavailable ||
                        admission.status == PluginBindingStatus::staleCatalogue) &&
                       juce::Time::getMillisecondCounter() < admissionDeadline)
                {
                    parameterCatalogue(diskRestored, true);
                    admission = diskRestored.validateAutomationBindings(
                        diskProject.plugin(savedPlugin.id)->automationBindings);
                }
                require(admission.status == PluginBindingStatus::ready && admission.generation != 0,
                        "actual provider restored declarations admitted against current native capabilities");
                report += "Control-thread cached catalogue/native binding admission PASS; no playback "
                          "activation or event-workload admission claim.\n";
                report += "Schema9 declaration from actual native metadata -> project disk -> new isolated "
                          "worker state/value PASS; opaque provider project remains local, not distributable "
                          "evidence.\n";
                catalogue = parameterCatalogue(host, true);
                require(parameterResult(host, host.setParameter(id, original, catalogue.generation)).result ==
                            0,
                        "real-provider original normalized control restored in the test instance");
                catalogue = parameterCatalogue(host, true);
                providerAutomationStream(host, spec, catalogue, id, folder, rate, frames);
                report += "Actual provider transport/control PASS; same-value event, no parameter-response "
                          "audio oracle.\n";
                report += "Stopped manual native edit -> parameter-only DSP flush -> component/controller "
                          "state capture/new worker restore PASS; no added audio packets.\n";
                continue;
            }
            // A stopped edit must reach component state even if no audio packet has ever run.
            juce::String stoppedState;
            const auto stoppedRequest = host.requestState();
            const auto stoppedDeadline = juce::Time::getMillisecondCounter() + 10000;
            while (!host.readState(stoppedRequest, stoppedState) &&
                   juce::Time::getMillisecondCounter() < stoppedDeadline)
                juce::Thread::sleep(2);
            require(stoppedState.isNotEmpty() && host.statistics().submitted == 0 &&
                        host.statistics().processed == 0,
                    "stopped native control captured without audio cursor advancement");
            auto stoppedSpec = spec;
            stoppedSpec.state = stoppedState;
            PluginHost stoppedRestored;
            require(stoppedRestored.start(stoppedSpec, rate, frames, true).wasOk(),
                    "stopped parameter state restored");
            require(std::abs(nativeParameter(parameterCatalogue(stoppedRestored), 0).value - .25f) < .00001f,
                    "stopped manual value persists in component/controller state");
            juce::MemoryBlock stoppedBytes;
            require(stoppedBytes.fromBase64Encoding(stoppedState), "fixture host snapshot decodes");
            auto stoppedXml =
                juce::AudioProcessor::getXmlFromBinary(stoppedBytes.getData(), int(stoppedBytes.getSize()));
            require(stoppedXml && stoppedXml->getChildByName("VibeRaParameterValues"),
                    "versioned host control snapshot accompanies intact provider state");
            if (rate == 48000 && frames == 256)
            {
                auto invalidXml = std::make_unique<juce::XmlElement>(*stoppedXml);
                invalidXml->getChildByName("VibeRaParameterValues")
                    ->getFirstChildElement()
                    ->setAttribute("id", "1234567");
                juce::MemoryBlock invalidBytes;
                juce::AudioProcessor::copyXmlToBinary(*invalidXml, invalidBytes);
                auto invalidSpec = spec;
                invalidSpec.state = invalidBytes.toBase64Encoding();
                PluginHost invalid;
                require(invalid.start(invalidSpec, rate, frames, true).failed(),
                        "unknown saved native target rejected instead of redirected");
                auto legacyXml = std::make_unique<juce::XmlElement>(*stoppedXml);
                legacyXml->removeChildElement(legacyXml->getChildByName("VibeRaParameterValues"), true);
                juce::MemoryBlock legacyBytes;
                juce::AudioProcessor::copyXmlToBinary(*legacyXml, legacyBytes);
                auto legacySpec = spec;
                legacySpec.state = legacyBytes.toBase64Encoding();
                PluginHost legacy;
                require(legacy.start(legacySpec, rate, frames, true).wasOk(),
                        "legacy opaque provider state remains supported");
                require(std::abs(nativeParameter(parameterCatalogue(legacy), 0).value - .25f) < .00001f,
                        "legacy fixture component/controller state unchanged");
            }
            require(catalogue.parameters.size() == 6 && nativeParameter(catalogue, 0).automatable(),
                    "native zero ID valid");
            require(nativeParameter(catalogue, 0xfedcba98u).unit == 7 &&
                        nativeParameter(catalogue, 42).steps == 3,
                    "sparse ID group and discrete steps");
            require(catalogue.parameters[2].id == 42, "JUCE grouped order differs from controller ordinal");
            require(!nativeParameter(catalogue, 99).writable() && !nativeParameter(catalogue, 777).writable(),
                    "read-only and hidden flags");
            require(nativeParameter(catalogue, 4000001234u).availability == 0 &&
                        !nativeParameter(catalogue, 4000001234u).writable() &&
                        !nativeParameter(catalogue, 4000001234u).hasDefault(),
                    "unavailable sentinel preserves metadata without fabricating a usable value/default");
            for (auto unavailable : {99u, 777u, 1234567u, 4000001234u})
                require(
                    parameterResult(host, host.setParameter(unavailable, .8f, catalogue.generation)).result ==
                        2,
                    "unavailable native target explicitly rejected");
            // Force an actual metadata notification while a ready catalogue remains unread.
            host.requestParameterCatalogue();
            juce::Thread::sleep(60);
            require(parameterResult(host, host.setParameter(42, 1.f, catalogue.generation)).result == 0,
                    "fixture schema notification");
            juce::Thread::sleep(60);
            auto updated = parameterCatalogue(host, true);
            require(updated.generation > catalogue.generation &&
                        !nativeParameter(updated, 0xfedcba98u).writable(),
                    "schema generation and updated native flags");
            require(parameterResult(host, host.setParameter(0, .7f, catalogue.generation)).result == 1,
                    "stale generation rejected");
            require(parameterResult(host, host.setParameter(42, 0.f, updated.generation)).result == 0,
                    "schema flags restored");
            juce::Thread::sleep(60);
            catalogue = parameterCatalogue(host, true);
            require(nativeParameter(catalogue, 0xfedcba98u).writable(), "new schema writable again");
            require(parameterResult(host, host.setParameter(0, .2f, catalogue.generation)).result == 0,
                    "pending cached gain before timed stream");
            std::array<ipc::ParameterEvent, 7> events{{{0, 0, .7f},
                                                       {0xfedcba98u, 0, .1f},
                                                       {0, 13, .3f},
                                                       {0, 13, .4f},
                                                       {0xfedcba98u, 27, .2f},
                                                       {0, 63, .9f},
                                                       {0xfedcba98u, frames - 1, .05f}}};
            std::array<ipc::MidiEvent, 2> midi{{{9, {0xb0, 7, 127}, 3}, {40, {0xb0, 7, 0}, 3}}};
            juce::AudioBuffer<float> block(2, frames);
            auto fill = [&]
            {
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < frames; ++n)
                        block.setSample(c, n, c ? .3f : .6f);
            };
            fill();
            host.process(block, 0, frames, midi.data(), int(midi.size()), 0, 120, true, false, 4, 4,
                         events.data(), int(events.size()), catalogue.generation);
            fill();
            host.process(block, 0, frames, nullptr, 0, frames / rate, 120, true, false);
            double maximumError = 0;
            for (int n = 0; n < frames; ++n)
            {
                const float gain = n < 9 ? .7f : n < 13 ? 1.f : n < 40 ? .4f : n < 63 ? 0.f : .9f;
                const float bias = n == frames - 1 ? .05f : n < 27 ? .1f : .2f;
                for (int c = 0; c < 2; ++c)
                {
                    const double error =
                        std::abs(double(block.getSample(c, n) - ((c ? .3f : .6f) * gain + bias)));
                    maximumError = std::max(maximumError, error);
                    require(error < .000001, "native sample-offset PCM including MIDI merge/cached "
                                             "offset-zero/duplicate/latest point");
                }
            }
            juce::Thread::sleep(30);
            auto finalValues = parameterCatalogue(host, true);
            require(std::abs(nativeParameter(finalValues, 0).value - .9f) < .00001f,
                    "final merged stream echoed to catalogue");
            const auto rejected = host.statistics().parametersRejected;
            ipc::ParameterEvent invalid{0, frames, .2f};
            fill();
            host.process(block, 0, frames, nullptr, 0, 2 * frames / rate, 120, true, false, 4, 4, &invalid, 1,
                         catalogue.generation);
            fill();
            host.process(block, 0, frames, nullptr, 0, 3 * frames / rate, 120, true, false, 4, 4, nullptr,
                         ipc::parameterEvents + 1, catalogue.generation);
            const std::array<ipc::ParameterEvent, 2> malformed{{{0, 20, .1f}, {0, 10, .3f}}};
            fill();
            host.process(block, 0, frames, nullptr, 0, 4 * frames / rate, 120, true, false, 4, 4,
                         malformed.data(), 2, catalogue.generation);
            invalid = {1234567, 10, .2f};
            fill();
            host.process(block, 0, frames, nullptr, 0, 5 * frames / rate, 120, true, false, 4, 4, &invalid, 1,
                         catalogue.generation);
            fill();
            host.process(block, 0, frames, nullptr, 0, 6 * frames / rate, 120, true, false);
            require(host.statistics().parametersRejected == rejected + ipc::parameterEvents + 5 &&
                        host.statistics().parametersApplied == 7,
                    "bounded malformed batches and unknown native event explicitly rejected");
            juce::String state;
            const auto stateRequest = host.requestState();
            const auto deadline = juce::Time::getMillisecondCounter() + 10000;
            while (!host.readState(stateRequest, state) && juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep(2);
            require(state.isNotEmpty(), "timed parameter state capture");
            auto restoredSpec = spec;
            restoredSpec.state = state;
            PluginHost restored;
            require(restored.start(restoredSpec, rate, frames, true).wasOk(),
                    "parameter state restore worker");
            auto restoredCatalogue = parameterCatalogue(restored);
            require(std::abs(nativeParameter(restoredCatalogue, 0).value - .9f) < .00001f &&
                        std::abs(nativeParameter(restoredCatalogue, 0xfedcba98u).value - .05f) < .00001f,
                    "native timed values survive plugin state roundtrip");
            // Full prepared capacity, including duplicate offsets, across arbitrary callback chunks.
            std::array<ipc::ParameterEvent, ipc::parameterEvents> dense;
            std::vector<float> expected(size_t(frames), .9f);
            for (int i = 0; i < ipc::parameterEvents; ++i)
            {
                const int frame = i * frames / ipc::parameterEvents;
                const float gain = float(i % 101) / 100;
                dense[size_t(i)] = {0, frame, gain};
                expected[size_t(frame)] = gain;
            }
            float held = .9f;
            for (int n = 0; n < frames; ++n)
            {
                // Hold the last event value through any empty sample positions.
                const auto it =
                    std::find_if(dense.rbegin(), dense.rend(), [n](const auto &e) { return e.frame == n; });
                if (it != dense.rend())
                    held = it->value;
                expected[size_t(n)] = held;
            }
            fill();
            int begin = 0;
            for (const int end : {7, 23, frames})
            {
                std::array<ipc::ParameterEvent, ipc::parameterEvents> chunk;
                int count = 0;
                for (const auto &event : dense)
                    if (event.frame >= begin && event.frame < end)
                    {
                        auto local = event;
                        local.frame -= begin;
                        chunk[size_t(count++)] = local;
                    }
                host.process(block, begin, end - begin, nullptr, 0, (7 * frames + begin) / rate, 120, true,
                             false, 4, 4, chunk.data(), count, catalogue.generation);
                begin = end;
            }
            fill();
            host.process(block, 0, frames, nullptr, 0, 8 * frames / rate, 120, true, false);
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < frames; ++n)
                    require(std::abs(block.getSample(c, n) - ((c ? .3f : .6f) * expected[size_t(n)] + .05f)) <
                                .000001f,
                            "maximum-capacity native PCM across callback chunks");
            require(host.statistics().parametersApplied == 7 + ipc::parameterEvents &&
                        host.statistics().parametersRejected == rejected + ipc::parameterEvents + 5,
                    "full-capacity accepted without silent event loss");
            PluginHost live;
            require(live.start(restoredSpec, rate, frames, false).wasOk(),
                    "prepared live parameter transfer worker");
            const auto liveGeneration = parameterCatalogue(live).generation;
            callbackAllocations = 0;
            callbackDestructions = 0;
            juce::String trace = "callback,wall_ms,thread_cycles,phase,heartbeat,applied,rejected\n";
            double maximumCallbackMs = 0;
            int overruns = 0;
            for (int callback = 0; callback < 16; ++callback)
            {
                fill();
                ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore);
                const auto start = juce::Time::getHighResolutionTicks();
                callbackAllocationProbe = true;
                live.process(block, 0, frames, nullptr, 0, callback * frames / rate, 120, true, false, 4, 4,
                             dense.data(), int(dense.size()), liveGeneration);
                callbackAllocationProbe = false;
                const auto end = juce::Time::getHighResolutionTicks();
                QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter);
                const double elapsed = 1000 * juce::Time::highResolutionTicksToSeconds(end - start);
                maximumCallbackMs = std::max(maximumCallbackMs, elapsed);
                if (elapsed > frames * 1000 / rate)
                    ++overruns;
                const auto statistics = live.statistics();
                trace += juce::String(callback) + "," + juce::String(elapsed, 6) + "," +
                         juce::String(juce::int64(cyclesAfter - cyclesBefore)) + "," +
                         ipc::dspPhaseName(statistics.workerPhase) + "," +
                         juce::String(juce::int64(statistics.workerHeartbeat)) + "," +
                         juce::String(juce::int64(statistics.parametersApplied)) + "," +
                         juce::String(juce::int64(statistics.parametersRejected)) + "\n";
                juce::Thread::sleep(3);
            }
            const auto file = folder.getChildFile("parameter-callback-" + juce::String(int(rate)) + "-" +
                                                  juce::String(frames) + ".csv");
            require(file.replaceWithText(trace), "callback raw cycle/wall/context evidence retained");
            require(callbackAllocations.load() == 0 && callbackDestructions.load() == 0,
                    "live full-capacity parameter transfer has zero observed C++ allocation/destruction");
            require(overruns == 0, "live parameter callback deadline; raw trace retained on failure");
            require(live.statistics().parametersApplied == 16 * ipc::parameterEvents &&
                        live.statistics().parametersRejected == 0 && live.statistics().dropped == 0,
                    "prepared live full-capacity events completed without loss");
            report += "Fixture " + juce::String(rate) + " Hz / " + juce::String(frames) +
                      " samples PASS, max PCM error=" + juce::String(maximumError, 10) + ", applied" +
                      juce::String(7 + ipc::parameterEvents) + "/rejected" +
                      juce::String(ipc::parameterEvents + 5) + "; " + juce::String(ipc::parameterEvents) +
                      "-point split callback PCM PASS; live16 callbacks/maxMs=" +
                      juce::String(maximumCallbackMs, 6) +
                      ", zero observed C++ allocations/destructions/overruns.\n";
        }
    if (fixture)
    {
        pluginReusableCaptureTests(folder, binary);
        pluginInitialBaselineTests(folder, binary);
        pluginSchemaNotificationTests(folder, binary);
        pluginControlOwnershipTests(folder, binary);
        pluginRecoveryTests(folder, binary);
        nativeEditorGestureTests(folder, binary);
        pluginStartupCancellationTests(folder, binary);
        nativeAutomationPacketSafetyTests(folder, binary);
        nativeDenseAutomationTests(folder, binary);
        nativeAutomationCurveTests(folder, binary);
        nativeAutomationStreamTests(folder, binary);
        nativeAutomationPreparationTests(folder, binary);
    }
    nativeClipGraphTests(folder, binary, fixture);
    require(folder.getChildFile("parameters.txt").replaceWithText(report), "parameter report saved");
    std::cout << "PASS isolated parameters " << assertions << " assertions" << std::endl;
}
