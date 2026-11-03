// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Studio.h"
#include "SyntheticDevice.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace vibera::test
{
// Actual cached installed provider through Studio's shared CONTROL/UI/MCP path.
// No scan/editor/hardware/listening/unity-response or callback-heap claim.
__declspec(noinline) inline juce::Result
verifyInstalledStudioEffectTransaction(Studio &s, const juce::File &folder, const juce::File &actualPlugin,
                                       const juce::File &metadata)
{
    const auto receipt = folder.getChildFile("installed-effect-transaction.json");
    if (!folder.isDirectory() || receipt.exists())
        return juce::Result::fail(
            "Fresh owned attempt required; preserve previous installed-provider receipts");
    auto object = [] { return juce::var(new juce::DynamicObject); };
    auto report = object();
    juce::Array<juce::var> events;
    juce::String stage = "preflight", failure,
                 csv = "stage,callback,ms,peak,masterPeak,busPeak,held,sample,context,sourceCalls,"
                       "masterAvailable,busAvailable,backgroundSubmitted,backgroundProcessed,"
                       "backgroundMissed,backgroundDropped,"
                       "effectSubmitted,effectProcessed,effectMissed,effectDropped\n";
    std::vector<float> pcm;
    pcm.reserve(4096 * 512);
    int callbacks = 0, held = 0;
    Id backgroundId = 0, effectId = 0;
    unsigned backgroundCapture = 0;
    bool pinned = false;
    AudioEngine::ProjectionDiagnostics baseline;
    juce::AudioBuffer<float> block(2, 256);
    SyntheticDevice *device = nullptr;
    const auto keep = [&]
    {
        report.getDynamicObject()->setProperty("stage", stage);
        report.getDynamicObject()->setProperty("error", failure);
        report.getDynamicObject()->setProperty("studioStatus", s.status);
        report.getDynamicObject()->setProperty("events", events);
        juce::Array<juce::var> workers;
        for (const auto &row : s.controller.project.mixer)
            for (const auto &spec : row.effects)
                if (const auto host = s.audio.parameterHost(spec.id))
                {
                    auto item = object();
                    auto *v = item.getDynamicObject();
                    const auto stats = host->statistics();
                    v->setProperty("plugin", juce::int64(spec.id));
                    v->setProperty("instance", host->runtimeIdentity());
                    v->setProperty("pid", int(host->processId()));
                    v->setProperty("alive", host->alive());
                    v->setProperty("latency", host->latencySamples());
                    v->setProperty("native", host->declaredLatency().nativeSamples);
                    v->setProperty("parameterRevision", int(host->parameterRevision()));
                    v->setProperty("processed", juce::int64(stats.processed));
                    v->setProperty("missed", juce::int64(stats.missed));
                    v->setProperty("dropped", juce::int64(stats.dropped));
                    workers.add(item);
                }
        report.getDynamicObject()->setProperty("currentWorkers", workers);
        if (!receipt.replaceWithText(juce::JSON::toString(report, true)) ||
            !folder.getChildFile("callbacks.csv").replaceWithText(csv))
            throw std::runtime_error("Cannot retain installed transaction receipt");
        auto raw = folder.getChildFile("callback-planar-f32.raw").createOutputStream();
        if (!raw || !raw->setPosition(0) || raw->truncate().failed() ||
            !raw->write(pcm.data(), pcm.size() * sizeof(float)))
            throw std::runtime_error("Cannot retain all installed-provider PCM");
        raw->flush();
        if (raw->getStatus().failed())
            throw std::runtime_error("Installed PCM flush failed");
    };
    const auto check = [&](bool ok, const char *why)
    {
        if (!ok)
        {
            failure = why;
            keep();
            throw std::runtime_error(why);
        }
    };
    const auto call = [&](const char *tool, const juce::var &args)
    {
        const auto response = s.mcpCall(tool, args);
        if (bool(response["isError"]))
        {
            events.add(response);
            keep();
        }
        check(!bool(response["isError"]), "Actual shared semantic operation returned structured failure");
        return response["structuredContent"];
    };
    const auto tick = [&]
    {
        dispatchMessages(6); // Actual Studio Timer, never private timerCallback or callback wait.
        device = dynamic_cast<SyntheticDevice *>(
            s.audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
        if (!device)
            return;
        check(callbacks < 4096, "Bounded callback evidence capacity");
        const auto before = s.audio.projectionDiagnostics();
        const auto began = juce::Time::getHighResolutionTicks();
        device->process(block);
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto after = s.audio.projectionDiagnostics();
        const auto master = s.audio.mixerMeter(0), bus = s.audio.mixerMeter(1);
        bool finite = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
            {
                const auto value = block.getSample(c, n);
                pcm.push_back(value);
                finite &= std::isfinite(value);
            }
        for (int c = 0; c < 2; ++c)
            finite &= std::isfinite(master.peak[c]) && std::isfinite(master.rms[c]) &&
                      std::isfinite(bus.peak[c]) && std::isfinite(bus.rms[c]);
        const auto channel =
            after.channels.empty() ? AudioEngine::ChannelDiagnostic{} : after.channels.front();
        const auto background = s.audio.parameterHost(backgroundId), effect = s.audio.parameterHost(effectId);
        const auto b = background ? background->statistics() : PluginHost::Statistics{};
        const auto e = effect ? effect->statistics() : PluginHost::Statistics{};
        csv += stage + "," + juce::String(++callbacks) + "," + juce::String(ms, 6) + "," +
               juce::String(block.getMagnitude(0, 0, 256), 12) + "," + juce::String(master.peak[0], 12) +
               "," + juce::String(bus.peak[0], 12) + "," + juce::String(channel.heldInputKeys) + "," +
               juce::String(juce::int64(channel.sample)) + "," +
               juce::String(juce::int64(after.playbackContext)) + "," +
               juce::String(juce::int64(channel.renderCalls)) + "," + juce::String(int(master.available)) +
               "," + juce::String(int(bus.available)) + "," + juce::String(juce::int64(b.submitted)) + "," +
               juce::String(juce::int64(b.processed)) + "," + juce::String(juce::int64(b.missed)) + "," +
               juce::String(juce::int64(b.dropped)) + "," + juce::String(juce::int64(e.submitted)) + "," +
               juce::String(juce::int64(e.processed)) + "," + juce::String(juce::int64(e.missed)) + "," +
               juce::String(juce::int64(e.dropped)) + "\n";
        check(finite && ms <= 256000. / 48000.,
              "Finite actual PCM/meters and original strict256-frame deadline");
        if (pinned)
        {
            check(after.channels.size() == 1 && before.channels.size() == 1 && after.edit == baseline.edit &&
                      after.playbackContext == baseline.playbackContext &&
                      channel.sample == baseline.channels.front().sample && channel.heldInputKeys == held &&
                      channel.renderCalls == before.channels.front().renderCalls + 1 && !s.audio.playing() &&
                      master.available && bus.available,
                  "Held original source/context survives actual private preparation/adoption without replay");
            check(background && background->stateCaptureSerial() == backgroundCapture && b.missed == 0 &&
                      b.dropped == 0,
                  "Unchanged background worker receives no new capture and loses no PCM packets");
        }
    };
    const auto projectJob = [&](const char *action, const juce::File &file)
    {
        auto args = object();
        auto *a = args.getDynamicObject();
        a->setProperty("action", action);
        a->setProperty("path", file.getFullPathName());
        a->setProperty("overwrite", false);
        a->setProperty("discardChanges", true);
        auto result = call("vibera_project", args);
        const auto id = result["operation"].toString();
        const auto began = juce::Time::getMillisecondCounter();
        while (result["completionState"].toString() == "pending" &&
               juce::Time::getMillisecondCounter() - began < 15000)
        {
            tick();
            auto poll = object();
            poll.getDynamicObject()->setProperty("action", "job");
            poll.getDynamicObject()->setProperty("operation", id);
            result = call("vibera_project", poll);
        }
        events.add(result);
        keep();
        check(result["completionState"].toString() == "completed",
              "Real project file job completed, not only accepted");
    };
    const auto finish = [&](std::uint64_t id)
    {
        check(id != 0, "Shared effect path returned actual owned operation");
        auto args = object();
        args.getDynamicObject()->setProperty("action", "routingJob");
        args.getDynamicObject()->setProperty("operation", juce::int64(id));
        auto result = call("vibera_mixer", args)["routing"];
        const auto began = juce::Time::getMillisecondCounter();
        while (result["completionState"].toString() == "pending" &&
               juce::Time::getMillisecondCounter() - began < 15000)
        {
            tick();
            result = call("vibera_mixer", args)["routing"];
        }
        events.add(result);
        keep();
        check(result["completionState"].toString() == "completed" && bool(result["documentCommitted"]) &&
                  bool(result["nativeCompletionKnown"]) && juce::int64(result["postedSerial"]) > 0 &&
                  juce::int64(result["postedSerial"]) == juce::int64(result["selectedSerial"]),
              "Installed provider transaction reaches actual selection/drain/final membership ACK");
    };
    try
    {
        check(s.controllerTestMode && s.pluginTestSettings && s.workflowTestSettings &&
                  bool(s.getProperties()["viberaInstalledEffectTransactionFixture"]) &&
                  s.getProperties()["viberaInstalledEffectTransactionOutput"].toString() ==
                      folder.getFullPathName() &&
                  metadata.existsAsFile() && actualPlugin.exists(),
              "Fresh isolated Studio and explicit existing provider/cache required");
        auto &manager = s.audio.engine.getDeviceManager();
        check(!manager.deviceManager.getCurrentAudioDevice(), "Refuse pre-existing hardware/device");
        for (auto *type : manager.deviceManager.getAvailableDeviceTypes())
            check(type->getTypeName() == "VibeRa synthetic",
                  "Main must remove physical device types before fixture");
        PluginCatalog catalogue(metadata, {}, false);
        juce::PluginDescription chosen;
        int matches = 0;
        for (const auto &type : catalogue.snapshot().plugins)
            if (type.pluginFormatName == "VST3" && !type.isInstrument &&
                juce::File(type.fileOrIdentifier) == actualPlugin)
            {
                chosen = type;
                ++matches;
            }
        check(matches == 1, "One exact cached VST3 effect class; ambiguous class is refused");
        // JUCE catalogue counts include auxiliary buses: MVibratoMB advertises
        // four inputs (stereo main plus detector). These counts do not describe
        // the negotiated active main layout. The actual worker must accept its
        // existing stereo-main/default-disabled-aux contract; never infer that
        // layout or reject it from the aggregate catalogue count.
        report.getDynamicObject()->setProperty("catalogueInputChannels", chosen.numInputChannels);
        report.getDynamicObject()->setProperty("catalogueOutputChannels", chosen.numOutputChannels);
        report.getDynamicObject()->setProperty("providerDescriptor", chosen.createXml()->toString());
        report.getDynamicObject()->setProperty("metadataSha256", juce::SHA256(metadata).toHexString());
        auto binary = actualPlugin;
        if (binary.isDirectory())
        {
            const auto modules = binary.getChildFile("Contents/x86_64-win")
                                     .findChildFiles(juce::File::findFiles, false, "*.vst3");
            check(modules.size() == 1,
                  "Bundle requires one exact Windows module for retained binary identity");
            binary = modules[0];
        }
        check(binary.existsAsFile(), "Actual installed native module file required");
        report.getDynamicObject()->setProperty("providerPath", actualPlugin.getFullPathName());
        report.getDynamicObject()->setProperty("providerSha256", juce::SHA256(binary).toHexString());
        report.getDynamicObject()->setProperty(
            "executableSha256",
            juce::SHA256(juce::File::getSpecialLocation(juce::File::currentExecutableFile)).toHexString());
        report.getDynamicObject()->setProperty("workerSha256",
                                               juce::SHA256(pluginWorkerExecutable()).toHexString());
        report.getDynamicObject()->setProperty("scope",
                                               "Actual installed Studio UI/MCP/worker/history/files; finite "
                                               "PCM observations only; no unity/listening/heap claim");
        auto wave = folder.getChildFile("original-held.wav");
        juce::AudioBuffer<float> input(2, 4096);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 4096; ++n)
                input.setSample(c, n, float(((n * (c ? 29 : 17) + 13) % 251) - 125) / 2048.f);
        juce::WavAudioFormat format;
        auto stream = wave.createOutputStream();
        check(bool(stream), "Fresh original source output");
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 24, {}, 0));
        check(writer && writer->writeFromAudioSampleBuffer(input, 0, 4096) && writer->flush(),
              "Original signed sample written");
        writer.reset();
        auto p = Project::empty();
        p.channels.resize(1);
        auto &c = p.channels.front();
        c.source = wave.getFullPathName();
        c.gain = 1;
        c.pan = 0;
        c.mixer = 1;
        c.sampler.root = 60;
        c.sampler.loop = true;
        c.sampler.envelope = true;
        c.sampler.attack = c.sampler.decay = c.sampler.release = 0;
        c.sampler.sustain = 1;
        for (auto &row : p.mixer)
        {
            row.gain = 1;
            row.pan = 0;
            row.muted = false;
        }
        p.patterns.front().notes.clear();
        PluginSpec base{p.allocate(), chosen.name, chosen.createXml()->toString(), {}, false, false};
        backgroundId = base.id;
        p.mixer[2].effects = {base};
        const auto initial = folder.getChildFile("initial.vibra");
        check(saveProject(p, initial).wasOk(), "Valid owned initial project saved");
        stage = "initial-open";
        projectJob("open", initial);
        s.patternMode = true;
        s.selectedChannel = c.id;
        s.selectedPattern = p.patterns.front().id;
        auto began = juce::Time::getMillisecondCounter();
        while ((!s.audio.preparedPluginHost(s.controller.project, base.id) || !s.audio.channelSample(c.id) ||
                !s.audio.projectionDiagnostics().edit) &&
               juce::Time::getMillisecondCounter() - began < 15000)
            tick();
        check(s.audio.preparedPluginHost(s.controller.project, base.id) && s.audio.channelSample(c.id),
              "Actual cached provider/sample initially prepared without hardware");
        // The public persistent device-preparation service requires a genuinely
        // deferred no-device projection. Reach it through actual canonical
        // offline Mixer assignments, as the application routing acceptance does;
        // never set private preparation flags or assume every control-only Edit
        // is a deferred projection.
        stage = "offline-routing-before-device";
        for (const int destination : {3, 1})
        {
            auto args = object(), values = object();
            values.getDynamicObject()->setProperty("channel", juce::int64(c.id));
            values.getDynamicObject()->setProperty("mixer", destination);
            args.getDynamicObject()->setProperty("command", "channel.mixer");
            args.getDynamicObject()->setProperty("args", values);
            const auto routed = call("vibera_command", args);
            events.add(routed);
            check(routed["completionState"].toString() == "completed" &&
                      routed["audioDisposition"].toString() == "deferredNoDevice" &&
                      !bool(routed["nativeCompletionKnown"]),
                  "Actual offline assignment prepares the declared deferred-device workflow");
        }
        check(s.audio.documentProjectionDeferred() && !s.audio.projectionDiagnostics().edit,
              "Persistent device preparation has its actual declared source prerequisite");
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        s.audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        // Native initialisation queues its preparation fence on CONTROL. Wait
        // for that real event before invoking the public deferred service,
        // rather than treating initialise() return as native completion.
        using Fence = te::DeviceManager::PreparationFenceStatus;
        const auto fenceDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (manager.getPreparationFenceStatus() != Fence::passed &&
               manager.getPreparationFenceStatus() != Fence::refused &&
               juce::Time::getMillisecondCounter() < fenceDeadline)
            dispatchMessages(2);
        report.getDynamicObject()->setProperty("nativePreparationFence",
                                               int(manager.getPreparationFenceStatus()));
        check(manager.getPreparationFenceStatus() == Fence::passed,
              "Actual native device preparation fence passed before application admission");
        device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        check(device && device->getCurrentSampleRate() == 48000 &&
                  device->getCurrentBufferSizeSamples() == 256,
              "Actual configured48k/256 in-memory provider, never physical hardware");
        stage = "actual-device-preparation";
        if (!s.devicePreparationBusy())
        {
            const auto admitted = s.beginDevicePreparation(false);
            report.getDynamicObject()->setProperty("deviceAdmissionError", admitted.getErrorMessage());
            check(admitted.wasOk(), "Owned actual device preparation admitted");
        }
        began = juce::Time::getMillisecondCounter();
        while (s.devicePreparationBusy() && juce::Time::getMillisecondCounter() - began < 15000)
            tick();
        events.add(s.devicePreparationState());
        keep();
        check(!s.devicePreparationBusy() &&
                  s.devicePreparationState()["completionState"].toString() == "completed",
              "Device preparation actually completed");
        auto background = s.audio.preparedPluginHost(s.controller.project, base.id);
        check(background && background->alive() && background->busLayout() == PluginBusRequest{} &&
                  background->processingQuantum() == 256 && s.audio.channelSample(c.id),
              "Actual unused installed effect and sample projected");
        s.audio.setMidiTarget(c.id);
        s.audio.stop();
        s.audio.setLooping(false);
        for (int n = 0; n < 4; ++n)
            tick();
        PreparedPluginTransport::Ptr plan;
        check(s.audio.stoppedEffectWorkerTransport(plan).wasOk() && plan && plan->budget().packets == 3,
              "Healthy actual ordinary3Q source admission");
        baseline = s.audio.projectionDiagnostics();
        backgroundCapture = background->stateCaptureSerial();
        check(backgroundCapture > 0 && background->statistics().missed == 0 &&
                  background->statistics().dropped == 0,
              "Actual native startup capture completed before processing without missed packets");
        report.getDynamicObject()->setProperty("backgroundCaptureSerial", int(backgroundCapture));
        pinned = true;
        held = 1;
        s.audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                    juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
        tick();
        stage = "ui-insert";
        CommandArgs insert;
        insert.mixer = 1;
        insert.text = base.descriptor;
        s.command("plugin.effect.insert", insert);
        finish(s.lastRoutingDispatchOperation);
        const auto &chain = mixerInsert(s.controller.project, 1)->effects;
        check(chain.size() == 1, "Actual installed effect committed");
        const auto id = chain.front().id;
        effectId = id;
        auto first = s.audio.parameterHost(id);
        check(first && first->alive(), "New installed native runtime alive");
        const auto firstInstance = first->runtimeIdentity();
        first.reset();
        stage = "ui-undo";
        s.performAction("edit.undo");
        finish(s.lastRoutingDispatchOperation);
        check(!s.controller.project.plugin(id), "One UI Undo removes same stable effect");
        stage = "mcp-redo";
        auto args = object();
        args.getDynamicObject()->setProperty("command", "edit.redo");
        const auto redo = call("vibera_command", args);
        finish(std::uint64_t(juce::int64(redo["operation"])));
        auto restored = s.audio.parameterHost(id);
        check(restored && restored->alive() && restored->runtimeIdentity() != firstInstance &&
                  s.controller.project.plugin(id),
              "MCP Redo restores same ID with fresh native instance");
        report.getDynamicObject()->setProperty("effectId", juce::int64(id));
        report.getDynamicObject()->setProperty("firstInstance", firstInstance);
        report.getDynamicObject()->setProperty("redoInstance", restored->runtimeIdentity());
        for (int n = 0; n < 8; ++n)
            tick();
        const auto processed = restored->statistics();
        check(processed.processed > 0 && processed.missed == 0 && processed.dropped == 0,
              "Real installed native packets consumed without missing/drop");
        restored.reset();
        stage = "mcp-remove";
        args = object();
        auto values = object();
        values.getDynamicObject()->setProperty("target", juce::int64(id));
        args.getDynamicObject()->setProperty("args", values);
        args.getDynamicObject()->setProperty("command", "plugin.remove");
        const auto removed = call("vibera_command", args);
        finish(std::uint64_t(juce::int64(removed["operation"])));
        check(!s.controller.project.plugin(id) && !s.audio.parameterHost(id),
              "Real MCP remove reaches canonical/native completion");
        for (int n = 0; n < 4; ++n)
            tick();
        held = 0;
        s.audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        tick();
#include "InstalledStudioEffectChainApply.inc"
        check(juce::SHA256(metadata).toHexString() == report["metadataSha256"].toString(),
              "Explicit catalogue remained read-only");
        check(juce::SHA256(binary).toHexString() == report["providerSha256"].toString(),
              "Installed module remained unchanged");
        report.getDynamicObject()->setProperty("passed", true);
        keep();
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        failure = error.what();
    }
    catch (...)
    {
        failure = "Unknown installed transaction failure";
    }
    try
    {
        report.getDynamicObject()->setProperty("passed", false);
        keep();
    }
    catch (...)
    {
    }
    return juce::Result::fail(stage + ": " + failure);
}
} // namespace vibera::test
