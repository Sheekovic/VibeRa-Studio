// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RoutingLifetimeAcceptance.h"
#include "NativePluginCpuReadback.h"
#include <array>
// Separate original diagnostic workload. This does not qualify or replace
// source-admission/worker tests, and must not be registered as an expected pass.
namespace vibera::test::native_idle
{
__declspec(noinline) inline void tests(const juce::File &folder, bool phaseDiagnostic = false,
                                       bool pruneCold = false)
{
    using namespace routing_lifetime;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned native idle diagnostic");
    Storage storage(folder.getChildFile("storage"));
    Fixture f(folder.getChildFile("fixture"), false, true, {}, pruneCold);
    if (pruneCold)
    {
        // First of the SAME existing four preludes, not an added callback.
        // Diagnostic count capture is removed before uninstrumented28 timing.
        auto *context = f.nativeEdit().getTransport().getCurrentPlaybackContext();
        require(context && f.coldPruning, "Actual cold context/proof");
        std::array<tracktion::graph::NativePhaseCapture::Context, 4> contexts{};
        std::array<tracktion::graph::NativePhaseCapture::Graph, 8> graphs{};
        tracktion::graph::NativePhaseCapture capture;
        capture.contexts = contexts.data();
        capture.contextCapacity = contexts.size();
        capture.graphs = graphs.data();
        capture.graphCapacity = graphs.size();
        require(context->setNativePhaseCapture(&capture), "Cold first-prelude count probe installed");
        struct ClearProbe
        {
            te::EditPlaybackContext *context;
            ~ClearProbe()
            {
                (void)context->setNativePhaseCapture(nullptr);
            }
        } clear{context};
        f.silentCallback("Cold derivative first original prelude");
        require(capture.graphUsed > 0 && capture.droppedContexts == 0 && capture.droppedGraphs == 0,
                "Actual cold native graph count captured, not estimated");
        for (size_t n = 0; n < capture.graphUsed; ++n)
        {
            require(graphs[n].completed && graphs[n].nodeCount > 0 &&
                        (n == 0 || graphs[n].nodeCount == graphs[0].nodeCount),
                    "Completed consistent cold graph count");
        }
        f.coldPruning->remainingNodes = graphs[0].nodeCount;
        f.coldPruning->selectedSerial = graphs[0].selectionSerial;
        cold_mixer_pruning::retain(*f.coldPruning, *f.audio);
        require(f.coldPruning->remainingNodes < 189,
                "Actual cold graph must reduce known identical ordinary189-node topology");
    }
    for (int n = pruneCold ? 1 : 0; n < 4; ++n)
        f.silentCallback("Prepared original closed no-note app context");
    if (pruneCold)
        cold_mixer_pruning::retain(*f.coldPruning, *f.audio);
    auto &transport = f.nativeEdit().getTransport();
    auto *const context = transport.getCurrentPlaybackContext();
    require(context && context->isPlaybackGraphAllocated() && context->getNodePlayHead(),
            "Actual allocated ordinary app graph/head before idle measurements");
    // Separate instrumented invocation only; ordinary CLI leaves both native
    // pointers null. Storage is fixed CONTROL-owned and read after callback.
    std::array<tracktion::graph::NativePhaseCapture::Context, 16> contextRecords{};
    std::array<tracktion::graph::NativePhaseCapture::Graph, 64> graphRecords{};
    tracktion::graph::NativePhaseCapture phases;
    phases.contexts = contextRecords.data();
    phases.contextCapacity = contextRecords.size();
    phases.graphs = graphRecords.data();
    phases.graphCapacity = graphRecords.size();
    require(!phaseDiagnostic || context->setNativePhaseCapture(&phases),
            "Install fixed probe into actual allocated context");
    struct CaptureBinding
    {
        te::TransportControl &transport;
        te::EditPlaybackContext *context;
        ~CaptureBinding()
        {
            if (context && transport.getCurrentPlaybackContext() == context)
                (void)context->setNativePhaseCapture(nullptr);
        }
    } binding{transport, phaseDiagnostic ? context : nullptr};
    juce::String contextCsv =
        "outer,index,enteredTicks,exitedTicks,frames,graphBegin,graphEnd,completed,contextToken\n";
    juce::String graphCsv = "outer,index,contextIndex,referenceStart,referenceEnd,frames,nodes,"
                            "configuredThreads,singleThreaded,selectionSerial,prepareStartTicks,"
                            "prepareEndTicks,processEndTicks,mixEndTicks,completed,playerToken\n";
    unsigned phaseFailures = 0;
    const auto source = f.audio->projectionDiagnostics();
    const auto starts = f.device->starts, stops = f.device->stops;
    require(f.audio->pluginDiagnostics().empty() && f.audio->retainedPluginHostCount() == 0 &&
                f.controller.project.patterns.front().notes.empty() && transport.isPlaying(),
            "Ordinary playing no-note fixture has no native plugin worker or recorded notes");
    const auto pointer = [](const void *p)
    { return juce::String::toHexString(juce::uint64(reinterpret_cast<std::uintptr_t>(p))); };
    juce::String csv =
        "outer,physical,callbackMs,deadlineMs,deadlineMet,newCalls,deleteCalls,exactSilence,"
        "referenceBeforeStart,referenceBeforeEnd,referenceAfterStart,referenceAfterEnd,referenceAdvance,"
        "clockExact,sameEdit,sameContext,contextPointer,playing,graph,headSeconds,headPlaying,"
        "renderCallsBefore,renderCallsAfter,heldInputKeys,deviceStarts,deviceStops,deviceRate,deviceBuffer,"
        "activeEdits,activeContexts,nativePlugins,nativeWorkers,";
    csv += SyntheticCallbackTiming::csvHeader;
    csv += "\n";
    juce::String cpu = source_burst_acceptance::nativePluginCpuCsvHeader;
    unsigned deadlineFailures = 0, safetyFailures = 0, clockFailures = 0, identityFailures = 0;
    unsigned silenceFailures = 0, totalNew = 0, totalDelete = 0;
    int frames = 0;
    // Exact ACTUAL original source diagnostic ordering, confirmed from its live source.
    constexpr std::array<int, 7> shape{127, 64, 65, 512, 256, 1, 255};
    for (int outer = 0; outer < 28; ++outer)
    {
        const auto count = shape[std::size_t(outer % 7)];
        juce::AudioBuffer<float> output(2, count); // CONTROL, before probe/timer.
        const auto before = f.audio->projectionDiagnostics();
        const auto referenceBefore = context->getNodePlayHead()->getReferenceSampleRange();
        const auto allocation = callbackAllocations.load(), destruction = callbackDestructions.load();
        if (phaseDiagnostic)
        {
            phases.contextUsed = phases.graphUsed = 0;
            phases.droppedContexts = phases.droppedGraphs = 0;
            phases.label = static_cast<uint64_t>(outer);
            phases.activeContext = std::numeric_limits<size_t>::max();
        }
        const auto began = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                bool old = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = old;
                }
            } probe;
            f.device->process(output);
        }
        const auto ended = juce::Time::getHighResolutionTicks();
        if (phaseDiagnostic)
        {
            bool coherent = phases.contextUsed > 0 && phases.graphUsed > 0 && phases.droppedContexts == 0 &&
                            phases.droppedGraphs == 0;
            uint64_t graphFrames = 0, contextFrames = 0;
            for (size_t index = 0; index < phases.contextUsed; ++index)
            {
                const auto &row = contextRecords[index];
                contextFrames += static_cast<uint64_t>(row.frames);
                coherent &= row.label == static_cast<uint64_t>(outer) && row.completed &&
                            row.context == reinterpret_cast<uintptr_t>(context) && row.frames > 0 &&
                            began <= row.entered && row.entered <= row.exited && row.exited <= ended &&
                            row.graphBegin < row.graphEnd && row.graphEnd <= phases.graphUsed;
                contextCsv += juce::String(outer) + "," + juce::String(int(index)) + "," +
                              juce::String(row.entered) + "," + juce::String(row.exited) + "," +
                              juce::String(row.frames) + "," + juce::String(int(row.graphBegin)) + "," +
                              juce::String(int(row.graphEnd)) + "," + juce::String(int(row.completed)) + "," +
                              juce::String::toHexString(juce::uint64(row.context)) + "\n";
            }
            for (size_t index = 0; index < phases.graphUsed; ++index)
            {
                const auto &row = graphRecords[index];
                graphFrames += row.frames;
                bool enclosed = row.contextIndex < phases.contextUsed;
                if (enclosed)
                {
                    const auto &parent = contextRecords[row.contextIndex];
                    enclosed = parent.graphBegin <= index && index < parent.graphEnd &&
                               parent.entered <= row.preparationStart && row.mixEnd <= parent.exited;
                }
                coherent &= enclosed && row.label == static_cast<uint64_t>(outer) && row.completed &&
                            row.frames > 0 && row.nodeCount > 0 && row.selectionSerial > 0 &&
                            row.referenceEnd - row.referenceStart == row.frames &&
                            row.preparationStart <= row.preparationEnd &&
                            row.preparationEnd <= row.processingEnd && row.processingEnd <= row.mixEnd;
                graphCsv += juce::String(outer) + "," + juce::String(int(index)) + "," +
                            juce::String(juce::uint64(row.contextIndex)) + "," +
                            juce::String(row.referenceStart) + "," + juce::String(row.referenceEnd) + "," +
                            juce::String(row.frames) + "," + juce::String(juce::uint64(row.nodeCount)) + "," +
                            juce::String(juce::uint64(row.configuredThreads)) + "," +
                            juce::String(int(row.singleThreaded)) + "," +
                            juce::String(juce::uint64(row.selectionSerial)) + "," +
                            juce::String(row.preparationStart) + "," + juce::String(row.preparationEnd) +
                            "," + juce::String(row.processingEnd) + "," + juce::String(row.mixEnd) + "," +
                            juce::String(int(row.completed)) + "," +
                            juce::String::toHexString(juce::uint64(row.player)) + "\n";
            }
            coherent &= graphFrames == uint64_t(count) && contextFrames == uint64_t(count);
            phaseFailures += !coherent;
            require(folder.getChildFile("native-phase-contexts.csv").replaceWithText(contextCsv) &&
                        folder.getChildFile("native-phase-graphs.csv").replaceWithText(graphCsv),
                    "Retain every actual context/subrange phase before strict aggregate assertions");
        }
        const auto a = callbackAllocations.load() - allocation, d = callbackDestructions.load() - destruction;
        const auto timing = f.device->lastTiming();
        const auto ms = SyntheticCallbackTiming::milliseconds(began, ended);
        const auto deadline =
            1000. * count / 48000.; // Original literal per-physical span, no floor/tolerance.
        const auto after = f.audio->projectionDiagnostics();
        const auto referenceAfter = context->getNodePlayHead()->getReferenceSampleRange();
        const auto advance = referenceAfter.getEnd() - referenceBefore.getEnd();
        const bool clockExact = advance == count;
        bool silence = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < count; ++n)
                silence &= output.getSample(c, n) == 0.f; // NaN/Inf also fail exact silence.
        const bool sameEdit = after.edit == source.edit && before.edit == source.edit;
        const bool sameContext = transport.getCurrentPlaybackContext() == context &&
                                 before.playbackContext == source.playbackContext &&
                                 after.playbackContext == source.playbackContext;
        const bool graph = context->isPlaybackGraphAllocated();
        const auto beforeRender = before.channels.empty() ? 0 : before.channels.front().renderCalls;
        const auto afterRender = after.channels.empty() ? 0 : after.channels.front().renderCalls;
        const auto held = after.channels.size() == 1 ? after.channels.front().heldInputKeys : -1;
        int edits = 0, contexts = 0, plugins = 0;
        // CONTROL after frozen callback end. No native telemetry/ValueTrees in DSP.
        for (auto *edit : f.audio->engine.getActiveEdits().getEdits())
            if (edit)
            {
                ++edits;
                contexts += edit->getTransport().getCurrentPlaybackContext() != nullptr;
                plugins += te::getAllPlugins(*edit, true).size();
            }
        const auto workers = f.audio->pluginDiagnostics().size();
        const bool identity = sameEdit && sameContext && graph && transport.isPlaying() && edits == 1 &&
                              contexts == 1 && workers == 0 && f.audio->retainedPluginHostCount() == 0 &&
                              f.device->starts == starts && f.device->stops == stops &&
                              f.device->getCurrentSampleRate() == 48000. &&
                              f.device->getCurrentBufferSizeSamples() == 256 && before.channels.size() == 1 &&
                              after.channels.size() == 1 && held == 0;
        const bool safety = a == 0 && d == 0 && timing.containedBy(began, ended);
        deadlineFailures += ms > deadline;
        safetyFailures += !safety;
        clockFailures += !clockExact;
        identityFailures += !identity;
        silenceFailures += !silence;
        totalNew += a;
        totalDelete += d;
        frames += count;
        csv += juce::String(outer) + "," + juce::String(count) + "," + juce::String(ms, 9) + "," +
               juce::String(deadline, 9) + "," + juce::String(int(ms <= deadline)) + "," + juce::String(a) +
               "," + juce::String(d) + "," + juce::String(int(silence)) + "," +
               juce::String(referenceBefore.getStart()) + "," + juce::String(referenceBefore.getEnd()) + "," +
               juce::String(referenceAfter.getStart()) + "," + juce::String(referenceAfter.getEnd()) + "," +
               juce::String(advance) + "," + juce::String(int(clockExact)) + "," +
               juce::String(int(sameEdit)) + "," + juce::String(int(sameContext)) + "," + pointer(context) +
               "," + juce::String(int(transport.isPlaying())) + "," + juce::String(int(graph)) + "," +
               juce::String(after.headSeconds, 12) + "," + juce::String(int(after.headPlaying)) + "," +
               juce::String(juce::uint64(beforeRender)) + "," + juce::String(juce::uint64(afterRender)) +
               "," + juce::String(held) + "," + juce::String(f.device->starts) + "," +
               juce::String(f.device->stops) + "," + juce::String(f.device->getCurrentSampleRate(), 1) + "," +
               juce::String(f.device->getCurrentBufferSizeSamples()) + "," + juce::String(edits) + "," +
               juce::String(contexts) + "," + juce::String(plugins) + "," + juce::String(int(workers)) + "," +
               timing.csv(began, ended) + "\n";
        source_burst_acceptance::appendNativePluginCpuControl(f.audio->engine, nullptr, outer, count, cpu);
        require(folder.getChildFile("idle-callbacks.csv").replaceWithText(csv) &&
                    folder.getChildFile("idle-native-plugin-cpu.csv").replaceWithText(cpu),
                "Retain ALL ordinary idle observations before any strict aggregate assertion");
        dispatchMessages(6); // Same serialized CONTROL pacing; outside callback, not an added warmup.
    }
    auto summary = std::make_unique<juce::DynamicObject>();
    summary->setProperty(
        "diagnostic",
        pruneCold ? "DERIVATIVE cold-insert-pruned ordinary fixture; no workers/SourceBurstSession"
                  : "ordinary no-note app context only; no worker/SourceBurstSession installed by this test");
    summary->setProperty(
        "qualification",
        pruneCold ? "Native processing flags changed before first context; not ordinary189-node, production "
                    "editing/meters, plugin, or worker qualification"
                  : "Different diagnostic workload; not original worker qualification or an A/B speedup");
    summary->setProperty("coldPruning", pruneCold);
    if (pruneCold)
    {
        cold_mixer_pruning::retain(*f.coldPruning, *f.audio);
        summary->setProperty("prunedColdInserts", 16);
        summary->setProperty("sourceChannels", 1);
        summary->setProperty("remainingNodes", juce::int64(f.coldPruning->remainingNodes));
    }
    if (phaseDiagnostic)
    {
        summary->setProperty("instrumented", true);
        summary->setProperty("phaseFailures", int(phaseFailures));
        summary->setProperty("tickFrequency", juce::Time::getHighResolutionTicksPerSecond());
        summary->setProperty(
            "probeQualification",
            "Separate intrusive fixed-phase localization, not original qualification or speedup");
    }
    summary->setProperty("callbacks", 28);
    summary->setProperty("frames", frames);
    summary->setProperty("preludeCallbacks", 4);
    summary->setProperty("preludeFramesEach", 256);
    summary->setProperty("sampleRate", 48000);
    summary->setProperty("configuredBufferFrames", 256);
    summary->setProperty("deadlineFailures", int(deadlineFailures));
    summary->setProperty("safetyFailures", int(safetyFailures));
    summary->setProperty("clockFailures", int(clockFailures));
    summary->setProperty("identityFailures", int(identityFailures));
    summary->setProperty("nonSilentCallbacks", int(silenceFailures));
    summary->setProperty("newCalls", int(totalNew));
    summary->setProperty("deleteCalls", int(totalDelete));
    const bool passed = deadlineFailures == 0 && safetyFailures == 0 && clockFailures == 0 &&
                        identityFailures == 0 && silenceFailures == 0 && frames == 5120 && phaseFailures == 0;
    summary->setProperty("state", passed ? "completed" : "failed");
    require(folder.getChildFile("idle-summary.json")
                .replaceWithText(juce::JSON::toString(juce::var(summary.release()))),
            "Retain actual aggregate failures before strict diagnostic assertion");
    require(safetyFailures == 0 && clockFailures == 0 && identityFailures == 0 && silenceFailures == 0 &&
                frames == 5120,
            "Every ordinary idle callback preserves exact silence/safety/clock/context/device");
    require(phaseFailures == 0, "Every diagnostic phase is fully bounded/attributed without lost records");
    require(deadlineFailures == 0,
            "Every ordinary idle callback meets unchanged frames/rate physical deadline; see ALL28 rows");
}
} // namespace vibera::test::native_idle
