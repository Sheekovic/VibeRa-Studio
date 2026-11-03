// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CoProducer.h"
#include "Commands.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace vibera;
using namespace vibera::coproducer;
static int checks = 0;
static void require(bool pass, const char *message)
{
    ++checks;
    if (!pass)
        throw std::runtime_error(message);
}
#include "CoProducerWorkerAcceptance.h"
#include "CoProducerAnticipationAcceptance.h"
int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    try
    {
        if (argc == 6 && juce::String(argv[1]) == "--anticipation-check")
        {
            anticipationAcceptance(juce::File(argv[2]), juce::File(argv[3]), juce::File(argv[4]),
                                   juce::File(argv[5]));
            std::cout << "PASS " << checks << " CoProducer native anticipation checks.\n";
            return 0;
        }
        if (argc == 6 && juce::String(argv[1]) == "--worker-check")
        {
            workerAcceptance(juce::File(argv[2]), juce::File(argv[3]), juce::File(argv[4]),
                             juce::File(argv[5]));
            std::cout << "PASS " << checks << " CoProducer native worker checks.\n";
            return 0;
        }
        {
            Controller replacing;
            auto original = Project::empty();
            const auto destination = original.channels.front().id, background = original.channels.back().id;
            auto &source = original.patterns.front();
            source.length = 12;
            const auto patternId = source.id;
            source.notes = {{original.allocate(), destination, 1, 9, 60, .8f, false},
                            {original.allocate(), destination, 5, 1, 62, .7f, false},
                            {original.allocate(), destination, 11, 1, 64, .6f, false},
                            {original.allocate(), background, 1.5, 8, 48, .5f, false}};
            const auto protectedId = source.notes.front().id, backgroundId = source.notes.back().id;
            original.placements = {{original.allocate(), patternId, 0, 0, 10, 2},
                                   {original.allocate(), patternId, 1, 20, 12, 0}};
            replacing.replace(original);
            Options config;
            config.projectIdentity = replacing.projectIdentity;
            config.selection = {true, patternId, destination, {4, 4}, {}, {}};
            Request request;
            require(collect(replacing.project, config, request.snapshot).wasOk(), "replacement context");
            Candidate candidate{request.id,
                                request.snapshot.fingerprint,
                                "Replacement fixture",
                                "fixture-1",
                                {{0, .5, 0, 67, .8f}, {3, .5, 0, 69, .7f}},
                                true};
            const auto before = replacing.project.state().toXmlString();
            auto locked = config;
            locked.lockedNotes = {protectedId};
            Request protectedRequest;
            require(collect(replacing.project, locked, protectedRequest.snapshot).wasOk(),
                    "protected replacement context");
            auto protectedCandidate = candidate;
            protectedCandidate.requestId = protectedRequest.id;
            protectedCandidate.fingerprint = protectedRequest.snapshot.fingerprint;
            require(accept(replacing, locked, protectedRequest, protectedCandidate, 0,
                           ApplyOperation::replaceRegion)
                            .failed() &&
                        replacing.project.state().toXmlString() == before,
                    "replacement cannot split/remove a protected source note");
            require(accept(replacing, config, request, candidate, 0, ApplyOperation::replaceRegion).wasOk(),
                    "explicit Pattern replacement accepts");
            const auto *part = replacing.project.pattern(patternId);
            require(replacing.project.patterns.size() == original.patterns.size() &&
                        replacing.project.placements.size() == 2 &&
                        replacing.project.placements[0].pattern == patternId &&
                        replacing.project.placements[1].pattern == patternId,
                    "Pattern replacement deliberately retains all linked source references");
            require(
                part->notes.size() == 6 &&
                    std::any_of(part->notes.begin(), part->notes.end(), [&](const auto &note)
                                { return note.id == backgroundId && note.beat == 1.5 && note.length == 8; }),
                "replacement preserves other Channels and outside notes");
            require(std::any_of(part->notes.begin(), part->notes.end(), [](const auto &note)
                                { return note.pitch == 60 && note.beat == 1 && note.length == 3; }) &&
                        std::any_of(part->notes.begin(), part->notes.end(), [](const auto &note)
                                    { return note.pitch == 60 && note.beat == 8 && note.length == 2; }),
                    "crossing notes keep exact left/right fragments");
            const auto after = replacing.project.state().toXmlString();
            replacing.undoEdit();
            require(replacing.project.state().toXmlString() == before, "replacement has exact one-step undo");
            replacing.redoEdit();
            require(replacing.project.state().toXmlString() == after, "replacement has exact one-step redo");
            require(accept(replacing, config, request, candidate, 0, ApplyOperation::replaceRegion).failed(),
                    "stale replacement cannot overwrite a later edit");

            auto song = original;
            song.patterns.front().notes.front().beat = 3;
            song.patterns.front().notes.front().length = 7;
            const auto hidden = song.allocate();
            song.patterns.front().notes.push_back({hidden, destination, 0, 12, 72, .4f, false});
            replacing.replace(song);
            config.projectIdentity = replacing.projectIdentity;
            config.selection = {false, patternId, destination, {2, 4}, {}, {song.placements.front().id}};
            require(collect(replacing.project, config, request.snapshot).wasOk(),
                    "offset Song replacement context");
            request.id = juce::Uuid().toString();
            candidate.requestId = request.id;
            candidate.fingerprint = request.snapshot.fingerprint;
            const auto songBefore = replacing.project.state().toXmlString();
            require(accept(replacing, config, request, candidate, 0, ApplyOperation::replaceRegion).wasOk(),
                    "selected offset Song clip replacement accepts");
            require(
                replacing.project.patterns.size() == song.patterns.size() + 1 &&
                    replacing.project.placements[0].pattern != patternId &&
                    replacing.project.placements[1].pattern == patternId &&
                    replacing.project.placements[0].offset == 2 &&
                    replacing.project.placements[0].length == 10 && replacing.project.placements[0].beat == 0,
                "Song replacement makes only the selected clip unique without changing placement geometry");
            require(replacing.project.pattern(patternId)->notes.size() ==
                            song.patterns.front().notes.size() &&
                        replacing.project.pattern(patternId)->notes.front().beat == 3 &&
                        replacing.project.pattern(patternId)->notes.back().id == hidden,
                    "Song unique replacement preserves original linked source");
            const auto &unique = replacing.project.patterns.back();
            require(std::any_of(unique.notes.begin(), unique.notes.end(),
                                [](const auto &note) { return note.pitch == 67 && note.beat == 4; }),
                    "Song absolute region maps through clip offset into source time");
            require(std::any_of(unique.notes.begin(), unique.notes.end(), [](const auto &note)
                                { return note.pitch == 72 && note.beat == 0 && note.length == 12; }),
                    "offset-omitted note is retained without introducing a chased tail");
            const auto songAfter = replacing.project.state().toXmlString();
            replacing.undoEdit();
            require(replacing.project.state().toXmlString() == songBefore,
                    "unique Song replacement one-step undo");
            replacing.redoEdit();
            require(replacing.project.state().toXmlString() == songAfter,
                    "unique Song replacement one-step redo");
            replacing.replace(song);
            config.projectIdentity = replacing.projectIdentity;
            config.selection.placements.clear();
            require(collect(replacing.project, config, request.snapshot).wasOk(), "unscoped Song context");
            request.id = juce::Uuid().toString();
            candidate.requestId = request.id;
            candidate.fingerprint = request.snapshot.fingerprint;
            require(accept(replacing, config, request, candidate, 0, ApplyOperation::replaceRegion).failed(),
                    "Song replacement requires one explicitly selected clip");
        }
        Controller controller;
        auto p = Project::empty();
        const auto destination = p.channels.front().id;
        auto &pattern = p.patterns.front();
        const auto patternId = pattern.id;
        pattern.length = 8;
        pattern.autoLength = false; // Explicit eight-beat source under the independent6/8 clock.
        pattern.notes = {{p.allocate(), destination, 0, 3, 60, .7f, false},
                         {p.allocate(), destination, 2, 2, 64, .8f, false},
                         {p.allocate(), destination, 4, 1, 67, .9f, false}};
        const auto protectedNote = pattern.notes[1].id;
        p.placements = {{p.allocate(), patternId, 0, 8, 3, 2}, {p.allocate(), patternId, 1, 12, 3, 2}};
        p.numerator = 6;
        p.denominator = 8;
        Options options;
        options.projectIdentity = controller.projectIdentity;
        options.selection = {false, patternId, destination, {9, 4}, {}, {}};
        {
            // Labelled runtime-state fixtures; no live-plugin notification claim.
            auto runtimeProject = p;
            auto runtimeOptions = options;
            juce::PluginDescription descriptor;
            descriptor.name = "Runtime fixture";
            descriptor.pluginFormatName = "VST3";
            descriptor.fileOrIdentifier = "fixture.vst3";
            descriptor.isInstrument = true;
            const auto instrument = runtimeProject.allocate();
            runtimeProject.channels.front().instrument =
                PluginSpec{instrument, descriptor.name, descriptor.createXml()->toString(), {}, true, false};
            const auto effect = runtimeProject.allocate();
            descriptor.isInstrument = false;
            runtimeProject.mixer.front().effects.push_back(
                PluginSpec{effect, descriptor.name, descriptor.createXml()->toString(), {}, false, false});
            runtimeOptions.automaticTracks = false;
            runtimeOptions.tracks = {destination};
            runtimeOptions.pluginRuntime = {{instrument, "fixture-instance-a", 7, true},
                                            {effect, "fixture-master-a", 3, true},
                                            {99999, "excluded-instance", 0, true}};
            Snapshot runtimeSnapshot;
            require(collect(runtimeProject, runtimeOptions, runtimeSnapshot).wasOk() &&
                        runtimeSnapshot.pluginRuntime.size() == 2,
                    "runtime snapshot contains relevant destination and master effect only");
            require(checkFresh(runtimeProject, runtimeOptions, runtimeSnapshot).wasOk(),
                    "unchanged live runtime is fresh");
            auto changedRuntime = runtimeOptions;
            ++changedRuntime.pluginRuntime[0].parameterRevision;
            require(checkFresh(runtimeProject, changedRuntime, runtimeSnapshot).failed(),
                    "unsaved destination parameter change invalidates candidate");
            changedRuntime = runtimeOptions;
            ++changedRuntime.pluginRuntime[1].parameterRevision;
            require(checkFresh(runtimeProject, changedRuntime, runtimeSnapshot).failed(),
                    "unsaved routed/master effect change invalidates candidate");
            changedRuntime = runtimeOptions;
            ++changedRuntime.pluginRuntime[2].parameterRevision;
            require(checkFresh(runtimeProject, changedRuntime, runtimeSnapshot).wasOk(),
                    "excluded plugin changes do not invalidate relevant context");
            changedRuntime = runtimeOptions;
            changedRuntime.pluginRuntime[0].instance = "fixture-restarted-instance";
            require(checkFresh(runtimeProject, changedRuntime, runtimeSnapshot).failed(),
                    "restarted host cannot reuse old runtime context");
            changedRuntime = runtimeOptions;
            changedRuntime.pluginRuntime[0].available = false;
            require(checkFresh(runtimeProject, changedRuntime, runtimeSnapshot).failed(),
                    "crashed host invalidates runtime context");
            changedRuntime = runtimeOptions;
            changedRuntime.pluginRuntime.clear();
            require(checkFresh(runtimeProject, changedRuntime, runtimeSnapshot).failed(),
                    "missing live host invalidates runtime context");
            Snapshot unavailable;
            require(collect(runtimeProject, changedRuntime, unavailable).wasOk() &&
                        checkFresh(runtimeProject, runtimeOptions, unavailable).failed(),
                    "newly available host requires regeneration from unknown runtime");
            changedRuntime.pluginRuntime = runtimeOptions.pluginRuntime;
            std::reverse(changedRuntime.pluginRuntime.begin(), changedRuntime.pluginRuntime.end());
            require(checkFresh(runtimeProject, changedRuntime, runtimeSnapshot).wasOk(),
                    "runtime enumeration order does not alter context identity");
        }
        {
            auto audibility = p;
            require(channelAudible(audibility, destination), "chooser auto policy includes audible Channel");
            audibility.channels.front().muted = true;
            require(!channelAudible(audibility, destination), "chooser auto policy excludes muted Channel");
            auto explicitTracks = options;
            explicitTracks.automaticTracks = false;
            explicitTracks.tracks = {destination};
            Snapshot mutedContext;
            require(collect(audibility, explicitTracks, mutedContext).wasOk() &&
                        !mutedContext.events.empty() && mutedContext.tracks.front().included &&
                        !mutedContext.tracks.front().audible,
                    "explicit context reads muted notes without relabelling playback audibility");
            require(audibility.channels.front().muted,
                    "context collection does not unmute selected material");
            audibility.channels.front().muted = false;
            audibility.mixer.front().gain = 0;
            require(!channelAudible(audibility, destination), "chooser respects silent Master routing");
            audibility.mixer.front().gain = 1;
            audibility.channels.front().gain = 0;
            require(!channelAudible(audibility, destination), "chooser respects zero Channel gain");
        }
        const auto before = p.state().toXmlString();
        Snapshot snapshot;
        require(collect(p, options, snapshot).wasOk(), "snapshot collects");
        require(p.state().toXmlString() == before, "context extraction cannot mutate source");
        require(snapshot.quarterNotesPerBar() == 3, "6/8 bar is three quarter notes");
        require(snapshot.events.size() == 4,
                "offset linked placements resolve four notes, no implicit repeats");
        require(snapshot.events[0].start == -1 && snapshot.events[0].length == 2 &&
                    snapshot.events[0].inTarget,
                "sustaining note enters from before selection and retains onset");
        require(snapshot.events[1].start == 1 && snapshot.events[1].sourceBeat == 4,
                "offset clip resolves original source and target-relative position");
        require(snapshot.events[0].note == snapshot.events[2].note &&
                    snapshot.events[0].placement != snapshot.events[2].placement,
                "linked copies retain note identity with distinct placement identity");
        require(snapshot.chords.empty() && snapshot.destinationProfile.provenance == Provenance::unknown,
                "missing harmony/capabilities remain unknown");
        auto sameOptions = options;
        sameOptions.revision = 100;
        require(checkFresh(p, sameOptions, snapshot).wasOk(),
                "revision alone does not invalidate relevant content");
        auto retuned = p;
        auto *retunedChannel = retuned.channel(options.selection.destination);
        if (retunedChannel)
        {
            retunedChannel->sampler.cents = 100;
            require(checkFresh(retuned, options, snapshot).failed(),
                    "sampler tuning invalidates generated destination context");
            for (auto field : {&SamplerSettings::cut, &SamplerSettings::cutBy, &SamplerSettings::maxPoly})
            {
                auto voiceChanged = p;
                voiceChanged.channel(options.selection.destination)->sampler.*field = 1;
                require(checkFresh(voiceChanged, options, snapshot).failed(),
                        "Audible sampler voice controls invalidate generated destination context");
            }
        }
        auto unrelated = p;
        unrelated.name = "Different project display name";
        auto outside = unrelated.patterns.front().notes.front();
        outside.id = unrelated.allocate();
        outside.beat = 100;
        unrelated.patterns.front().notes.push_back(outside);
        require(checkFresh(unrelated, options, snapshot).wasOk(),
                "unrelated out-of-range edit remains fresh");
        auto edited = p;
        edited.patterns.front().notes[1].pitch++;
        require(checkFresh(edited, options, snapshot).failed(), "relevant pitch edit is stale");
        auto changedDestination = p;
        changedDestination.channels.front().pan = .2f;
        require(checkFresh(changedDestination, options, snapshot).failed(),
                "destination state changes are stale");
        auto otherProject = options;
        otherProject.projectIdentity = "other-project";
        require(checkFresh(p, otherProject, snapshot).failed(),
                "identical notes from another project are stale");
        auto newSelection = options;
        newSelection.selection.region.start++;
        require(checkFresh(p, newSelection, snapshot).failed(), "selection change is stale");
        auto muted = p;
        muted.channels.front().muted = true;
        Snapshot mutedSnapshot;
        require(collect(muted, options, mutedSnapshot).wasOk() && mutedSnapshot.events.empty(),
                "automatic tracks exclude muted Channel");
        auto explicitTracks = options;
        explicitTracks.automaticTracks = false;
        explicitTracks.tracks = {destination};
        require(collect(muted, explicitTracks, mutedSnapshot).wasOk() && mutedSnapshot.events.size() == 4 &&
                    !mutedSnapshot.events.front().audible,
                "deliberate muted override includes data without pretending it is audible");
        for (auto &bus : muted.mixer)
            if (bus.index == 0)
                bus.muted = true;
        require(collect(muted, options, mutedSnapshot).wasOk() && mutedSnapshot.events.empty(),
                "Master mute affects automatic audibility");
        auto locks = explicitTracks;
        locks.lockedNotes = {protectedNote};
        require(collect(p, locks, mutedSnapshot).wasOk() && mutedSnapshot.events.front().locked,
                "locks retain exact source identity across placements");
        locks.tracks.clear();
        require(collect(p, locks, mutedSnapshot).wasOk() && !mutedSnapshot.complete,
                "excluded lock cannot silently relax");
        auto chords = options;
        chords.chords = {{8, 4, "User C minor", {0, 3, 7}, Provenance::userSpecified}};
        require(collect(p, chords, mutedSnapshot).wasOk() &&
                    mutedSnapshot.chords.front().label == "User C minor" &&
                    mutedSnapshot.chords.front().provenance == Provenance::userSpecified,
                "corrected harmony remains user-specified, not inferred");
        require(checkFresh(p, chords, snapshot).failed(), "harmony correction invalidates previous request");
        auto budget = options;
        budget.maxEvents = 1;
        require(collect(p, budget, mutedSnapshot).wasOk() && !mutedSnapshot.complete &&
                    mutedSnapshot.omittedEvents == 3,
                "event omission is explicit and counted");
        require(checkFresh(p, budget, mutedSnapshot).failed(), "incomplete context cannot be accepted");
        budget.maxEvents = 8192;
        budget.maxExaminedNotes = 1;
        require(collect(p, budget, mutedSnapshot).wasOk() && !mutedSnapshot.complete &&
                    mutedSnapshot.examinedNotes == 1,
                "scan budget bounds work and discloses unknown omissions");
        auto invalid = options;
        invalid.selection.destination = 99999;
        require(collect(p, invalid, mutedSnapshot).failed(), "deleted destination is rejected");
        invalid = options;
        invalid.selection.region.length = std::nan("");
        require(collect(p, invalid, mutedSnapshot).failed(), "nonfinite range rejected");
        invalid = options;
        invalid.chords = {{0, 4, "bad chord", {12}, Provenance::userSpecified}};
        require(collect(p, invalid, mutedSnapshot).failed(), "invalid chord pitches rejected");
        auto patternOptions = options;
        patternOptions.selection.patternMode = true;
        patternOptions.selection.region = {1, 2};
        require(collect(p, patternOptions, mutedSnapshot).wasOk() && mutedSnapshot.events.front().start == -1,
                "Pattern mode uses source timeline independent of Song placements");
        auto id = controller.projectIdentity;
        controller.replace(p);
        require(id != controller.projectIdentity, "replacing project renews session identity");
        auto applyOptions = options;
        applyOptions.projectIdentity = controller.projectIdentity;
        Request request;
        request.role = "lead";
        require(collect(controller.project, applyOptions, request.snapshot).wasOk(), "accept snapshot");
        Candidate candidate{request.id,
                            request.snapshot.fingerprint,
                            "deterministic test fixture",
                            "fixture-1",
                            {{0, 1, 0, 60, .8f}, {2, 1, 0, 64, .7f}},
                            true};
        const auto sourceState = controller.project.state().toXmlString();
        auto malformed = candidate;
        malformed.events[0].start = std::nan("");
        require(accept(controller, applyOptions, request, malformed).failed() &&
                    controller.project.state().toXmlString() == sourceState,
                "invalid provider output preserves source");
        malformed = candidate;
        malformed.events[0].length = 5;
        require(validate(request, malformed).failed(), "out-of-region provider notes rejected");
        malformed = candidate;
        malformed.events[0].cents = 50;
        require(validate(request, malformed).failed(),
                "unmapped microtonal output cannot silently become integer MIDI");
        auto constrained = request;
        constrained.snapshot.destinationProfile.polyphony = 1;
        malformed = candidate;
        malformed.events[1].start = .5;
        require(validate(constrained, malformed).failed(),
                "declared monophonic destination rejects overlapping voices");
        request.variation = "idea variation";
        request.variationAmount = .25;
        auto invalidAmount = request;
        invalidAmount.variationAmount = std::nan("");
        require(validate(invalidAmount, candidate).failed(),
                "invalid variation amount cannot enter acceptance");
        require(accept(controller, applyOptions, request, candidate, 2).wasOk(),
                "candidate accepts canonically");
        require(controller.project.patterns.size() == p.patterns.size() + 1 &&
                    controller.project.placements.size() == p.placements.size() + 1,
                "acceptance creates a new source and one Song placement");
        require(controller.project.patterns.front().notes.size() == p.patterns.front().notes.size() &&
                    controller.project.placements.back().beat == 9 &&
                    controller.project.patterns.back().notes.front().channel == destination,
                "new part preserves linked sources, time origin and intended instrument");
        const auto acceptedState = controller.project.state().toXmlString();
        require(!controller.project.patterns.back().autoLength &&
                    controller.project.patterns.back().length == request.snapshot.selection.region.length,
                "accepted part preserves its explicitly requested region and trailing rests");
        const auto metadata = juce::JSON::parse(controller.project.patterns.back().generation);
        require(metadata["requestId"].toString() == request.id && bool(metadata["fixture"]) &&
                    double(metadata["variationMix"]) == .25,
                "accepted part stores reproducible settings and fixture provenance");
        controller.undoEdit();
        require(controller.project.state().toXmlString() == sourceState,
                "single undo restores exact original project");
        controller.redoEdit();
        require(controller.project.state().toXmlString() == acceptedState,
                "single redo restores exact accepted part");
        require(accept(controller, applyOptions, request, candidate).failed(),
                "accepted part changes relevant context; stale duplicate rejected");
        if (argc > 1)
        {
            const juce::File folder(argv[1]);
            require(folder.createDirectory().wasOk(), "context evidence directory");
            require(folder.getChildFile("context-summary.txt").replaceWithText(snapshot.summary()),
                    "summary evidence");
            require(saveProject(p, folder.getChildFile("context-fixture.vibra")).wasOk(), "fixture saves");
            Project loaded;
            require(loadProject(folder.getChildFile("context-fixture.vibra"), loaded).wasOk(),
                    "fixture reopens");
            require(checkFresh(loaded, options, snapshot).wasOk(),
                    "snapshot round trip preserves musical context");
            const auto acceptedFile = folder.getChildFile("accepted-region.vibra");
            require(saveProject(controller.project, acceptedFile).wasOk() &&
                        loadProject(acceptedFile, loaded).wasOk() &&
                        loaded.state().toXmlString() == acceptedState,
                    "accepted region and native provenance survive actual save/reopen exactly");
        }
        std::cout << "PASS " << checks << " CoProducer context checks; no AI or listening qualification.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
