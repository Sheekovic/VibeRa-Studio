// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <thread>
static void samplePreviewTests(const juce::File &folder, const PluginSpec *plugin = nullptr)
{
    {
        SamplePreview preview;
        auto sample = std::make_shared<Sample>();
        sample->data.setSize(2, 4800);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::fill(sample->data.getWritePointer(c), .2f, 4800);
        std::weak_ptr<const Sample> retained = sample;
        require(preview.publish(sample), "preview prepared publication");
        sample.reset();
        juce::AudioBuffer<float> block(2, 256);
        auto tick = [&]
        {
            preview.audioDeviceIOCallbackWithContext(nullptr, 0, block.getArrayOfWritePointers(), 2, 256, {});
        };
        tick();
        require(preview.isActive() && block.getMagnitude(0, 256) > .1f && !retained.expired(),
                "preview renders and pins prepared sample");
        preview.stop();
        tick();
        require(!preview.isActive() && block.getMagnitude(0, 256) == 0 && !retained.expired(),
                "preview stop is silent without callback destruction");
        auto replacement = std::make_shared<Sample>(makeOriginalSample("kick"));
        require(preview.publish(replacement) && retained.expired(), "preview control-thread retirement");
        for (int i = 0; i < 100; ++i)
            tick();
        require(!preview.isActive(), "preview natural completion");
        require(!preview.publish({}), "preview rejects null resources");
        auto longSample = std::make_shared<Sample>();
        longSample->data.setSize(2, 48000 * 6);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::fill(longSample->data.getWritePointer(c), .2f, 48000 * 6);
        SyntheticDevice rateDevice;
        rateDevice.open({}, {}, 44100., 256);
        preview.audioDeviceAboutToStart(&rateDevice);
        require(preview.publish(longSample), "preview resampled long sample publication");
        int rendered = 0;
        do
        {
            tick();
            rendered += 256;
        } while (preview.isActive() && rendered < 300000);
        require(!preview.isActive() && rendered >= 44100 * 5 && rendered <= 44100 * 5 + 256,
                "preview five-second output duration at 44.1 kHz");

        preview.setFull(true);
        require(preview.publish(longSample), "preview full-length publication");
        rendered = 0;
        do
        {
            tick();
            rendered += 256;
        } while (preview.isActive() && rendered < 300000);
        require(!preview.isActive() && rendered >= 44100 * 6 - 256 && rendered <= 44100 * 6 + 256,
                "preview full duration at device rate");
        auto seekSample = std::make_shared<Sample>();
        seekSample->data.setSize(2, 8192);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < 8192; ++frame)
                seekSample->data.setSample(channel, frame, frame < 4096 ? .2f : -.4f);
        preview.setFull(false);
        require(preview.publish(seekSample, .75f, true), "preview prepared seek publication");
        tick();
        require(block.getSample(0, 0) == 0 && block.getSample(0, 200) < -.27f &&
                    preview.positionFraction() > .75f && preview.positionFraction() < .8f,
                "preview seek renders requested PCM with fade and bounded position");
        require(preview.playingSerial() == preview.publishedSerial(), "preview position identity");
        require(!preview.publish(seekSample, std::numeric_limits<float>::quiet_NaN()),
                "preview rejects nonfinite seek before handoff");
        require(preview.publish(seekSample, 1.f, true), "preview end seek publication");
        tick();
        require(!preview.isActive() && block.getMagnitude(0, 256) == 0,
                "preview end seek retires without invalid PCM read");
        require(preview.publish(longSample, 0, true), "preview play-to-end override publication");
        rendered = 0;
        do
        {
            tick();
            rendered += 256;
        } while (preview.isActive() && rendered < 300000);
        require(!preview.getFull() && rendered >= 44100 * 6 - 256,
                "preview Play override does not alter persistent full toggle");
        preview.setFull(true);
        require(preview.publish(longSample), "preview volume test publication");
        tick();
        preview.setVolume(.35f);
        for (int i = 0; i < 8; ++i)
            tick();
        require(preview.isActive() && std::abs(block.getMagnitude(0, 256) - .07f) < .001f,
                "preview volume changes sounding PCM without retrigger");
        preview.setVolume(0);
        for (int i = 0; i < 12; ++i)
            tick();
        require(preview.isActive() && block.getMagnitude(0, 256) < .000001f,
                "preview silent volume keeps playback alive");
        preview.setVolume(std::numeric_limits<float>::quiet_NaN());
        require(preview.getVolume() == 0, "preview ignores nonfinite volume");
        preview.setVolume(5);
        require(preview.getVolume() == 1, "preview volume upper clamp");
        preview.setVolume(-2);
        require(preview.getVolume() == 0, "preview volume lower clamp");
        preview.setVolume(.7f);
        preview.setFull(false);
        preview.stop();
        tick();
        std::atomic<bool> done{false};
        std::thread callback(
            [&]
            {
                while (!done.load())
                    tick();
                for (int i = 0; i < 100; ++i)
                    tick();
            });
        int published = 0;
        for (int i = 0; i < 3000; ++i)
        {
            published += preview.publish(replacement);
            if (i % 7 == 0)
                preview.stop();
        }
        done = true;
        callback.join();
        require(published > 0 && preview.publish(replacement), "preview bounded busy handoff recovers");
        preview.stop();
        tick();
        require(!preview.isActive(), "preview concurrent stop/replacement recovery");
    }
    {
        auto await = [](PreviewLoader &loader, std::shared_ptr<const Sample> &sample, juce::Result &result)
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
            while (juce::Time::getMillisecondCounterHiRes() < deadline)
            {
                if (loader.poll(sample, result))
                    return true;
                juce::Thread::sleep(1);
            }
            return false;
        };
        juce::WaitableEvent entered, release;
        PreviewLoader controlled(
            [&](const juce::String &source, std::shared_ptr<const Sample> &sample)
            {
                if (source == "held")
                {
                    entered.signal();
                    release.wait(-1);
                }
                if (source == "throw")
                    throw std::runtime_error("Injected decoder failure");
                sample = std::make_shared<Sample>(makeOriginalSample(source == "held" ? "bass" : "hat"));
                return juce::Result::ok();
            });
        controlled.request("held");
        const bool blocked = entered.wait(2000);
        controlled.cancel();
        controlled.request("latest");
        release.signal();
        require(blocked, "async preview fixture entered actual decoder work");
        std::shared_ptr<const Sample> prepared;
        auto decoded = juce::Result::ok();
        require(await(controlled, prepared, decoded) && decoded.wasOk() &&
                    prepared->data.getNumSamples() == 24000,
                "async preview discards in-flight cancelled generation");
        controlled.request("throw");
        require(await(controlled, prepared, decoded) && decoded.failed(),
                "async preview catches decoder failure");
        controlled.request("latest");
        require(await(controlled, prepared, decoded) && decoded.wasOk(),
                "async preview worker survives failure");
        controlled.cancel();
        require(!controlled.poll(prepared, decoded), "async preview Cancel clears completion");
        const auto file = folder.getChildFile("async-preview-original.wav");
        {
            juce::WavAudioFormat format;
            auto stream = file.createOutputStream();
            require(bool(stream), "async preview WAV fixture output");
            stream->setPosition(0);
            stream->truncate();
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.release(), 48000., 2, 16, {}, 0));
            require(bool(writer), "async preview WAV writer");
            const auto original = makeOriginalSample("snare");
            require(writer->writeFromAudioSampleBuffer(original.data, 0, original.data.getNumSamples()),
                    "async preview WAV PCM");
        }
        PreviewLoader real;
        real.request(file.getFullPathName());
        require(await(real, prepared, decoded) && decoded.wasOk() && prepared->data.getNumSamples() == 24000,
                "async preview real file decode");
        const auto cached = prepared;
        real.request(file.getFullPathName());
        require(await(real, prepared, decoded) && decoded.wasOk() && prepared == cached,
                "async preview immutable WAV cache reuse");
        real.request(file.getSiblingFile("absent-preview.wav").getFullPathName());
        require(await(real, prepared, decoded) && decoded.failed(), "async preview missing file failure");
        real.request(file.getFullPathName());
        require(await(real, prepared, decoded) && decoded.wasOk(),
                "async preview recovery after missing file");
    }
    {
        Sample polarity;
        polarity.data.setSize(2, 1024);
        for (int frame = 0; frame < 1024; ++frame)
        {
            polarity.data.setSample(0, frame, frame < 512 ? -.25f : .5f);
            polarity.data.setSample(1, frame, frame < 512 ? .25f : -.5f);
        }
        const auto waveform = PreviewWaveform::prepare(polarity, "builtin:polarity");
        require(waveform.name == "polarity" && waveform.seconds == 1024. / 48000. &&
                    waveform.peaks[0].low == -.25f && waveform.peaks[0].high == .25f &&
                    waveform.peaks[511].low == -.5f && waveform.peaks[511].high == .5f,
                "preview waveform preserves stereo polarity and full duration");
    }
    juce::String report;
    for (bool patternMode : {true, false})
    {
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
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device != nullptr, "preview virtual output device");
        auto project = Project::empty();
        project.channels[0].source = "builtin:bass";
        if (plugin)
        {
            project.channels[0].instrument = *plugin;
            project.channels[0].instrument->id = project.allocate();
        }
        auto &pattern = project.patterns.front();
        pattern.notes.push_back({project.allocate(), project.channels[0].id, 0, 4, 48, .5f, false});
        project.placements.push_back({project.allocate(), pattern.id, 0, 0, 4, 0});
        require(awaitProjection(audio, project, patternMode, pattern.id).wasOk(), "preview main projection");
        if (plugin)
        {
            const auto status = audio.pluginStatus(project.channels[0].instrument->id);
            folder.getChildFile("preview-provider-setup.txt").replaceWithText(status);
            require(audio.pluginDiagnostics().size() == 1,
                    "preview provider must be loaded, not a placeholder: " + status.toStdString());
        }
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        for (int i = 0; i < 12; ++i)
        {
            device->process(block);
            if (plugin)
                dispatchMessages(6);
        }
        const auto before = audio.projectionDiagnostics();
        const auto beforePackets = audio.pluginDiagnostics();
        const int starts = device->starts, stops = device->stops;
        const auto originalProject = project.state();
        double maximum = 0;
        int silent = 0, overruns = 0, auditionCallbacks = 0, seeks = 0;
        for (int i = 0; i < 420; ++i)
        {
            if (i % 2 == 0)
                audio.requestAudition(i % 4 ? "builtin:kick" : "builtin:hat", i % 8 == 0);
            if (i % 17 == 0)
                audio.stopAudition();
            if (i % 60 == 0)
            {
                audio.setAuditionVolume(i % 120 ? .35f : .7f);
                audio.setFullAudition(i % 120 != 0);
            }
            require(audio.pollAudition().wasOk(), "async preview handoff during playback");
            const bool sought = i % 61 == 0 && audio.canReplayAudition();
            if (sought)
            {
                require(audio.seekAudition(.5f).wasOk(), "preview seek while main transport plays");
                ++seeks;
            }
            const auto start = juce::Time::getMillisecondCounterHiRes();
            device->process(block);
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            if (sought)
                require(audio.auditioning() && audio.auditionPosition() > .5f,
                        "preview seek reaches callback for current request");
            maximum = std::max(maximum, elapsed);
            overruns += elapsed > 256. / 48000. * 1000.;
            silent += block.getMagnitude(0, 256) == 0;
            auditionCallbacks += audio.auditioning();
            dispatchMessages(plugin ? 6 : 1);
        }
        require(auditionCallbacks > 0 && seeks > 0,
                "async preview and seeking actually rendered during live edits");
        audio.stopAudition();
        device->process(block);
        const auto after = audio.projectionDiagnostics();
        if (plugin)
        {
            const auto afterPackets = audio.pluginDiagnostics();
            juce::String packets = "before hosts " + juce::String(int(beforePackets.size())) + ", after " +
                                   juce::String(int(afterPackets.size())) + "; silent " +
                                   juce::String(silent) + "; max callback " + juce::String(maximum, 4) +
                                   " ms\n";
            for (const auto &host : beforePackets)
                packets += "before PID " + juce::String(host.processId) + ": missed " +
                           juce::String(host.packets.missed) + ", dropped " +
                           juce::String(host.packets.dropped) + "\n";
            for (const auto &host : afterPackets)
                packets += "after PID " + juce::String(host.processId) + ": missed " +
                           juce::String(host.packets.missed) + ", dropped " +
                           juce::String(host.packets.dropped) + ", alive " + juce::String(int(host.alive)) +
                           "\n";
            folder.getChildFile(patternMode ? "pattern-preview-packets.txt" : "song-preview-packets.txt")
                .replaceWithText(packets);
            require(beforePackets.size() == 1 && afterPackets.size() == 1 && afterPackets[0].alive &&
                        afterPackets[0].processId == beforePackets[0].processId &&
                        afterPackets[0].packets.missed == beforePackets[0].packets.missed &&
                        afterPackets[0].packets.dropped == beforePackets[0].packets.dropped,
                    "preview preserves real VST3 worker and packets");
        }
        require(audio.playing() && after.edit == before.edit &&
                    after.playbackContext == before.playbackContext &&
                    after.replacements == before.replacements && device->starts == starts &&
                    device->stops == stops,
                "preview preserves transport/graph/device lifecycle");
        require(after.channels[0].graphInitialisations == before.channels[0].graphInitialisations &&
                    after.channels[0].sampler == before.channels[0].sampler &&
                    block.getMagnitude(0, 256) > 0 && project.state().isEquivalentTo(originalProject),
                "preview preserves sustained sequence/project");
        require(silent == 0 && overruns == 0, "preview virtual callback deadline/non-silence");
        require(audio.audition("builtin:unknown").failed(), "preview invalid source rejection");
        audio.stop();
        device->process(block);
        require(!audio.auditioning(), "global Stop cancels preview");
        require(audio.audition("builtin:snare").wasOk(), "preview works with main stopped");
        device->process(block);
        require(audio.auditioning() && block.getMagnitude(0, 256) > 0, "stopped preview actual mixed PCM");
        if (patternMode && !plugin)
        {
            audio.stop();
            device->process(block);
            const auto fullFile = folder.getChildFile("preview-six-second-original.wav");
            {
                juce::AudioBuffer<float> pcm(2, 48000 * 6);
                for (int channel = 0; channel < 2; ++channel)
                    juce::FloatVectorOperations::fill(pcm.getWritePointer(channel), .2f, pcm.getNumSamples());
                auto stream = fullFile.createOutputStream();
                require(bool(stream), "preview full-file stream");
                stream->setPosition(0);
                stream->truncate();
                juce::WavAudioFormat format;
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    format.createWriterFor(stream.release(), 48000., 2, 16, {}, 0));
                require(bool(writer) && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                        "preview full-file WAV fixture");
            }
            audio.setFullAudition(false);
            auto duration = [&](bool fullRequest)
            {
                audio.requestAudition(fullFile.getFullPathName(), fullRequest);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
                while (!audio.canReplayAudition() && juce::Time::getMillisecondCounterHiRes() < deadline)
                {
                    require(audio.pollAudition().wasOk(), "preview async full-file completion");
                    dispatchMessages(1);
                }
                require(audio.canReplayAudition(), "preview async full-file deadline");
                int frames = 0;
                do
                {
                    device->process(block);
                    frames += 256;
                } while (audio.auditioning() && frames < 48000 * 7);
                require(!audio.auditioning() && !audio.fullAudition(), "preview override leaves Full off");
                return frames;
            };
            const int fullFrames = duration(true), normalFrames = duration(false);
            require(fullFrames >= 48000 * 6 - 256 && fullFrames <= 48000 * 6 + 256 &&
                        normalFrames >= 48000 * 5 && normalFrames <= 48000 * 5 + 256,
                    "preview async per-request full override and subsequent normal reset");
            const auto baselineFile = folder.getChildFile("preview-export-baseline.wav");
            const auto previewFile = folder.getChildFile("preview-export-audition.wav");
            require(audio.renderSync(project, baselineFile).wasOk(), "preview baseline export");
            require(audio.audition(baselineFile.getFullPathName()).wasOk(), "preview decoded actual WAV");
            device->process(block);
            require(audio.auditioning() && block.getMagnitude(0, 256) > 0, "preview WAV output PCM");
            const auto preparations = audio.projectionDiagnostics().cache.preparations;
            require(audio.audition(baselineFile.getFullPathName()).wasOk() &&
                        audio.projectionDiagnostics().cache.preparations == preparations,
                    "preview reuses decoded WAV data");
            device->process(block);
            require(audio.renderSync(project, previewFile).wasOk(), "preview active export");
            double rateA = 0, rateB = 0;
            const auto a = readWav(baselineFile, rateA), b = readWav(previewFile, rateB);
            require(rateA == rateB && a.getNumSamples() == b.getNumSamples(), "preview export dimensions");
            for (int c = 0; c < a.getNumChannels(); ++c)
                require(std::equal(a.getReadPointer(c), a.getReadPointer(c) + a.getNumSamples(),
                                   b.getReadPointer(c)),
                        "preview excluded from offline exported PCM");
        }
        report += (plugin ? plugin->name + " " : juce::String{}) +
                  juce::String(patternMode ? "Pattern" : "Song") + ": 420 callbacks / 210 selections, max " +
                  juce::String(maximum, 4) + " ms, preview-active callbacks " +
                  juce::String(auditionCallbacks) + ", silence " + juce::String(silent) + ", overruns " +
                  juce::String(overruns) + ", callback-applied seeks " + juce::String(seeks) +
                  "; lifecycle preserved. Synthetic output; no listening/hardware.\n";
    }
    require(folder.getChildFile("sample-preview-verification.txt").replaceWithText(report),
            "preview evidence");
}
