// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CoProducerWorker.h"
#include <algorithm>

static void anticipationAcceptance(const juce::File &python, const juce::File &repo,
                                   const juce::File &installation, const juce::File &folder)
{
    Controller controller;
    auto project = Project::empty();
    auto &pattern = project.patterns.front();
    pattern.length = 16;
    pattern.autoLength = false; // Exact context window includes trailing rests.
    const auto destination = project.channels[0].id;
    auto note = [&](int channel, double start, double length, int pitch)
    {
        pattern.notes.push_back(
            {project.allocate(), project.channels[size_t(channel)].id, start, length, pitch, .8f, false});
    };
    for (int pitch : {60, 64, 67})
        note(1, 4, 2, pitch);
    for (int pitch : {65, 69, 72})
        note(1, 6, 2, pitch);
    note(2, 5, .6, 43);
    note(0, 1, 1, 43);
    note(0, 2.5, 1, 48);
    note(3, 10, 1, 67);
    const auto patternId = pattern.id;
    controller.replace(project);
    const auto source = controller.project.state().toXmlString();
    Options options;
    options.projectIdentity = controller.projectIdentity;
    options.selection = {true, patternId, destination, {4, 4}, {}, {}};
    for (int index = 0; index < 4; ++index)
    {
        ModelInstrumentMapping mapping{project.channels[size_t(index)].id,
                                       index == 1   ? 0
                                       : index == 3 ? 25
                                                    : 32,
                                       {},
                                       Provenance::userSpecified};
        for (int pitch = 0; pitch < 128; ++pitch)
            mapping.pitches.push_back(pitch);
        options.modelMappings.push_back(std::move(mapping));
    }
    Request request;
    request.role = "native part";
    request.seed = 17;
    require(collect(controller.project, options, request.snapshot).wasOk(), "mapped immutable snapshot");
    require(folder.createDirectory().wasOk(), "native anticipation evidence directory");
    ProviderConfig config{python,
                          repo.getChildFile("scripts/coproducer/anticipation-worker.py"),
                          installation,
                          folder,
                          "anticipation-small",
                          120000,
                          4096,
                          20};
    require(providerPreflight(request, config).wasOk(), "native anticipation preflight");
    auto smallLimit = config;
    smallLimit.memoryMegabytes = 1024;
    require(providerPreflight(request, smallLimit).failed(),
            "insufficient memory policy rejected before launch");
    auto changed = options;
    changed.modelMappings.front().program = 33;
    require(checkFresh(controller.project, changed, request.snapshot).failed(),
            "changed GM mapping invalidates context");
    changed = options;
    changed.modelMappings.front().pitches.pop_back();
    require(checkFresh(controller.project, changed, request.snapshot).failed(),
            "changed pitch map invalidates context");
    changed = options;
    std::reverse(changed.modelMappings.begin(), changed.modelMappings.end());
    std::reverse(changed.modelMappings.front().pitches.begin(), changed.modelMappings.front().pitches.end());
    require(checkFresh(controller.project, changed, request.snapshot).wasOk(), "mapping order canonicalized");
    changed = options;
    changed.modelMappings.push_back(changed.modelMappings.front());
    Snapshot invalid;
    require(collect(controller.project, changed, invalid).failed(), "duplicate Channel mapping rejected");
    changed = options;
    changed.modelMappings.front().provenance = Provenance::inferred;
    require(collect(controller.project, changed, invalid).failed(), "unqualified inferred mapping rejected");
    auto unsupported = request;
    unsupported.style = "funk";
    require(providerPreflight(unsupported, config).failed(), "unsupported native style rejected");
    unsupported = request;
    unsupported.snapshot.modelMappings.clear();
    require(providerPreflight(unsupported, config).failed(), "unknown native destination mapping rejected");
    require(folder.getChildFile("native-request.json")
                .replaceWithText(juce::JSON::toString(serializeRequest(request, config))),
            "native request evidence");
    auto await = [&](GenerationJob &job)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 125000;
        auto result = job.poll();
        while (result.status == JobStatus::running && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            juce::Thread::sleep(10);
            result = job.poll();
        }
        require(result.status != JobStatus::running, "owned anticipation worker terminal");
        return result;
    };
    GenerationJob job;
    require(job.start(request, config).wasOk(), "real Python worker starts via native owner");
    const auto actual = await(job);
    require(actual.status == JobStatus::succeeded && actual.candidates.size() == 1,
            ("native anticipation result: " + actual.error).toRawUTF8());
    const auto &candidate = actual.candidates.front();
    require(!candidate.testFixture && candidate.hasSeed && candidate.seed == 17,
            "real candidate provenance and seed");
    require(candidate.events.size() == 2 && candidate.events[0].pitch == 42 &&
                candidate.events[1].pitch == 41 && candidate.events[0].start == 1.5 &&
                candidate.events[1].length == 2,
            "native quarter-note model output matches independent probe");
    require(actual.contextReport["ledger"].isArray() &&
                actual.contextReport["ledger"].getArray()->size() == 10,
            "consumption ledger survives native response parsing");
    require(actual.measurements["networkInference"].isBool() &&
                !bool(actual.measurements["networkInference"]),
            "native inference offline");
    require(controller.project.state().toXmlString() == source, "native inference preserves source project");
    auto restricted = request;
    restricted.snapshot.modelMappings.front().pitches = {36};
    require(validate(restricted, candidate).failed(),
            "native validator rejects unmapped output pitches independently");
    require(accept(controller, options, request, candidate).wasOk(),
            "native candidate accepts through one canonical command");
    const auto accepted = controller.project.state().toXmlString();
    require(controller.project.patterns.back().generation.contains("modelInstrumentMappings"),
            "accepted mapping provenance retained");
    require(saveProject(controller.project, folder.getChildFile("accepted.vibra")).wasOk(),
            "accepted mapped project saves");
    Project loaded;
    require(loadProject(folder.getChildFile("accepted.vibra"), loaded).wasOk() &&
                loaded.state().toXmlString() == accepted,
            "mapped project exact round trip");
    controller.undoEdit();
    require(controller.project.state().toXmlString() == source, "mapped generation exact one-step undo");
    controller.redoEdit();
    require(controller.project.state().toXmlString() == accepted, "mapped generation exact redo");
    controller.undoEdit();
    require(folder.getChildFile("native-measurements.json")
                .replaceWithText(juce::JSON::toString(actual.measurements)),
            "native measurements retained");
    require(folder.getChildFile("native-context.json")
                .replaceWithText(juce::JSON::toString(actual.contextReport)),
            "native ledger retained");
    GenerationJob cancelled;
    require(cancelled.start(request, config).wasOk(), "native Python cancellation job starts");
    juce::Thread::sleep(100);
    const auto cancelTime = juce::Time::getMillisecondCounterHiRes();
    cancelled.cancel();
    require(juce::Time::getMillisecondCounterHiRes() - cancelTime < 10,
            "native Python cancel is nonblocking");
    require(await(cancelled).status == JobStatus::cancelled, "native owned Python cancellation reaps worker");
    require(controller.project.state().toXmlString() == source, "native cancellation preserves source");
    config.timeoutMilliseconds = 50;
    GenerationJob timeout;
    require(timeout.start(request, config).wasOk(), "native Python timeout job starts");
    require(await(timeout).status == JobStatus::timedOut, "native owned Python timeout terminal");
    config.timeoutMilliseconds = 120000;
    require(timeout.start(request, config).wasOk() && await(timeout).status == JobStatus::succeeded,
            "native real inference recovers after timeout");
    require(folder.findChildFiles(juce::File::findDirectories, false, "job-*").isEmpty(),
            "native job directories reclaimed after terminal outcomes");
    folder.getChildFile("native-acceptance.txt")
        .replaceWithText("PASS: real offline Anticipation through native owned CPU/memory-limited process, "
                         "mapped context, provenance, acceptance/round-trip/undo, cancellation, timeout and "
                         "recovery. No GUI/playback/hardware/listening qualification.\n");
}
