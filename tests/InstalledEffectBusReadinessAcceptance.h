// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginCatalog.h"
#include "PluginStateEncoding.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Source-only generic provider probe. Include after require/dispatchMessages and
// JUCE crypto helpers. No fixture-name DSP oracle, installed-directory scan,
// parameter writes, editor requests, audio hardware or production policy changes.
namespace installed_effect_bus
{
using Object = juce::DynamicObject;
constexpr int rate = 48000, quantum = 256, blocks = 32;
inline bool pathEqual(const juce::File &a, const juce::File &b)
{
    return a.getFullPathName().equalsIgnoreCase(b.getFullPathName());
}
inline juce::PluginDescription descriptor(const juce::File &binary, const juce::File &metadata)
{
    auto xml = juce::XmlDocument::parse(metadata);
    require(bool(xml), "Explicit descriptor/catalogue parses; no fallback filesystem discovery");
    std::vector<juce::PluginDescription> candidates;
    if (xml->hasTagName("VIBERA_PLUGINS"))
    {
        require(xml->getIntAttribute("version") == 1, "Authoritative cached-catalogue version");
        // Existing read-only catalogue constructor/loadFromXml semantics. Never
        // call scan(), scanOne(), save() or mutate a user's blacklist/settings.
        PluginCatalog catalogue(metadata, {}, false);
        candidates = catalogue.snapshot().plugins;
    }
    else
    {
        juce::PluginDescription value;
        require(value.loadFromXml(*xml), "JUCE checked explicit plugin descriptor");
        candidates.push_back(value);
    }
    std::optional<juce::PluginDescription> selected;
    for (const auto &candidate : candidates)
    {
        if (candidate.pluginFormatName != "VST3" || candidate.isInstrument ||
            !juce::File::isAbsolutePath(candidate.fileOrIdentifier) ||
            !pathEqual(juce::File(candidate.fileOrIdentifier), binary))
            continue;
        require(!selected,
                "Ambiguous cached class: supply one explicit descriptor, never choose first silently");
        selected = candidate;
    }
    require(selected.has_value(),
            "Exact explicit absolute VST3 EFFECT descriptor exists in supplied metadata");
    return *selected;
}
inline juce::File nativeImage(const juce::File &binary)
{
    if (binary.existsAsFile())
        return binary;
    // Deterministic known package entry candidates, no recursive plugin discovery.
    const auto conventional = binary.getChildFile("Contents/x86_64-win").getChildFile(binary.getFileName());
    const auto legacy = binary.getChildFile(binary.getFileName());
    const bool a = conventional.existsAsFile(), b = legacy.existsAsFile();
    require(a != b,
            "Exactly one known native VST3 package entry; unknown/ambiguous image is explicit refusal");
    return a ? conventional : legacy;
}
inline float signal(int channel, int frame, bool detector)
{
    const int stride = detector ? (channel ? 23 : 29) : (channel ? 17 : 13);
    return float((frame * stride + (detector ? 11 : 7)) % 127 - 63) / (detector ? 384.f : 512.f);
}
inline void checkpoint(const juce::File &folder, const juce::var &report)
{
    require(folder.getChildFile("verification.json").replaceWithText(juce::JSON::toString(report, true)),
            "Retain real provider outcome before assertions");
}
inline juce::var probe(const juce::File &folder, const PluginSpec &spec, PluginBusRequest requested,
                       bool offline)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh per-request provider worker evidence");
    juce::var report(new Object());
    auto &r = *report.getDynamicObject();
    r.setProperty("finished", false);
    r.setProperty("accepted", false);
    r.setProperty("readinessPassed", false);
    r.setProperty("nativeExactPcmQualified", false);
    r.setProperty("detectorResponseQualified", false);
    r.setProperty("detectorNoLeakQualified", false);
    r.setProperty("routePdcQualified", false);
    r.setProperty("dynamicLatencyFidelityQualified", false);
    r.setProperty("hardware", false);
    r.setProperty("listening", false);
    r.setProperty("editorRequested", false);
    r.setProperty("physicalCallbackQualified", false);
    r.setProperty("callbackAllocationQualified", false);
    r.setProperty("offline", offline);
    r.setProperty("policy", "default1Q");
    r.setProperty("rate", rate);
    r.setProperty("quantum", quantum);
    r.setProperty("requestedAuxBus", requested.auxiliaryInputBus);
    r.setProperty("requestedAuxChannels", requested.auxiliaryChannels);
    r.setProperty("busSupport", "unknown");
    r.setProperty("outcome", "started");
    checkpoint(folder, report);
    const auto began = juce::Time::getMillisecondCounterHiRes();
    try
    {
        PluginHost host; // Same production default transport in live and offline.
        const auto initial = juce::Time::getMillisecondCounter();
        const auto opened = host.start(
            spec, rate, quantum, offline,
            [initial] { return juce::Time::getMillisecondCounter() - initial >= 15000; }, requested);
        r.setProperty("startMs", juce::Time::getMillisecondCounterHiRes() - began);
        r.setProperty("workerPid", juce::int64(host.processId()));
        r.setProperty("instance", host.runtimeIdentity());
        if (opened.failed())
        {
            r.setProperty("outcome", "startupRefused");
            r.setProperty("error", opened.getErrorMessage());
            // A layout refusal, missing dependency, crash or licence failure is
            // not enough to infer the plugin's actual aux support. Keep raw reason.
            r.setProperty("busSupport", "unknownStartupRefused");
        }
        else
        {
            r.setProperty("accepted", true);
            r.setProperty("busSupport", "negotiated");
            const auto actual = host.busLayout();
            r.setProperty("actualAuxBus", actual.auxiliaryInputBus);
            r.setProperty("actualAuxChannels", actual.auxiliaryChannels);
            r.setProperty("mainStereoValidatedByWorker", true);
            const auto pid = host.processId();
            const auto instance = host.runtimeIdentity();
            const auto reset = host.midiResetGeneration();
            const auto declaration = host.declaredLatency();
            require(actual == requested && host.transportPacketCount() == 1 &&
                        host.transportLatencySamples() == 256 && declaration.valid(quantum) &&
                        !host.consumedLatency(),
                    "Actual validated requested layout/default transport/startup receipt absence");
            r.setProperty("initialDeclaredNative", declaration.nativeSamples);
            r.setProperty("initialDeclaredGeneration", juce::int64(declaration.generation));
            r.setProperty("initialConsumed", juce::var());
            // Host::start prepares its dry ring at initial native latency even
            // though no wet packet has yet been consumed. Never label that a receipt.
            r.setProperty("initialPreparedDryDelay", 256 + declaration.nativeSamples);
            juce::AudioBuffer<float> audio(2, quantum),
                auxiliary(std::max(1, requested.auxiliaryChannels), quantum), captured(2, blocks * quantum);
            captured.clear();
            juce::String csv =
                "block,firstFrame,declaredNative,declaredGeneration,consumedSequence,consumedNative,"
                "consumedGeneration,submitted,processed,missed,dropped,bypass,peakL,peakR,hostMs\n";
            double peak = 0, controlWaitMs = 0;
            bool finite = true, declarationsChanged = false;
            std::uint32_t previousGeneration = declaration.generation;
            for (int block = 0; block < blocks; ++block)
            {
                require(
                    juce::Time::getMillisecondCounterHiRes() - began < 45000,
                    "Bounded per-case Control probe budget; native outcome remains failed/unknown on expiry");
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < quantum; ++n)
                        audio.setSample(c, n, signal(c, block * quantum + n, false));
                for (int c = 0; c < auxiliary.getNumChannels(); ++c)
                    for (int n = 0; n < quantum; ++n)
                        auxiliary.setSample(c, n, signal(c, block * quantum + n, true));
                const auto processBegan = juce::Time::getMillisecondCounterHiRes();
                // No editor/native controls, parameter writes, MIDI, partial span
                // or physical admission policy. The last3 blocks exercise HOST
                // bypass/readback, not the plugin's wet DSP or native bypass widget.
                host.process(audio, 0, quantum, nullptr, 0, block * quantum / double(rate), 120, true,
                             block >= blocks - 3, 4, 4, nullptr, 0, 0, true,
                             requested.auxiliaryChannels ? &auxiliary : nullptr, 0);
                const auto hostMs = juce::Time::getMillisecondCounterHiRes() - processBegan;
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < quantum; ++n)
                    {
                        const auto sample = audio.getSample(c, n);
                        finite &= std::isfinite(sample);
                        peak = std::max(peak, std::abs(double(sample)));
                        captured.setSample(c, block * quantum + n, sample);
                    }
                const auto consumed = host.consumedLatency();
                const auto latest = host.declaredLatency();
                const auto stats = host.statistics();
                declarationsChanged |= latest.generation != previousGeneration;
                previousGeneration = latest.generation;
                csv +=
                    juce::String(block) + "," + juce::String(block * quantum) + "," +
                    juce::String(latest.nativeSamples) + "," + juce::String(latest.generation) + "," +
                    juce::String(consumed ? juce::int64(consumed->sequence) : -1) + "," +
                    juce::String(consumed ? consumed->stamp.nativeSamples : -1) + "," +
                    juce::String(consumed ? consumed->stamp.generation : 0) + "," +
                    juce::String(juce::int64(stats.submitted)) + "," +
                    juce::String(juce::int64(stats.processed)) + "," +
                    juce::String(juce::int64(stats.missed)) + "," + juce::String(juce::int64(stats.dropped)) +
                    "," + juce::String(host.bypassBlend().value_or(-1), 9) + "," +
                    juce::String(audio.getMagnitude(0, 0, quantum), 9) + "," +
                    juce::String(audio.getMagnitude(1, 0, quantum), 9) + "," + juce::String(hostMs, 6) + "\n";
                require(folder.getChildFile("blocks.csv").replaceWithText(csv),
                        "Retain actual declarations/consumption separately");
                auto raw = folder.getChildFile("actual-planar-float32le.raw").createOutputStream();
                require(bool(raw) && raw->setPosition(0) && raw->truncate().wasOk(),
                        "Owned captured output truncate");
                for (int c = 0; c < 2; ++c)
                    require(raw->write(captured.getReadPointer(c),
                                       std::size_t((block + 1) * quantum) * sizeof(float)),
                            "All current/startup output retained before musical/API assertions");
                raw.reset();
                r.setProperty("workerError", host.error());
                require(
                    finite && latest.valid(quantum) && stats.missed == 0 && stats.dropped == 0 &&
                        stats.auxiliaryRejected == 0 && !host.deliveryFailed() && host.alive() &&
                        host.processId() == pid && host.runtimeIdentity() == instance &&
                        host.midiResetGeneration() == reset &&
                        (block == 0 ? !consumed
                                    : consumed && consumed->sequence == std::uint64_t(block - 1) &&
                                          consumed->stamp.valid(quantum)),
                    "Actual accepted bus produced finite PCM with truthful current consumed packet identity");
                const auto waitBegan = juce::Time::getMillisecondCounter();
                while (host.statistics().processed < host.statistics().submitted && host.alive() &&
                       juce::Time::getMillisecondCounter() - waitBegan < 2000)
                    dispatchMessages(1); // CONTROL only, outside Host processing.
                // Explicit provider-readiness scheduling, not live-device cadence:
                // no claim that a processed counter alone is a completed-slot fence.
                dispatchMessages(1);
                controlWaitMs += juce::Time::getMillisecondCounter() - waitBegan;
                require(host.alive() && host.statistics().processed == host.statistics().submitted,
                        "Bounded actual native worker prefix, no fabricated async completion");
            }
            require(host.bypassBlend() && *host.bypassBlend() == 1,
                    "Healthy HOST5ms bypass reaches declared dry endpoint with same worker");
            r.setProperty("finitePcm", finite);
            r.setProperty("outputNonzeroObserved", peak > 0);
            r.setProperty("peak", peak);
            r.setProperty("processedBlocks", blocks);
            r.setProperty("declarationChangedObserved", declarationsChanged);
            r.setProperty("controlSchedulingMs", controlWaitMs);
            r.setProperty("hostBypassEndpointObserved", true);
            r.setProperty("nativeWetOrBypassFidelityQualified", false);
            const auto stateSerial = host.requestState();
            require(stateSerial != 0, "Actual state capture admitted, cannot silently omit it");
            const auto stateBegan = juce::Time::getMillisecondCounter();
            juce::String state;
            unsigned stateRevision = 0;
            bool stateCompleted = false;
            while (!(stateCompleted = host.readState(stateSerial, state, &stateRevision)) && host.alive() &&
                   juce::Time::getMillisecondCounter() - stateBegan < 10000)
                dispatchMessages(2);
            r.setProperty("stateRequest", juce::int64(stateSerial));
            r.setProperty("stateCompleted", stateCompleted);
            r.setProperty("stateMs", juce::int64(juce::Time::getMillisecondCounter() - stateBegan));
            r.setProperty("stateParameterRevision", juce::int64(stateRevision));
            r.setProperty("workerError", host.error());
            juce::MemoryBlock opaque;
            require(stateCompleted && decodePluginState(state, opaque),
                    "Actual bounded valid opaque state capture completed");
            const auto stateFile = folder.getChildFile("native-state.txt");
            require(stateFile.replaceWithText(state) && stateFile.loadFileAsString() == state,
                    "Owned state persistence/readback preserves exact opaque payload");
            r.setProperty("stateBytes", juce::int64(opaque.getSize()));
            r.setProperty("stateFileSha256", juce::SHA256(stateFile).toHexString());
            r.setProperty("stateRestoreQualified",
                          false); // No second worker/volatile state equality fiction.
            PluginParameterCatalogue catalogue;
            const auto request = host.requestParameterCatalogue();
            require(request != 0, "Actual control readback request admitted after capture");
            const auto metadataBegan = juce::Time::getMillisecondCounter();
            bool catalogueCompleted = false;
            while (!(catalogueCompleted =
                         host.readParameterCatalogue(catalogue) && catalogue.serial >= request) &&
                   host.alive() && juce::Time::getMillisecondCounter() - metadataBegan < 10000)
                dispatchMessages(2);
            r.setProperty("catalogueCompleted", catalogueCompleted);
            r.setProperty("catalogueGeneration", juce::int64(catalogue.generation));
            r.setProperty("catalogueError", catalogue.error);
            r.setProperty("catalogueReportedCount", catalogue.reportedCount);
            r.setProperty("catalogueDiagnostic", catalogue.diagnostic);
            require(catalogueCompleted,
                    "Actual catalogue terminal reply, error catalogue remains explicit availability limit");
            r.setProperty("parameterMetadataAvailable", catalogue.error == 0);
            r.setProperty("readinessPassed", true);
            r.setProperty("outcome", "ready");
        }
    }
    catch (const std::exception &failure)
    {
        r.setProperty("outcome", bool(report["accepted"]) ? "acceptedRuntimeFailed" : "preparationFailed");
        r.setProperty("error", juce::String(failure.what()));
    }
    catch (...)
    {
        r.setProperty("outcome", bool(report["accepted"]) ? "acceptedRuntimeFailed" : "preparationFailed");
        r.setProperty("error", "Unknown contained probe exception");
    }
    r.setProperty("finished", true);
    r.setProperty("elapsedIncludingOwnedCleanupMs", juce::Time::getMillisecondCounterHiRes() - began);
    checkpoint(folder, report);
    return report;
}
} // namespace installed_effect_bus

