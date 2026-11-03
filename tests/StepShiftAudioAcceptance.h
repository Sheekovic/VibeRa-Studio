// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include "LiveMidi.h"
#include "MidiScore.h"

// Independent literal sample/tick oracles. No physical device/listening claim.
static void stepShiftAudioTests()
{
    using namespace vibera;
    Sample sample;
    sample.sampleRate = 48000;
    sample.oneShot = false;
    sample.data.setSize(2, 48000);
    for (int side = 0; side < 2; ++side)
        for (int frame = 0; frame < 48000; ++frame)
            sample.data.setSample(side, frame, .2f);
    sample.prepareNormalization();
    SamplerSettings settings;
    settings.envelope = true;
    settings.attack = settings.decay = settings.release = 0;
    settings.sustain = 1;
    for (bool patternMode : {true, false})
        for (bool maximum : {false, true})
        {
            auto project = Project::empty();
            auto &part = project.patterns.front();
            part.autoLength = false;
            part.length = 1;
            const auto channel = project.channels.front().id;
            part.notes = {
                {project.allocate(), channel, 0, .25, 60, 1.f, true, false, 0, 0, maximum ? 100. : 50.}};
            project.placements = {{project.allocate(), part.id, 0, 0, 1, 0}};
            auto document = compileLiveDocument(project, patternMode, part.id, 1);
            VoiceBank voices;
            voices.prepare(sample, 48000);
            voices.configure(PreparedSamplerSettings(settings));
            LiveSequencer sequence;
            LiveMidi midi;
            juce::AudioBuffer<float> pcm(2, 256);
            const int expectedOn = maximum ? 5940 : 2970;
            const int expectedOff = maximum ? 11940 : 8970;
            int on = -1, off = -1, onCount = 0, offCount = 0;
            for (int begin = 0; begin < 12288; begin += 256)
            {
                pcm.clear();
                const auto allocations = callbackAllocations.load();
                const auto destructions = callbackDestructions.load();
                callbackAllocationProbe = true;
                sequence.process(*document, channel, begin / 48000., 256, 48000, true, voices, pcm, 0);
                const auto count = midi.process(*document, channel, begin / 48000., 256, 48000, true);
                callbackAllocationProbe = false;
                require(callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == destructions,
                        "Prepared Shift callback has no measured C++ allocation/destruction");
                for (int i = 0; i < count; ++i)
                {
                    const auto &event = midi.events[size_t(i)];
                    if ((event.bytes[0] & 0xf0) == 0x90)
                    {
                        on = begin + event.frame;
                        ++onCount;
                    }
                    if ((event.bytes[0] & 0xf0) == 0x80)
                    {
                        off = begin + event.frame;
                        ++offCount;
                    }
                }
                for (int frame = 0; frame < 256; ++frame)
                {
                    const int position = begin + frame;
                    const float expected = position >= expectedOn && position < expectedOff ? .2f : 0.f;
                    require(std::abs(pcm.getSample(0, frame) - expected) < 1.e-6 &&
                                std::abs(pcm.getSample(1, frame) - expected) < 1.e-6,
                            "Pattern/Song PCM obeys literal shifted onset and end sample");
                }
            }
            require(onCount == 1 && offCount == 1 && on == expectedOn && off == expectedOff &&
                        midi.overflow == 0 && voices.activeVoices() == 0,
                    "Native MIDI schedule shares exact shifted edges, no duplicate/stuck gates");

            juce::MemoryBlock bytes;
            int omitted = -1;
            require(writeMidiScore(part, channel, 120, 4, 4, bytes, &omitted).wasOk() && omitted == 0,
                    "MIDI score encodes Shift rather than classifying it as omitted expression");
            juce::MemoryInputStream stream(bytes, false);
            juce::MidiFile file;
            require(file.readFrom(stream) && file.getTimeFormat() == 960,
                    "Actual shifted MIDI has the declared 960PPQ format");
            double tickOn = -1, tickOff = -1;
            for (int index = 0; index < file.getTrack(0)->getNumEvents(); ++index)
            {
                const auto &message = file.getTrack(0)->getEventPointer(index)->message;
                if (message.isNoteOn())
                    tickOn = message.getTimeStamp();
                if (message.isNoteOff())
                    tickOff = message.getTimeStamp();
            }
            require(tickOn == (maximum ? 238 : 119) && tickOff == (maximum ? 478 : 359),
                    "Encoded SMF edges match independently rounded literal ticks");
        }
}
