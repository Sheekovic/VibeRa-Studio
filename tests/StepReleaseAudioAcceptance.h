// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveMidi.h"
#include "Commands.h"

// Original prepared score/MIDI/PCM fixtures. No plugin, device or listening claim.
static void stepReleaseAudioTests(const juce::File &)
{
    using namespace vibera;
    const auto wave = [](int frame) { return float(frame % 257 - 128) / 1024.f; };
    Sample sample;
    sample.sampleRate = 48000;
    sample.oneShot = false;
    sample.data.setSize(2, 48000);
    for (int frame = 0; frame < 48000; ++frame)
        for (int side = 0; side < 2; ++side)
            sample.data.setSample(side, frame, wave(frame));
    sample.prepareNormalization();
    SamplerSettings settings;
    settings.envelope = true;
    settings.attack = settings.decay = settings.release = 0;
    settings.sustain = 1;
    const auto process = [&](LiveTimeline &timeline, LiveSequencer &sequencer, LiveMidi &midi,
                             VoiceBank &bank, juce::AudioBuffer<float> &pcm, int begin, int frames,
                             bool playing = true)
    {
        pcm.clear();
        int count = -1;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        callbackAllocationProbe = true;
        {
            auto lease = timeline.acquire();
            if (lease.document)
            {
                sequencer.process(*lease.document, lease.document->channels.front().id, begin / 48000.,
                                  frames, 48000, playing, bank, pcm, 0);
                count = midi.process(*lease.document, lease.document->channels.front().id, begin / 48000.,
                                     frames, 48000, playing);
            }
        }
        callbackAllocationProbe = false;
        require(count >= 0 && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions && midi.overflow == 0,
                "Release prepared lease/score/MIDI processing allocates/destroys no measured C++ objects or "
                "overflows");
        return count;
    };
    struct Observed
    {
        int frame, status, pitch, velocity;
    };
    const auto append = [](std::vector<Observed> &observed, const LiveMidi &midi, int begin, int count)
    {
        for (int index = 0; index < count; ++index)
        {
            const auto &event = midi.events[size_t(index)];
            observed.push_back({begin + event.frame, event.bytes[0], event.bytes[1], event.bytes[2]});
        }
    };
    struct ReleaseCase
    {
        double normalized;
        int byte;
    };
    for (bool patternMode : {true, false})
        for (int block : {128, 256})
            for (int repeat : {0, 10})
                for (const auto test : {ReleaseCase{0, 0}, ReleaseCase{.5, 64}, ReleaseCase{1, 127}})
                {
                    auto project = Project::empty();
                    auto &part = project.patterns.front();
                    part.autoLength = false;
                    part.length = 1;
                    const auto channel = project.channels.front().id, source = project.allocate();
                    part.notes = {{source, channel, 0, .25, 60, 1.f, true, false, 0, 0, 0, repeat}};
                    if (test.normalized != 0)
                        part.notes.front().releaseVelocity = test.normalized;
                    project.placements = {{project.allocate(), part.id, 0, 0, 1, 0}};
                    const auto before = project.state();
                    auto document = compileLiveDocument(project, patternMode, part.id, 1);
                    const auto *lane = document->find(channel);
                    const int interval = repeat == 10 ? 3000 : 6000, hits = 6000 / interval;
                    require(lane && lane->notes.size() == size_t(hits),
                            "Release fixture compiles exact literal hit count");
                    for (int ordinal = 0; ordinal < hits; ++ordinal)
                    {
                        const auto *note = lane->find(source, patternMode ? 0 : project.placements.front().id,
                                                      std::uint32_t(ordinal));
                        require(note && note->noteOffVelocity == test.byte &&
                                    std::abs(note->start * 48000 - ordinal * interval) < 1.e-9 &&
                                    std::abs(note->end * 48000 - (ordinal + 1) * interval) < 1.e-9,
                                "Real prepared neutral/Repeat hit retains source identity and independently "
                                "specified release byte/time");
                    }
                    LiveTimeline timeline;
                    require(timeline.publish(document) && !document &&
                                timeline.controlPublicationSerial() == 1,
                            "Actual timeline owns the prepared Release document before callback acquisition");
                    VoiceBank bank;
                    bank.prepare(sample, 48000);
                    bank.configure(PreparedSamplerSettings(settings));
                    LiveSequencer sequencer;
                    LiveMidi midi;
                    juce::AudioBuffer<float> pcm(2, block);
                    std::vector<Observed> events;
                    for (int begin = 0; begin < 6400; begin += block)
                    {
                        const auto count = process(timeline, sequencer, midi, bank, pcm, begin, block);
                        append(events, midi, begin, count);
                        for (int frame = 0; frame < block; ++frame)
                        {
                            const int position = begin + frame;
                            const float expected = position < 6000 ? wave(position % interval) : 0;
                            require(std::abs(pcm.getSample(0, frame) - expected) < 1.e-6 &&
                                        std::abs(pcm.getSample(1, frame) - expected) < 1.e-6,
                                    "Release data does not alter literal signed sampler PCM or its natural "
                                    "gate endpoint");
                        }
                    }
                    require(events.size() == size_t(hits * 2) && bank.activeVoices() == 0 &&
                                project.state().isEquivalentTo(before),
                            "Natural release leaves exact source state and no stuck sampler/MIDI gates");
                    for (int hit = 0; hit < hits; ++hit)
                    {
                        const auto &on = events[size_t(hit * 2)], &off = events[size_t(hit * 2 + 1)];
                        require(on.status == 0x90 && on.pitch == 60 && on.velocity == 127 &&
                                    on.frame == hit * interval && off.status == 0x80 && off.pitch == 60 &&
                                    off.velocity == test.byte && off.frame == (hit + 1) * interval,
                                "Literal natural note-offs are0/64/127, including off-before-next-on "
                                "repeated boundaries");
                    }
                }

    for (bool patternMode : {true, false})
    {
        auto project = Project::empty();
        auto &part = project.patterns.front();
        part.autoLength = false;
        part.length = 1;
        const auto pattern = part.id, channel = project.channels.front().id;
        const auto edited = project.allocate(), held = project.allocate();
        part.notes = {{edited, channel, 0, .5, 60, 1.f, true, false, 0, 0, 0, 0, 0},
                      {held, channel, 0, 1, 72, 1.f, false, false, 0, 0, 0, 0, 1}};
        project.placements = {{project.allocate(), pattern, 0, 0, 1, 0}};
        Controller controller;
        controller.replace(project);
        LiveTimeline timeline;
        std::uint64_t generation = 0;
        const auto publish = [&]()
        {
            auto document = compileLiveDocument(controller.project, patternMode, pattern, ++generation);
            require(timeline.publish(document) && !document &&
                        timeline.controlPublicationSerial() == generation,
                    "Actual changed Release document publishes outside the callback");
        };
        publish();
        VoiceBank bank;
        bank.prepare(sample, 48000);
        bank.configure(PreparedSamplerSettings(settings));
        LiveSequencer sequencer;
        LiveMidi midi;
        juce::AudioBuffer<float> pcm(2, 256);
        std::vector<Observed> events;
        for (int begin = 0; begin < 12288; begin += 256)
        {
            const bool changed = begin == 2048 || begin == 4096 || begin == 6144;
            if (begin == 2048)
            {
                CommandArgs args;
                args.pattern = pattern;
                args.channel = channel;
                args.ids = {edited};
                args.value = .5;
                require(controller.execute("steps.release", args).wasOk(),
                        "Actual canonical held Release edit");
                publish();
            }
            if (begin == 4096)
            {
                require(controller.undoEditChecked().wasOk(), "Held Release edit Undo");
                publish();
            }
            if (begin == 6144)
            {
                require(controller.redoEditChecked().wasOk(), "Held Release edit Redo");
                publish();
            }
            const auto count = process(timeline, sequencer, midi, bank, pcm, begin, 256);
            require(!changed || count == 0,
                    "Release-only edit/Undo/Redo never emits a spurious note-on or forced note-off");
            append(events, midi, begin, count);
            double heldPosition = -1, editedPosition = -1;
            bank.visitActive(
                [&](const auto &voice)
                {
                    if (!voice.releasing && voice.note == held)
                        heldPosition = voice.position;
                    if (!voice.releasing && voice.note == edited)
                        editedPosition = voice.position;
                });
            require(
                heldPosition == 2. * (begin + 256) && (begin + 256 >= 12000 || editedPosition == begin + 256),
                "Release publications preserve exact unrelated octave-held and edited sampler source phases");
            for (int frame = 0; frame < 256; ++frame)
            {
                const int position = begin + frame;
                const float expected = (position < 12000 ? wave(position) : 0) + wave(position * 2);
                require(std::abs(pcm.getSample(0, frame) - expected) < 1.e-6 &&
                            std::abs(pcm.getSample(1, frame) - expected) < 1.e-6,
                        "Held Release edits preserve every signed PCM frame through real prepared "
                        "reconciliation");
            }
        }
        require(events.size() == 3 && events[0].status == 0x90 && events[0].pitch == 60 &&
                    events[0].frame == 0 && events[1].status == 0x90 && events[1].pitch == 72 &&
                    events[1].frame == 0 && events[2].status == 0x80 && events[2].pitch == 60 &&
                    events[2].frame == 12000 && events[2].velocity == 64,
                "A held Release edit changes only its eventual natural note-off, retaining all original "
                "note-ons");
        CommandArgs remove;
        remove.pattern = pattern;
        remove.channel = channel;
        remove.ids = {held};
        require(controller.execute("notes.delete", remove).wasOk(),
                "Delete the actual remaining held source");
        publish();
        require(process(timeline, sequencer, midi, bank, pcm, 12288, 256) == 1 && midi.events[0].frame == 0 &&
                    midi.events[0].bytes[0] == 0x80 && midi.events[0].bytes[1] == 72 &&
                    midi.events[0].bytes[2] == 0 && bank.activeVoices() == 0,
                "Forced source deletion sends ordinary safety0, never the source's nonzero natural Release");
        require(process(timeline, sequencer, midi, bank, pcm, 12544, 256, false) == 0,
                "Stop after deletion cannot duplicate a retired gate's note-off");

        LiveTimeline safetyTimeline;
        auto safetyProject = project;
        safetyProject.pattern(pattern)->notes.front().releaseVelocity = .5;
        auto safetyDocument = compileLiveDocument(safetyProject, patternMode, pattern, 1);
        require(safetyTimeline.publish(safetyDocument),
                "Publish independent ordinary seek/loop safety fixture");
        VoiceBank safetyBank;
        safetyBank.prepare(sample, 48000);
        safetyBank.configure(PreparedSamplerSettings(settings));
        LiveSequencer safetySequencer;
        LiveMidi safetyMidi;
        require(process(safetyTimeline, safetySequencer, safetyMidi, safetyBank, pcm, 0, 256) == 2,
                "Both original nonzero-release score gates start before safety transitions");
        const auto forced = [&](int begin, bool playing, bool retrigger)
        {
            const auto count =
                process(safetyTimeline, safetySequencer, safetyMidi, safetyBank, pcm, begin, 256, playing);
            require(count == (retrigger ? 4 : 2),
                    "Ordinary safety transition has exact bounded off/chase count");
            for (int index = 0; index < 2; ++index)
                require(
                    safetyMidi.events[size_t(index)].frame == 0 &&
                        safetyMidi.events[size_t(index)].bytes[0] == 0x80 &&
                        safetyMidi.events[size_t(index)].bytes[1] == (index == 0 ? 60 : 72) &&
                        safetyMidi.events[size_t(index)].bytes[2] == 0,
                    "Forced seek/loop/Stop cleanup uses safety0 irrespective of prepared natural release");
            if (retrigger)
                for (int index = 2; index < 4; ++index)
                    require(safetyMidi.events[size_t(index)].frame == 0 &&
                                safetyMidi.events[size_t(index)].bytes[0] == 0x90 &&
                                safetyMidi.events[size_t(index)].bytes[1] == (index == 2 ? 60 : 72),
                            "Ordinary seek/loop chases each original source exactly once after closing prior "
                            "gates");
        };
        forced(1024, true, true); // Actual discontinuity, not a contiguous transport update.
        forced(1280, false, false);
        require(safetyBank.activeVoices() == 0, "Stop closes all actual prepared sampler safety voices");
        require(process(safetyTimeline, safetySequencer, safetyMidi, safetyBank, pcm, 0, 256) == 2,
                "Explicit restart opens original source gates once");
        require(process(safetyTimeline, safetySequencer, safetyMidi, safetyBank, pcm, 256, 256) == 0,
                "Contiguous playback has no synthetic release/retrigger");
        forced(0, true, true);
        forced(256, false, false);
        require(safetyBank.activeVoices() == 0, "Loop then Stop leaves neither live score gate stuck");
    }
    // A source publication at a natural edge must retain its release velocity.
    for (bool patternMode : {true, false})
    {
        auto project = Project::empty();
        project.tempo = 120;
        auto &part = project.patterns.front();
        part.autoLength = false;
        part.length = 1;
        const auto channel = project.channels.front().id, pattern = part.id;
        part.notes = {{project.allocate(), channel, 0, .032, 60, 1.f, false}};
        part.notes.front().releaseVelocity = 1;
        project.placements = {{project.allocate(), pattern, 0, 0, 1, 0}};
        auto initial = compileLiveDocument(project, patternMode, pattern, 1);
        LiveMidi midi;
        require(midi.process(*initial, channel, 0, 256, 48000, true) == 1 && midi.events[0].bytes[0] == 0x90,
                "Exact-boundary fixture starts its actual prepared gate");
        require(midi.process(*initial, channel, 256. / 48000, 256, 48000, true) == 0 &&
                    midi.process(*initial, channel, 512. / 48000, 256, 48000, true) == 0,
                "Held gate reaches literal frame768 without an early off");
        project.pattern(pattern)->notes.front().releaseVelocity = .5;
        project.channels.front().name = "Unrelated name update";
        auto changed = compileLiveDocument(project, patternMode, pattern, 2);
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        callbackAllocationProbe = true;
        const auto count = midi.process(*changed, channel, 768. / 48000, 256, 48000, true);
        callbackAllocationProbe = false;
        require(count == 1 && midi.events[0].frame == 0 && midi.events[0].bytes[0] == 0x80 &&
                    midi.events[0].bytes[1] == 60 && midi.events[0].bytes[2] == 64 && midi.overflow == 0 &&
                    callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Publication at the exact natural edge emits release64 once without retrigger or callback "
                "allocation/destruction");
        require(midi.process(*changed, channel, 1024. / 48000, 256, 48000, true) == 0 &&
                    midi.process(*changed, channel, 1280. / 48000, 256, 48000, false) == 0,
                "Boundary publication leaves no stuck gate or duplicate Stop off");
    }
}
