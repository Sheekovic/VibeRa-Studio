// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PatternChannelLoops.h"
#include "LiveMidi.h"
#include "MidiInput.h"
#include "MidiScore.h"
#include "CallbackAllocationProbe.h"
#include "PianoLoopViewAcceptance.h"
#include "PianoLoopSliceAcceptance.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

static void channelLoopPreparedPlaybackTests(const juce::File &folder)
{
    Sample sample;
    sample.oneShot = false;
    sample.data.setSize(2, 65536);
    for (int i = 0; i < sample.data.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            sample.data.setSample(
                ch, i, float((i % 17 - 8) * .015 + ((i / 17) % 11 - 5) * .005) * (ch == 0 ? 1.f : -.625f));
    juce::String report;
    for (bool song : {false, true})
        for (int frames : {128, 256})
        {
            auto looped = Project::empty();
            looped.tempo = 240;
            const auto channel = looped.channels.front().id, pattern = looped.patterns.front().id;
            auto &part = looped.patterns.front();
            part.length = 2;
            part.autoLength = false; // This fixture explicitly tests a fixed two-beat window.
            const auto sustained = looped.allocate(), chord = looped.allocate();
            part.notes = {{sustained, channel, 0, .75, 60, .7f}, {chord, channel, .25, .25, 67, .4f}};
            part.channelLoops = {{channel, ChannelLoopMode::fixed, .5}};
            looped.placements = {{looped.allocate(), pattern, 0, 0, 2, 0}};
            auto explicitScore = looped;
            auto &referencePart = explicitScore.patterns.front();
            referencePart.channelLoops.clear();
            referencePart.notes.clear();
            // Independent literal reference score: never produced by raw expansion or Burn.
            for (double beat : {0., .5, 1., 1.5})
                referencePart.notes.push_back({explicitScore.allocate(), channel, beat, .75, 60, .7f});
            for (double beat : {.25, .75, 1.25, 1.75})
                referencePart.notes.push_back({explicitScore.allocate(), channel, beat, .25, 67, .4f});
            auto document = compileLiveDocument(looped, !song, pattern, 1);
            auto reference = compileLiveDocument(explicitScore, !song, pattern, 1);
            auto actualBankOwner = std::make_unique<VoiceBank>(),
                 referenceBankOwner = std::make_unique<VoiceBank>();
            auto &actualBank = *actualBankOwner, &referenceBank = *referenceBankOwner;
            actualBank.prepare(sample, 48000);
            referenceBank.prepare(sample, 48000);
            LiveSequencer actual, expected;
            LiveMidi midi;
            juce::AudioBuffer<float> actualPcm(2, frames), expectedPcm(2, frames);
            int position = 0, ons = 0, offs = 0, overruns = 0;
            float error = 0, peak = 0;
            double maximum = 0;
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            while (position < 30000)
            {
                actualPcm.clear();
                expectedPcm.clear();
                const auto start = juce::Time::getHighResolutionTicks();
                callbackAllocationProbe = true;
                actual.process(*document, channel, position / 48000., frames, 48000, true, actualBank,
                               actualPcm, 0);
                expected.process(*reference, channel, position / 48000., frames, 48000, true, referenceBank,
                                 expectedPcm, 0);
                const auto count = midi.process(*document, channel, position / 48000., frames, 48000, true);
                callbackAllocationProbe = false;
                const auto ms =
                    juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) *
                    1000;
                maximum = std::max(maximum, ms);
                overruns += ms > frames * 1000. / 48000.;
                for (int i = 0; i < count; ++i)
                {
                    const auto &event = midi.events[std::size_t(i)];
                    const int at = position + event.frame;
                    const auto kind = event.bytes[0] & 0xf0;
                    if (kind == 0x90)
                    {
                        ++ons;
                        const auto origin = event.bytes[1] == 60 ? 0 : 3000;
                        require((event.bytes[1] == 60 || event.bytes[1] == 67) &&
                                    event.bytes[2] == (event.bytes[1] == 60 ? 89 : 51) && at >= origin &&
                                    (at - origin) % 6000 == 0 && at < 24000,
                                "LiveMidi loop note-on has literal source origin and exact sample offset");
                    }
                    else if (kind == 0x80)
                    {
                        ++offs;
                        require((event.bytes[1] == 60 &&
                                 (at == 9000 || at == 15000 || at == 21000 || at == 24000)) ||
                                    (event.bytes[1] == 67 && at >= 6000 && at <= 24000 && at % 6000 == 0),
                                "LiveMidi repeated overlapping note-offs have independent literal sample "
                                "positions");
                    }
                    else
                        require(false, "Ordinary loop playback emits no panic or unrelated MIDI action");
                }
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < frames; ++i)
                        error = std::max(error,
                                         std::abs(actualPcm.getSample(ch, i) - expectedPcm.getSample(ch, i)));
                peak = std::max(peak, actualPcm.getMagnitude(0, frames));
                position += frames;
            }
            report += juce::String(song ? "song" : "pattern") + " frames=" + juce::String(frames) +
                      " pcmError=" + juce::String(error, 9) + " peak=" + juce::String(peak, 9) +
                      " on=" + juce::String(ons) + " off=" + juce::String(offs) +
                      " maxMs=" + juce::String(maximum, 6) + " overruns=" + juce::String(overruns) + "\n";
            folder.getChildFile("channel-loop-prepared.txt").replaceWithText(report);
            require(error == 0 && peak > .01f && ons == 8 && offs == 8 && midi.overflow == 0 &&
                        actualBank.activeVoices() == 0,
                    "Prepared loop matches independent signed nonconstant PCM and releases every overlap");
            require(callbackAllocations.load() == allocations &&
                        callbackDestructions.load() == destructions && overruns == 0,
                    "128/256-frame loop processing has zero callback heap/destruction and original block "
                    "deadline");
            if (!song && frames == 128)
            {
                juce::MemoryBlock bytes;
                MidiScore imported;
                require(writeMidiScore(part, channel, 240, 4, 4, bytes).wasOk() &&
                            readMidiScore(bytes, {}, imported).wasOk() && imported.notes.size() == 8,
                        "Actual MIDI score export includes all loop repetitions");
                for (double beat : {0., .5, 1., 1.5})
                    require(
                        std::count_if(imported.notes.begin(), imported.notes.end(), [&](const auto &n)
                                      { return n.pitch == 60 && n.beat == beat && n.length == .75; }) == 1,
                        "MIDI export retains literal raw long-note lengths rather than fitting clipped PCM");
                for (double beat : {.25, .75, 1.25, 1.75})
                    require(std::count_if(imported.notes.begin(), imported.notes.end(), [&](const auto &n)
                                          { return n.pitch == 67 && n.beat == beat && n.length == .25; }) ==
                                1,
                            "MIDI exported chord repetitions have exact origins and duration");
            }
        }

    for (int frames : {128, 256})
    {
        Controller c;
        c.project.tempo = 240;
        const auto channel = c.project.channels.front().id, pattern = c.project.patterns.front().id;
        const auto source = c.project.allocate(), chord = c.project.allocate();
        auto &part = c.project.patterns.front();
        part.notes = {{source, channel, 0, 1.5, 60, .7f}, {chord, channel, .25, 1, 67, .4f}};
        part.channelLoops = {{channel, ChannelLoopMode::fixed, 1}};
        auto document = compileLiveDocument(c.project, true, pattern, 1);
        LiveSequencer scheduler;
        LiveMidi midi;
        auto scoreBankOwner = std::make_unique<VoiceBank>(), heldBankOwner = std::make_unique<VoiceBank>(),
             heldReferenceOwner = std::make_unique<VoiceBank>();
        auto &scoreBank = *scoreBankOwner, &heldBank = *heldBankOwner, &heldReference = *heldReferenceOwner;
        scoreBank.prepare(sample, 48000);
        heldBank.prepare(sample, 48000);
        heldReference.prepare(sample, 48000);
        MidiInputRouter input(false), referenceInput(false);
        auto lane = input.createLane(), referenceLane = referenceInput.createLane();
        input.select(lane.get());
        referenceInput.select(referenceLane.get());
        MidiMonitor monitor, referenceMonitor;
        monitor.bind(lane);
        referenceMonitor.bind(referenceLane);
        const auto held = juce::MidiMessage::noteOn(1, 72, juce::uint8(100));
        input.handleIncomingMidiMessage(nullptr, held);
        referenceInput.handleIncomingMidiMessage(nullptr, held);
        juce::AudioBuffer<float> scorePcm(2, frames), heldPcm(2, frames), referencePcm(2, frames);
        int position = 0, on = 0, off = 0, overruns = 0;
        double maximum = 0;
        float heldError = 0;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        auto tick = [&]
        {
            scorePcm.clear();
            heldPcm.clear();
            referencePcm.clear();
            const auto start = juce::Time::getHighResolutionTicks();
            callbackAllocationProbe = true;
            monitor.read();
            referenceMonitor.read();
            monitor.render(heldBank, heldPcm, 0, frames);
            referenceMonitor.render(heldReference, referencePcm, 0, frames);
            scheduler.process(*document, channel, position / 48000., frames, 48000, true, scoreBank, scorePcm,
                              0);
            const auto count = midi.process(*document, channel, position / 48000., frames, 48000, true);
            callbackAllocationProbe = false;
            const auto ms =
                juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1000;
            maximum = std::max(maximum, ms);
            overruns += ms > frames * 1000. / 48000.;
            on = off = 0;
            for (int i = 0; i < count; ++i)
            {
                on += (midi.events[std::size_t(i)].bytes[0] & 0xf0) == 0x90;
                off += (midi.events[std::size_t(i)].bytes[0] & 0xf0) == 0x80;
            }
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < frames; ++i)
                    heldError = std::max(heldError,
                                         std::abs(heldPcm.getSample(ch, i) - referencePcm.getSample(ch, i)));
            position += frames;
        };
        while (position < 24576)
            tick();
        std::uint64_t generation = 1;
        auto publish = [&] { document = compileLiveDocument(c.project, true, pattern, ++generation); };
        auto events = [&](int expectedOn, int expectedOff, const char *message)
        {
            tick();
            require(on == expectedOn && off == expectedOff && midi.overflow == 0, message);
        };
        CommandArgs edit;
        edit.pattern = pattern;
        edit.channel = channel;
        edit.ids = {source};
        edit.pitch = 0;
        edit.length = -.5;
        require(c.execute("notes.move", edit).wasOk(),
                "Resize overlapping loop source through canonical history");
        publish();
        events(0, 1, "Resize releases expired previous occurrence without retriggering current overlap");
        edit.ids = {chord};
        edit.beat = .125;
        edit.length = 0;
        require(c.execute("notes.move", edit).wasOk(), "Move loop chord source");
        publish();
        events(1, 1, "Move releases old chord occurrence and chases exact replacement at retained clock");
        edit.ids = {source};
        require(c.execute("notes.delete", edit).wasOk(), "Delete overlapping loop source");
        publish();
        events(0, 1, "Delete releases only currently sounding source occurrence");
        require(c.undoEditChecked().wasOk(), "Undo source delete");
        publish();
        events(1, 0, "Undo delete chases restored current source occurrence");
        require(c.redoEditChecked().wasOk(), "Redo source delete");
        publish();
        events(0, 1, "Redo delete releases restored occurrence once");
        require(c.undoEditChecked().wasOk(), "Restore source before undoing move and resize");
        publish();
        events(1, 0, "Second Undo delete restores current overlap once after Redo");
        require(c.undoEditChecked().wasOk() && c.undoEditChecked().wasOk(),
                "Restore exact move/resize baseline");
        publish();
        events(2, 1,
               "Undo restores previous overlap and original chord without resetting retained current voice");
        CommandArgs loop;
        loop.pattern = pattern;
        loop.channel = channel;
        loop.text = "fixed";
        loop.length = 2;
        require(c.execute("pattern.channelLoop", loop).wasOk(),
                "Change actual fixed loop during sustained score");
        publish();
        events(1, 3, "Period change releases stale origins and chases only actual new sounding occurrence");
        require(c.undoEditChecked().wasOk(), "Undo live loop-period edit");
        publish();
        events(3, 1, "Undo loop-period edit restores exact independent sounding occurrence set");
        edit.ids = {source, chord};
        require(c.execute("notes.delete", edit).wasOk(), "Delete all looped score sources");
        publish();
        events(0, 3, "Final delete releases all overlapping occurrences without panic");
        for (int i = 0; i < 12; ++i)
            tick();
        require(scoreBank.activeVoices() == 0 && heldBank.activeVoices() == 1 &&
                    monitor.liveKeyCountRT() == 1 && heldError == 0 && heldPcm.getMagnitude(0, frames) > .01f,
                "Loop edits release score tails while real routed held MIDI retains exact independent PCM "
                "cursor");
        input.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
        referenceInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
        for (int i = 0; i < 12; ++i)
            tick();
        report += "edits frames=" + juce::String(frames) + " heldError=" + juce::String(heldError, 9) +
                  " maxMs=" + juce::String(maximum, 6) + " overruns=" + juce::String(overruns) + "\n";
        folder.getChildFile("channel-loop-prepared.txt").replaceWithText(report);
        require(heldBank.activeVoices() == 0 && monitor.liveKeyCountRT() == 0 && heldError == 0 &&
                    callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions && overruns == 0,
                "Actual held note-off completes, all loop callback paths remain heap-free and within block "
                "deadline");
    }
}

