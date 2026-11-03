// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StoppedEffectNativeAcceptance.h"
#include <exception>

// Separate additive ordinary-graph oracle. No inverse operation, legacy Host
// override, fixture phase fitting or finite LOCAL history is used here.
namespace vibera::test::ordinary_forward
{
constexpr int quantum = 256, rate = 48000, delay = 799, capacity = 128 * quantum;
inline float sourceFrame(int channel, int frame) noexcept
{
    if (frame < 0)
        return 0;
    // Different signed, nonconstant, exactly representable original channels.
    return channel == 0 ? float((frame * 17 + 13) % 251 - 125) / 1024.f
                        : float((frame * 29 + 7) % 257 - 128) / 2048.f;
}
inline juce::File writeSource(const juce::File &folder)
{
    const auto file = folder.getChildFile("original-signed-source.wav");
    require(!file.exists(), "Original source must not overwrite an existing file");
    {
        juce::AudioBuffer<float> data(2, 65536);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < data.getNumSamples(); ++n)
                data.setSample(c, n, sourceFrame(c, n));
        auto stream = file.createOutputStream();
        require(bool(stream), "New owned source stream");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples()) &&
                    writer->flush(),
                "Original binary-fraction WAV written and flushed");
    }
    double decodedRate = 0;
    const auto decoded = readWav(file, decodedRate);
    require(decodedRate == rate && decoded.getNumChannels() == 2 && decoded.getNumSamples() == 65536,
            "Decoded original source has literal expected shape");
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < decoded.getNumSamples(); ++n)
            require(decoded.getSample(c, n) == sourceFrame(c, n),
                    "Every decoded source sample equals independent signed formula");
    return file;
}
inline void writeRaw(const juce::File &file, const juce::AudioBuffer<float> &buffer, int frames)
{
    auto stream = file.createOutputStream();
    require(stream && stream->setPosition(0) && stream->truncate().wasOk(), "Owned exact PCM stream reset");
    for (int c = 0; c < 2; ++c)
        require(stream->write(buffer.getReadPointer(c), std::size_t(frames) * sizeof(float)),
                "Retain all planar float32 samples");
    stream->flush();
    require(stream->getStatus().wasOk(), "PCM evidence flush succeeded");
}
struct Trace
{
    stopped_effects::Fixture &f;
    const juce::File output;
    const AudioEngine::ProjectionDiagnostics original;
    const std::shared_ptr<PluginHost> baselineHost;
    const juce::String baselineIdentity;
    te::EditPlaybackContext *const context;
    const tracktion::graph::PlayHead *const head;
    const juce::int64 referenceOrigin;
    const double position;
    const int starts, stops;
    juce::AudioBuffer<float> block{2, quantum}, actual{2, capacity}, expected{2, capacity};
    int frames = 0, callbacks = 0, publishedAt = -1, completedAt = -1;
    AudioEngine::PreparedChannelRouting *observedTransition = nullptr;
    double maximumError = 0, maximumMs = 0;
    juce::String csv =
        "phase,callback,frame,error,ms,newCalls,deleteCalls,referenceStart,referenceEnd,"
        "expectedReferenceStart,sourceCalls,held,edit,context,sampler,sample,"
        "baselineSubmitted,baselineProcessed,baselineMissed,baselineDropped,"
        "addedSubmitted,addedProcessed,addedMissed,addedDropped,consumedSequence,consumedNative\n";
    Trace(stopped_effects::Fixture &fixture, const juce::File &folder, std::shared_ptr<PluginHost> host)
        : f(fixture), output(folder), original(f.audio->projectionDiagnostics()),
          baselineHost(std::move(host)), baselineIdentity(baselineHost->runtimeIdentity()),
          context(f.nativeEdit().getTransport().getCurrentPlaybackContext()),
          head(context ? context->getNodePlayHead() : nullptr),
          referenceOrigin(head ? head->getReferenceSampleRange().getEnd() : 0),
          position(f.audio->positionBeat()), starts(f.device->starts), stops(f.device->stops)
    {
        require(context && head && original.channels.size() == 1 && original.channels.front().sample &&
                    original.channels.front().sampler && !f.audio->playing() &&
                    baselineHost->transportPacketCount() == 3 && baselineHost->latencySamples() == delay &&
                    baselineHost->declaredLatency().nativeSamples == 31 &&
                    te::getDefaultPanLaw() == te::PanLawLinear && !f.audio->metronome(),
                "Actual stopped ordinary context, original sample, linear neutral controls and literal799");
    }
    void retain()
    {
        require(output.getChildFile("callbacks.csv").replaceWithText(csv), "Retain all callback facts");
        writeRaw(output.getChildFile("actual-planar-f32.raw"), actual, frames);
        writeRaw(output.getChildFile("expected-planar-f32.raw"), expected, frames);
        require(
            output.getChildFile("oracle.txt")
                .replaceWithText("expected=source(t-799); signed binary fractions; exact error0\nframes=" +
                                 juce::String(frames) + "\nmaximumError=" + juce::String(maximumError, 12) +
                                 "\nmaximumMs=" + juce::String(maximumMs, 6) + "\npublishedAt=" +
                                 juce::String(publishedAt) + "\ncompletedAt=" + juce::String(completedAt) +
                                 "\nOriginal SDK stopped append/history oracle; no "
                                 "listening/hardware/generic-tail qualification.\n"),
            "Retain literal oracle and transition bounds");
    }
    void drive(const char *phase, const std::shared_ptr<PluginHost> &added = {},
               bool mustProcessAdded = false)
    {
        require(frames + quantum <= capacity, "Bounded all-callback evidence capacity");
        const auto before = f.audio->projectionDiagnostics();
        const auto oldPackets = baselineHost->statistics();
        const auto oldAdded = added ? added->statistics() : PluginHost::Statistics{};
        const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            f.device->process(block);
        }
        const auto elapsed =
            SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto newCalls = callbackAllocations.load() - allocated;
        const auto deleteCalls = callbackDestructions.load() - deleted;
        const auto now = f.audio->projectionDiagnostics();
        const auto packets = baselineHost->statistics();
        const auto addedPackets = added ? added->statistics() : PluginHost::Statistics{};
        const auto consumed = added ? added->consumedLatency() : std::optional<PluginConsumedLatency>{};
        auto *current = f.nativeEdit().getTransport().getCurrentPlaybackContext();
        const bool sameHead = current == context && current && current->getNodePlayHead() == head;
        const auto range = sameHead ? head->getReferenceSampleRange() : juce::Range<juce::int64>{};
        const auto channel =
            now.channels.size() == 1 ? now.channels.front() : AudioEngine::ChannelDiagnostic{};
        double error = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < quantum; ++n)
            {
                const auto value = block.getSample(c, n), wanted = sourceFrame(c, frames + n - delay);
                actual.setSample(c, frames + n, value);
                expected.setSample(c, frames + n, wanted);
                error = std::isfinite(value) ? std::max(error, std::abs(double(value) - wanted))
                                             : std::numeric_limits<double>::infinity();
            }
        maximumError = std::max(maximumError, error);
        maximumMs = std::max(maximumMs, elapsed);
        const auto calls =
            channel.renderCalls - (before.channels.size() == 1 ? before.channels.front().renderCalls : 0);
        csv += juce::String(phase) + "," + juce::String(callbacks) + "," + juce::String(frames) + "," +
               juce::String(error, 12) + "," + juce::String(elapsed, 6) + "," + juce::String(newCalls) + "," +
               juce::String(deleteCalls) + "," + juce::String(range.getStart()) + "," +
               juce::String(range.getEnd()) + "," + juce::String(referenceOrigin + frames) + "," +
               juce::String(juce::int64(calls)) + "," + juce::String(channel.heldInputKeys) + "," +
               juce::String(juce::int64(now.edit)) + "," + juce::String(juce::int64(now.playbackContext)) +
               "," + juce::String(juce::int64(channel.sampler)) + "," +
               juce::String(juce::int64(channel.sample)) + "," +
               juce::String(juce::int64(packets.submitted)) + "," +
               juce::String(juce::int64(packets.processed)) + "," +
               juce::String(juce::int64(packets.missed)) + "," + juce::String(juce::int64(packets.dropped)) +
               "," + juce::String(juce::int64(addedPackets.submitted)) + "," +
               juce::String(juce::int64(addedPackets.processed)) + "," +
               juce::String(juce::int64(addedPackets.missed)) + "," +
               juce::String(juce::int64(addedPackets.dropped)) + "," +
               (consumed ? juce::String(juce::int64(consumed->sequence)) : "unavailable") + "," +
               (consumed ? juce::String(consumed->stamp.nativeSamples) : "unavailable") + "\n";
        frames += quantum;
        ++callbacks;
        if (observedTransition)
        {
            const auto state = f.audio->stoppedEffectMembershipReadback(*observedTransition);
            require(output.getChildFile("handoff-status.txt").replaceWithText(
                        "phase=" + juce::String(unsigned(state.phase)) + "\nerror=" +
                        juce::String(state.error ? state.error : "") + "\n"),
                    "Retain actual transition failure before the independent PCM assertion");
        }
        retain(); // Every sample and raw condition survives the first failing assertion.
        require(
            error == 0 && newCalls == 0 && deleteCalls == 0 && elapsed <= 256000. / 48000.,
            "Every signed sample equals literal source(t-799), no C++ heap and unchanged strict deadline");
        require(sameHead && range.getStart() == referenceOrigin + frames - quantum &&
                    range.getLength() == quantum && now.edit == original.edit &&
                    now.playbackContext == original.playbackContext && now.channels.size() == 1 &&
                    channel.channel == original.channels.front().channel &&
                    channel.sample == original.channels.front().sample && channel.heldInputKeys == 1 &&
                    calls == 1 && !f.audio->playing() && f.audio->positionBeat() == position &&
                    f.device->starts == starts && f.device->stops == stops,
                "No source replay/reset, duplicated processing, reference discontinuity or device/context "
                "replacement");
        require(
            baselineHost->alive() && baselineHost->runtimeIdentity() == baselineIdentity &&
                f.audio->parameterHost(f.controller.project.mixer[2].effects.front().id) == baselineHost &&
                packets.submitted == oldPackets.submitted + 1 && packets.missed == 0 && packets.dropped == 0,
            "Original unused baseline worker identity and exact one-packet cadence retained");
        if (added)
            require(added->alive() && addedPackets.missed == 0 && addedPackets.dropped == 0 &&
                        addedPackets.submitted >= oldAdded.submitted &&
                        addedPackets.submitted <= oldAdded.submitted + 1 &&
                        (!mustProcessAdded || addedPackets.submitted == oldAdded.submitted + 1),
                    "Real new worker bounded once per callback; exact once after terminal adoption");
        dispatchMessages(6); // Original serialized CONTROL pacing, no callback waits.
        f.audio->reclaimPluginHosts();
    }
};
inline void expand(stopped_effects::Fixture &f, Trace &trace, stopped_effects::Change &change)
{
    require(f.audio->prepareStoppedEffectMembership(f.controller, *change.model, *change.native).wasOk() &&
                f.audio->requestStoppedEffectMembership(f.controller, *change.model, *change.native).wasOk(),
            "Real prepublication worker union request");
    for (int n = 0; n < 16; ++n)
    {
        trace.drive("expanded_union_pending_old_graph");
        const auto state = f.audio->stoppedEffectMembershipReadback(*change.native);
        if (state.phase == AudioEngine::StoppedEffectPhase::expanded)
        {
            require(state.membership && state.physicalSerial && !state.graphPosted,
                    "Expansion has real membership/physical ACK and graph is not published");
            return;
        }
        require(state.phase == AudioEngine::StoppedEffectPhase::pending,
                "Explicit bounded expansion pending");
    }
    require(false, "Actual expanded union ACK missing");
}
__declspec(noinline) inline void scenario(const juce::File &folder, const juce::File &sample,
                                          const PluginSpec &seed, bool patternMode)
{
    auto owned = std::make_unique<stopped_effects::Fixture>(
        folder, false, patternMode,
        [&](Project &p)
        {
            auto &c = p.channels.front();
            c.source = sample.getFullPathName();
            c.instrument.reset();
            c.audioClip = false;
            c.gain = 1;
            c.pan = 0;
            c.sampler = {};
            c.sampler.root = 60;
            c.sampler.envelope = true;
            c.sampler.attack = c.sampler.decay = c.sampler.release = 0;
            c.sampler.sustain = 1;
            for (auto &insert : p.mixer)
            {
                insert.gain = 1;
                insert.pan = 0;
                insert.muted = false;
                insert.stereoSeparation = 0;
                insert.phaseInverted = false;
                insert.stereoSwapped = false;
                insert.effects.clear();
            }
        },
        false, quantum);
    auto &f = *owned;
    stopped_effects::baseline(f, seed, patternMode); // ONE original NoteOn at next callback frame0.
    const auto base = stopped_effects::canonical(f);
    auto baselineHost = f.audio->parameterHost(f.controller.project.mixer[2].effects.front().id);
    require(baselineHost && baselineHost->alive(), "Actual original baseline SDK worker");
    auto traceOwner = std::make_unique<Trace>(f, folder, baselineHost);
    auto &trace = *traceOwner;
    for (int n = 0; n < 8; ++n)
        trace.drive("held_baseline_all_startup_samples");
    {
        auto discarded = stopped_effects::append(f, seed);
        for (int n = 0; n < 2; ++n)
            trace.drive("private_candidate_no_publication");
        discarded.native.reset();
        discarded.model.reset();
        discarded.workers.reset();
        require(stopped_effects::canonical(f) == base, "Private discard preserves exact canonical history");
    }
    {
        auto cancelled = stopped_effects::append(f, seed);
        expand(f, trace, cancelled);
        require(f.audio->cancelStoppedEffectMembership(*cancelled.native).wasOk(),
                "Real union cancellation admitted");
        bool terminal = false;
        for (int n = 0; n < 16 && !terminal; ++n)
        {
            trace.drive("cancel_waits_real_reverse_bank_ack");
            const auto state = f.audio->stoppedEffectMembershipReadback(*cancelled.native);
            terminal = state.phase == AudioEngine::StoppedEffectPhase::cancelled;
            require(terminal || state.phase == AudioEngine::StoppedEffectPhase::cancelling,
                    "Cancellation is not completed before actual reverse-bank/native close ACK");
        }
        require(terminal && !cancelled.native->wasPosted() && stopped_effects::canonical(f) == base,
                "Cancelled membership never commits model/history or graph");
        cancelled.native.reset();
        cancelled.model.reset();
        cancelled.workers.reset();
    }
    auto change = stopped_effects::append(f, seed);
    const auto id = change.model->destinationBeforeCommit()->mixer[1].effects.front().id;
    expand(f, trace, change);
    require(f.controller.validatePreparedChange(*change.model).wasOk() &&
                f.audio->validateStoppedEffectChange(f.controller, *change.model, *change.native).status ==
                    stopped_effects::Status::ready,
            "Final real CONTROL native/history admission after expanded-bank ACK");
    const auto expanded = f.audio->stoppedEffectMembershipReadback(*change.native);
    const auto historySize = f.controller.undo.getUndoDescriptions().size();
    const auto serial = change.native->publicationSerial();
    trace.publishedAt = trace.frames;
    require(serial && f.audio->postChannelRouting(*change.native) == stopped_effects::Status::posted &&
                f.controller.commitPreparedChange(*change.model),
            "Exact candidate post and single canonical commit");
    f.controller.deliverPendingNotifications();
    auto added = f.audio->parameterHost(id);
    require(added && added != baselineHost && added->alive() && added->transportPacketCount() == 3 &&
                added->transportLatencySamples() == 768 && added->declaredLatency().nativeSamples == 31 &&
                added->latencySamples() == delay && added->statistics().submitted == 0,
            "Actual fresh SDK worker, native31 plus prepared768, no hidden candidate DSP");
    bool complete = false;
    for (int n = 0; n < 16 && !complete; ++n)
    {
        trace.drive("post_selection_drain_and_final_bank_pending", added);
        const auto observed = f.audio->observeChannelRoutingSelection();
        const auto member = f.audio->stoppedEffectMembershipReadback(*change.native);
        require(member.phase != AudioEngine::StoppedEffectPhase::failed,
                "Actual forward membership must stay healthy");
        complete = member.phase == AudioEngine::StoppedEffectPhase::completed;
        if (complete)
        {
            require(observed.status == stopped_effects::Status::selected &&
                        observed.selectedSerial == serial && observed.overlayActive &&
                        observed.referenceEnd > observed.referenceStart && member.graphPosted &&
                        member.graphRetired && member.membership &&
                        member.membership != expanded.membership &&
                        member.physicalSerial > expanded.physicalSerial,
                    "Completion requires exact selected ticket, finished native drain and real final smaller "
                    "bank ACK");
            trace.completedAt = trace.frames;
        }
        else
            require((observed.status == stopped_effects::Status::posted ||
                     observed.status == stopped_effects::Status::readbackUnavailable) &&
                        (member.phase == AudioEngine::StoppedEffectPhase::expanded ||
                         member.phase == AudioEngine::StoppedEffectPhase::retiring),
                    "No accepted/posted graph is mislabeled completed");
    }
    require(complete, "Native drain/final smaller bank ACK remains pending");
    // Independently require a whole799-frame transition plus a full extra quantum;
    // no startup, drain, overlapping output or delayed adoption samples excluded.
    while (trace.frames - trace.publishedAt < delay + quantum ||
           trace.frames - trace.completedAt < 4 * quantum)
        trace.drive("completed_forward_all_samples", added, true);
    require(added->statistics().processed > 0 && added->consumedLatency() &&
                added->consumedLatency()->stamp.nativeSamples == 31 &&
                f.controller.project.mixer[1].effects.size() == 1 &&
                f.controller.project.mixer[1].effects.front().id == id && f.controller.undo.canUndo() &&
                f.controller.undo.getUndoDescriptions().size() == historySize + 1,
            "Real native packets, exact canonical added effect and one prepared history transaction");
    trace.retain();
    change.native.reset();
    change.model.reset();
    change.workers.reset(); // CONTROL only AFTER native drain/final bank completion.

    auto undo = stopped_effects::history(f, HistoryDirection::undo, false);
    expand(f, trace, undo);
    require(f.controller.validatePreparedChange(*undo.model).wasOk() &&
                f.audio->validateStoppedEffectChange(f.controller, *undo.model, *undo.native).status ==
                    stopped_effects::Status::ready,
            "Actual removal history and graph jointly admitted after real union close");
    const auto undoSerial = undo.native->publicationSerial();
    trace.observedTransition = undo.native.get();
    const auto removalStart = trace.frames;
    require(undoSerial && f.audio->postChannelRouting(*undo.native) == stopped_effects::Status::posted &&
                f.controller.commitPreparedChange(*undo.model),
            "Actual removal post and canonical Undo commit");
    f.controller.deliverPendingNotifications();
    bool removed = false;
    for (int n = 0; n < 16 && !removed; ++n)
    {
        trace.drive("actual_removal_fade_dry_transfer_and_final_bank");
        const auto selected = f.audio->observeChannelRoutingSelection();
        const auto member = f.audio->stoppedEffectMembershipReadback(*undo.native);
        require(member.phase != AudioEngine::StoppedEffectPhase::failed,
                (juce::String("Actual removal remains healthy: ") +
                 (member.error ? member.error : "" )).toStdString());
        removed = member.phase == AudioEngine::StoppedEffectPhase::completed;
        if (removed)
            require(selected.status == stopped_effects::Status::selected &&
                        selected.selectedSerial == undoSerial && member.graphPosted && member.graphRetired,
                    "Removal completed only after actual dry transfer, selection and final bank ACK");
    }
    require(removed && f.controller.project.mixer[1].effects.empty() && !f.audio->parameterHost(id) &&
                f.controller.undo.canRedo() && trace.frames - removalStart >= quantum,
            "Actual single Undo removes canonical effect, retains Redo and preserves exact799 PCM");
    const auto finalPackets = added->statistics();
    for (int n = 0; n < 4; ++n)
        trace.drive("retained_dry_after_worker_retirement");
    require(added->statistics().submitted == finalPackets.submitted && added->statistics().missed == 0 &&
                added->statistics().dropped == 0,
            "Retained dry graph performs no further removed-worker submissions or fallback");
    const auto exitDeadline = juce::Time::getMillisecondCounter() + 10000;
    while (!added->retirementComplete() && juce::Time::getMillisecondCounter() < exitDeadline)
    {
        f.audio->reclaimPluginHosts();
        dispatchMessages(2);
    }
    require(added->retirementComplete(), "Actual removed worker exits while retained dry graph stays alive");
    trace.drive("exact_dry_pcm_after_actual_worker_exit");
    trace.observedTransition = nullptr;
    undo.native.reset();
    undo.model.reset();

    const auto retiredIdentity = added->runtimeIdentity();
    added.reset();
    auto redo = stopped_effects::history(f, HistoryDirection::redo, true);
    expand(f, trace, redo);
    require(f.controller.validatePreparedChange(*redo.model).wasOk() &&
                f.audio->validateStoppedEffectChange(f.controller, *redo.model, *redo.native).status ==
                    stopped_effects::Status::ready,
            "Fresh Redo worker and canonical history admitted after real expanded bank close");
    const auto redoSerial = redo.native->publicationSerial();
    trace.observedTransition = redo.native.get();
    require(redoSerial && f.audio->postChannelRouting(*redo.native) == stopped_effects::Status::posted &&
                f.controller.commitPreparedChange(*redo.model),
            "Fresh same-delay Redo graph and canonical history commit together");
    f.controller.deliverPendingNotifications();
    auto restored = f.audio->parameterHost(id);
    require(restored && restored->runtimeIdentity() != retiredIdentity &&
                restored->statistics().submitted == 0 && restored->latencySamples() == delay,
            "Actual Redo uses a fresh unprocessed worker and unchanged real799 producing delay");
    bool rejoined = false;
    for (int n = 0; n < 16 && !rejoined; ++n)
    {
        trace.drive("fresh_Redo_startup_same_time_join_and_final_bank", restored);
        const auto state = f.audio->stoppedEffectMembershipReadback(*redo.native);
        require(state.phase != AudioEngine::StoppedEffectPhase::failed,
                (juce::String("Actual Redo join remains healthy: ") + (state.error ? state.error : ""))
                    .toStdString());
        rejoined = state.phase == AudioEngine::StoppedEffectPhase::completed;
        if (rejoined)
        {
            const auto selected = f.audio->observeChannelRoutingSelection();
            require(selected.status == stopped_effects::Status::selected &&
                        selected.selectedSerial == redoSerial && state.graphRetired,
                    "Fresh join completion waits its actual wet endpoint, selection and final close");
        }
    }
    require(rejoined && f.controller.project.mixer[1].effects.size() == 1 &&
                f.controller.project.mixer[1].effects.front().id == id && f.controller.undo.canUndo(),
            "Single real Redo restores the same stable effect and history");
    for (int n = 0; n < 4; ++n)
        trace.drive("completed_fresh_Redo_exact_signed_pcm", restored, true);
    trace.observedTransition = nullptr;
    redo.native.reset();
    redo.model.reset();
    redo.workers.reset();
}
} // namespace vibera::test::ordinary_forward

__declspec(noinline) inline void stoppedEffectOrdinaryForwardTests(const juce::File &folder,
                                                                   const juce::File &binary)
{
    using namespace vibera::test;
    require(folder.createDirectory().wasOk(), "Owned ordinary-forward attempt");
    routing_lifetime::Storage storage(folder.getChildFile("storage"));
    const auto sample = ordinary_forward::writeSource(folder);
    const auto seed = stopped_effects::seedAndDirectOracle(folder, binary); // Existing 1Q oracle unchanged.
    juce::String result;
    int failures = 0;
    for (const bool patternMode : {true, false})
    {
        const auto name = juce::String(patternMode ? "pattern" : "song");
        try
        {
            ordinary_forward::scenario(folder.getChildFile(name), sample, seed, patternMode);
            result += name + " PASS exact signed source(t-799), "
                             "append/discard/cancel/Undo/removal/workerExit/freshRedo/finalACK\n";
        }
        catch (const std::exception &error)
        {
            ++failures;
            result += name + " FAIL " + juce::String(error.what()) + "\n";
        }
        require(folder.getChildFile("cases.txt").replaceWithText(result),
                "Retain each independent mode outcome");
    }
    require(failures == 0, result.toStdString());
}
