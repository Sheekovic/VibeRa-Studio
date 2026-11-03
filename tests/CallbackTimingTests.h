// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

static void callbackTimingTests(const juce::File &folder)
{
    struct Fixture final : juce::AudioIODeviceCallback
    {
        bool pause = false;
        void audioDeviceAboutToStart(juce::AudioIODevice *) override {}
        void audioDeviceStopped() override {}
        void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *, int, int,
                                              const juce::AudioIODeviceCallbackContext &) override
        {
            if (pause)
                juce::Thread::sleep(30); // Deliberate test injection, not production DSP.
        }
    } fixture;
    SyntheticDevice device;
    juce::AudioBuffer<float> output(2, 256);
    device.start(&fixture);
    juce::String report = "injection,outerMs," + juce::String(SyntheticCallbackTiming::csvHeader) + "\n";
    for (int phase = 0; phase < 3; ++phase)
    {
        fixture.pause = phase == 1;
        const auto began = juce::Time::getHighResolutionTicks();
        if (phase == 0)
            juce::Thread::sleep(30);
        device.process(output);
        if (phase == 2)
            juce::Thread::sleep(30);
        const auto ended = juce::Time::getHighResolutionTicks();
        const auto &timing = device.lastTiming();
        require(timing.containedBy(began, ended), "callback phase timestamps nest inside the wrapper");
        const auto injected =
            phase == 0   ? SyntheticCallbackTiming::milliseconds(began, timing.processBegan)
            : phase == 1 ? SyntheticCallbackTiming::milliseconds(timing.dispatchBegan, timing.dispatchEnded)
                         : SyntheticCallbackTiming::milliseconds(timing.processEnded, ended);
        require(injected >= 20, "known pause is attributed to its actual wrapper/dispatch phase");
        const auto sum = SyntheticCallbackTiming::milliseconds(began, timing.processBegan) +
                         SyntheticCallbackTiming::milliseconds(timing.processBegan, timing.dispatchBegan) +
                         SyntheticCallbackTiming::milliseconds(timing.dispatchBegan, timing.dispatchEnded) +
                         SyntheticCallbackTiming::milliseconds(timing.dispatchEnded, timing.processEnded) +
                         SyntheticCallbackTiming::milliseconds(timing.processEnded, ended);
        require(std::abs(sum - SyntheticCallbackTiming::milliseconds(began, ended)) < .000001,
                "phase durations account for the full original measured interval without subtracting stalls");
        report += juce::String(phase) + "," +
                  juce::String(SyntheticCallbackTiming::milliseconds(began, ended), 6) + "," +
                  timing.csv(began, ended) + "\n";
    }
    device.stop();
    require(folder.getChildFile("callback-timing-selftest.csv").replaceWithText(report),
            "callback timing attribution injection receipts retained");

    // A manually driven device has no automatic first callback. CONTROL must
    // return promptly without inventing device time so its caller can drive it.
    AudioEngine audio(false);
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20); // Complete normal device setup before measuring registration.
    auto *manual = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(manual && manual->getCurrentSampleRate() == 48000. &&
                manual->getCurrentBufferSizeSamples() == 256,
            "context registration uses the declared manual synthetic configuration");
    auto project = Project::empty();
    require(audio.rebuild(project, true, project.patterns.front().id).wasOk(),
            "context registration prepares the real native graph");
    const auto streamBefore = manager.getCurrentStreamTime();
    const auto controlBegan = juce::Time::getMillisecondCounterHiRes();
    audio.playStop();
    dispatchMessages(20); // Service transport setup without driving a device callback.
    const auto controlMs = juce::Time::getMillisecondCounterHiRes() - controlBegan;
    require(controlMs < 1000, "manual-device Play returns within its CONTROL observation bound");
    require(audio.playing() && manager.getCurrentStreamTime() == streamBefore,
            "context registration cannot manufacture a callback or advance device time");
    manual->process(output);
    const auto advancedSamples = (manager.getCurrentStreamTime() - streamBefore) * 48000.;
    auto *registration = new juce::DynamicObject;
    registration->setProperty("controlMilliseconds", controlMs);
    registration->setProperty("advancedSamples", advancedSamples);
    registration->setProperty("managerSampleRate", manager.getSampleRate());
    registration->setProperty("streamBefore", streamBefore);
    registration->setProperty("streamAfter", manager.getCurrentStreamTime());
    registration->setProperty("scope", "Native CONTROL registration with manual synthetic device; "
                                       "not a physical callback deadline, listening or hardware check");
    require(folder.getChildFile("context-registration.json")
                .replaceWithText(juce::JSON::toString(juce::var(registration))),
            "retain actual context registration and first-callback evidence");
    require(std::abs(advancedSamples - 256.) < .000001,
            "first actual manual callback advances exactly one real device block");
    audio.stop();
}
