// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "../tests/SyntheticDevice.h"
#include <array>
#include <cmath>

namespace vibera
{
juce::Result Studio::verifyBrowserLive(const juce::File &folder, const juce::File &fixture)
{
    if (folder.createDirectory().failed())
        return juce::Result::fail("Browser live output");
    const auto library = folder.getChildFile("Library-" + juce::Uuid().toString());
    if (library.createDirectory().failed())
        return juce::Result::fail("Browser owned live library");
    auto sample = library.getChildFile("Kick audition.wav");
    {
        juce::WavAudioFormat format;
        auto stream = sample.createOutputStream();
        if (!stream)
            return juce::Result::fail("Browser live WAV stream");
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 1, 16, {}, 0));
        if (!writer)
            return juce::Result::fail("Browser live WAV writer");
        juce::AudioBuffer<float> pcm(1, 12000);
        for (int i = 0; i < pcm.getNumSamples(); ++i)
            pcm.setSample(0, i, .1f * std::sin(float(i) * .021f));
        if (!writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()))
            return juce::Result::fail("Browser live PCM");
    }
    if (!sample.copyFileTo(library.getChildFile("Hat audition.wav")))
        return juce::Result::fail("Browser second sample");
    for (int i = 0; i < 1537; ++i)
        if (!library.getChildFile("Metadata " + juce::String(i) + ".mid")
                 .replaceWithText("Index metadata fixture; not playable MIDI"))
            return juce::Result::fail("Browser live index metadata");
    std::optional<PluginSpec> instrument;
    if (fixture != juce::File{})
    {
        Project loaded;
        if (loadProject(fixture, loaded).failed())
            return juce::Result::fail("Browser provider fixture load");
        for (const auto &channel : loaded.channels)
            if (channel.instrument)
            {
                instrument = channel.instrument;
                break;
            }
        if (!instrument)
            return juce::Result::fail("Browser provider fixture has no instrument");
    }
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
    if (!device)
        return juce::Result::fail("Browser synthetic output");
    setSize(1280, 684);
    maximiseEditor(1);
    resized();
    auto &files = browser->filesForTest();
    files.setRoot(library);
    const auto metadataFile = folder.getChildFile("browser-live-prefs-" + juce::Uuid().toString() + ".json");
    const auto prefs = files.useOwnedPreferencesForTest(metadataFile);
    if (prefs.failed())
        return prefs;
    if (files.setTagsForTest(sample, {"Drum", "Tight"}).failed() ||
        files.setTagsForTest(library.getChildFile("Hat audition.wav"), {"Drum", "Loose"}).failed())
        return juce::Result::fail("Browser live tag preparation");
    if (!files.collectionsForTest().starred(sample))
        files.starForTest(sample);
    files.viewAction("browser.find");
    struct Row
    {
        double ms = 0, beat = 0;
        float left = 0, right = 0;
    };
    class Capture final : public juce::Thread
    {
        SyntheticDevice &device;
        AudioEngine &audio;
        juce::AudioBuffer<float> block{2, 256};

      public:
        std::array<Row, 768> rows{};
        std::atomic<int> count{0};
        std::atomic<bool> done{false};
        explicit Capture(SyntheticDevice &d, AudioEngine &a)
            : Thread("Browser virtual callback"), device(d), audio(a)
        {
        }
        ~Capture() override
        {
            signalThreadShouldExit();
            waitForThreadToExit(-1);
        }
        void run() override
        {
            for (int i = 0; i < int(rows.size()) && !threadShouldExit(); ++i)
            {
                const auto began = juce::Time::getMillisecondCounterHiRes();
                device.process(block);
                rows[size_t(i)] = {juce::Time::getMillisecondCounterHiRes() - began, audio.positionBeat(),
                                   block.getMagnitude(0, 0, 256), block.getMagnitude(1, 0, 256)};
                count = i + 1;
                wait(6);
            }
            done = true;
        }
    };
    juce::String report;
    bool allPassed = true;
    for (const bool pattern : {true, false})
    {
        audio.stop();
        auto project = Project::empty();
        project.channels.resize(1);
        auto &channel = project.channels[0];
        channel.source = "builtin:bass";
        channel.gain = .5f;
        channel.pan = 1;
        if (instrument)
        {
            channel.instrument = instrument;
            channel.instrument->id = project.allocate();
        }
        project.tempo = 240;
        project.patterns[0].length = 1;
        project.patterns[0].notes.push_back({project.allocate(), channel.id, 0, 1, 48, .8f, false});
        project.placements.push_back({project.allocate(), project.patterns[0].id, 0, 0, 1, 0});
        controller.replace(project);
        selectedPattern = project.patterns[0].id;
        selectedChannel = channel.id;
        patternMode = pattern;
        refresh();
        auto prepared = audio.rebuild(controller.project, patternMode, selectedPattern);
        const auto end = juce::Time::getMillisecondCounter() + 15000;
        while (AudioEngine::preparationPending(prepared) && juce::Time::getMillisecondCounter() < end)
        {
            dispatchMessages(5);
            prepared = audio.rebuild(controller.project, patternMode, selectedPattern);
        }
        if (prepared.failed())
            return prepared;
        needsRebuild = false;
        engineRevision = controller.revision;
        audio.setMidiTarget(selectedChannel);
        audio.setMetronome(false);
        audio.setLooping(true);
        audio.playStop();
        juce::AudioBuffer<float> warm(2, 256);
        for (int i = 0; i < 20; ++i)
        {
            device->process(warm);
            dispatchMessages(6);
        }
        audio.midiInput.capture.begin();
        audio.typingInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
        audio.typingInput.route(juce::MidiMessage::noteOn(1, 64, juce::uint8(80)), true, true);
        for (int i = 0; i < 8; ++i)
            device->process(warm);
        const auto before = audio.projectionDiagnostics();
        const auto workers = audio.pluginDiagnostics();
        const auto writes = audio.midiInput.capture.write.load(),
                   typingWrites = audio.typingInput.capture.write.load();
        const auto starts = device->starts, stops = device->stops;
        const auto auditionBefore = audio.auditionRequestId();
        const auto document = controller.project.state();
        Capture capture(*device, audio);
        int action = 0, selection = 0;
        bool auditioned = false;
        bool savedThisMode = false;
        int metadataEdits = 0;
        bool tagModeChanged = false;
        auto next = juce::Time::getMillisecondCounter();
        capture.startThread();
        while (!capture.done)
        {
            // A bounded pump keeps continuous WM_PAINT traffic from starving
            // the test's interaction driver. This is not a product event loop.
#if JUCE_WINDOWS
            MSG message;
            const auto pumpStart = juce::Time::getMillisecondCounter();
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessage(&message);
                if (juce::Time::getMillisecondCounter() - pumpStart >= 2)
                    break;
            }
            juce::Thread::sleep(1);
#else
            dispatchMessages(2);
#endif
            // Observe completed preparations throughout the scenario. A later
            // selection legitimately clears its waveform while a replacement
            // request is pending; the final instant alone cannot prove that
            // none of the earlier selections prepared successfully.
            auditioned = auditioned || (audio.auditionRequestId() > auditionBefore &&
                                        audio.auditionInfo().name.endsWith("audition.wav"));
            const auto now = juce::Time::getMillisecondCounter();
            if (now < next)
                continue;
            next = now + 75;
            switch (action++ % 10)
            {
            case 0:
            {
                const auto edited = files.setTagsForTest(
                    sample, metadataEdits % 2 == 0 ? juce::StringArray{"Drum", "Tight", "Edited"}
                                                   : juce::StringArray{"Drum", "Tight"});
                if (edited.failed())
                    return edited;
                ++metadataEdits;
                const auto filter = files.filterTagsForTest({{"Drum", "Tight"}, false});
                if (filter.failed())
                    return filter;
                files.filterForTest("(k?ck OR bass) AND NOT absent type:wav");
                break;
            }
            case 1:
                files.viewAction("browser.reindex");
                break;
            case 2:
                if (files.viewsForTest().getCurrentTabIndex() != 1)
                {
                    files.viewAction("browser.showStarred");
                    const auto filter = files.filterTagsForTest({{"Drum", "Tight"}, false});
                    if (filter.failed())
                        return filter;
                }
                if (const auto *data = files.searchDataForTest();
                    data && data->error.isEmpty() && data->matches.size() == 1)
                {
                    if (!tagModeChanged)
                    {
                        files.viewAction("browser.matchAllTags");
                        tagModeChanged = true;
                        --action;
                        next = now + 25;
                        break;
                    }
                    files.resultsForTest().selectRow(0);
                    ++selection;
                }
                else
                {
                    --action;
                    next = now + 25;
                }
                break;
            case 3:
                files.viewAction("browser.showAll");
                if (files.filterTagsForTest({{"Drum", "Tight"}, false}).failed())
                    return juce::Result::fail("Browser live All tag filter");
                files.filterForTest("(kick OR clap) NOT absent type:wav");
                break;
            case 4:
            case 5:
                if (const auto *data = files.searchDataForTest();
                    data && data->error.isEmpty() && data->matches.size() == 1)
                {
                    files.resultsForTest().deselectAllRows();
                    files.viewAction("browser.nextMatch");
                    ++selection;
                    if (!savedThisMode)
                    {
                        const auto saved = files.saveSearchForTest(pattern ? "Pattern drums" : "Song drums");
                        if (saved.failed())
                            return saved;
                        savedThisMode = true;
                    }
                }
                else
                {
                    --action;
                    next = now + 25;
                }
                break;
            case 6:
                files.filterForTest("h?t AND NOT kick type:wav");
                break;
            case 7:
                files.viewAction("browser.showTags");
                break;
            case 8:
                files.viewAction("browser.clearSearch");
                break;
            case 9:
                files.filterForTest("kick type:wav");
                break;
            default:
                break;
            }
        }
        capture.waitForThreadToExit(-1);
        const auto after = audio.projectionDiagnostics();
        auto afterWorkers = audio.pluginDiagnostics();
        int loops = 0, silent = 0, overruns = 0;
        double maximum = 0, previous = capture.rows[0].beat;
        juce::String csv = "callback,wallMs,beat,left,right\n";
        for (int i = 0; i < capture.count; ++i)
        {
            const auto &row = capture.rows[size_t(i)];
            loops += row.beat < previous - .25;
            previous = row.beat;
            silent += i > 8 && row.right < .0000001f;
            overruns += row.ms > 256000. / 48000.;
            maximum = std::max(maximum, row.ms);
            csv += juce::String(i) + "," + juce::String(row.ms, 6) + "," + juce::String(row.beat, 7) + "," +
                   juce::String(row.left, 9) + "," + juce::String(row.right, 9) + "\n";
        }
        bool retained = before.edit == after.edit && before.playbackContext == after.playbackContext &&
                        before.replacements == after.replacements && before.updates == after.updates &&
                        before.samplerCreations == after.samplerCreations &&
                        before.cache.preparations == after.cache.preparations && device->starts == starts &&
                        device->stops == stops && audio.playing();
        retained = retained && before.channels.size() == after.channels.size();
        for (size_t i = 0; i < before.channels.size() && retained; ++i)
            retained = before.channels[i].track == after.channels[i].track &&
                       before.channels[i].sampler == after.channels[i].sampler &&
                       before.channels[i].sample == after.channels[i].sample &&
                       before.channels[i].graphInitialisations == after.channels[i].graphInitialisations;
        bool workerRetained = workers.size() == afterWorkers.size();
        for (size_t i = 0; i < workers.size() && workerRetained; ++i)
            workerRetained = workers[i].processId == afterWorkers[i].processId && afterWorkers[i].alive;
        const bool captureRetained = audio.midiInput.capture.active && audio.typingInput.capture.active &&
                                     audio.midiInput.capture.write == writes &&
                                     audio.typingInput.capture.write == typingWrites;
        const bool unchanged = document.isEquivalentTo(controller.project.state());
        auditioned = auditioned || (audio.auditionRequestId() > auditionBefore &&
                                    audio.auditionInfo().name.endsWith("audition.wav"));
        const bool passed = retained && workerRetained && captureRetained && unchanged && auditioned &&
                            savedThisMode && metadataEdits > 0 && tagModeChanged && loops > 0 &&
                            selection > 0 && overruns == 0 && silent == 0 && capture.count == 768;
        const auto mode = pattern ? "pattern" : "song";
        if (!folder.getChildFile(juce::String("browser-live-") + mode + ".csv").replaceWithText(csv))
            return juce::Result::fail("Browser callback evidence write failed");
        report += juce::String(mode) + ": callbacks=" + juce::String(capture.count.load()) +
                  " loops=" + juce::String(loops) + " actions=" + juce::String(action) +
                  " selections=" + juce::String(selection) + " maxMs=" + juce::String(maximum, 4) +
                  " overruns=" + juce::String(overruns) + " silentRight=" + juce::String(silent) +
                  " lifecycle=" + juce::String(int(retained)) +
                  " workers=" + juce::String(int(workerRetained)) +
                  " heldCapture=" + juce::String(int(captureRetained)) +
                  " document=" + juce::String(int(unchanged)) + "\n";
        report += "auditionPrepared=" + juce::String(int(auditioned)) + "\n";
        report += "savedSearchCreated=" + juce::String(int(savedThisMode)) + "\n";
        report += "metadataEdits=" + juce::String(metadataEdits) +
                  " tagModeChanged=" + juce::String(int(tagModeChanged)) + "\n";
        allPassed = allPassed && passed;
        audio.midiInput.capture.active = false;
        audio.typingInput.capture.active = false;
        audio.midiInput.panic();
        audio.typingInput.panic();
        audio.stopAudition();
        audio.stop();
        files.viewAction("browser.clearSearch");
    }
    const auto persisted = juce::JSON::parse(metadataFile);
    const bool metadataSaved = int(persisted["version"]) == 3 && persisted["fileTags"].isArray() &&
                               persisted["fileTags"].size() == 2 && persisted["searches"].isArray() &&
                               persisted["searches"].size() >= 2;
    allPassed = allPassed && metadataSaved;
    report += "metadataFileSaved=" + juce::String(int(metadataSaved)) +
              "; actual atomic writes to owned preferences during callbacks.\n";
    report += "Actual Browser query/reindex/cancel/result-selection handlers with independent synthetic "
              "callbacks and injected held MIDI/typing. Metadata files are index fixtures. No OS input, "
              "physical hardware or listening.\n";
    if (!folder.getChildFile("browser-live-verification.txt").replaceWithText(report))
        return juce::Result::fail("Browser live report write failed");
    return allPassed ? juce::Result::ok()
                     : juce::Result::fail(
                           "Browser live invariants/deadline/search result failed; inspect report and CSV");
}
} // namespace vibera
