// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

static void pluginGraphRollbackTests(const juce::File &folder, const juce::File &binary)
{
    juce::String report;
    for (bool patternMode : {true, false})
    {
        AudioEngine audio(false, juce::File{}, 256); // This fixture only processes256-frame blocks.
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
        require(device != nullptr, "plugin rollback uses synthetic output only");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.patterns.front().notes.push_back(
            {project.allocate(), project.channels.front().id, 0, 4, 48, .8f, false});
        project.placements.push_back({project.allocate(), project.patterns.front().id, 0, 0, 4, 0});
        auto spec = parameterSpec(binary);
        require(!spec.instrument, "rollback native fixture is a master effect");
        spec.id = project.allocate();
        project.mixer.front().effects.push_back(spec);
        const auto rebuild = [&](const Project &candidate)
        {
            auto deadline = juce::Time::getMillisecondCounter() + 30000;
            auto result = audio.rebuild(candidate, patternMode, candidate.patterns.front().id);
            while (AudioEngine::preparationPending(result) && juce::Time::getMillisecondCounter() < deadline)
            {
                dispatchMessages(5);
                result = audio.rebuild(candidate, patternMode, candidate.patterns.front().id);
            }
            return result;
        };
        require(rebuild(project).wasOk(), "rollback initial plugin graph prepared");
        auto host = audio.parameterHost(spec.id);
        require(host && host->alive(), "rollback retains the actual owned fixture worker");
        const auto catalogue = parameterCatalogue(*host);
        audio.consumePluginChanges(); // Establish a known observation boundary before the edit.
        const auto revision = host->parameterRevision();
        const auto edited = parameterResult(*host, host->setParameter(0, .25f, catalogue.generation));
        require(edited.result == 0 && edited.value == .25f && host->parameterRevision() != revision,
                "unconsumed native manual edit advances the original worker revision");
        // Do not consume that edit until after a new host was observed by a failed graph build.
        audio.playStop();
        juce::AudioBuffer<float> block(2, 256);
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(6);
        }
        const auto before = audio.projectionDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto pid = host->processId();
        auto rejected = project;
        // Equivalent XML with a distinct descriptor string forces a separately
        // prepared worker, without altering or replacing the live instance.
        rejected.mixer.front().effects.front().descriptor = "\n" + spec.descriptor;
        auto missing = rejected.channels.front();
        missing.id = rejected.allocate();
        missing.name = "Unavailable replacement source";
        missing.source = folder.getChildFile("deliberately-absent-rollback.wav").getFullPathName();
        require(!juce::File(missing.source).exists(), "rollback missing-source fixture is absent");
        rejected.channels.push_back(std::move(missing));
        require(rejected.validate().wasOk(), "rollback failure occurs during graph resource preparation");
        const auto result = rebuild(rejected);
        require(result.failed() && result.getErrorMessage().startsWith("Sample unavailable:"),
                "replacement graph fails after master worker preparation at the missing source");
        const auto after = audio.projectionDiagnostics();
        require(audio.parameterHost(spec.id) == host && host->alive() && host->processId() == pid &&
                    after.edit == before.edit && after.playbackContext == before.playbackContext &&
                    after.replacements == before.replacements && device->starts == starts &&
                    device->stops == stops && audio.playing(),
                "failed graph preparation preserves live worker, transport, Edit, context and device");
        const bool pendingEdit = audio.consumePluginChanges();
        report += (patternMode ? "Pattern" : "Song") + juce::String(" pending original edit=") +
                  juce::String(int(pendingEdit)) +
                  "; previous worker/graph/device retained; error=" + result.getErrorMessage() + "\n";
        folder.getChildFile("plugin-graph-rollback.txt").replaceWithText(report);
        require(pendingEdit, "failed graph preparation must preserve unconsumed original plugin changes");
        require(!audio.consumePluginChanges(), "retained plugin edit is consumed exactly once");
        audio.reclaimPluginHosts();
        require(!audio.consumePluginChanges(), "control-thread reclamation preserves consumed revision");
        require(rebuild(project).wasOk() && audio.parameterHost(spec.id) == host,
                "valid project recovers with the same original plugin worker");
        audio.stop();
    }
    require(folder.getChildFile("plugin-graph-rollback.txt")
                .appendText("PASS both modes; isolated native SDK fixture and synthetic output. No physical "
                            "hardware, listening or installed-provider compatibility claim.\n"),
            "plugin rollback evidence saved");
}