// Score/model plus prepared component processing; not native graph, hardware or listening evidence.
static void runChannelLoopAcceptance(const juce::File &folder)
{
    pianoLoopViewTests();
    pianoLoopSliceTests();
    Controller c;
    const auto pattern = c.project.patterns.front().id;
    c.project.pattern(pattern)->autoLength = false; // Preserve the deliberate dormant-source window.
    const auto channel = c.project.channels[0].id, other = c.project.channels[1].id;
    auto add = [&](Id owner, double beat, double length, int pitch, float velocity, bool step = false,
                   bool muted = false)
    {
        const auto id = c.project.allocate();
        c.project.pattern(pattern)->notes.push_back({id, owner, beat, length, pitch, velocity, step, muted});
        return id;
    };
    const auto longNote = add(channel, 0, 1.5, 60, .7f, true);
    const auto shortNote = add(channel, .25, .25, 64, .5f, true);
    const auto chord = add(channel, .25, .5, 67, .6f);
    const auto dormant = add(channel, 1, .25, 72, .8f);
    add(channel, .5, .25, 74, .8f, false, true);
    add(channel, .75, .25, 76, 0);
    const auto beyondPattern = add(channel, 4.5, .25, 78, .8f);
    const auto untouched = add(other, .5, .5, 40, .9f);
    Pattern separate;
    separate.id = c.project.allocate();
    separate.name = "Independent Pattern";
    separate.length = 2;
    separate.autoLength = false;
    separate.notes = {{c.project.allocate(), channel, 0, .25, 48, .5f}};
    separate.channelLoops = {{channel, ChannelLoopMode::fixed, 2}};
    c.project.patterns.push_back(separate);
    c.project.placements = {{c.project.allocate(), pattern, 0, 10, 2.5, .25},
                            {c.project.allocate(), pattern, 1, 20, 4, 0}};
    require(c.project.validate().wasOk(),
            "Loop fixture has chords, long notes, dormant and other-Channel data");
    const auto initial = c.project.state();
    const auto originalNotes = c.project.pattern(pattern)->notes;
    auto sameNote = [](const Note &a, const Note &b)
    {
        return a.id == b.id && a.channel == b.channel && a.beat == b.beat && a.length == b.length &&
               a.pitch == b.pitch && a.velocity == b.velocity && a.step == b.step && a.muted == b.muted;
    };
    const auto off = rawPatternOccurrences(c.project, *c.project.pattern(pattern));
    require(channelLoopLength(c.project, *c.project.pattern(pattern), channel) == 0 &&
                off.size() == originalNotes.size(),
            "Off retains all raw notes, including outside the Pattern horizon");
    for (std::size_t i = 0; i < off.size(); ++i)
        require(off[i].iteration == 0 && sameNote(off[i].note, originalNotes[i]),
                "Unlooped occurrence is exact source data with iteration zero");

    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.text = "fixed";
    args.length = 1;
    require(c.execute("pattern.channelLoop", args).wasOk(), "Canonical fixed per-Pattern Channel loop");
    const auto looped = c.project.state();
    auto normalized = c.project;
    normalized.pattern(pattern)->channelLoops.clear();
    require(normalized.state().isEquivalentTo(initial) &&
                channelLoopLength(c.project, *c.project.pattern(pattern), channel) == 1 &&
                channelLoopLength(c.project, *c.project.pattern(pattern), other) == 0,
            "Loop setter changes only chosen Pattern/Channel metadata, never source notes or routing");
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(initial) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(looped),
            "Loop setter has one exact Undo/Redo");
    const auto raw = rawPatternOccurrences(c.project, *c.project.pattern(pattern));
    for (unsigned iteration = 0; iteration < 4; ++iteration)
        for (const auto source : {longNote, shortNote, chord})
        {
            const auto found =
                std::find_if(raw.begin(), raw.end(), [&](const auto &occurrence)
                             { return occurrence.note.id == source && occurrence.iteration == iteration; });
            require(found != raw.end(),
                    "Every loop cycle retains source-note identity and explicit iteration");
            const auto original = std::find_if(originalNotes.begin(), originalNotes.end(),
                                               [&](const auto &note) { return note.id == source; });
            auto expected = *original;
            expected.beat += iteration;
            require(sameNote(found->note, expected),
                    "Raw repeats preserve chord pitches, velocity, step flag and crossing long-note lengths");
        }
    require(std::none_of(raw.begin(), raw.end(), [&](const auto &o) { return o.note.id == dormant; }),
            "Onset exactly at fixed loop boundary is dormant, not a second origin");

    // Independent literal score positions; neither raw expansion nor Burn is the expected-value generator.
    auto score = sequence(c.project, true, pattern);
    require(score.size() == 13, "Four cycles of three audible notes plus one unlooped peer");
    for (unsigned iteration = 0; iteration < 4; ++iteration)
        for (const auto source : {longNote, shortNote, chord})
        {
            const Id tag = iteration == 0 ? 0 : -Id(iteration);
            const auto found = std::find_if(score.begin(), score.end(), [&](const auto &note)
                                            { return note.note == source && note.placement == tag; });
            const auto onset = double(iteration) + (source == longNote ? 0 : .25);
            const auto duration = source == longNote ? 1.5 : source == shortNote ? .25 : .5;
            require(found != score.end() && found->beat == onset &&
                        found->length == std::min(duration, 4 - onset),
                    "Pattern score has literal independent loop origin and horizon-clipped note duration");
        }
    const auto again = sequence(c.project, true, pattern);
    require(score.size() == again.size(), "Repeated scheduling preserves occurrence count");
    for (std::size_t i = 0; i < score.size(); ++i)
        require(score[i].note == again[i].note && score[i].placement == again[i].placement &&
                    score[i].beat == again[i].beat,
                "Synthetic schedule identities are deterministic across repeated compilation");
    c.project.swing = 1;
    c.project.channel(channel)->truncateSwing = true;
    const auto song = sequence(c.project, false, pattern);
    const auto placement = c.project.placements[0].id;
    const auto shifted = std::find_if(song.begin(), song.end(), [&](const auto &n)
                                      { return n.note == shortNote && n.placement == placement; });
    const auto repeated =
        std::find_if(song.begin(), song.end(), [&](const auto &n)
                     { return n.note == longNote && n.placement == -(placement * Id(4000001) + 1); });
    require(shifted != song.end() && shifted->beat == 10.125 && shifted->length == .125 &&
                repeated != song.end() && repeated->beat == 10.75 && repeated->length == 1.5,
            "Song offset crops source time, Swing uses expanded source phase and identity binds actual "
            "placement");

    auto musical = [](const std::vector<ScheduledNote> &notes)
    {
        std::vector<std::tuple<Id, double, double, int, float, bool>> values;
        for (const auto &note : notes)
            values.emplace_back(note.channel, note.beat, note.length, note.pitch, note.velocity, note.step);
        std::sort(values.begin(), values.end());
        return values;
    };
    {
        auto extended = c.project;
        extended.placements = {{extended.allocate(), pattern, 0, 10, 8, .25}};
        const auto events = sequence(extended, false, pattern);
        require(!events.empty() && std::all_of(events.begin(), events.end(),
                                               [](const auto &n) { return n.beat + n.length <= 13.75; }),
                "Long offset Song placement clips crossing notes at actual Pattern horizon");
        Controller burner;
        burner.replace(extended);
        require(burner.execute("pattern.burnChannelLoop", args).wasOk() &&
                    musical(sequence(burner.project, false, pattern)) == musical(events),
                "Burn retains exact source-window clipping inside a longer Song placement");
    }
    const auto patternBeforeBurn = musical(sequence(c.project, true, pattern));
    const auto songBeforeBurn = musical(song);
    const auto beforeBurn = c.project.state();
    const auto allocationFloor = c.project.nextId;
    require(c.execute("pattern.burnChannelLoop", args).wasOk() &&
                channelLoopLength(c.project, *c.project.pattern(pattern), channel) == 0,
            "Burn materializes chosen Channel and clears only its loop");
    const auto &burnedNotes = c.project.pattern(pattern)->notes;
    std::set<Id> burnedIds;
    for (const auto &note : burnedNotes)
    {
        require(burnedIds.insert(note.id).second, "Burn assigns globally distinct materialized note IDs");
        if (note.channel == channel && note.beat < 4 && note.beat >= 1)
            require(note.id >= allocationFloor, "Repeated notes receive fresh canonical IDs");
    }
    for (const auto id : {longNote, shortNote, chord, beyondPattern, untouched})
    {
        const auto old = std::find_if(originalNotes.begin(), originalNotes.end(),
                                      [&](const auto &n) { return n.id == id; });
        const auto kept =
            std::find_if(burnedNotes.begin(), burnedNotes.end(), [&](const auto &n) { return n.id == id; });
        require(kept != burnedNotes.end() && sameNote(*old, *kept),
                "Burn preserves cycle-zero, out-of-horizon and other-Channel source identities exactly");
    }
    require(!burnedIds.contains(dormant) &&
                musical(sequence(c.project, true, pattern)) == patternBeforeBurn &&
                musical(sequence(c.project, false, pattern)) == songBeforeBurn &&
                channelLoopLength(c.project, *c.project.pattern(separate.id), channel) == 2,
            "Burn removes dormant source material and preserves exact Pattern/Song music including Swing");
    const auto burned = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeBurn) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(burned),
            "Burn is one exact Undo/Redo with identical materialized IDs");
    require(c.undoEditChecked().wasOk(), "Retain actual loop metadata for persistence checks");
    // A second loop has a full-Pattern period, so it adds metadata without changing peer music.
    auto peerArgs = args;
    peerArgs.channel = other;
    peerArgs.length = 4;
    require(c.execute("pattern.channelLoop", peerArgs).wasOk(), "Independent peer Channel loop");
    const auto withPeer = c.project.state();
    require(c.execute("pattern.burnChannelLoop", args).wasOk() &&
                channelLoopLength(c.project, *c.project.pattern(pattern), other) == 4 &&
                musical(sequence(c.project, false, pattern)) == songBeforeBurn &&
                c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(withPeer),
            "Burn clears only chosen Channel loop while retaining peer metadata and exact music");
    const auto file = folder.getChildFile("channel-loop-roundtrip.vibra");
    Project restored;
    require(!file.exists() && saveProject(c.project, file).wasOk() && loadProject(file, restored).wasOk() &&
                restored.state().isEquivalentTo(c.project.state()),
            "Loop metadata, dormant notes and independent Channel state survive exact save/reopen");

    auto refused = [&](const char *command, const CommandArgs &bad)
    {
        const auto state = c.project.state();
        const auto revision = c.revision;
        const auto dirty = c.dirty;
        const auto undo = c.undo.getUndoDescriptions(), redo = c.undo.getRedoDescriptions();
        require(
            c.execute(command, bad).failed() && c.project.state().isEquivalentTo(state) &&
                c.revision == revision && c.dirty == dirty && c.undo.getUndoDescriptions() == undo &&
                c.undo.getRedoDescriptions() == redo,
            "Invalid loop command preserves document, allocator, revision, dirty and both history branches");
    };
    for (double length : {0., .125, .3, 1000000.25, std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()})
    {
        auto bad = args;
        bad.length = length;
        refused("pattern.channelLoop", bad);
    }
    auto bad = args;
    bad.text = "auto";
    refused("pattern.channelLoop", bad);

    const auto retainParsed = restored.state();
    auto invalidTree = c.project.state();
    auto patternTree = invalidTree.getChildWithName("PATTERNS").getChild(0);
    auto loopTree = patternTree.getChildWithName("CHANNEL_LOOP");
    require(loopTree.isValid(), "Persisted loop has the declared CHANNEL_LOOP subtree");
    patternTree.appendChild(loopTree.createCopy(), nullptr);
    require(Project::parse(invalidTree, restored).failed() && restored.state().isEquivalentTo(retainParsed),
            "Duplicate persisted Channel loops refuse atomically");
    invalidTree = c.project.state();
    loopTree = invalidTree.getChildWithName("PATTERNS").getChild(0).getChildWithName("CHANNEL_LOOP");
    loopTree.setProperty("mode", "beat", nullptr);
    loopTree.setProperty("length", .5, nullptr);
    require(Project::parse(invalidTree, restored).failed() && restored.state().isEquivalentTo(retainParsed),
            "Auto mode persisted length must be canonical one, not a second hidden duration");
    bad = args;
    bad.pattern = 999999999;
    refused("pattern.channelLoop", bad);
    bad = args;
    bad.channel = 999999999;
    refused("pattern.burnChannelLoop", bad);
    refused("pattern.channelLoop", bad);

    // Meter is in quarter-note beat units. Muted/zero-velocity notes cannot extend Auto's boundary.
    Project automatic = Project::empty();
    auto &pat = automatic.patterns.front();
    const auto autoChannel = automatic.channels.front().id;
    pat.notes = {{automatic.allocate(), autoChannel, .5, .75, 60, .8f},
                 {automatic.allocate(), autoChannel, 2, 2, 62, .8f, false, true},
                 {automatic.allocate(), autoChannel, 3, 1, 64, 0},
                 {automatic.allocate(), autoChannel, 5, 1, 65, .8f}};
    automatic.numerator = 7;
    automatic.denominator = 8;
    pat.channelLoops = {{autoChannel, ChannelLoopMode::step, 1}};
    require(channelLoopLength(automatic, pat, autoChannel) == 1.25,
            "Auto Step rounds active end to quarter-step without adding an exact-boundary cycle");
    pat.channelLoops.front().mode = ChannelLoopMode::beat;
    require(channelLoopLength(automatic, pat, autoChannel) == 1.5,
            "Auto Beat uses half-quarter-note beats in 7/8");
    pat.channelLoops.front().mode = ChannelLoopMode::bar;
    require(channelLoopLength(automatic, pat, autoChannel) == 3.5, "Auto Bar uses seven eighth notes");
    pat.notes.front().length = 3.75;
    pat.channelLoops.front().mode = ChannelLoopMode::step;
    require(channelLoopLength(automatic, pat, autoChannel) == 4,
            "Crossing long note contributes only up to Pattern horizon to Auto boundary");
    pat.notes.clear();
    for (const auto [mode, expected] :
         {std::pair{ChannelLoopMode::step, .25}, std::pair{ChannelLoopMode::beat, .5},
          std::pair{ChannelLoopMode::bar, 3.5}})
    {
        pat.channelLoops.front().mode = mode;
        require(channelLoopLength(automatic, pat, autoChannel) == expected,
                "Empty Auto Channel uses exactly one metrical unit");
    }
    pat.channelLoops.front().mode = ChannelLoopMode::step;
    pat.length = 25000;
    pat.notes.push_back({automatic.allocate(), autoChannel, 0, .25, 60, .8f});
    require(automatic.validate().wasOk(),
            "Exactly 100000 raw loop occurrences retains valid bounded admission");
    pat.length = 25000.25;
    require(automatic.validate().failed(),
            "More than 100000 actual raw loop occurrences refuses model admission");

    const auto current = c.project.state();
    auto legacy = current.createCopy();
    legacy.setProperty("version", 28, nullptr);
    stripSamplerFilterForLegacy(legacy);
    auto destination = restored;
    require(Project::parse(legacy, destination).failed() &&
                destination.state().isEquivalentTo(restored.state()),
            "Genuine schema28 cannot carry schema29 loop data, destination preserved");
    for (auto p : legacy.getChildWithName("PATTERNS"))
    {
        for (const auto *field : {"autoLength", "numerator", "denominator"})
            p.removeProperty(field, nullptr);
        for (int i = p.getNumChildren(); --i >= 0;)
            if (p.getChild(i).hasType("CHANNEL_LOOP"))
                p.removeChild(i, nullptr);
    }
    require(Project::parse(legacy, destination).wasOk() && destination.version == Project::currentVersion &&
                destination.pattern(pattern)->channelLoops.empty(),
            "Schema28 without loop metadata migrates to unchanged off behavior");
    const auto oldMusic = musical(sequence(destination, true, pattern));
    auto plain = c.project;
    plain.pattern(pattern)->channelLoops.clear();
    require(oldMusic == musical(sequence(plain, true, pattern)),
            "Old-project migration never invents looping");
    {
        auto legacyIds = Project::empty();
        constexpr Id large = 9007199254740993LL;
        legacyIds.channels.front().id = large;
        legacyIds.nextId = large + 1;
        legacyIds.patterns.front().channelLoops = {{large, ChannelLoopMode::fixed, 1}};
        Project roundtrip;
        require(legacyIds.validate().wasOk() && Project::parse(legacyIds.state(), roundtrip).wasOk() &&
                    roundtrip.patterns.front().channelLoops.front().channel == large,
                "Loop Channel IDs preserve full signed64 precision without a double round trip");
        legacyIds.placements.push_back({large - 1, legacyIds.patterns.front().id, 0, 0, 4, 0});
        require(legacyIds.validate().failed(),
                "Unrepresentable legacy occurrence tags refuse before multiplication/publication");
    }
    args.text = "off";
    const auto beforeOff = c.project.state();
    require(c.execute("pattern.channelLoop", args).wasOk() &&
                channelLoopLength(c.project, *c.project.pattern(pattern), channel) == 0 &&
                channelLoopLength(c.project, *c.project.pattern(pattern), other) == 4 &&
                c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeOff),
            "Explicit Off clears only chosen loop and restores exact metadata in one Undo");
    CommandArgs step;
    step.pattern = pattern;
    step.channel = channel;
    step.beat = 2.25;
    step.enabled = false;
    const auto beforeStep = c.project.state();
    const auto sourceRevision = c.revision;
    require(c.execute("steps.set", step).wasOk() && c.project.state().isEquivalentTo(beforeStep) &&
                c.revision == sourceRevision,
            "Erasing a source-empty repeated position never erases the earlier source note");
    step.enabled = true;
    require(c.execute("steps.set", step).wasOk() &&
                c.project.pattern(pattern)->notes.size() == originalNotes.size() + 1 &&
                c.project.pattern(pattern)->notes.back().beat == 2.25 && c.undoEditChecked().wasOk() &&
                c.project.state().isEquivalentTo(beforeStep),
            "A later source step retains explicit fixed extent and has exact independent history");
    step.beat = .25;
    step.enabled = false;
    require(c.execute("steps.set", step).wasOk() &&
                std::none_of(c.project.pattern(pattern)->notes.begin(),
                             c.project.pattern(pattern)->notes.end(),
                             [&](const auto &n) { return n.id == shortNote; }) &&
                c.project.pattern(pattern)->notes.size() + 1 == originalNotes.size() &&
                c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeStep),
            "Explicit original source erase preserves the melodic chord and one exact Undo");
    channelLoopPreparedPlaybackTests(folder);
}
