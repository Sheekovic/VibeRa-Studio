// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CoProducerWorker.h"

static void workerAcceptance(const juce::File &node, const juce::File &repo, const juce::File &installation,
                             const juce::File &folder)
{
    using namespace vibera::coproducer;
    Controller controller;
    const auto source = controller.project.state().toXmlString();
    Options options;
    options.projectIdentity = controller.projectIdentity;
    options.selection = {
        true, controller.project.patterns.front().id, controller.project.channels.front().id, {0, 4}, {}, {}};
    Request request;
    request.role = "lead";
    request.variation = "continuation";
    request.seed = 17;
    require(collect(controller.project, options, request.snapshot).wasOk(), "worker immutable context");
    require(folder.createDirectory().wasOk(), "worker evidence directory");
    ProviderConfig config{node,        repo.getChildFile("scripts/coproducer/fixture-worker.cjs"),
                          {},          folder,
                          "basic_rnn", 10000,
                          1024,        20};
    auto await = [&](GenerationJob &job)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000;
        auto result = job.poll();
        while (result.status == JobStatus::running && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            juce::Thread::sleep(10);
            result = job.poll();
        }
        require(result.status != JobStatus::running, "owned worker reaches terminal status");
        return result;
    };
    for (const char *mode : {"ok", "malformed", "crash", "hang"})
    {
        config.installation = folder.getChildFile(mode);
        require(config.installation.createDirectory().wasOk(), "fixture installation directory");
        config.timeoutMilliseconds = juce::String(mode) == "hang" ? 250 : 10000;
        GenerationJob job;
        require(job.start(request, config).wasOk(), "owned fixture job starts asynchronously");
        auto result = await(job);
        if (juce::String(mode) == "ok")
            require(result.status == JobStatus::succeeded && result.candidates.size() == 1 &&
                        result.candidates.front().testFixture,
                    "fixture remains explicitly labelled");
        else if (juce::String(mode) == "hang")
            require(result.status == JobStatus::timedOut && result.candidates.empty(),
                    "timeout kills only owned worker and discards results");
        else
            require(result.status == JobStatus::failed && result.candidates.empty(),
                    "crash/malformed output cannot yield candidates");
        require(controller.project.state().toXmlString() == source,
                "failed/successful generation preserves source before acceptance");
        require(folder.findChildFiles(juce::File::findDirectories, false, "job-*").isEmpty(),
                "terminal worker reclaims only its unique job directory");
    }
    config.installation = folder.getChildFile("hang");
    config.timeoutMilliseconds = 10000;
    GenerationJob cancelledJob;
    require(cancelledJob.start(request, config).wasOk(), "cancellable worker starts");
    juce::Thread::sleep(100);
    const auto cancelTime = juce::Time::getMillisecondCounterHiRes();
    cancelledJob.cancel();
    require(juce::Time::getMillisecondCounterHiRes() - cancelTime < 10, "cancel API is nonblocking");
    auto cancelledResult = await(cancelledJob);
    require(cancelledResult.status == JobStatus::cancelled && cancelledResult.candidates.empty(),
            "cancel discards late results");
    require(controller.project.state().toXmlString() == source, "cancel keeps the project intact");
    config.installation = folder.getChildFile("ok");
    require(cancelledJob.start(request, config).wasOk() && await(cancelledJob).status == JobStatus::succeeded,
            "worker can recover after cancellation");
    cancelledJob.cancel();
    require(cancelledJob.poll().status == JobStatus::cancelled && cancelledJob.poll().candidates.empty(),
            "cancellation before consuming completed results discards them");

    config.script = repo.getChildFile("scripts/coproducer/model-worker.cjs");
    config.installation = installation;
    config.timeoutMilliseconds = 120000;
    GenerationJob realJob;
    require(realJob.start(request, config).wasOk(), "actual Magenta job starts through owned native process");
    auto actual = await(realJob);
    require(actual.status == JobStatus::succeeded && !actual.candidates.empty() &&
                !actual.candidates.front().testFixture,
            ("actual native model inference: " + actual.error).toRawUTF8());
    require(controller.project.state().toXmlString() == source, "actual model leaves source unchanged");
    require(actual.measurements["networkInference"].isBool() &&
                !bool(actual.measurements["networkInference"]),
            "actual inference uses local files");
    require(accept(controller, options, request, actual.candidates.front()).wasOk(),
            "actual model output accepts through canonical command");
    controller.undoEdit();
    require(controller.project.state().toXmlString() == source, "actual generation has exact one-step undo");
    auto unsupported = request;
    unsupported.style = "funk";
    require(providerPreflight(unsupported, config).failed(),
            "unsupported conditioning is rejected instead of ignored");
    for (double amount : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
    {
        unsupported = request;
        unsupported.variationAmount = amount;
        require(providerPreflight(unsupported, config).failed(),
                "invalid latent mix is rejected before worker launch");
    }
    unsupported = request;
    unsupported.snapshot.numerator = 6;
    unsupported.snapshot.denominator = 8;
    require(providerPreflight(unsupported, config).failed(), "unsupported meter cannot silently become 4/4");
    folder.getChildFile("worker-acceptance.txt")
        .replaceWithText("PASS: owned native model process with memory/CPU limits, async generation, "
                         "fixture/crash/malformed/timeout/cancel/recovery, "
                         "actual local MelodyRNN inference, validated acceptance and exact undo. No "
                         "hardware/listening or complete model qualification.\n");
}
