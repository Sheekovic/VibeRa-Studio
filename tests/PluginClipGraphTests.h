// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void nativeClipGraphTests(const juce::File &folder, const juce::File &binary, bool fixture)
{
    std::cout << "STAGE nativeClipGraphTests engineBytes=" << sizeof(AudioEngine) << std::endl;
    // Original steady sample gives an independent gain-ratio oracle in exported PCM.
    const auto sample = folder.getChildFile("native-clip-original.wav");
    {
        auto stream = sample.createOutputStream();
        require(stream != nullptr, "native clip original sample stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), 48000, 1, 16, {}, 0));
        require(writer != nullptr, "native clip original sample writer");
        stream.release();
        juce::AudioBuffer<float> data(1, 144000);
        juce::FloatVectorOperations::fill(data.getWritePointer(0), .2f, data.getNumSamples());
        require(writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples()), "native clip source PCM");
    }
    auto spec = parameterSpec(binary);
    juce::String report;
    Project rendered;
    for (bool pattern : {true, false})
    {
        auto engine = std::make_unique<AudioEngine>(false, juce::File{}, 256);
        auto &audio = *engine;
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement settings("DEVICESETUP");
        settings.setAttribute("deviceType", "VibeRa synthetic");
        settings.setAttribute("audioOutputDeviceName", "In-memory output");
        settings.setAttribute("audioDeviceRate", 48000.);
        settings.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, settings);
        manager.initialise(0, 2);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device != nullptr, "native clip synthetic device");
        Controller controller;
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels[0].source = sample.getFullPathName();
        project.patterns[0].length = 4;
        project.patterns[0].notes.push_back(
            {project.allocate(), project.channels[0].id, 0, 4, 60, .8f, false});
        project.placements.push_back({project.allocate(), project.patterns[0].id, 0, 0, 4, 0});
        spec.id = project.allocate();
        if (spec.instrument)
            project.channels[0].instrument = spec;
        else
            project.mixer[0].effects.push_back(spec);
        controller.replace(project);
        const auto rebuild = [&]
        {
            const auto end = juce::Time::getMillisecondCounter() + 30000;
            auto result = audio.rebuild(controller.project, pattern, project.patterns[0].id);
            while (AudioEngine::preparationPending(result) && juce::Time::getMillisecondCounter() < end)
            {
                dispatchMessages(5);
                result = audio.rebuild(controller.project, pattern, project.patterns[0].id);
            }
            require(result.wasOk(),
                    "native clip graph publication: " + result.getErrorMessage().toStdString());
        };
        rebuild();
        const auto host = audio.parameterHost(spec.id);
        require(host && host->alive(), "native clip owned worker");
        const auto metadata = parameterCatalogue(*host);
        const auto schemaText = [](const PluginParameterCatalogue &catalogue)
        {
            juce::String text = "count=" + juce::String(catalogue.reportedCount) +
                                " error=" + juce::String(catalogue.error) + "\n";
            for (const auto &parameter : catalogue.parameters)
                text += juce::String(juce::int64(parameter.id)) + "|" + juce::String(parameter.flags) + "|" +
                        juce::String(parameter.availability) + "|" + juce::String(parameter.steps) + "|" +
                        juce::String(parameter.unit) + "|" + juce::String(parameter.defaultValue, 9) + "|" +
                        parameter.name + "|" + parameter.label + "\n";
            return text;
        };
        require(folder
                    .getChildFile(pattern ? "native-clip-pattern-schema-before.txt"
                                          : "native-clip-song-schema-before.txt")
                    .replaceWithText(schemaText(metadata)),
                "native clip initial metadata evidence excludes changing current values");
        const auto target = std::find_if(
            metadata.parameters.begin(), metadata.parameters.end(),
            [&](const auto &p)
            {
                const PluginBinding b{p.id, p.flags, p.steps, p.value, p.name, p.label};
                return (!fixture || p.id == 0) && p.automatable() && validPluginBindings({&b, 1});
            });
        require(target != metadata.parameters.end(), "actual provider has a valid automatable native target");
        CommandArgs create;
        create.target = spec.id;
        create.length = 4;
        create.lane = 1;
        create.binding = PluginBinding{target->id,    target->flags, target->steps,
                                       target->value, target->name,  target->label};
        require(controller.execute("automation.plugin.create", create).wasOk(),
                "native clip graph canonical creation");
        if (fixture)
        {
            auto &source = controller.project.automation.sources[0];
            const auto first = source.points[0].id, last = source.points[1].id;
            CommandArgs point;
            point.target = first;
            point.beat = 0;
            point.value = .25;
            require(controller.execute("automation.point.move", point).wasOk(), "native curve first level");
            point.target = last;
            point.beat = 4;
            point.value = .75;
            require(controller.execute("automation.point.move", point).wasOk(), "native curve final level");
            point.target = controller.project.automation.sources[0].id;
            point.beat = 2;
            require(controller.execute("automation.point.add", point).wasOk(), "native curve midpoint");
            point.target = controller.project.automation.sources[0].points[1].id;
            point.text = "hold";
            require(controller.execute("automation.point.shape", point).wasOk(),
                    "native curve independent hold boundary");
        }
        rebuild();
        audio.seekBeat(0);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(25);
        juce::AudioBuffer<float> buffer(2, 256);
        for (int i = 0; i < 16; ++i)
        {
            device->process(buffer);
            dispatchMessages(6);
        }
        const auto before = audio.projectionDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto identity = host->runtimeIdentity();
        const auto applied = host->statistics().parametersApplied;
        const auto packetsBefore = host->statistics();
        const auto intact = controller.project.state();
        double maximum = 0, previous = audio.positionBeat();
        int loops = 0, silent = 0;
        juce::String trace("index,beat,wallMs,magnitude,missed,dropped\n");
        for (int i = 0; i < 448; ++i)
        {
            if (fixture && (i == 64 || i == 96))
            {
                const auto receipt =
                    parameterResult(*host, host->setParameter(42, i == 64 ? .25f : 0.f, metadata.generation));
                require(receipt.result == 0, "live graph redundant native metadata notification");
                juce::Thread::sleep(30);
                const auto currentMetadata = parameterCatalogue(*host, true);
                require(currentMetadata.generation == metadata.generation,
                        "redundant notification preserves the actual Pattern/Song native route");
            }
            if (i == 128)
            {
                CommandArgs edit;
                const auto &source = controller.project.automation.sources[0];
                edit.target = source.points.back().id;
                edit.beat = 4;
                edit.value = fixture ? .6 : (target->value > .5 ? .4 : .6);
                require(controller.execute("automation.point.move", edit).wasOk(), "live native point edit");
                rebuild();
            }
            if (i == 160)
            {
                controller.undoEdit();
                rebuild();
            }
            if (i == 192)
            {
                controller.redoEdit();
                rebuild();
            }
            if (i == 224)
            {
                controller.undoEdit();
                rebuild();
            }
            const auto start = juce::Time::getHighResolutionTicks();
            device->process(buffer);
            const double ms =
                1000 * juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
            maximum = std::max(maximum, ms);
            require(ms < 256000. / 48000., "native clip callback deadline");
            const auto beat = audio.positionBeat();
            const auto magnitude = buffer.getMagnitude(0, buffer.getNumSamples());
            if (magnitude < 1.e-7f)
                ++silent;
            const auto packets = host->statistics();
            trace += juce::String(i) + "," + juce::String(beat, 8) + "," + juce::String(ms, 6) + "," +
                     juce::String(magnitude, 8) + "," + juce::String(juce::int64(packets.missed)) + "," +
                     juce::String(juce::int64(packets.dropped)) + "\n";
            if (beat + .1 < previous)
                ++loops;
            previous = beat;
            dispatchMessages(6);
        }
        const auto after = audio.projectionDiagnostics();
        const auto packetsAfter = host->statistics();
        require(folder
                    .getChildFile(pattern ? "native-clip-pattern-callbacks.csv"
                                          : "native-clip-song-callbacks.csv")
                    .replaceWithText(trace),
                "native clip callback evidence");
        require(packetsAfter.missed == packetsBefore.missed &&
                    packetsAfter.dropped == packetsBefore.dropped && (!fixture || silent == 0),
                "native clip live window has no lost packets or unexpected fixture silence");
        require(intact.isEquivalentTo(controller.project.state()), "live native curve edit exact Undo/Redo");
        require(audio.playing() && before.edit == after.edit &&
                    before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements && device->starts == starts &&
                    device->stops == stops && audio.parameterHost(spec.id) == host &&
                    host->runtimeIdentity() == identity && loops > 0,
                "native curve edits retain transport/loop/graph/device/owned worker");
        const auto automation = host->automationStatistics();
        const auto finalPackets = host->statistics();
        PluginParameterCatalogue finalMetadata;
        host->readParameterCatalogue(finalMetadata);
        require(folder
                    .getChildFile(pattern ? "native-clip-pattern-schema-after.txt"
                                          : "native-clip-song-schema-after.txt")
                    .replaceWithText(schemaText(finalMetadata)),
                "native clip final metadata evidence excludes changing current values");
        const auto automationDiagnostic =
            juce::String(pattern ? "Pattern" : "Song") +
            " generation before=" + juce::String(metadata.generation) +
            " after=" + juce::String(finalMetadata.generation) +
            " target=" + juce::String(juce::int64(target->id)) +
            " applied before=" + juce::String(juce::int64(applied)) +
            " after=" + juce::String(juce::int64(finalPackets.parametersApplied)) +
            " rejected=" + juce::String(juce::int64(finalPackets.parametersRejected)) +
            " timing=" + juce::String(juce::int64(automation.invalidTiming)) +
            " capacity=" + juce::String(juce::int64(automation.capacityExceeded)) +
            " unavailable=" + juce::String(juce::int64(automation.unavailableTarget)) +
            " snapshot=" + juce::String(juce::int64(automation.snapshotUnavailable)) + "\n";
        require(
            folder
                .getChildFile(pattern ? "native-clip-pattern-delivery.txt" : "native-clip-song-delivery.txt")
                .replaceWithText(automationDiagnostic),
            "native clip delivery diagnostics including rejected and unavailable counts");
        require(host->statistics().parametersRejected == 0 && !host->automationStatistics().failed() &&
                    (pattern ? host->statistics().parametersApplied == applied
                             : host->statistics().parametersApplied > applied),
                "Pattern excludes Playlist curves; Song delivers native events without rejection");
        audio.stop();
        require(audio.requestPluginState(controller.project).wasOk(),
                "native curve project state capture request");
        juce::Result capture = juce::Result::ok();
        Project saved;
        bool captured = false;
        const auto end = juce::Time::getMillisecondCounter() + 15000;
        do
        {
            captured = audio.pollPluginState(saved, capture);
            if (!captured)
                dispatchMessages(5);
        } while (!captured && juce::Time::getMillisecondCounter() < end);
        require(captured && capture.wasOk() && !saved.plugin(spec.id)->state.isEmpty(),
                "native curve opaque state capture");
        const auto file =
            folder.getChildFile(pattern ? "native-clip-pattern.vibra" : "native-clip-song.vibra");
        Project loaded;
        require(saveProject(saved, file).wasOk() && loadProject(file, loaded).wasOk(),
                "native curve and opaque plugin state save/reopen");
        auto portable = saved;
        for (auto &channel : portable.channels)
            if (!channel.instrument && !channel.source.startsWith("builtin:"))
            {
                const auto *collected = loaded.channel(channel.id);
                require(collected && juce::SHA256(juce::File(collected->source)).toHexString() ==
                                         juce::SHA256(juce::File(channel.source)).toHexString(),
                        "collected native-clip sample retains exact original bytes");
                channel.source = collected->source;
            }
        require(portable.state().isEquivalentTo(loaded.state()),
                "native clip semantic roundtrip permits only verified media relocation");
        rendered = std::move(loaded);
        report += "Native target: " + target->name + " id=" + juce::String(juce::int64(target->id)) + "\n";
        report += (pattern ? "Pattern" : "Song") + juce::String(":448callbacks loops=") +
                  juce::String(loops) + " maximumMs=" + juce::String(maximum, 4) +
                  " retained graph/device/worker and exact Undo/Redo; native events=" +
                  juce::String(juce::int64(host->statistics().parametersApplied - applied)) +
                  " silentBlocks=" + juce::String(silent) +
                  " missedDelta=" + juce::String(juce::int64(packetsAfter.missed - packetsBefore.missed)) +
                  " droppedDelta=" + juce::String(juce::int64(packetsAfter.dropped - packetsBefore.dropped)) +
                  "\n";
    }
    // Offline rendering is unchanged; the later missing-plugin live branch
    // explicitly attaches this fixture's fixed256-frame provider.
    auto renderEngine = std::make_unique<AudioEngine>(false, juce::File{}, 256);
    auto &renderer = *renderEngine;
    const auto output = folder.getChildFile("native-clip-export.wav");
    const auto exported = renderer.renderSync(rendered, output);
    require(exported.wasOk(), "whole graph native curve WAV: " + exported.getErrorMessage().toStdString());
    if (fixture)
    {
        auto baseline = rendered;
        for (auto &point : baseline.automation.sources[0].points)
            point.value = 1;
        const auto full = folder.getChildFile("native-clip-reference.wav");
        require(renderer.renderSync(baseline, full).wasOk(), "independent full-gain reference WAV");
        const auto level = [](const juce::File &file, int start)
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> r(
                wav.createReaderFor(file.createInputStream().release(), true));
            require(r && r->numChannels == 2 && r->lengthInSamples >= 96000,
                    "native clip stereo WAV dimensions");
            juce::AudioBuffer<float> samples(2, 12000);
            require(r->read(&samples, 0, 12000, start, true, true), "native clip PCM read");
            return samples.getRMSLevel(0, 0, 12000);
        };
        const auto early = level(output, 12000) / level(full, 12000),
                   late = level(output, 60000) / level(full, 60000);
        require(std::abs(early - .25) < .015 && std::abs(late - .75) < .015,
                "whole Tracktion/native processor exported PCM follows hold curve .25/.75");
        report +=
            "Independent exported gain ratios=" + juce::String(early, 5) + "/" + juce::String(late, 5) + "\n";
    }
    auto missing = rendered;
    auto *savedPlugin = missing.plugin(spec.id);
    auto xml = juce::parseXML(savedPlugin->descriptor);
    require(xml != nullptr, "saved native descriptor XML");
    xml->setAttribute("file", folder.getChildFile("absent-native-clip-provider.vst3").getFullPathName());
    savedPlugin->descriptor = xml->toString();
    const auto prior = juce::MD5(output).toHexString();
    require(missing.validate().wasOk() && renderer.renderSync(missing, output).failed() &&
                juce::MD5(output).toHexString() == prior,
            "missing native plugin retains valid curves/state and cannot replace successful export");
    const auto missingState = missing.state();
    auto &missingManager = renderer.engine.getDeviceManager();
    missingManager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement missingSettings("DEVICESETUP");
    missingSettings.setAttribute("deviceType", "VibeRa synthetic");
    missingSettings.setAttribute("audioOutputDeviceName", "In-memory output");
    missingSettings.setAttribute("audioDeviceRate", 48000.);
    missingSettings.setAttribute("audioDeviceBufferSize", 256);
    renderer.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, missingSettings);
    missingManager.initialise(0, 2);
    auto missingResult = renderer.rebuild(missing, false, missing.patterns[0].id);
    const auto missingDeadline = juce::Time::getMillisecondCounter() + 30000;
    while (AudioEngine::preparationPending(missingResult) &&
           juce::Time::getMillisecondCounter() < missingDeadline)
    {
        dispatchMessages(5);
        missingResult = renderer.rebuild(missing, false, missing.patterns[0].id);
    }
    require(missingResult.wasOk() && !renderer.parameterHost(spec.id) &&
                renderer.pluginStatus(spec.id).containsIgnoreCase("Missing VST3") &&
                missingState.isEquivalentTo(missing.state()),
            "whole editing graph retains visible missing-native placeholder, exact state and curves");
    require(folder.getChildFile("native-clip-graph.txt")
                .replaceWithText(
                    report +
                    "PASS original SDK or named installed provider whole-graph native clip creation, live "
                    "Pattern/Song "
                    "edits/loops/Undo/Redo, opaque save/reopen and WAV. No physical input or listening.\n"),
            "native graph evidence saved");
}
