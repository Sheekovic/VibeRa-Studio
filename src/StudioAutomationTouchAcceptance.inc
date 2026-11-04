// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoInline.h"
// Original actual Studio/Controller/MCP + synthetic-device acceptance. No hardware.
VIBERA_NOINLINE juce::Result Studio::verifyAutomationTouch(const juce::File &folder)
{
    if (!bool(getProperties()["viberaAutomationTouchFixture"]) ||
        getProperties()["viberaAutomationTouchOutput"].toString() != folder.getFullPathName())
        return juce::Result::fail("Owned isolated automation-touch fixture required");
    stopTimer();
    juce::String trace = "callback,headBeat,peak,milliseconds,heldKeys,events\n";
    std::vector<float> pcm;
    pcm.reserve(2 * 256 * 256);
    int callbacks = 0, overruns = 0;
    double maximumMs = 0;
    auto receipt = [&](bool passed, const juce::String &failure)
    {
        auto *o = new juce::DynamicObject;
        o->setProperty("passed", passed);
        o->setProperty("failure", failure);
        o->setProperty("evidenceKind", "Actual shared Studio/MCP + injected MIDI and synthetic device");
        o->setProperty("hardware", false);
        o->setProperty("listening", false);
        o->setProperty("nativeOsInput", false);
        o->setProperty("nativeParameterRecording", false);
        o->setProperty("callbackHeapInstrumentation",
                       "Not present in GUI executable; core recorder/ingress proof separate");
        o->setProperty("callbacks", callbacks);
        o->setProperty("overruns", overruns);
        o->setProperty("maximumMs", maximumMs);
        o->setProperty("automationRecording", automationRecordingState());
        const bool json = folder.getChildFile("automation-recording.json")
                              .replaceWithText(juce::JSON::toString(juce::var(o), true));
        const bool csv = folder.getChildFile("callbacks.csv").replaceWithText(trace);
        auto out = folder.getChildFile("callbacks-planar-f32.raw").createOutputStream();
        return json && csv && out && out->setPosition(0) && out->truncate().wasOk() &&
               out->write(pcm.data(), pcm.size() * sizeof(float)) && (out->flush(), out->getStatus().wasOk());
    };
    auto check = [](bool value, const char *why)
    {
        if (!value)
            throw std::runtime_error(why);
    };
    try
    {
        check(folder.createDirectory().wasOk(), "Fresh owned automation acceptance output");
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        check(device && device->getCurrentSampleRate() == 48000 &&
                  device->getCurrentBufferSizeSamples() == 256,
              "Actual original 48k/256 synthetic device");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels[0].source = "builtin:bass";
        project.channels[0].mixer = 1;
        project.tempo = 120;
        project.mixer[1].routes = {{0, 1}, {2, .25f}};
        project.placements.push_back({project.allocate(), project.patterns.front().id, 0, 0, 4, 0});
        controller.replace(project);
        controller.dirty = false;
        selectedPattern = project.patterns.front().id;
        selectedChannel = project.channels.front().id;
        selectedInsert = 1;
        patternMode = false;
        mode.setButtonText("Song");
        audio.setMetronome(false);
        audio.setCountInBars(0);
        audio.setLooping(false);
        refresh();
        check(audio.rebuild(controller.project, false, selectedPattern).wasOk(),
              "Actual original sampler projection");
        needsRebuild = false;
        engineRevision = controller.revision;
        audio.setMidiTarget(selectedChannel);
        audio.seekBeat(0);
        juce::AudioBuffer<float> block(2, 256);
        const auto process = [&](int count)
        {
            float peak = 0;
            for (int i = 0; i < count; ++i)
            {
                if (engineRevision != controller.revision)
                {
                    check(audio.rebuild(controller.project, patternMode, selectedPattern).wasOk(),
                          "Actual current live-document publication");
                    needsRebuild = false;
                    engineRevision = controller.revision;
                }
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto end = juce::Time::getHighResolutionTicks();
                const double ms = juce::Time::highResolutionTicksToSeconds(end - began) * 1000.;
                maximumMs = std::max(maximumMs, ms);
                overruns += ms > 1000. * 256 / 48000.;
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < 256; ++f)
                    {
                        const float v = block.getSample(c, f);
                        check(std::isfinite(v), "Every callback PCM frame finite");
                        pcm.push_back(v);
                    }
                const auto d = audio.projectionDiagnostics();
                const auto magnitude = block.getMagnitude(0, 256);
                peak = std::max(peak, magnitude);
                trace += juce::String(callbacks++) + "," + juce::String(audio.positionBeat(), 9) + "," +
                         juce::String(magnitude, 9) + "," + juce::String(ms, 6) + "," +
                         juce::String(d.channels.empty() ? 0 : d.channels.front().heldInputKeys) + "," +
                         juce::String(automationTouch ? int(automationTouch->events.size()) : 0) + "\n";
                dispatchMessages(1);
            }
            return peak;
        };
        process(8);
        record.onClick();
        play.onClick();
        process(12);
        check(midiTaking && recordArmed && pendingMidiTake, "Genuine existing Record/Play starts MIDI take");
        audio.midiInput.route(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), true, true);
        drainMidiTake();
        check(process(8) > 1.e-6f && midiTake.heldCount() == 1, "Actual original held note produces PCM");
        const auto source = audio.projectionDiagnostics();
        const auto *noteOwner = pendingMidiTake.get();
        const auto session = audio.midiInput.capture.session.load();
        const auto serial = midiTakeSerial();
        const int starts = device->starts, stops = device->stops;
        auto request = [](const char *action)
        {
            auto *o = new juce::DynamicObject;
            o->setProperty("action", action);
            return juce::var(o);
        };
        juce::var tool;
        const auto discovery = mcpTools();
        for (const auto &item : *discovery.getArray())
            if (item["name"].toString() == "vibera_automationRecording")
                tool = item;
        check(tool.isObject(), "Actual recording tool discovery");
        const auto invoke = [&](const juce::var &args)
        {
            validateMcpSchema(tool["inputSchema"], args);
            const auto response = mcpCall("vibera_automationRecording", args)["structuredContent"];
            validateMcpSchema(tool["outputSchema"], response);
            return response;
        };
        const auto arm = [&]
        {
            auto args = request("arm");
            args.getDynamicObject()->setProperty("insert", 1);
            args.getDynamicObject()->setProperty("lane", 3);
            args.getDynamicObject()->setProperty("projectIdentity", controller.projectIdentity);
            args.getDynamicObject()->setProperty("revision", juce::int64(controller.revision));
            const auto r = invoke(args);
            check(r["phase"].toString() == "capturing" && r["completionState"].toString() == "pending",
                  "Actual MCP arm captures source clock");
            return r["token"].toString();
        };
        const auto unchanged = [&]
        {
            const auto d = audio.projectionDiagnostics();
            check(d.edit == source.edit && d.playbackContext == source.playbackContext &&
                      d.samplerCreations == source.samplerCreations && !d.channels.empty() &&
                      d.channels.front().sampler == source.channels.front().sampler &&
                      d.channels.front().sample == source.channels.front().sample &&
                      d.channels.front().heldInputKeys == 1 && device->starts == starts &&
                      device->stops == stops && midiTaking && recordArmed &&
                      pendingMidiTake.get() == noteOwner && midiTakeSerial() == serial &&
                      midiTake.heldCount() == 1 && audio.midiInput.capture.session.load() == session &&
                      audio.midiInput.capture.active.load(),
                  "Recorder preserves exact held sample/native context/device/MIDI take ownership");
        };
        auto mapped = [&](const char *action, int cc, double low, double high, int destination = -1)
        {
            ControllerBinding b;
            b.action = action;
            b.label = "Original recorder input";
            b.port = 1;
            b.channel = 1;
            b.number = cc;
            b.mode = 1;
            b.mixer = 1;
            b.minimum = low;
            b.maximum = high;
            b.pickup = false;
            b.sendTarget = destination;
            return b;
        };
        midiController.bindings = {mapped("mixer.gain", 20, 0, 2), mapped("mixer.pan", 21, -1, 1),
                                   mapped("mixer.stereoSeparation", 22, -1, 1),
                                   mapped("mixer.send", 23, 0, 2, 2)};
        midiController.publish();
        const auto baseline = controller.project.state();
        const auto undo = controller.undo.getUndoDescription();
        auto token = arm();
        {
            const auto before = controller.project.state();
            const auto history = controller.undo.getUndoDescription();
            auto invalid = request("value");
            invalid.getDynamicObject()->setProperty("token", "wrong-take");
            invalid.getDynamicObject()->setProperty("control", "gain");
            invalid.getDynamicObject()->setProperty("value", .5);
            bool refused = false;
            try
            {
                (void)invoke(invalid);
            }
            catch (const McpFailure &e)
            {
                refused = e.code == "automationRecording.staleToken";
            }
            check(refused && before.isEquivalentTo(controller.project.state()) &&
                      controller.undo.getUndoDescription() == history,
                  "Actual stale token refuses without model/history/input changes");
            invalid.getDynamicObject()->setProperty("token", token);
            invalid.getDynamicObject()->setProperty("sendTarget", 0);
            refused = false;
            try
            {
                (void)invoke(invalid);
            }
            catch (const McpFailure &e)
            {
                refused = e.code == "automationRecording.invalidArguments";
            }
            check(refused && before.isEquivalentTo(controller.project.state()),
                  "Foreign send metadata refuses without poisoning active take");
        }
        for (int cc = 20; cc <= 23; ++cc)
        {
            audio.midiInput.controls.observe(juce::MidiMessage::controllerEvent(1, cc, 80 + cc), 1);
            midiController.drain();
            check(audio.rebuild(controller.project, false, selectedPattern).wasOk(),
                  "Actual live manual preview publication");
            needsRebuild = false;
            engineRevision = controller.revision;
            process(2);
            unchanged();
            check(controller.undo.getUndoDescription() == undo, "MIDI point previews never consume history");
        }
        auto cancel = request("cancel");
        cancel.getDynamicObject()->setProperty("token", token);
        check(invoke(cancel)["completionState"].toString() == "cancelled" &&
                  controller.project.state().isEquivalentTo(baseline) &&
                  controller.undo.getUndoDescription() == undo,
              "Actual shared cancel restores exact model/history while MIDI stays held");
        process(2);
        unchanged();
        token = arm();
        for (int cc = 20; cc <= 23; ++cc)
        {
            audio.midiInput.controls.observe(juce::MidiMessage::controllerEvent(1, cc, cc + 60), 1);
            midiController.drain();
            process(2);
            unchanged();
        }
        auto value = request("value");
        value.getDynamicObject()->setProperty("token", token);
        value.getDynamicObject()->setProperty("control", "pan");
        value.getDynamicObject()->setProperty("value", -.25);
        check(invoke(value)["phase"].toString() == "capturing",
              "Actual semantic value shares live recorder without MIDI injection");
        process(2);
        auto finish = request("finish");
        finish.getDynamicObject()->setProperty("token", token);
        check(invoke(finish)["completionState"].toString() == "completed" && !automationRecordingActive() &&
                  controller.project.automation.sources.size() == 4,
              "Actual take commits four captured sources exactly once");
        const auto automationOnly = controller.project.state();
        unchanged();
        audio.midiInput.route(juce::MidiMessage::noteOff(1, 60), true, true);
        drainMidiTake();
        process(2);
        stop.onClick();
        check(!midiTaking && !hasPendingMidiTake() &&
                  controller.project.pattern(selectedPattern)->notes.size() == 1,
              "Independent genuine note take commits once after automation, never discarded");
        const auto completed = controller.project.state();
        check(controller.undoEditChecked().wasOk() &&
                  controller.project.state().isEquivalentTo(automationOnly),
              "First Undo removes only note take");
        check(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
              "Second Undo removes one exact automation take");
        check(controller.redoEditChecked().wasOk() && controller.redoEditChecked().wasOk() &&
                  controller.project.state().isEquivalentTo(completed),
              "Two real Redos restore exact note plus automation IDs/curves");
        const auto saved = folder.getChildFile("original-recorded-song.vibra");
        check(!saved.exists() && saveProject(controller.project, saved).wasOk(),
              "Actual original project save");
        check(openProjectFile(saved, true).wasOk(), "Actual normal project reopen");
        check(controller.project.state().isEquivalentTo(completed),
              "Exact persisted automation and note payload");
        check(audio.rebuild(controller.project, false, selectedPattern).wasOk(),
              "Actual reopened Automation Clip replay projection");
        needsRebuild = false;
        engineRevision = controller.revision;
        audio.seekBeat(0);
        play.onClick();
        check(process(48) > 1.e-6f,
              "Actual reopened recorded note/Automation Clip Song has finite nonzero PCM");
        stop.onClick();
        check(
            receipt(false, "All runtime facts recorded before unchanged strict callback deadline assertion"),
            "Retain all callback PCM/evidence");
        check(overruns == 0, "Every original256-frame callback deadline remains strict");
        check(receipt(true, {}), "Retain successful original acceptance");
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        audio.midiInput.controls.endAutomationStamp();
        audio.stop();
        (void)receipt(false, e.what());
        return juce::Result::fail(e.what());
    }
}
