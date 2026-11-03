// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdlib>

static void pluginStartupCancellationTests(const juce::File &folder, const juce::File &binary)
{
    auto spec = parameterSpec(binary);
    struct ScopedDelay
    {
        juce::String previous = juce::SystemStats::getEnvironmentVariable("VIBERA_FIXTURE_INIT_DELAY_MS", {});
        bool active = true;
        ScopedDelay()
        {
            require(_wputenv_s(L"VIBERA_FIXTURE_INIT_DELAY_MS", L"6000") == 0,
                    "owned fixture delay environment");
        }
        void restore()
        {
            if (active)
            {
                _wputenv_s(L"VIBERA_FIXTURE_INIT_DELAY_MS", previous.toWideCharPointer());
                active = false;
            }
        }
        ~ScopedDelay()
        {
            restore();
        }
    } delay;
    auto host = std::make_unique<PluginHost>();
    const auto begin = juce::Time::getMillisecondCounter();
    const auto result = host->start(spec, 48000, 256, false,
                                    [begin] { return juce::Time::getMillisecondCounter() - begin >= 100; });
    const auto responseMs = juce::Time::getMillisecondCounter() - begin;
    require(result.failed() && result.getErrorMessage().containsIgnoreCase("cancelled") && responseMs < 1000,
            "startup owner cancels before delayed fixture initializes");
    const auto pid = host->processId();
    require(pid != 0, "cancelled preparation launched a specifically owned worker");
    HANDLE retained = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    require(retained != nullptr, "retain exact cancelled worker object before cleanup");
    host.reset(); // Existing shared shutdown + bounded grace + verified owned timeout policy.
    const auto exited = WaitForSingleObject(retained, 2500) == WAIT_OBJECT_0;
    CloseHandle(retained);
    require(exited, "cancelled startup leaves no owned delayed worker running");

    auto project = Project::empty();
    spec.id = project.allocate();
    project.mixer[0].effects.push_back(spec);
    auto engine = std::make_unique<AudioEngine>(false);
    require(AudioEngine::preparationPending(engine->rebuild(project, false, project.patterns[0].id)),
            "engine starts pending fixture preparation");
    dispatchMessages(200);
    const auto closing = juce::Time::getMillisecondCounter();
    engine.reset();
    const auto closeMs = juce::Time::getMillisecondCounter() - closing;
    require(closeMs < 4000,
            "engine destruction cancels startup without waiting full initialization deadline");

    engine = std::make_unique<AudioEngine>(false, juce::File{}, 256);
    auto &manager = engine->engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement settings("DEVICESETUP");
    settings.setAttribute("deviceType", "VibeRa synthetic");
    settings.setAttribute("audioOutputDeviceName", "In-memory output");
    settings.setAttribute("audioDeviceRate", 48000.);
    settings.setAttribute("audioDeviceBufferSize", 256);
    engine->engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, settings);
    manager.initialise(0, 2);
    require(AudioEngine::preparationPending(engine->rebuild(project, false, project.patterns[0].id)),
            "superseded preparation starts");
    dispatchMessages(200);
    auto changed = project;
    changed.mixer[0].effects[0].descriptor = "\n" + spec.descriptor;
    require(AudioEngine::preparationPending(engine->rebuild(changed, false, project.patterns[0].id)),
            "new descriptor cancels in-flight startup");
    delay.restore();
    auto recovered = engine->rebuild(project, false, project.patterns[0].id);
    const auto retry = juce::Time::getMillisecondCounter();
    while (AudioEngine::preparationPending(recovered) && juce::Time::getMillisecondCounter() - retry < 10000)
    {
        dispatchMessages(10);
        recovered = engine->rebuild(project, false, project.patterns[0].id);
    }
    require(recovered.wasOk() && engine->parameterHost(spec.id) && engine->parameterHost(spec.id)->alive(),
            "returning to original selection starts fresh instead of reusing cancelled results");
    require(
        folder.getChildFile("native-startup-cancellation.txt")
            .replaceWithText("PASS original delayed SDK fixture: cancelled preparation responseMs=" +
                             juce::String(responseMs) +
                             "; owned worker terminated after graceful shutdown timeout; engine closeMs=" +
                             juce::String(closeMs) +
                             "; superseded/restored selection recovers through a new worker. No physical "
                             "audio or listening.\n"),
        "startup cancellation receipt");
}
