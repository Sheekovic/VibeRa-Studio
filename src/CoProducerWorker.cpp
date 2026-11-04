// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CoProducerWorker.h"
#include "WorkerProcess.h"
#include <cmath>
#include <algorithm>
#include <set>

namespace vibera::coproducer
{
namespace
{
juce::var object()
{
    return juce::var(new juce::DynamicObject());
}
void set(juce::var &v, const char *name, const juce::var &value)
{
    v.getDynamicObject()->setProperty(name, value);
}
bool numeric(const juce::var &v)
{
    return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite(double(v));
}
juce::Result readBounded(const juce::File &file, juce::var &value, juce::int64 limit)
{
    if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > limit)
        return juce::Result::fail("Model worker response is missing or exceeds its byte budget.");
    auto stream = file.createInputStream();
    if (!stream)
        return juce::Result::fail("Cannot read model worker response.");
    juce::MemoryBlock bytes;
    stream->readIntoMemoryBlock(bytes, size_t(limit + 1));
    if (bytes.getSize() > size_t(limit))
        return juce::Result::fail("Model worker response grew past its budget.");
    const auto *characters = static_cast<const char *>(bytes.getData());
    int depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < bytes.getSize(); ++i)
    {
        const char c = characters[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        }
        else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > 16)
                return juce::Result::fail("Model response nesting exceeds its schema budget.");
        }
        else if (c == '}' || c == ']')
            --depth;
    }
    return juce::JSON::parse(
        juce::String::fromUTF8(static_cast<const char *>(bytes.getData()), int(bytes.getSize())), value);
}
} // namespace
juce::Result providerPreflight(const Request &request, const ProviderConfig &config)
{
    auto fail = [](const char *message) { return juce::Result::fail(message); };
    if (!request.snapshot.complete || request.id.isEmpty() || request.id.length() > 128 ||
        request.snapshot.fingerprint.length() != 64 || request.candidateCount < 1 ||
        request.candidateCount > 3 || !std::isfinite(request.variationAmount) ||
        request.variationAmount < 0 || request.variationAmount > 1 || request.seed > 0xffffffffULL ||
        request.snapshot.events.size() > 8192)
        return fail("Invalid or incomplete CoProducer generation request.");
    if (config.timeoutMilliseconds < 50 || config.timeoutMilliseconds > 300000 ||
        config.memoryMegabytes < 256 || config.memoryMegabytes > 4096 || config.cpuPercent < 1 ||
        config.cpuPercent > 50)
        return fail("Invalid model worker resource limits.");
    if (!config.executable.existsAsFile() || !config.script.existsAsFile() ||
        !config.installation.isDirectory())
        return fail("Local generation runtime or model installation is missing. Ordinary DAW features remain "
                    "available.");
    if (config.model == "anticipation-small")
    {
        const auto &s = request.snapshot;
        if (!request.style.isEmpty() || request.role != "native part" || request.variation != "new idea" ||
            !s.chords.empty())
            return fail("Anticipation supports native new parts; supplied chord/style/role/variation "
                        "controls are unqualified.");
        if (config.memoryMegabytes < 4096)
            return fail("This Anticipation CPU runtime requires the 4 GiB worker policy.");
        if (!std::isfinite(s.tempo) || s.tempo < 20 || s.tempo > 400 || s.numerator < 1 || s.numerator > 32 ||
            (s.denominator != 2 && s.denominator != 4 && s.denominator != 8 && s.denominator != 16))
            return fail("Invalid Anticipation fixed tempo/meter.");
        const auto scale = 6000.0 / s.tempo;
        const auto ticks = s.selection.region.length * scale;
        if (!std::isfinite(ticks) || ticks < 1 || ticks > 1500 || std::abs(ticks - std::round(ticks)) > 1e-7)
            return fail("Anticipation target must fit 10 ms boundaries and at most 15 seconds.");
        auto mappingFor = [&](Id channel) -> const ModelInstrumentMapping *
        {
            for (const auto &mapping : s.modelMappings)
                if (mapping.channel == channel)
                    return &mapping;
            return nullptr;
        };
        if (!mappingFor(s.selection.destination))
            return fail("Choose an explicit destination GM program and pitch/drum map.");
        std::set<Id> channels;
        if (s.modelMappings.size() > 256)
            return fail("Excessive instrument mappings.");
        for (const auto &mapping : s.modelMappings)
        {
            std::set<int> pitches(mapping.pitches.begin(), mapping.pitches.end());
            if (!mapping.channel || !channels.insert(mapping.channel).second || mapping.program < 0 ||
                mapping.program > 128 || mapping.provenance != Provenance::userSpecified || pitches.empty() ||
                pitches.size() > 128 || pitches.size() != mapping.pitches.size() || *pitches.begin() < 0 ||
                *pitches.rbegin() > 127)
                return fail("Invalid explicit GM/pitch mapping.");
        }
        size_t consumed = 0;
        for (const auto &event : s.events)
        {
            const bool target = event.start < s.selection.region.length && event.start + event.length > 0;
            if (event.channel == s.selection.destination && event.locked && target)
                return fail("Exact destination locks are unsupported by Anticipation.");
            if (!event.audible || event.start * scale < -500 || event.start * scale > ticks + 500 ||
                (event.channel == s.selection.destination && target))
                continue;
            const auto *mapping = mappingFor(event.channel);
            if (!mapping || std::find(mapping->pitches.begin(), mapping->pitches.end(), event.pitch) ==
                                mapping->pitches.end())
                return fail("An included context Channel needs an explicit program and matching pitch map.");
            const auto duration = std::round(event.length * scale);
            if (!std::isfinite(duration) || duration < 1 || duration > 999 || ++consumed > 240)
                return fail(
                    "Anticipation context exceeds representable duration/event bounds; narrow context.");
        }
        return juce::Result::ok();
    }
    if (config.model != "basic_rnn" && config.model != "multitrack_chords")
        return fail("Selected generation provider is unsupported.");
    if (!request.style.isEmpty())
        return fail("These Magenta baselines do not support a style control.");
    if (config.model == "basic_rnn" &&
        (request.role != "lead" || request.variation != "continuation" || !request.snapshot.chords.empty()))
        return fail(
            "MelodyRNN supports melodic continuation; chord/style/performance conditioning is unavailable.");
    const auto &s = request.snapshot;
    if (s.numerator != 4 || s.denominator != 4)
        return fail("This initial Magenta adapter requires 4/4; the project meter will not be changed.");
    if (!std::isfinite(s.selection.region.length) || s.selection.region.length <= 0 ||
        s.selection.region.length > 32)
        return fail("This provider supports at most eight 4/4 bars per request.");
    if (config.model == "multitrack_chords" &&
        (std::fmod(s.selection.region.length, 4.0) != 0 || request.role != "native part" ||
         (request.variation != "new idea" && request.variation != "idea variation")))
        return fail("Multitrack MusicVAE needs whole 4/4 bars and a supported idea variation mode.");
    if (config.model == "basic_rnn" && std::fmod(s.selection.region.length * 4.0, 1.0) != 0)
        return fail("MelodyRNN duration must fit its sixteenth-note grid.");
    if (config.model == "multitrack_chords" && s.destinationProfile.family != "keys" &&
        s.destinationProfile.family != "bass" && s.destinationProfile.family != "guitar" &&
        s.destinationProfile.family != "strings" && s.destinationProfile.family != "synth")
        return fail("Choose a supported melodic instrument family. Drum-map qualification is pending.");
    return juce::Result::ok();
}
juce::var serializeRequest(const Request &request, const ProviderConfig &config)
{
    auto root = object();
    set(root, "schema", 1);
    set(root, "requestId", request.id);
    set(root, "fingerprint", request.snapshot.fingerprint);
    set(root, "model", config.model);
    set(root, "seed", juce::int64(request.seed));
    set(root, "count", request.candidateCount);
    set(root, "role", request.role);
    set(root, "style", request.style);
    set(root, "variation", request.variation);
    set(root, "variationAmount", request.variationAmount);
    set(root, "family", request.snapshot.destinationProfile.family);
    set(root, "length", request.snapshot.selection.region.length);
    set(root, "origin", request.snapshot.selection.region.start);
    set(root, "tempo", request.snapshot.tempo);
    set(root, "numerator", request.snapshot.numerator);
    set(root, "denominator", request.snapshot.denominator);
    // IDs travel as decimal strings, not JavaScript numbers. No paths, names or plugin state.
    set(root, "destination", juce::String(request.snapshot.selection.destination));
    juce::Array<juce::var> events;
    for (const auto &event : request.snapshot.events)
    {
        auto item = object();
        set(item, "channel", juce::String(event.channel));
        set(item, "note", juce::String(event.note));
        set(item, "start", event.start);
        set(item, "length", event.length);
        set(item, "pitch", event.pitch);
        set(item, "velocity", event.velocity);
        set(item, "locked", event.locked);
        set(item, "audible", event.audible);
        events.add(item);
    }
    set(root, "events", events);
    if (config.model == "anticipation-small")
    {
        auto profiles = object();
        for (const auto &mapping : request.snapshot.modelMappings)
        {
            auto item = object();
            set(item, "program", mapping.program);
            set(item, "source", "user-specified");
            juce::Array<juce::var> pitches;
            for (auto pitch : mapping.pitches)
                pitches.add(pitch);
            set(item, "pitches", pitches);
            profiles.getDynamicObject()->setProperty(juce::String(mapping.channel), item);
        }
        set(root, "profiles", profiles);
        set(root, "allowUnmappedContext", false);
    }
    juce::Array<juce::var> chords;
    for (const auto &chord : request.snapshot.chords)
    {
        auto item = object();
        juce::Array<juce::var> pitches;
        for (int pitch : chord.pitchClasses)
            pitches.add(pitch);
        set(item, "start", chord.start - request.snapshot.selection.region.start);
        set(item, "length", chord.length);
        set(item, "pitches", pitches);
        chords.add(item);
    }
    set(root, "chords", chords);
    return root;
}
juce::Result parseResponse(const juce::var &data, const Request &request, JobResult &output)
{
    auto fail = [](const char *message) { return juce::Result::fail(message); };
    if (!data.isObject() || !data["schema"].isInt() || int(data["schema"]) != 1 ||
        !data["requestId"].isString() || data["requestId"].toString() != request.id ||
        !data["fingerprint"].isString() || data["fingerprint"].toString() != request.snapshot.fingerprint)
        return fail("Worker response schema or request identity does not match.");
    if (!data["status"].isString())
        return fail("Worker response is missing status.");
    if (data["status"].toString() != "ok")
        return juce::Result::fail(data["error"].isString() ? data["error"].toString().substring(0, 1024)
                                                           : "Model worker failed.");
    const auto *candidates = data["candidates"].getArray();
    if (!candidates || candidates->isEmpty() || candidates->size() > request.candidateCount)
        return fail("Worker returned an invalid candidate count.");
    JobResult parsed;
    parsed.status = JobStatus::succeeded;
    parsed.progress = 1;
    for (const auto &item : *candidates)
    {
        if (!item.isObject() || !item["provider"].isString() || !item["modelVersion"].isString() ||
            item["provider"].toString().length() > 256 || item["modelVersion"].toString().length() > 256 ||
            !item["testFixture"].isBool())
            return fail("Missing candidate provenance.");
        Candidate candidate{request.id,
                            request.snapshot.fingerprint,
                            item["provider"].toString(),
                            item["modelVersion"].toString(),
                            {},
                            bool(item["testFixture"])};
        if ((!item["seed"].isInt() && !item["seed"].isInt64()) || juce::int64(item["seed"]) < 0 ||
            juce::int64(item["seed"]) > 0xffffffffLL)
            return fail("Missing or invalid per-candidate seed provenance.");
        candidate.seed = std::uint64_t(juce::int64(item["seed"]));
        candidate.hasSeed = true;
        const auto *events = item["events"].getArray();
        if (!events || events->isEmpty() || events->size() > 65536)
            return fail("Invalid model event count.");
        for (const auto &event : *events)
        {
            if (!event.isObject() || !numeric(event["start"]) || !numeric(event["length"]) ||
                !numeric(event["velocity"]) || !numeric(event["cents"]) || !event["pitch"].isInt())
                return fail("Model event has missing, nonfinite or incorrect numeric types.");
            candidate.events.push_back({double(event["start"]), double(event["length"]),
                                        double(event["cents"]), int(event["pitch"]),
                                        float(event["velocity"])});
        }
        auto validation = validate(request, candidate);
        if (validation.failed())
            return validation;
        parsed.candidates.push_back(std::move(candidate));
    }
    if (const auto *warnings = data["warnings"].getArray())
    {
        if (warnings->size() > 128)
            return fail("Excessive model warnings.");
        for (const auto &warning : *warnings)
        {
            if (!warning.isString() || warning.toString().length() > 1024)
                return fail("Invalid model warning.");
            parsed.warnings.add(warning.toString());
        }
    }
    parsed.measurements = data["measurements"];
    parsed.contextReport = data["context"];
    output = std::move(parsed);
    return juce::Result::ok();
}
GenerationJob::~GenerationJob()
{
    cancel();
    if (thread.joinable())
        thread.join();
}
JobResult GenerationJob::poll() const
{
    std::lock_guard guard(mutex);
    if (cancelled.load() && result.status != JobStatus::idle && result.status != JobStatus::running)
    {
        JobResult discarded;
        discarded.status = JobStatus::cancelled;
        return discarded;
    }
    return result;
}
juce::Result GenerationJob::start(Request request, ProviderConfig config)
{
    if (poll().status == JobStatus::running)
        return juce::Result::fail("Generation is already running.");
    auto ready = providerPreflight(request, config);
    if (ready.failed())
        return ready;
    if (thread.joinable())
        thread.join();
    cancelled.store(false);
    {
        std::lock_guard guard(mutex);
        result = {};
        result.status = JobStatus::running;
    }
    thread = std::thread(
        [this, request = std::move(request), config = std::move(config)]
        {
            JobResult terminal;
            terminal.status = JobStatus::failed;
            const auto directory = config.cache.getChildFile("job-" + juce::Uuid().toString());
            const auto input = directory.getChildFile("request.json"),
                       output = directory.getChildFile("response.json");
            std::unique_ptr<WorkerProcess> worker;
            auto finish = [&]
            {
                worker.reset(); // Reap owned process/job handles before cleanup or terminal publication.
                if (cancelled.load())
                {
                    terminal = {};
                    terminal.status = JobStatus::cancelled;
                }
                // Only the exact newly-created job files are reclaimed; no recursive user-data cleanup.
                for (const char *name : {"request.json", "response.json", "response.json.tmp", "cancel",
                                         "progress.json", "progress.json.tmp"})
                    directory.getChildFile(name).deleteFile();
                directory.deleteFile();
                std::lock_guard guard(mutex);
                result = std::move(terminal);
            };
            if (directory.createDirectory().failed() ||
                !input.replaceWithText(juce::JSON::toString(serializeRequest(request, config))))
            {
                terminal.error = "Could not prepare a private local generation request.";
                finish();
                return;
            }
            if (cancelled.load())
            {
                finish();
                return;
            }
            worker = std::make_unique<WorkerProcess>();
            juce::StringArray arguments{config.executable.getFullPathName()};
            if (config.model == "anticipation-small")
                arguments.add("-B"); // No installation/source bytecode mutations.
            else
                arguments.add("--max-old-space-size=512");
            arguments.addArray({config.script.getFullPathName(), config.installation.getFullPathName(),
                                input.getFullPathName(), output.getFullPathName()});
            if (!worker->start(arguments, 0, {config.memoryMegabytes, config.cpuPercent, true, true}))
            {
                terminal.error = "Could not start the contained generation worker.";
                finish();
                return;
            }
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + config.timeoutMilliseconds;
            while (worker->isRunning())
            {
                if (cancelled.load() || juce::Time::getMillisecondCounterHiRes() >= deadline)
                {
                    directory.getChildFile("cancel").replaceWithText("cancel");
                    worker->requestGracefulShutdown();
                    if (!worker->waitForProcessToFinish(200))
                        worker->kill();
                    worker->waitForProcessToFinish(1000);
                    terminal.status = cancelled.load() ? JobStatus::cancelled : JobStatus::timedOut;
                    terminal.error =
                        cancelled.load() ? "" : "Generation timed out; the project was preserved.";
                    finish();
                    return;
                }
                juce::var progress;
                if (readBounded(directory.getChildFile("progress.json"), progress, 4096).wasOk() &&
                    progress["requestId"].toString() == request.id && numeric(progress["progress"]))
                {
                    std::lock_guard guard(mutex);
                    result.progress = juce::jlimit(0.0f, .99f, float(progress["progress"]));
                }
                worker->waitForProcessToFinish(30);
            }
            if (worker->getExitCode() != 0)
                terminal.error =
                    "Local model worker exited with error " + juce::String(worker->getExitCode());
            else
            {
                juce::var response;
                auto parsed = readBounded(output, response, 4 * 1024 * 1024);
                if (parsed.wasOk())
                    parsed = parseResponse(response, request, terminal);
                if (parsed.failed())
                {
                    terminal.status = JobStatus::failed;
                    terminal.error = parsed.getErrorMessage();
                }
            }
            finish();
        });
    return juce::Result::ok();
}
} // namespace vibera::coproducer
