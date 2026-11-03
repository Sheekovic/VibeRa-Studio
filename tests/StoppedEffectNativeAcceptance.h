// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/StoppedEffectWorkers.h"
#include "../src/effects/NativeEffectsRegistry.h"
#include "../src/ChannelRoutingCoordinator.h"
#include "RoutingLifetimeAcceptance.h"
#include <array>
#include <cmath>
// Include after ParameterAcceptance, SyntheticDevice, awaitProjection and heap probes.
namespace vibera::test::stopped_effects
{
using Status = AudioEngine::ChannelRoutingStatus;
using Fixture = routing_lifetime::Fixture;
struct ProcessObject
{
    HANDLE handle = nullptr;
    explicit ProcessObject(PluginHost &host)
        : handle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, host.processId()))
    {
        // The retained child-process owner must still be alive AFTER opening:
        // otherwise a recycled numeric PID would not prove instance ownership.
        const bool sameOwnedInstance = handle && GetProcessId(handle) == host.processId() && host.alive();
        if (!sameOwnedInstance && handle)
        {
            CloseHandle(handle);
            handle = nullptr;
        }
        require(sameOwnedInstance,
                "Retain verified live owned worker kernel identity, never numeric PID alone");
    }
    ~ProcessObject()
    {
        if (handle)
            CloseHandle(handle);
    }
    bool exited() const
    {
        return WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
    }
};
inline juce::String opaque(PluginHost &host)
{
    const auto serial = host.requestState();
    require(serial != 0, "Actual fresh native state admission");
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    juce::String result;
    while (!host.readState(serial, result) && juce::Time::getMillisecondCounter() < deadline)
    {
        host.service();
        dispatchMessages(2);
    }
    require(result.isNotEmpty() && host.recoveryState() == result,
            "Matching actual native opaque result, never older PluginSpec bytes");
    return result;
}
inline Project captured(Fixture &f)
{
    require(f.audio->requestPluginState(f.controller.project).wasOk(), "Actual application capture admitted");
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    Project result;
    auto status = juce::Result::ok();
    bool complete = false;
    while (!(complete = f.audio->pollPluginState(result, status)) &&
           juce::Time::getMillisecondCounter() < deadline)
    {
        f.audio->reclaimPluginHosts();
        dispatchMessages(2);
    }
    require(complete && status.wasOk(), "Fresh complete native snapshot, not accepted-only capture");
    return result;
}
inline juce::String canonical(const Fixture &f)
{
    const auto &c = f.controller;
    return c.project.state().toXmlString() + "\n" + c.projectIdentity + "/" +
           juce::String(juce::int64(c.revision)) + "/" + juce::String(int(c.dirty)) + "/" +
           juce::String(c.undo.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
           c.undo.getUndoDescriptions().joinIntoString("|") + "/" +
           c.undo.getRedoDescriptions().joinIntoString("|");
}
struct Trace
{
    Fixture &f;
    juce::File folder;
    juce::AudioBuffer<float> block{2, 256};
    std::array<std::vector<float>, 2> pcm;
    juce::String csv = "phase,callback,held,head,context,sampler,renderCalls,newCalls,deleteCalls,ms,peak,"
                       "submitted,processed,missed,dropped,consumedSequence,consumedNative\n";
    int callbacks = 0;
    double stoppedPosition = 0;
    int starts = 0, stops = 0;
    explicit Trace(Fixture &fixture, const juce::File &output)
        : f(fixture), folder(output), stoppedPosition(f.audio->positionBeat()), starts(f.device->starts),
          stops(f.device->stops)
    {
        pcm[0].reserve(32768);
        pcm[1].reserve(32768);
    }
    void retain()
    {
        require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                "Retain each actual callback before assertions");
        for (int c = 0; c < 2; ++c)
        {
            auto stream = folder.getChildFile("channel-" + juce::String(c) + ".f32").createOutputStream();
            require(bool(stream), "Owned all-callback raw PCM output");
            require(stream->setPosition(0) && stream->truncate().wasOk(),
                    "Fresh exact PCM stream position/truncation succeeded");
            require(stream->write(pcm[size_t(c)].data(), pcm[size_t(c)].size() * sizeof(float)),
                    "Retain every captured sample including startup/transition silence");
        }
    }
    void drive(const char *phase, const std::shared_ptr<PluginHost> &host = {}, bool mustProcessHost = true)
    {
        const auto before = f.audio->projectionDiagnostics();
        const auto stats = host ? host->statistics() : PluginHost::Statistics{};
        const auto allocation = callbackAllocations.load(), destruction = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            f.device->process(block);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto newCalls = callbackAllocations.load() - allocation;
        const auto deleteCalls = callbackDestructions.load() - destruction;
        const auto after = f.audio->projectionDiagnostics();
        const auto packets = host ? host->statistics() : PluginHost::Statistics{};
        const auto consumed = host ? host->consumedLatency() : std::optional<PluginConsumedLatency>{};
        bool finite = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
            {
                const auto sample = block.getSample(c, n);
                finite &= std::isfinite(sample);
                pcm[size_t(c)].push_back(sample); // CONTROL, after the callback/probe.
            }
        ++callbacks;
        const auto &channel = after.channels.front();
        csv += juce::String(phase) + "," + juce::String(callbacks) + "," +
               juce::String(channel.heldInputKeys) + "," + juce::String(after.headSeconds, 12) + "," +
               juce::String(juce::int64(after.playbackContext)) + "," +
               juce::String(juce::int64(channel.sampler)) + "," +
               juce::String(juce::int64(channel.renderCalls)) + "," + juce::String(juce::int64(newCalls)) +
               "," + juce::String(juce::int64(deleteCalls)) + "," + juce::String(ms, 6) + "," +
               juce::String(block.getMagnitude(0, 0, 256), 12) + "," +
               juce::String(juce::int64(packets.submitted)) + "," +
               juce::String(juce::int64(packets.processed)) + "," +
               juce::String(juce::int64(packets.missed)) + "," + juce::String(juce::int64(packets.dropped)) +
               "," + (consumed ? juce::String(juce::int64(consumed->sequence)) : "unavailable") + "," +
               (consumed ? juce::String(consumed->stamp.nativeSamples) : "unavailable") + "\n";
        retain();
        require(finite && newCalls == 0 && deleteCalls == 0 && ms <= 256000. / 48000.,
                "Actual full native callback finite/zero new-delete/unchanged256-frame deadline");
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.channels.front().sample == channel.sample && !f.audio->playing() &&
                    f.audio->positionBeat() == stoppedPosition && f.device->starts == starts &&
                    f.device->stops == stops && channel.heldInputKeys == 1,
                "Stopped callback keeps actual context and one held MIDI owner through candidate selection");
        if (host)
            require(
                host->alive() &&
                    (mustProcessHost ? packets.submitted == stats.submitted + 1
                                     : packets.submitted >= stats.submitted &&
                                           packets.submitted <= stats.submitted + 1) &&
                    packets.missed == 0 && packets.dropped == 0,
                "Actual adopted effect submits exactly once per quantum, without missing/dropped packets");
        dispatchMessages(6); // Serialized test pacing/control service, never a callback wait.
        f.audio->reclaimPluginHosts();
    }
};
// Independent ORIGINAL SDK component/Host PCM proof; distinct from graph transition PCM.
__declspec(noinline) inline PluginSpec seedAndDirectOracle(const juce::File &folder, const juce::File &binary)
{
    auto spec = parameterSpec(binary);
    require(!spec.instrument && spec.name == "VibeRa Mixer Latency Fixture",
            "Original genuine SDK effect only");
    PluginHost host;
    require(host.start(spec, 48000, 256, true).wasOk() && host.latencySamples() == 287 &&
                host.declaredLatency().nativeSamples == 31,
            "Real independent native31 plus fixed worker256, no inferred/calibrated phase");
    spec.state = opaque(host);
    juce::AudioBuffer<float> buffer(2, 256);
    double maximumError = 0;
    for (int callback = 0; callback < 4; ++callback)
    {
        for (int c = 0; c < 2; ++c)
            std::fill_n(buffer.getWritePointer(c), 256, c == 0 ? .125f : -.0625f);
        host.process(buffer, 0, 256, nullptr, 0, callback * 256. / 48000., 120, true, false);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
            {
                const auto expected = callback * 256 + n < 287 ? 0.f : c == 0 ? .125f : -.0625f;
                const auto actual = buffer.getSample(c, n);
                maximumError = std::isfinite(actual)
                                   ? std::max(maximumError, std::abs(double(actual) - expected))
                                   : std::numeric_limits<double>::infinity();
            }
    }
    require(folder.getChildFile("direct-host-oracle.txt")
                .replaceWithText(
                    "Original SDK, offline waiting; literal287-frame unity main delay; maximumError=" +
                    juce::String(maximumError, 12) +
                    "\nNot a graph-transition/live-timing/listening qualification.\n"),
            "Independent Host oracle retained before asserting");
    require(maximumError == 0 && host.statistics().missed == 0 && host.statistics().dropped == 0,
            "Literal independent native-worker PCM oracle, no tolerance/phase fit");
    return spec;
}
inline void baseline(Fixture &f, PluginSpec spec, bool patternMode = true)
{
    f.audio->stop();
    auto p = f.controller.project;
    spec.id = p.allocate();
    p.mixer[2].effects = {
        spec}; // Genuine original cached effect on unused insert; explicit ownership witness.
    f.controller.replace(p);
    require(awaitProjection(*f.audio, p, patternMode, f.pattern).wasOk(),
            "Actual baseline SDK effect projection");
    ChannelRoutingCoordinator canonicalOwner(f.controller, *f.audio);
    f.audio->setMidiTarget(f.channel);
    f.audio->stop();
    dispatchMessages(10);
    for (int n = 0; n < 3; ++n)
        f.silentCallback("Initial no-note SDK graph remains silent");
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
}
struct Change
{
    std::unique_ptr<PreparedControllerChange> model;
    std::unique_ptr<AudioEngine::PreparedChannelRouting> native;
    StoppedEffectWorkers::Ptr workers;
};
// ROOT INTEGRATION: AudioEngine supplies its exact current production plan on CONTROL.
// Preparing an equivalent plan here would forge a different membership authority.
inline PreparedPluginTransport::Ptr workerTransport(Fixture &f)
{
    PreparedPluginTransport::Ptr result;
    require(f.audio->stoppedEffectWorkerTransport(result).wasOk() && result &&
                result->sampleRate() == 48000. && result->budget().quantum == 256 &&
                result->budget().packets == 3 && result->budget().transportSamples == 768,
            "Actual ordinary production source plan is independently K3/768 at48k256");
    return result;
}
inline void nativeMasterFallbackAdmission(Fixture &f)
{
    const auto before = canonical(f);
    const auto source = captured(f);
    auto destination = source;
    const auto description = effects::nativeDescription(effects::EffectId::balance);
    PluginSpec effect{
        destination.allocate(), description.name, description.createXml()->toString(), {}, false, false};
    destination.mixer[0].effects.push_back(effect);
    StoppedEffectWorkers::Ptr workers;
    require(StoppedEffectWorkers::prepare({effect}, workerTransport(f), {}, workers).wasOk() && workers,
            "Exact native descriptor prepares a real isolated worker on the stopped production plan");
    std::unique_ptr<AudioEngine::PreparedChannelRouting> ticket;
    const auto prepared = f.audio->prepareStoppedEffectChange(source, destination, 0, workers, ticket);
    require(prepared.status == Status::unsupportedDelta && !ticket && canonical(f) == before,
            ("Master explicitly selects canonical full rebuild without consuming workers or changing "
             "model/history: " +
             prepared.message)
                .toStdString());
    auto ordinary = source;
    require(ordinary.allocate() == effect.id, "Independent candidate keeps the exact private effect ID");
    ordinary.mixer[1].effects.push_back(effect);
    const auto ordinaryPrepared = f.audio->prepareStoppedEffectChange(source, ordinary, 1, workers, ticket);
    require(ordinaryPrepared.status == Status::unsupportedDelta && !ticket && canonical(f) == before,
            ("Native regular-insert graphs explicitly select the supported full rebuild before worker "
             "consumption: " +
             ordinaryPrepared.message)
                .toStdString());
    const auto original = workers;
    auto spoofed = description;
    spoofed.manufacturerName = "Unrelated provider";
    auto invalid = effect;
    invalid.descriptor = spoofed.createXml()->toString();
    require(
        StoppedEffectWorkers::prepare({invalid}, workerTransport(f), {}, workers).failed() &&
            workers == original,
        "Reserved native format still requires exact registry identity; failed preparation retains output");
    require(StoppedEffectWorkers::prepare(
                {effect}, workerTransport(f), [] { return true; }, workers)
                    .failed() &&
                workers == original,
            "Cancelled native preparation retains existing private workers and canonical project");
    require(canonical(f) == before, "Native Master preparation/refusal never publishes private state");
}
inline Change append(Fixture &f, PluginSpec seed)
{
    Change result;
    const auto before = canonical(f);
    const auto source = captured(f);
    auto next = source;
    seed.id = next.allocate();
    next.mixer[1].effects = {seed};
    require(StoppedEffectWorkers::prepare({seed}, workerTransport(f), {}, result.workers).wasOk(),
            "Private independent actual native workers restored off DSP");
    require(f.controller
                .prepareSnapshotChange(next, source, "Original native chain append", true, false, true,
                                       result.model)
                .wasOk(),
            "Actual one-transaction canonical history prepared, not applied");
    const auto prepared = f.audio->prepareStoppedEffectChange(source, next, 1, result.workers, result.native);
    require(prepared.status == Status::ready && result.native,
            ("Actual private stopped graph preparation: " + prepared.message).toStdString());
    require(canonical(f) == before && !f.audio->parameterHost(seed.id),
            "Private graph/worker preparation does not publish model/history or expose new worker");
    return result;
}
inline std::uint64_t post(Fixture &f, Trace &trace, Change &change)
{
    require(f.audio->prepareStoppedEffectMembership(f.controller, *change.model, *change.native).wasOk() &&
                f.audio->requestStoppedEffectMembership(f.controller, *change.model, *change.native).wasOk(),
            "Actual canonical worker union prepared/requested, not yet expanded or completed");
    bool expanded = false;
    for (int n = 0; n < 16 && !expanded; ++n)
    {
        trace.drive("membership_expansion_callback"); // Record EVERY pending sample and original deadline.
        const auto state = f.audio->stoppedEffectMembershipReadback(*change.native);
        require(state.phase == AudioEngine::StoppedEffectPhase::pending ||
                    state.phase == AudioEngine::StoppedEffectPhase::expanded,
                "Expansion preserves explicit pending versus actual paired close acknowledgement");
        expanded = state.phase == AudioEngine::StoppedEffectPhase::expanded;
    }
    require(expanded && f.controller.validatePreparedChange(*change.model).wasOk() &&
                f.audio->validateStoppedEffectChange(f.controller, *change.model, *change.native).status ==
                    Status::ready,
            "Exact CONTROL model/native final validation after real expanded/native-close ACK");
    const auto serial = change.native->publicationSerial();
    require(serial != 0 && f.audio->postChannelRouting(*change.native) == Status::posted &&
                f.controller.commitPreparedChange(*change.model),
            "Joint validated no-reentry native post and canonical history commit");
    require(change.native->wasPosted() && f.audio->channelRoutingReadback().status == Status::posted,
            "Actual graph not yet selected; accepted publication is pending");
    f.controller.deliverPendingNotifications();
    return serial;
}
inline void selected(Fixture &f, Trace &trace, std::uint64_t serial,
                     AudioEngine::PreparedChannelRouting &transition,
                     const std::shared_ptr<PluginHost> &host = {})
{
    // Publication is asynchronous; record EVERY callback while pending. Nothing
    // is excluded from PCM capture or fitted to an expected adoption offset.
    for (int n = 0; n < 16; ++n)
    {
        trace.drive("actual_selection_callback", host, false);
        const auto observed = f.audio->observeChannelRoutingSelection();
        if (observed.status == Status::selected)
        {
            require(observed.selectedSerial == serial && observed.overlayActive &&
                        observed.referenceEnd > observed.referenceStart,
                    "Exact context-scoped native ticket actually selected, with real reference span");
            const auto membership = f.audio->stoppedEffectMembershipReadback(transition);
            require(membership.phase != AudioEngine::StoppedEffectPhase::failed && membership.graphRetired,
                    "Actual predecessor DSP readers excluded before final bank request; no false completion");
            if (membership.phase == AudioEngine::StoppedEffectPhase::completed)
                return;
            require(membership.phase == AudioEngine::StoppedEffectPhase::retiring,
                    "Selected graph waits for real final bank close acknowledgement");
            continue;
        }
        require(observed.status == Status::posted || observed.status == Status::readbackUnavailable,
                "Native selection cannot be silently superseded or mistaken for completion");
    }
    require(false, "Actual selection remains pending after bounded recorded callbacks");
}
inline Change history(Fixture &f, HistoryDirection direction, bool appending)
{
    Change result;
    const auto source = captured(f);
    require(f.controller.prepareHistoryChange(direction, result.model).wasOk() &&
                result.model->destinationBeforeCommit(),
            "Actual same-manager prepared Undo/Redo destination");
    const auto &next = *result.model->destinationBeforeCommit();
    if (appending)
        require(StoppedEffectWorkers::prepare(next.mixer[1].effects, workerTransport(f), {}, result.workers)
                    .wasOk(),
                "Redo creates genuinely fresh restored worker, not retired stream resurrection");
    const auto admission =
        f.audio->prepareStoppedEffectChange(source, next, 1, result.workers, result.native);
    require(admission.status == Status::ready,
            ("Actual native inverse/history preparation: " + admission.message).toStdString());
    return result;
}
__declspec(noinline) inline void failureCases(const juce::File &folder, const PluginSpec &seed)
{
    Fixture f(folder, false, true, {}, false, 256);
    baseline(f, seed);
    Trace trace(f, folder);
    for (int n = 0; n < 2; ++n)
        trace.drive("failure_case_held_baseline");
    const auto before = canonical(f);
    const auto source = captured(f);
    auto next = source;
    auto copy = seed;
    copy.id = next.allocate();
    next.mixer[1].effects = {copy};
    StoppedEffectWorkers::Ptr workers;
    require(StoppedEffectWorkers::prepare({copy}, workerTransport(f), {}, workers).wasOk(),
            "Actual private failure-case worker");
    const auto savedWorkers = workers;
    require(StoppedEffectWorkers::prepare({copy}, PreparedPluginTransport::Ptr{}, {}, workers).failed() &&
                workers == savedWorkers && canonical(f) == before,
            "Null production transport refuses without a legacy fallback or output/history mutation");
    require(StoppedEffectWorkers::prepare(
                {copy}, workerTransport(f), [] { return true; }, workers)
                    .failed() &&
                workers == savedWorkers && canonical(f) == before,
            "Cancelled startup preserves prior caller-owned receipt and exact incumbent state");
    std::unique_ptr<AudioEngine::PreparedChannelRouting> native;
    auto unrelated = next;
    unrelated.tempo += 1;
    require(f.audio->prepareStoppedEffectChange(source, unrelated, 1, workers, native).status ==
                    Status::unsupportedDelta &&
                !native && canonical(f) == before,
            "Unrelated musical edit refuses before graph/history publication");
    require(f.audio->prepareStoppedEffectChange(source, next, 1, workers, native).status == Status::ready,
            "Pure invalid-delta refusal did not consume the independent worker set");
    const auto *prior = native.get();
    require(f.audio->prepareStoppedEffectChange(source, unrelated, 1, workers, native).status ==
                    Status::unsupportedDelta &&
                native.get() == prior && canonical(f) == before,
            "Refused replacement preserves already-prepared private ticket");
    const auto oldHost = f.audio->parameterHost(f.controller.project.mixer[2].effects.front().id);
    auto metadata = parameterCatalogue(*oldHost);
    require(parameterResult(*oldHost, oldHost->setParameter(71, .5f, metadata.generation)).result == 0,
            "Actual source native parameter change after captured admission");
    require(f.audio->validateChannelRouting(*native).status == Status::workerChanged &&
                f.audio->postChannelRouting(*native) != Status::posted && !native->wasPosted() &&
                canonical(f) == before,
            "Stale acknowledged source capture refuses without consuming model/history");
    native.reset();
    workers.reset();
    trace.drive("stale_capture_refusal_keeps_held_source");
    const auto fresh = captured(f);
    auto changedSource = fresh;
    changedSource.name = "Different source without canonical publication";
    auto changedDestination = changedSource;
    copy.id = changedDestination.allocate();
    changedDestination.mixer[1].effects = {copy};
    require(StoppedEffectWorkers::prepare({copy}, workerTransport(f), {}, workers).wasOk() &&
                f.audio->prepareStoppedEffectChange(changedSource, changedDestination, 1, workers, native)
                        .status == Status::staleProjection &&
                !native && canonical(f) == before,
            "Foreign source snapshot refuses; private native startup is never a hidden model commit");

    auto finalChange = append(f, seed);
    const auto sourceBeforeModelDrift = canonical(f);
    f.controller.project.name = "Actual direct canonical edit during preparation";
    const auto afterModelDrift = canonical(f);
    require(f.controller.validatePreparedChange(*finalChange.model).failed() &&
                !f.controller.commitPreparedChange(*finalChange.model) && canonical(f) == afterModelDrift &&
                sourceBeforeModelDrift != afterModelDrift && !finalChange.native->wasPosted(),
            "Joint Controller admission preserves a newer source edit rather than publishing prepared "
            "destination");
    finalChange.native.reset();
    finalChange.model.reset();
    finalChange.workers.reset();
    trace.drive("newer_model_refusal_keeps_old_native_graph");
}
__declspec(noinline) inline void deviceRefusal(const juce::File &folder, const PluginSpec &seed)
{
    Fixture f(folder, false, true, {}, false, 256);
    baseline(f, seed);
    Trace trace(f, folder);
    trace.drive("device_refusal_baseline");
    auto change = append(f, seed);
    const auto before = canonical(f);
    auto &manager = f.audio->engine.getDeviceManager();
    auto settings = manager.deviceManager.getAudioDeviceSetup();
    settings.sampleRate = 44100.; // Genuine supported rate change, same256 frame buffer.
    require(manager.deviceManager.setAudioDeviceSetup(settings, true).isEmpty(),
            "Actual configured native device lifecycle");
    dispatchMessages(40);
    require(f.audio->validateChannelRouting(*change.native).status == Status::ownerGone &&
                f.audio->postChannelRouting(*change.native) == Status::ownerGone &&
                !change.native->wasPosted() && canonical(f) == before,
            "Real preparation fence invalidates private effect ticket before device reprepare, with no "
            "history commit");
    // No stale device callback or pretend recovery. Fixture destruction is CONTROL.
}
} // namespace vibera::test::stopped_effects

