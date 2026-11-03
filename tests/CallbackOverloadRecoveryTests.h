// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Failure injection only. This callback is registered with a synthetic device
// in the test runner, never a physical driver or the VibeRa product DSP path.
static void callbackOverloadRecoveryTests(const juce::File &folder)
{
    juce::String summary;
    for (int mode = 0; mode < 3; ++mode)
    {
        AudioEngine audio(false);
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement settings("DEVICESETUP");
        settings.setAttribute("deviceType", "VibeRa synthetic");
        settings.setAttribute("audioOutputDeviceName", "In-memory output");
        settings.setAttribute("audioDeviceRate", 48000.);
        settings.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, settings);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device != nullptr, "overload recovery uses synthetic output only");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.placements.push_back({project.allocate(), project.patterns.front().id, 0, 0, 4, 0});
        require(audio.rebuild(project, mode != 2, project.patterns.front().id).wasOk(),
                "overload recovery graph prepared");
        audio.setMidiTarget(project.channels.front().id);
        dispatchMessages(30);
        if (mode != 0)
            audio.playStop();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 48, juce::uint8(100)));
        juce::AudioBuffer<float> buffer(2, 256);
        for (int i = 0; i < 16; ++i)
            device->process(buffer);
        require(buffer.getMagnitude(0, 256) > .0001f, "held input sounds before injected overload");
        struct Stall final : juce::AudioIODeviceCallback
        {
            juce::AudioDeviceManager &deviceManager;
            bool armed = true, injected = false;
            explicit Stall(juce::AudioDeviceManager &d) : deviceManager(d)
            {
                d.addAudioCallback(this);
            }
            ~Stall() override
            {
                deviceManager.removeAudioCallback(this);
            }
            void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
            void audioDeviceStopped() override {}
            void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *output,
                                                  int channels, int samples,
                                                  const juce::AudioIODeviceCallbackContext &) override
            {
                for (int c = 0; c < channels; ++c)
                    if (output[c])
                        juce::FloatVectorOperations::clear(output[c], samples);
                if (armed)
                {
                    armed = false;
                    juce::Thread::sleep(120); // Deliberately over budget; test-only failure injection.
                    injected = true;
                }
            }
        } stall(manager.deviceManager);
        const auto before = audio.projectionDiagnostics();
        const int starts = device->starts, stops = device->stops;
        juce::String trace = "callback,engineCpuBefore,engineCpuAfter,deviceCpuAfter,wallMs,peak,"
                             "renderCallsBefore,renderCallsAfter,headSeconds\n";
        int skips = 0, silent = 0;
        bool exercisedHighCpu = false;
        double maximum = 0;
        for (int i = 0; i < 16; ++i)
        {
            const auto previous = audio.projectionDiagnostics();
            const auto cpu = manager.getCpuUsage();
            const auto began = juce::Time::getHighResolutionTicks();
            device->process(buffer);
            const auto milliseconds =
                1000 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began);
            maximum = std::max(maximum, milliseconds);
            const auto after = audio.projectionDiagnostics();
            skips += after.channels.front().renderCalls == previous.channels.front().renderCalls;
            silent += buffer.getMagnitude(0, 256) <= .0001f;
            exercisedHighCpu = exercisedHighCpu || cpu > .98;
            trace += juce::String(i) + "," + juce::String(cpu, 6) + "," +
                     juce::String(manager.getCpuUsage(), 6) + "," +
                     juce::String(manager.deviceManager.getCpuUsage(), 6) + "," +
                     juce::String(milliseconds, 6) + "," + juce::String(buffer.getMagnitude(0, 256), 9) +
                     "," + juce::String(juce::int64(previous.channels.front().renderCalls)) + "," +
                     juce::String(juce::int64(after.channels.front().renderCalls)) + "," +
                     juce::String(after.headSeconds, 9) + "\n";
        }
        require(
            folder.getChildFile("overload-recovery-" + juce::String(mode) + ".csv").replaceWithText(trace),
            "overload recovery trace retained before assertions");
        summary += juce::String(mode) + ": injected=" + juce::String(int(stall.injected)) +
                   " highCpu=" + juce::String(int(exercisedHighCpu)) + " skipped=" + juce::String(skips) +
                   " silent=" + juce::String(silent) + " maximumMs=" + juce::String(maximum, 6) + "\n";
        folder.getChildFile("overload-recovery.txt").replaceWithText(summary);
        require(stall.injected && exercisedHighCpu && maximum >= 100,
                "failure injection actually exercised the cached high-CPU branch condition");
        const auto after = audio.projectionDiagnostics();
        require(after.edit == before.edit && after.playbackContext == before.playbackContext &&
                    after.replacements == before.replacements && device->starts == starts &&
                    device->stops == stops,
                "overload recovery retains graph and device identities");
        require(skips == 0 && silent == 0,
                "cached CPU overload must not skip DSP or add silent recovery blocks");
        audio.stop();
    }
    require(folder.getChildFile("overload-recovery.txt")
                .appendText("PASS stopped/Pattern/Song failure injection. The deliberate120ms callback "
                            "is an overrun, not a passing normal deadline/listening/hardware result.\n"),
            "overload recovery summary saved");
}
