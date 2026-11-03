// SPDX-License-Identifier: AGPL-3.0-or-later
// Independent half-open callback contract; original20 oracle remains untouched.
#pragma once
#include "RetainedMidiAcceptance.h"

namespace vibera::draft::retained_midi
{
inline void retainedMidiHalfOpenTests(const juce::File &folder)
{
    if (folder.createDirectory().failed())
        throw std::runtime_error("Owned half-open MIDI evidence folder");
    constexpr int block = 64;
    const std::vector<Event> input{{0, Kind::on, 60, 1, 101},   {63, Kind::off, 60, 1, 101},
                                   {64, Kind::on, 64, 2, 202},  {127, Kind::off, 64, 2, 202},
                                   {128, Kind::on, 67, 3, 303}, {159, Kind::off, 67, 3, 303}};
    juce::String report = "Independent actual-player MIDI half-open callback oracle. block64; "
                          "valid raw/rounded offsets [0,64), exact enqueue+delay deadlines; "
                          "no device/plugin/RT/listening/native-input qualification.\n";
    bool allPassed = true;
    int attempted = 0, completed = 0;
    for (double rate : {48000., 44100.})
        for (int delay : {0, 128})
        {
            ++attempted;
            juce::String detail;
            try
            {
                auto script = std::make_shared<const std::vector<Event>>(input);
                tracktion::graph::LockFreeMultiThreadedNodePlayer player;
                player.setNumThreads(0);
                player.setNode(std::make_unique<Output>(script, rate, delay), rate, block);
                choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2, block));
                tracktion::MidiMessageArray midi;
                midi.reserve(256);
                std::vector<Observation> observed;
                observed.reserve(16);
                bool offsetsValid = true, noUnexpected = true;
                for (std::int64_t reference = 0; reference < 512; reference += block)
                {
                    pcm.clear();
                    midi.clear();
                    player.process({block, {reference, reference + block}, {pcm.getView(), midi}});
                    noUnexpected = noUnexpected && !midi.isAllNotesOff;
                    for (const auto &message : midi)
                    {
                        const double seconds = message.getTimeStamp();
                        const double offset = seconds * rate;
                        // Check the timestamp before rounding/casting: a NaN must
                        // not produce undefined conversion or be hidden as offset0.
                        const bool finite = std::isfinite(seconds) && std::isfinite(offset);
                        const std::int64_t rounded =
                            finite && std::abs(offset) < 1000000 ? std::llround(offset) : -1000001;
                        const bool halfOpen = finite && seconds >= 0 && seconds < double(block) / rate &&
                                              rounded >= 0 && rounded < block;
                        offsetsValid = offsetsValid && halfOpen;
                        Kind kind = Kind::cc123; // Unknown output cannot masquerade as a scripted note.
                        if (message.isNoteOn())
                            kind = Kind::on;
                        else if (message.isNoteOff())
                            kind = Kind::off;
                        else
                            noUnexpected = false;
                        const auto absolute = reference + rounded;
                        const int channel = message.getChannel(), key = message.getNoteNumber();
                        observed.push_back({{absolute, kind, key, channel, message.mpeSourceID}, reference});
                        detail += "  observed callback=" + juce::String(juce::int64(reference)) +
                                  " seconds=" + juce::String(seconds, 15) +
                                  " offsetSamples=" + juce::String(offset, 12) +
                                  " roundedOffset=" + juce::String(juce::int64(rounded)) +
                                  " absolute=" + juce::String(juce::int64(absolute)) +
                                  " kind=" + juce::String(int(kind)) + " key=" + juce::String(key) +
                                  " channel=" + juce::String(channel) +
                                  " source=" + juce::String(int(message.mpeSourceID)) +
                                  " halfOpen=" + juce::String(int(halfOpen)) + "\n";
                    }
                }
                bool tuples = observed.size() == input.size();
                for (auto expected : input)
                {
                    expected.sample += delay; // Independent absolute deadline, no queue algorithm.
                    const auto dueCallback = (expected.sample / block) * block;
                    tuples = tuples && std::count_if(observed.begin(), observed.end(),
                                                     [&](const Observation &actual)
                                                     {
                                                         return sameMessage(actual.event, expected) &&
                                                                actual.callbackStart == dueCallback;
                                                     }) == 1;
                    detail += "  expected absolute=" + juce::String(juce::int64(expected.sample)) +
                              " callback=" + juce::String(juce::int64(dueCallback)) +
                              " offset=" + juce::String(juce::int64(expected.sample - dueCallback)) +
                              " kind=" + juce::String(int(expected.kind)) +
                              " key=" + juce::String(expected.key) +
                              " channel=" + juce::String(expected.channel) +
                              " source=" + juce::String(int(expected.source)) + "\n";
                }
                const bool passed = offsetsValid && tuples && noUnexpected;
                allPassed = allPassed && passed;
                ++completed;
                report += "rate=" + juce::String(rate, 0) + " delay=" + juce::String(delay) +
                          " pass=" + juce::String(int(passed)) +
                          " halfOpen=" + juce::String(int(offsetsValid)) +
                          " exactTuplesAndCallback=" + juce::String(int(tuples)) +
                          " noUnexpected=" + juce::String(int(noUnexpected)) +
                          " count=" + juce::String(int(observed.size())) + "\n" + detail;
            }
            catch (const std::exception &error)
            {
                allPassed = false;
                report += "rate=" + juce::String(rate, 0) + " delay=" + juce::String(delay) +
                          " pass=0 failureClass=case-exception-unclassified diagnostic=" +
                          juce::String(error.what()) + "\n" + detail;
            }
            catch (...)
            {
                allPassed = false;
                report += "rate=" + juce::String(rate, 0) + " delay=" + juce::String(delay) +
                          " pass=0 failureClass=case-exception-unclassified diagnostic=unknown\n" + detail;
            }
        }
    report +=
        "attempted=" + juce::String(attempted) + " completed=" + juce::String(completed) + " expected=4\n";
    if (!folder.getChildFile("retained-midi-half-open.txt").replaceWithText(report))
        throw std::runtime_error("Retain half-open callback evidence before assertion");
    if (!allPassed || attempted != 4 || completed != 4)
        throw std::runtime_error(
            "MIDI event callback offset/deadline violation; see retained-midi-half-open.txt");
}
} // namespace vibera::draft::retained_midi