__declspec(noinline) static void stoppedEffectChangeNativeTests(const juce::File &folder,
                                                                const juce::File &binary)
{
    using namespace vibera::test::stopped_effects;
    require(folder.createDirectory().wasOk() && !folder.getChildFile("native-stopped.txt").exists(),
            "Fresh owned stopped-native attempt, preserving prior evidence");
    vibera::test::routing_lifetime::Storage storage(folder.getChildFile("storage"));
    juce::String report = "Original actual SDK/native graph acceptance invocation.\n"
                          "Graph transition PCM is retained in full but NOT exact-oracle qualified; direct "
                          "Host PCM is separate.\n";
    try
    {
        const auto seed = seedAndDirectOracle(folder, binary);
        for (const bool patternMode : {true, false})
        {
            const auto output =
                folder.getChildFile(patternMode ? "append-history-pattern" : "append-history-song");
            Fixture f(output, false, patternMode, {}, false, 256);
            baseline(f, seed, patternMode);
            if (patternMode)
                nativeMasterFallbackAdmission(f);
            Trace trace(f, output);
            for (int n = 0; n < 4; ++n)
                trace.drive("held_baseline");
            require(trace.block.getMagnitude(0, 0, 256) > 1.e-6f,
                    "Actual stopped live MIDI sample bed is audible in PCM");
            const auto base = canonical(f);
            const auto native = f.audio->projectionDiagnostics();
            const auto starts = f.device->starts, stops = f.device->stops;
            auto baseHost = f.audio->parameterHost(f.controller.project.mixer[2].effects.front().id);
            require(baseHost && baseHost->alive(), "Actual public baseline effect worker");
            auto cancelled = append(f, seed);
            for (int n = 0; n < 2; ++n)
                trace.drive("private_prepared_old_graph_runs");
            auto consumed = cancelled.workers;
            cancelled.native.reset();
            cancelled.model.reset();
            cancelled.workers.reset();
            require(canonical(f) == base,
                    "Discard is actual cancellation before publication, no hidden history");
            std::unique_ptr<AudioEngine::PreparedChannelRouting> refused;
            auto destination = captured(f);
            auto copy = seed;
            copy.id = destination.allocate();
            destination.mixer[1].effects = {copy};
            require(
                f.audio->prepareStoppedEffectChange(captured(f), destination, 1, consumed, refused).status ==
                        Status::workerChanged &&
                    !refused && canonical(f) == base,
                "Previously admitted worker set cannot be reused after graph discard");
            consumed.reset();
            const auto cancelRetirement = juce::Time::getMillisecondCounter() + 10000;
            while (f.audio->retainedPluginHostCount() != 1 &&
                   juce::Time::getMillisecondCounter() < cancelRetirement)
            {
                f.audio->reclaimPluginHosts();
                dispatchMessages(2);
            }
            require(f.audio->retainedPluginHostCount() == 1 && canonical(f) == base,
                    "Discarded private detached worker is retired on CONTROL; only genuine baseline worker "
                    "remains");
            auto change = append(f, seed);
            const auto effect = change.model->destinationBeforeCommit()->mixer[1].effects.front().id;
            const auto serial = post(f, trace, change);
            auto added = f.audio->parameterHost(effect);
            require(added && added != baseHost && added->alive() &&
                        added->transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                        added->transportPacketCount() == 3 && added->transportLatencySamples() == 768 &&
                        added->declaredLatency().nativeSamples == 31 && added->latencySamples() == 799 &&
                        added->statistics().submitted == 0,
                    "Candidate prepared3Q/native31 did not process a hidden graph; first exposed at commit");
            ProcessObject addedProcess(*added);
            const auto addedInstance = added->runtimeIdentity();
        std::weak_ptr<PluginHost> detachedLifetime = added;
        selected(f, trace, serial, *change.native, added);
        for (int n = 0; n < 4; ++n)
            trace.drive("real_native_effect_processed", added);
        require(added->statistics().processed > 0 && trace.block.getMagnitude(0, 0, 256) > 1.e-6f &&
                    added->consumedLatency() && added->consumedLatency()->stamp.nativeSamples == 31,
                "Actual graph consumes real native31 packets and produces finite nonzero held audio");
        const auto pasted = f.controller.project.state();
        change.native.reset();
        change.model.reset();
        change.workers.reset();
        auto undo = history(f, HistoryDirection::undo, false);
        const auto undoSerial = post(f, trace, undo);
        selected(f, trace, undoSerial, *undo.native);
        require(f.controller.project.mixer[1].effects.empty() && !f.audio->parameterHost(effect) &&
                    f.controller.undo.canRedo(),
                "One real Undo removes exact chain and retains Redo");
        undo.native.reset();
        undo.model.reset();
        added.reset();
        const auto retirement = juce::Time::getMillisecondCounter() + 10000;
        while ((!detachedLifetime.expired() || !addedProcess.exited()) &&
               juce::Time::getMillisecondCounter() < retirement)
        {
            f.audio->reclaimPluginHosts();
            dispatchMessages(2);
        }
            require(output.getChildFile("removed-worker-lifetime.txt").replaceWithText(
                        "hostStrongOwners=" + juce::String(juce::int64(detachedLifetime.use_count())) +
                        "\nworkerExited=" + juce::String(int(addedProcess.exited())) +
                        "\nretainedEngineHosts=" + juce::String(f.audio->retainedPluginHostCount()) + "\n"),
                    "Retain actual removed Host ownership and kernel exit before lifetime assertion");
            require(detachedLifetime.expired() && addedProcess.exited(),
                    "New detached chain/worker retires on CONTROL while owning Edit remains alive");
            auto redo = history(f, HistoryDirection::redo, true);
            const auto redoSerial = post(f, trace, redo);
            auto restored = f.audio->parameterHost(effect);
            require(restored && restored->runtimeIdentity() != addedInstance,
                    "Redo owns a distinct restored stream");
            selected(f, trace, redoSerial, *redo.native, restored);
            for (int n = 0; n < 4; ++n)
                trace.drive("redo_native_packets", restored);
            require(pasted.isEquivalentTo(f.controller.project.state()) &&
                        f.audio->projectionDiagnostics().edit == native.edit &&
                        f.audio->projectionDiagnostics().playbackContext == native.playbackContext &&
                        f.device->starts == starts && f.device->stops == stops && !f.audio->playing(),
                    "Redo exact state/order and original device/context, no transport/device restart");
            redo.native.reset();
            redo.model.reset();
            redo.workers.reset();
            const auto baselineId = f.controller.project.mixer[2].effects.front().id;
            std::weak_ptr<PluginHost> cachedLifetime = baseHost;
            ProcessObject cachedProcess(*baseHost);
            const auto source = captured(f);
            auto removed = source;
            removed.mixer[2].effects.clear();
            Change removeCached;
            require(f.controller
                        .prepareSnapshotChange(removed, source, "Original cached chain removal", true, false,
                                               true, removeCached.model)
                        .wasOk(),
                    "Canonical cached-baseline chain removal prepared");
            const auto removal =
                f.audio->prepareStoppedEffectChange(source, removed, 2, {}, removeCached.native);
            require(removal.status == Status::ready,
                    ("Actual cached-baseline removal: " + removal.message).toStdString());
            const auto removalSerial = post(f, trace, removeCached);
            selected(f, trace, removalSerial, *removeCached.native, restored);
            removeCached.native.reset();
            removeCached.model.reset();
            baseHost.reset();
            // Cached native graphs may survive removal; their obsolete wet
            // shell must now release the exact retired Host after actual grace.
            // This strengthens the old known-limitation assertion, which only
            // required that worker to exit on entire Edit destruction.
            const auto cachedRetirement = juce::Time::getMillisecondCounter() + 10000;
            while ((!cachedLifetime.expired() || !cachedProcess.exited()) &&
                   juce::Time::getMillisecondCounter() < cachedRetirement)
            {
                f.audio->reclaimPluginHosts();
                dispatchMessages(2);
            }
            require(output.getChildFile("cached-worker-lifetime.txt").replaceWithText(
                        "hostStrongOwners=" + juce::String(juce::int64(cachedLifetime.use_count())) +
                        "\nworkerExited=" + juce::String(int(cachedProcess.exited())) + "\n"),
                    "Retain cached baseline actual ownership and kernel exit before assertion");
            require(!f.audio->parameterHost(baselineId) && cachedLifetime.expired() &&
                        cachedProcess.exited(),
                    "Cached baseline Host and worker retire while the actual Edit remains alive");
            trace.drive("cached_worker_exited_remaining_fresh_effect_continues", restored);
            restored.reset();
            f.audio.reset(); // Explicit CONTROL teardown; all native tickets have been released first.
            require(cachedLifetime.expired() && cachedProcess.exited(),
                    "Original cached baseline worker releases when its actual owning Edit retires");
            report +=
                juce::String(patternMode ? "pattern" : "song") +
                " append_discard_selected_undo_redo PASS; graph PCM transition oracle remains UNQUALIFIED\n";
            require(folder.getChildFile("native-stopped.txt").replaceWithText(report),
                    "Retain completed actual native scenario");
        }
        failureCases(folder.getChildFile("refused-cancelled-stale"), seed);
        report += "invalid_cancelled_stale_source_capture PASS\n";
        require(folder.getChildFile("native-stopped.txt").replaceWithText(report),
                "Retain completed refusals");
        deviceRefusal(folder.getChildFile("genuine-device-refusal"), seed);
        report += "genuine_device_ownerGone PASS\n";
        require(folder.getChildFile("native-stopped.txt").replaceWithText(report),
                "Retain completed genuine device refusal");
    }
    catch (const std::exception &error)
    {
        folder.getChildFile("native-stopped.txt")
            .replaceWithText(report + "FAIL " + juce::String(error.what()) + "\n");
        throw;
    }
}
