// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveMidi.h"
#include "MidiScore.h"
#include "RepeatMidiBudget.h"

// Original literal sample/tick oracles. No real-provider/hardware claim.
static void stepRepeatAudioTests()
{
    using namespace vibera;
    Sample sample;
    sample.sampleRate = 48000;
    sample.oneShot = false;
    sample.data.setSize(2, 48000);
    for (int frame = 0; frame < 48000; ++frame)
        for (int side = 0; side < 2; ++side)
            sample.data.setSample(side, frame, .1f + frame * .00001f);
    sample.prepareNormalization();
    SamplerSettings settings;
    settings.envelope = true;
    settings.attack = settings.decay = settings.release = 0;
    settings.sustain = 1;
    for (bool patternMode : {true, false})
        for (int mode : {0, 10, 13, 14})
        {
            auto project = Project::empty();
            auto &part = project.patterns.front();
            part.autoLength = false;
            part.length = 1;
            const auto channel = project.channels.front().id;
            const auto source = project.allocate();
            part.notes = {{source, channel, 0, .25, 60, 1.f, true, false, 0, 0, 0, mode}};
            project.placements = {{project.allocate(), part.id, 0, 0, 1, 0}};
            const int interval = mode == 10 ? 3000 : mode == 13 ? 1500 : mode == 14 ? 1000 : 6000;
            const int hits = 6000 / interval;
            const auto document = compileLiveDocument(project, patternMode, part.id, 1);
            const auto *lane = document->find(channel);
            require(lane && lane->notes.size() == size_t(hits), "Repeat compiles distinct prepared hits");
            for (int ordinal = 0; ordinal < hits; ++ordinal)
            {
                const auto *note = lane->find(source, patternMode ? 0 : project.placements.front().id,
                                              std::uint32_t(ordinal));
                require(note && note->repeatOrdinal == std::uint32_t(ordinal) &&
                            std::abs(note->start * 48000 - ordinal * interval) < 1.e-9,
                        "Prepared source/placement/ordinal identity retains independent literal onsets");
            }
            VoiceBank bank;
            bank.prepare(sample, 48000);
            bank.configure(PreparedSamplerSettings(settings));
            LiveSequencer sequence;
            LiveMidi midi;
            juce::AudioBuffer<float> pcm(2, 256);
            std::vector<int> ons, offs;
            for (int begin = 0; begin < 6400; begin += 256)
            {
                pcm.clear();
                const auto allocations = callbackAllocations.load(),
                           destructions = callbackDestructions.load();
                callbackAllocationProbe = true;
                sequence.process(*document, channel, begin / 48000., 256, 48000, true, bank, pcm, 0);
                const auto count = midi.process(*document, channel, begin / 48000., 256, 48000, true);
                callbackAllocationProbe = false;
                require(callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == destructions,
                        "Prepared Repeat processing allocates/destroys no measured C++ objects");
                for (int index = 0; index < count; ++index)
                {
                    const auto &event = midi.events[size_t(index)];
                    if ((event.bytes[0] & 0xf0) == 0x90)
                        ons.push_back(begin + event.frame);
                    if ((event.bytes[0] & 0xf0) == 0x80)
                        offs.push_back(begin + event.frame);
                }
                for (int frame = 0; frame < 256; ++frame)
                {
                    const int position = begin + frame;
                    const float expected = position < 6000 ? .1f + (position % interval) * .00001f : 0;
                    require(std::abs(pcm.getSample(0, frame) - expected) < 1.e-6 &&
                                std::abs(pcm.getSample(1, frame) - expected) < 1.e-6,
                            "Repeat retriggers actual ramp PCM at independently specified sample boundaries");
                }
            }
            require(ons.size() == size_t(hits) && offs.size() == size_t(hits) && midi.overflow == 0 &&
                        bank.activeVoices() == 0,
                    "Repeat score and native MIDI have exact hit count with no stuck gates or overflow");
            for (int index = 0; index < hits; ++index)
                require(ons[size_t(index)] == index * interval &&
                            offs[size_t(index)] == (index + 1) * interval,
                        "Native Repeat note-on/off samples match literal subdivision including final end");
        }

    // Actual native metadata is unnecessary to test the pure capacity contract.
    // No processor is loaded and no fixture is claimed as third-party support.
    auto project = Project::empty();
    auto &part = project.patterns.front();
    part.autoLength = false;
    part.length = 1;
    const auto channel = project.channels.front().id;
    project.channels.front().instrument = PluginSpec{};
    project.channels.front().instrument->id = project.allocate();
    project.tempo = 400;
    for (int note = 0; note < 9; ++note)
        part.notes.push_back({project.allocate(), channel, 0, 1, 60 + note, .5f, true, false, 0, 0, 0, 14});
    const auto before = project.state();
    require(validateRepeatMidiBudget(project, true, part.id, 256, 48000).wasOk() &&
                validateRepeatMidiBudget(project, true, part.id, 8192, 44100).failed() &&
                project.state().isEquivalentTo(before),
            "Dense native repeats qualify at the small block and refuse the large packet before mutation");
    for (auto &note : part.notes)
        note.repeatMode = 0;
    require(validateRepeatMidiBudget(project, true, part.id, 8192, 44100).wasOk(),
            "New Repeat admission does not silently redefine legacy neutral score acceptance");

    // Sparse edges alone do not bound seek/edit chase work. These long notes
    // overlap despite their starts/ends being far apart in callback time.
    part.length = 300;
    project.tempo = 120;
    part.notes = {{project.allocate(), channel, 0, .25, 60, .5f, true, false, 0, 0, 0, 14}};
    for (int index = 0; index < 128; ++index)
        part.notes.push_back({project.allocate(), channel, 1 + index * .25, 100, 60, .5f});
    require(validateRepeatMidiBudget(project, true, part.id, 256, 48000).wasOk(),
            "128 sparse-edge overlapping native gates leave bounded reconciliation headroom");
    part.notes.push_back({project.allocate(), channel, 33, 100, 60, .5f});
    const auto tooManyGates = validateRepeatMidiBudget(project, true, part.id, 256, 48000);
    require(tooManyGates.failed() && tooManyGates.getErrorMessage().contains("repeat.midiGateCapacity"),
            "129 spanning native sources refuse before an unbounded generation/seek chase");
    part.notes.front().repeatMode = 0;
    require(validateRepeatMidiBudget(project, true, part.id, 256, 48000).wasOk(),
            "Legacy neutral overlapping score keeps its original admission policy");

    for (bool patternMode : {true, false})
    {
        auto edited = Project::empty();
        auto &score = edited.patterns.front();
        score.autoLength = false;
        score.length = 1;
        const auto lane = edited.channels.front().id;
        const auto repeated = edited.allocate(), held = edited.allocate();
        score.notes = {{repeated, lane, 0, .25, 60, 1.f, true, false, 0, 0, 0, 13},
                       {held, lane, 0, 1, 72, 1.f}};
        edited.placements = {{edited.allocate(), score.id, 0, 0, 1, 0}};
        const auto original = edited;
        auto doc = compileLiveDocument(edited, patternMode, score.id, 1);
        VoiceBank bank;
        bank.prepare(sample, 48000);
        bank.configure(PreparedSamplerSettings(settings));
        LiveSequencer sequencer;
        LiveMidi midi;
        juce::AudioBuffer<float> pcm(2, 256);
        const auto process = [&](int begin, bool playing = true)
        {
            pcm.clear();
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            callbackAllocationProbe = true;
            sequencer.process(*doc, lane, begin / 48000., 256, 48000, playing, bank, pcm, 0);
            const auto count = midi.process(*doc, lane, begin / 48000., 256, 48000, playing);
            callbackAllocationProbe = false;
            require(callbackAllocations.load() == allocations &&
                        callbackDestructions.load() == destructions && midi.overflow == 0,
                    "Repeat edit/restore/delete/loop/stop has no measured callback "
                    "allocation/destruction/overflow");
            return count;
        };
        const auto heldPosition = [&]()
        {
            double position = -1;
            bank.visitActive(
                [&](const auto &voice)
                {
                    if (voice.note == held && !voice.releasing)
                        position = voice.position;
                });
            return position;
        };
        for (int begin = 0; begin < 4096; begin += 256)
            process(begin);
        require(heldPosition() == 8192,
                "Unchanged octave-held voice starts at its exact running sample phase");
        score.notes.front().repeatMode = 10;
        doc = compileLiveDocument(edited, patternMode, score.id, 2);
        require(process(4096) == 2 && midi.events[0].bytes[0] == 0x80 && midi.events[1].bytes[0] == 0x90 &&
                    midi.events[0].bytes[1] == 60 && midi.events[1].bytes[1] == 60 && heldPosition() == 8704,
                "Rate edit reconciles only the changed source ordinal and preserves held sample phase");
        require(std::abs(pcm.getSample(0, 0) - .28192f) < 1.e-6,
                "Actual changed Repeat PCM sums the uninterrupted held phase and newly chased hit");
        edited = original;
        doc = compileLiveDocument(edited, patternMode, edited.patterns.front().id, 3);
        require(process(4352) == 4 && heldPosition() == 9216,
                "Restoring the prior rate reconciles its ordinal and the next subdivision without resetting "
                "held audio");
        edited.patterns.front().notes.erase(edited.patterns.front().notes.begin());
        doc = compileLiveDocument(edited, patternMode, edited.patterns.front().id, 4);
        require(process(4608) == 1 && midi.events[0].bytes[0] == 0x80 && midi.events[0].bytes[1] == 60 &&
                    heldPosition() == 9728 && bank.activeVoices() == 1,
                "Deleting a sounding repeated source releases its own gate and leaves the held voice intact");
        require(process(0) == 2 && midi.events[0].bytes[1] == 72 && midi.events[1].bytes[1] == 72 &&
                    heldPosition() == 512,
                "Loop jump restarts the remaining source without resurrecting deleted Repeat gates");
        require(process(256, false) == 1 && bank.activeVoices() == 0,
                "Stop after edit and loop leaves neither sampler nor native score gates stuck");
    }
}