// Explicit metadata may be one JUCE PLUGIN descriptor XML or the existing cached
// VIBERA_PLUGINS/version1 file; binary must match one exact non-instrument class.
static void installedEffectBusReadinessTests(const juce::File &folder, const juce::String &absoluteBinary,
                                             const juce::String &absoluteMetadata)
{
    using namespace installed_effect_bus;
    require(juce::File::isAbsolutePath(absoluteBinary) && juce::File::isAbsolutePath(absoluteMetadata),
            "Only explicit absolute installed binary and checked descriptor/catalogue input");
    const juce::File binary(absoluteBinary), metadata(absoluteMetadata);
    require(binary.exists() && metadata.existsAsFile(),
            "Explicit existing provider/descriptor, no fallback scan");
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned provider readiness attempt");
    juce::var inputReport(new Object());
    inputReport.getDynamicObject()->setProperty("finished", false);
    inputReport.getDynamicObject()->setProperty("passed", false);
    inputReport.getDynamicObject()->setProperty("outcome", "inputValidation");
    inputReport.getDynamicObject()->setProperty("binary", absoluteBinary);
    inputReport.getDynamicObject()->setProperty("metadata", absoluteMetadata);
    checkpoint(folder, inputReport);
    juce::var terminalReport = inputReport;
    try
    {
        juce::PluginDescription description;
        juce::File image;
        try
        {
            description = descriptor(binary, metadata);
            image = nativeImage(binary);
        }
        catch (const std::exception &failure)
        {
            inputReport.getDynamicObject()->setProperty("finished", true);
            inputReport.getDynamicObject()->setProperty("outcome", "inputRefused");
            inputReport.getDynamicObject()->setProperty("error", juce::String(failure.what()));
            checkpoint(folder, inputReport);
            throw;
        }
        auto descriptorXml = description.createXml();
        require(bool(descriptorXml) &&
                    folder.getChildFile("selected-descriptor.xml").replaceWithText(descriptorXml->toString()),
                "Record exact selected cached/explicit class descriptor");
        const PluginSpec spec{1, description.name, descriptorXml->toString(), {}, false, false};
        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const auto worker = pluginWorkerExecutable();
        require(worker.existsAsFile(), "Exact matched sibling worker required");
        const std::array<juce::File, 4> inputs{executable, worker, image, metadata};
        std::array<juce::String, 4> hashes;
        juce::var report(new Object());
        terminalReport = report;
        auto &r = *report.getDynamicObject();
        r.setProperty("finished", false);
        r.setProperty("passed", false);
        r.setProperty("noDiscoveryScan", true);
        r.setProperty("provider", description.name);
        r.setProperty("version", description.version);
        r.setProperty("binary", binary.getFullPathName());
        r.setProperty("nativeImage", image.getFullPathName());
        r.setProperty("resourcesAndExternalDependenciesHashed", false);
        r.setProperty("detectorResponseQualified", false);
        r.setProperty("noLeakQualified", false);
        r.setProperty("routePdcQualified", false);
        r.setProperty("nativeExactPcmQualified", false);
        r.setProperty("liveOfflinePcmEqualityQualified", false);
        r.setProperty("productionPolicyChanged", false);
        r.setProperty("qualificationScope",
                      "Main and accepted-layout API readiness only; not all requested layouts supported");
        r.setProperty("listening", false);
        r.setProperty("hardware", false);
        r.setProperty("nativeInput", false);
        juce::Array<juce::var> identities, outcomes;
        for (std::size_t n = 0; n < inputs.size(); ++n)
        {
            hashes[n] = juce::SHA256(inputs[n]).toHexString();
            juce::var entry(new Object());
            entry.getDynamicObject()->setProperty("path", inputs[n].getFullPathName());
            entry.getDynamicObject()->setProperty("sha256", hashes[n]);
            identities.add(entry);
        }
        r.setProperty("inputs", identities);
        r.setProperty("cases", outcomes);
        checkpoint(folder, report);
        bool baseline = true, acceptedCases = true;
        std::array<int, 3> readyPerLayout{};
        for (const int channels : {0, 1, 2})
            for (const bool offline : {false, true})
            {
                const auto name = (channels == 0 ? juce::String("main") : "aux" + juce::String(channels)) +
                                  (offline ? "_offline" : "_live");
                auto result = probe(folder.getChildFile(name), spec,
                                    channels ? PluginBusRequest{1, channels} : PluginBusRequest{}, offline);
                const bool accepted = bool(result["accepted"]), ready = bool(result["readinessPassed"]);
                if (ready)
                    ++readyPerLayout[std::size_t(channels)];
                if (channels == 0)
                    baseline &= ready;
                if (accepted || result["outcome"].toString() != "startupRefused")
                    acceptedCases &= ready;
                outcomes.add(result);
                r.setProperty("cases", outcomes);
                checkpoint(folder, report);
            }
        bool stable = true;
        for (std::size_t n = 0; n < inputs.size(); ++n)
            stable &= juce::SHA256(inputs[n]).toHexString() == hashes[n];
        r.setProperty("inputsStable", stable);
        r.setProperty("mainLiveOfflineReady", baseline);
        r.setProperty("auxMonoNegotiationAndReadinessPassed", readyPerLayout[1] == 2);
        r.setProperty("auxStereoNegotiationAndReadinessPassed", readyPerLayout[2] == 2);
        r.setProperty("allAcceptedCasesReady", acceptedCases);
        r.setProperty("finished", true);
        r.setProperty("passed", baseline && acceptedCases && stable);
        checkpoint(folder, report);
        require(
            baseline && acceptedCases && stable,
            "Provider baseline or accepted-layout readiness failed; refusal/unknown aux is retained, never "
            "exact DSP qualification");
    }
    catch (const std::exception &failure)
    {
        if (!bool(terminalReport["finished"]))
        {
            auto &terminal = *terminalReport.getDynamicObject();
            terminal.setProperty("finished", true);
            terminal.setProperty("passed", false);
            terminal.setProperty("outcome", "preparationFailed");
            terminal.setProperty("error", juce::String(failure.what()));
            // Best-effort terminal evidence must not hide the original failure
            // if the output filesystem itself has failed.
            folder.getChildFile("verification.json")
                .replaceWithText(juce::JSON::toString(terminalReport, true));
        }
        throw;
    }
    catch (...)
    {
        auto &terminal = *terminalReport.getDynamicObject();
        terminal.setProperty("finished", true);
        terminal.setProperty("passed", false);
        terminal.setProperty("outcome", "preparationFailed");
        terminal.setProperty("error", "Unknown contained provider setup exception");
        folder.getChildFile("verification.json").replaceWithText(juce::JSON::toString(terminalReport, true));
        throw;
    }
}
