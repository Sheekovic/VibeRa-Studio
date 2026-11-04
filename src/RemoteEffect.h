// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationStream.h"
#include "PluginDryHistory.h"
#include <atomic>
#include <vector>
#include <tracktion_engine/tracktion_engine.h>
#include <stdexcept>
#include <optional>
#include <cmath>
#include <limits>

namespace vibera
{
class RemoteEffect final : public tracktion::Plugin
{
    std::shared_ptr<PluginHost> host;
    std::shared_ptr<LiveTimeline> timeline;
    // Nonmoving worker-packet owner. Graph shells may overlap in lifetime,
    // but the retained graph player must process exactly ONE adopted shell.
    // Only DSP touches stream; all attachment/configuration metadata is CONTROL.
    struct Runtime
    {
        PluginAutomationStream stream;
        // Sole DSP fallback clock, retained across a shell switch when a
        // timeline lease is temporarily unavailable. The timeline itself is
        // shell-local; CONTROL never copies or reads these mutable values.
        double clockTempo = 120;
        int clockNumerator = 4, clockDenominator = 4;
        unsigned attachedShells = 0;
        bool prepared = false;
        float wetTarget = 1;
        bool bypassTarget = false, effectControlsSeen = false; // DSP fallback across a failed lease.
        double preparedRate = 0;
        int preparedBlock = 0, quantum = 0;
        std::uint64_t epoch = 0;
        const PluginHost *worker = nullptr;
        juce::String workerInstance;
        PluginBusRequest buses;
        Id plugin = 0;
        Runtime() = default;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;
        Runtime(Runtime &&) = delete;
        Runtime &operator=(Runtime &&) = delete;
    };

  public:
    class FreshJoinProgress;
    // One nonmoving observer per normal wet owner; removal gets a NEW observer
    // pinning that exact predecessor. CONTROL never copies live DSP payload.
    class RemovalProgress final
    {
        friend class RemoteEffect;
        friend class FreshJoinProgress;

      public:
        enum class Mode
        {
            wet,
            removal
        };
        enum class Phase : std::uint32_t
        {
            prepared,
            observing,
            wetReady,
            fading,
            dry,
            failed
        };
        enum class Fault : std::uint32_t
        {
            none,
            revoked,
            clock,
            span,
            identity,
            packet,
            native,
            history,
            overflow
        };
        struct Observation
        {
            Phase phase;
            Fault fault;
        };
        struct Binding
        {
            const void *processingRuntime = nullptr, *dryHistory = nullptr, *hostWriter = nullptr;
            PluginPcmIdentity pcm;
            PluginLatencyStamp native;
            int latency = 0, maximumSpan = 0;
        };
        const Binding &bindingControl() const noexcept
        {
            return binding;
        }
        // CONTROL only: immutable predecessor identity, never live DSP payload.
        std::shared_ptr<RemovalProgress> predecessorControl() const { return previous; }
        bool isRemoval() const noexcept
        {
            return mode == Mode::removal;
        }
        Observation observation() const noexcept
        {
            const auto value = published.load(std::memory_order_acquire);
            return {Phase(value & 0xffffffffu), Fault(value >> 32)};
        }
        // Actual selected root prefetch, BEFORE any child. The native root
        // separately proves exact old root/generation/end and source authority.
        bool beginReference(juce::Range<std::int64_t> range) noexcept
        {
            if (fault != Fault::none)
                return false;
            if (open || range.getStart() < 0 || range.getEnd() <= range.getStart() ||
                range.getEnd() - range.getStart() > binding.maximumSpan ||
                (started && range.getStart() != nextReference))
                return fail(Fault::clock);
            const auto actual = history->witness(); // Sole serialized DSP owner only.
            if (actual.exhausted)
                return fail(Fault::overflow);
            if (mode == Mode::removal && !started &&
                (!previous || !previous->lastReady || previous->lastReferenceEnd != range.getStart() ||
                 previous->lastWitness != actual))
                return fail(Fault::history);
            if (mode == Mode::removal &&
                (actual.native != binding.native || actual.delay != binding.latency ||
                 actual.knownFrames < binding.latency))
                return fail(Fault::native);
            if (history->phase() !=
                (phase == Phase::dry ? PluginDryHistory::Phase::continuation : PluginDryHistory::Phase::host))
                return fail(Fault::history);
            before = actual;
            currentReference = range;
            rendered = 0;
            allCompleted = true;
            open = true;
            if (phase != Phase::dry)
                phase = mode == Mode::wet ? Phase::observing : Phase::fading;
            publish();
            return true;
        }
        // Actual post-child root check. Never equates Host logical frames with
        // native reference time; it checks their observed interval DIFFERENCE.
        bool finishReference(juce::Range<std::int64_t> range) noexcept
        {
            if (fault != Fault::none)
                return false;
            if (!open || range != currentReference || rendered != range.getLength())
                return fail(Fault::span);
            const auto actual = history->witness();
            if (std::uint64_t(rendered) > std::numeric_limits<std::uint64_t>::max() - before.frames ||
                actual.frames != before.frames + std::uint64_t(rendered))
                return fail(Fault::history);
            open = false;
            started = true;
            nextReference = range.getEnd();
            lastReferenceEnd = range.getEnd();
            lastWitness = actual;
            lastReady = allCompleted && actual.native == binding.native && actual.delay == binding.latency &&
                        actual.knownFrames >= binding.latency;
            if (mode == Mode::wet)
                phase = lastReady ? Phase::wetReady : Phase::observing;
            else if (phase != Phase::dry && lastReady && actual.dryEndpoint)
            {
                const auto status = history->claim(binding.hostWriter, this, actual, binding.latency,
                                                   binding.native, binding.maximumSpan);
                if (status != PluginDryHistory::Status::ready)
                    return fail(Fault::history);
                phase = Phase::dry;
            }
            publish();
            return true;
        }
        void refuse() noexcept
        {
            fail(Fault::revoked);
        }
        // DSP-only eligibility witness for the actual old stage. CONTROL uses
        // observation() only; true is not a graph/native/source ACK.
        bool readyAtDsp(juce::int64 end) const noexcept
        {
            return fault == Fault::none && !open && lastReady && lastReferenceEnd == end;
        }

      private:
        RemovalProgress(Mode kind, PluginDryHistory::Ptr ring, Binding actual,
                        std::shared_ptr<RemovalProgress> predecessor = {})
            : mode(kind), history(std::move(ring)), binding(actual), previous(std::move(predecessor)),
              segments(std::size_t(1 + (std::int64_t(actual.maximumSpan) + actual.pcm.quantum - 1) /
                                           actual.pcm.quantum)),
              capture(std::span<PluginPcmSegment>(segments), actual.pcm)
        {
        }
        bool fail(Fault value) noexcept
        {
            if (fault == Fault::none)
                fault = value;
            phase = Phase::failed;
            lastReady = false;
            publish();
            return false;
        }
        void publish() noexcept
        {
            published.store((std::uint64_t(fault) << 32) | std::uint32_t(phase), std::memory_order_release);
        }
        bool piece(const tracktion::PluginRenderContext &fc) noexcept
        {
            if (fault != Fault::none || !open || !fc.destBuffer || fc.bufferStartSample < 0 ||
                fc.bufferNumSamples < 0 || fc.bufferStartSample > fc.destBuffer->getNumSamples() ||
                fc.bufferNumSamples > fc.destBuffer->getNumSamples() - fc.bufferStartSample ||
                fc.destBuffer->getNumChannels() < 2 ||
                fc.bufferNumSamples > currentReference.getLength() - rendered)
                return fail(Fault::span);
            return true;
        }
        bool record(int count, const PluginDryHistory::Witness &input) noexcept
        {
            if (capture.status != PluginPcmReceiptStatus::complete || capture.identity != binding.pcm ||
                capture.count == 0 || capture.count > capture.storage.size())
                return fail(Fault::identity);
            int covered = 0;
            const auto q = std::uint64_t(binding.pcm.quantum);
            const auto k = std::uint64_t(binding.pcm.transportSamples / binding.pcm.quantum);
            for (const auto &segment : capture.storage.first(capture.count))
            {
                if (segment.offset != covered || segment.count <= 0 || segment.count > count - covered ||
                    segment.packetFrame < 0 || segment.packetFrame >= binding.pcm.quantum ||
                    segment.count > binding.pcm.quantum - segment.packetFrame ||
                    segment.outputPacket >
                        (std::numeric_limits<std::uint64_t>::max() - std::uint64_t(segment.packetFrame)) /
                            q ||
                    std::uint64_t(covered) > std::numeric_limits<std::uint64_t>::max() - input.frames ||
                    segment.outputPacket * q + std::uint64_t(segment.packetFrame) !=
                        input.frames + std::uint64_t(covered) ||
                    segment.forcedDry)
                    return fail(Fault::packet);
                if (segment.packet == PluginPcmPacketKind::startup && mode == Mode::wet)
                {
                    if (segment.consumed || segment.outputPacket >= k ||
                        segment.dryDelaySamples != binding.latency)
                        return fail(Fault::packet);
                    allCompleted = false; // Expected3Q startup is pending, never an optional fault.
                }
                else if (segment.packet != PluginPcmPacketKind::completed || !segment.consumed ||
                         segment.outputPacket < k || segment.consumed->sequence != segment.outputPacket - k ||
                         segment.consumed->stamp != binding.native ||
                         segment.dryDelaySamples != binding.latency)
                    return fail(Fault::native);
                covered += segment.count;
            }
            if (covered != count)
                return fail(Fault::packet);
            rendered += count;
            return true;
        }
        const Mode mode;
        const PluginDryHistory::Ptr history;
        const Binding binding;
        const std::shared_ptr<RemovalProgress> previous;
        std::vector<PluginPcmSegment> segments; // CONTROL prepared, never resized on DSP.
        PluginPcmCapture capture;
        Phase phase = Phase::prepared;
        Fault fault = Fault::none;
        bool open = false, started = false, allCompleted = false, lastReady = false;
        int rendered = 0;
        juce::Range<std::int64_t> currentReference;
        juce::int64 nextReference = 0, lastReferenceEnd = 0;
        PluginDryHistory::Witness before, lastWitness;
        std::atomic<std::uint64_t> published{std::uint32_t(Phase::prepared)};
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    };

    // A fresh restored worker and the selected dry predecessor execute from
    // ONE copied pre-FX input. No Host/stream is revived or advanced on CONTROL.
    class FreshJoinProgress final
    {
        friend class RemoteEffect;

      public:
        enum class Phase : std::uint32_t
        {
            prepared,
            filling,
            fading,
            joined,
            failed
        };
        enum class Fault : std::uint32_t
        {
            none,
            revoked,
            clock,
            span,
            predecessor,
            freshOwner,
            packet,
            native,
            history,
            overflow
        };
        struct Observation
        {
            Phase phase;
            Fault fault;
        };
        // CONTROL only, immutable identities/pins. No live payload reads.
        std::shared_ptr<RemovalProgress> predecessorControl() const
        {
            return predecessor;
        }
        std::shared_ptr<RemovalProgress> outputControl() const
        {
            return output;
        }
        Observation observation() const noexcept
        {
            const auto value = published.load(std::memory_order_acquire);
            return {Phase(value & 0xffffffffu), Fault(value >> 32)};
        }
        // Root must separately bind the actual predecessor root/generation and
        // continuity owner. Delegate here INSTEAD of opening either observer.
        bool beginReference(juce::Range<std::int64_t> range) noexcept
        {
            if (fault != Fault::none)
                return false;
            if (open || range.getStart() < 0 || range.getEnd() <= range.getStart() ||
                range.getLength() > maximum || (started && nextReference != range.getStart()))
                return fail(Fault::clock);
            const auto fresh = output->history->witness(); // Selected DSP owner only.
            if (fresh.exhausted || fresh.frames != frames ||
                (!started && (fresh.frames != 0 || fresh.spans != 0 || fresh.knownFrames != 0)))
                return fail(Fault::freshOwner);
            if (!started && (!predecessor->readyAtDsp(range.getStart()) ||
                             predecessor->phase != RemovalProgress::Phase::dry ||
                             predecessor->history->witness() != predecessor->lastWitness))
                return fail(Fault::predecessor);
            usingDry = phase != Phase::joined;
            if (usingDry && !predecessor->beginReference(range))
                return fail(Fault::predecessor);
            if (!output->beginReference(range))
                return fail(Fault::freshOwner);
            current = range;
            rendered = 0;
            open = true;
            if (phase == Phase::prepared)
                phase = Phase::filling;
            publish();
            return true;
        }
        bool finishReference(juce::Range<std::int64_t> range) noexcept
        {
            if (fault != Fault::none)
                return false;
            if (!open || range != current || rendered != range.getLength())
                return fail(Fault::span);
            if (!output->finishReference(range))
                return fail(Fault::freshOwner);
            if (usingDry && !predecessor->finishReference(range))
                return fail(Fault::predecessor);
            if (output->history->witness().frames != frames)
                return fail(Fault::history);
            // Completion is published only AFTER actual full-root processing,
            // including both rings' exact advancement and all ramp samples.
            if (fadePosition == fadeFrames)
                phase = Phase::joined;
            open = false;
            started = true;
            nextReference = range.getEnd();
            publish();
            return true;
        }
        void refuse() noexcept
        {
            fail(Fault::revoked);
        }

      private:
        FreshJoinProgress(std::shared_ptr<RemovalProgress> oldDry, std::shared_ptr<RemovalProgress> freshWet,
                          double rate, int block, int count, int ramp)
            : predecessor(std::move(oldDry)), output(std::move(freshWet)), preparedRate(rate),
              preparedBlock(block), maximum(count), fadeFrames(ramp), left(std::size_t(count)),
              right(std::size_t(count))
        {
        }
        bool fail(Fault value) noexcept
        {
            if (fault == Fault::none)
                fault = value;
            phase = Phase::failed;
            publish();
            return false;
        }
        void publish() noexcept
        {
            published.store((std::uint64_t(fault) << 32) | std::uint32_t(phase), std::memory_order_release);
        }
        bool piece(const tracktion::PluginRenderContext &fc) noexcept
        {
            if (fault != Fault::none)
                return false;
            if (!open || fc.bufferNumSamples < 0 || fc.bufferNumSamples > maximum ||
                fc.bufferNumSamples > current.getLength() - rendered || !output->piece(fc) ||
                (usingDry && !predecessor->piece(fc)))
                return fail(Fault::span);
            const auto before = output->history->witness();
            if (before.frames != frames)
                return fail(Fault::history);
            if (std::uint64_t(fc.bufferNumSamples) > std::numeric_limits<std::uint64_t>::max() - frames)
                return fail(Fault::overflow);
            return true;
        }
        // Called after the actual fresh Host has emitted and validated EVERY
        // captured segment. Original input is still in the prepared scratch.
        bool blend(const tracktion::PluginRenderContext &fc) noexcept
        {
            if (output->fault != RemovalProgress::Fault::none ||
                output->history->witness().frames != frames + std::uint64_t(fc.bufferNumSamples))
                return fail(Fault::freshOwner);
            if (usingDry)
            {
                if (predecessor->history->render(predecessor.get(), left.data(), right.data(),
                                                 fc.bufferNumSamples) != PluginDryHistory::Status::ready)
                    return fail(Fault::history);
                predecessor->rendered += fc.bufferNumSamples;
            }
            const auto &capture = output->capture;
            // output.record already validates exact identity, full native
            // stamp, sequence, packet offsets/coverage and real dry delay.
            for (const auto &segment : capture.storage.first(capture.count))
                for (int i = 0; i < segment.count; ++i)
                {
                    const auto n = segment.offset + i;
                    const auto ordinal = frames + std::uint64_t(n);
                    const bool sameTime =
                        ordinal >= std::uint64_t(output->binding.latency) &&
                        segment.packet == PluginPcmPacketKind::completed && segment.consumed &&
                        segment.consumed->stamp == output->binding.native && !segment.forcedDry &&
                        segment.dryDelaySamples == output->binding.latency;
                    if (fadePosition != fadeFrames && sameTime)
                    {
                        ++fadePosition;
                        phase = Phase::fading;
                    }
                    else if (fadePosition != 0 && !sameTime)
                        return fail(Fault::native); // Never restart a partially joined clock.
                    if (usingDry)
                    {
                        const auto mix = float(double(fadePosition) / double(fadeFrames));
                        const auto offset = fc.bufferStartSample + n;
                        const auto wetL = fc.destBuffer->getSample(0, offset);
                        const auto wetR = fc.destBuffer->getSample(1, offset);
                        // Exact endpoints avoid cancellation at0 and1.
                        fc.destBuffer->setSample(0, offset,
                                                 fadePosition == 0 ? left[std::size_t(n)]
                                                 : fadePosition == fadeFrames
                                                     ? wetL
                                                     : left[std::size_t(n)] +
                                                           (wetL - left[std::size_t(n)]) * mix);
                        fc.destBuffer->setSample(1, offset,
                                                 fadePosition == 0 ? right[std::size_t(n)]
                                                 : fadePosition == fadeFrames
                                                     ? wetR
                                                     : right[std::size_t(n)] +
                                                           (wetR - right[std::size_t(n)]) * mix);
                    }
                }
            frames += std::uint64_t(fc.bufferNumSamples);
            rendered += fc.bufferNumSamples;
            return true;
        }
        const std::shared_ptr<RemovalProgress> predecessor, output;
        const double preparedRate;
        const int preparedBlock, maximum, fadeFrames;
        std::vector<float> left, right; // CONTROL allocated, never resized.
        Phase phase = Phase::prepared;
        Fault fault = Fault::none;
        bool open = false, started = false, usingDry = true;
        int rendered = 0, fadePosition = 0;
        std::uint64_t frames = 0;
        juce::Range<std::int64_t> current;
        juce::int64 nextReference = 0;
        std::atomic<std::uint64_t> published{std::uint32_t(Phase::prepared)};
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    };

  private:
    std::shared_ptr<FreshJoinProgress> freshJoin;
    std::shared_ptr<RemovalProgress> outputProgress; // Prepared once before shell publication.

    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    bool runtimeAttached = false, sharingAdmissionPending = false, configured = false;
    std::uint64_t sharingEpoch = 0;
    Id pluginId = 0;
    std::atomic<bool> bypass{false};
    float initialWet = 1; // Immutable shell fallback, configured before attachment.
    std::atomic<int> graphLatencySamples{0};
    int auxiliaryChannels = 0;
    std::optional<int> sidechainBus;
    using InputProbe = void (*)(void *, const tracktion::PluginRenderContext &) noexcept;
    void *probeContext = nullptr;
    InputProbe inputProbe = nullptr;
    void *outputProbeContext = nullptr;
    InputProbe outputProbe = nullptr;

    void requirePrivateRuntimeConfiguration() const
    {
        if (runtimeAttached || runtime->attachedShells || sharingAdmissionPending)
            throw std::logic_error(
                "effect.runtime.configurationInUse: Configure worker identity before graph attachment");
    }
    bool runtimeConfigurationMatches(double rate, int block) const noexcept
    {
        if (isRemovalShell() && outputProgress->observation().phase == RemovalProgress::Phase::dry)
            return runtime->prepared && runtime->preparedRate == rate && runtime->preparedBlock == block &&
                   outputProgress->binding.processingRuntime == runtime.get() &&
                   graphLatencySamples.load(std::memory_order_relaxed) == outputProgress->binding.latency;
        return runtime->prepared && runtime->preparedRate == rate && runtime->preparedBlock == block &&
               runtime->worker == host.get() && runtime->plugin == pluginId &&
               (!host ||
                (runtime->workerInstance == host->runtimeIdentity() &&
                 runtime->quantum == host->processingQuantum() && runtime->buses == host->busLayout() &&
                 auxiliaryChannels == runtime->buses.auxiliaryChannels));
    }
    void detachRuntime() noexcept
    {
        if (!runtimeAttached)
            return;
        runtimeAttached = false;
        jassert(runtime->attachedShells != 0);
        if (runtime->attachedShells == 0 || --runtime->attachedShells != 0)
            return;
        // Last initialized shell ONLY, after graph readers quiesce. Preserve
        // the packet until the next ordinary first/device initialization.
        // In particular, incumbent retirement never resets a successor stream.
        runtime->prepared = false;
    }

  public:
    // Engineering capture only: attach before graph publication, retain storage
    // until graph retirement. Probe must be bounded and allocation/lock/I/O-free.
    // Normal application graphs never attach a probe.
    void setInputProbe(void *context, InputProbe probe) noexcept
    {
        probeContext = context;
        inputProbe = probe;
    }
    void setOutputProbe(void *context, InputProbe probe) noexcept
    {
        outputProbeContext = context;
        outputProbe = probe;
    }
    // Internal control/preparation identity; lifetime is pinned by graph/plan.
    PluginHost *phasePairHost() const noexcept
    {
        return host.get();
    }
    Id phasePairPluginId() const noexcept
    {
        return pluginId;
    }
    static constexpr const char *xmlTypeName = "vibera_vst3_effect";
    static const char *getPluginName()
    {
        return "VST3 effect";
    }
    explicit RemoteEffect(tracktion::PluginCreationInfo info) : tracktion::Plugin(info) {}
    // Internal preparation only: caller owns a private, already identified tree.
    static tracktion::Plugin::Ptr createDetachedGraph(tracktion::Edit &edit, const juce::ValueTree &state)
    {
        if (state[tracktion::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new RemoteEffect({edit, state, false, tracktion::PluginCreationInfo::Mode::detachedGraph});
    }

    // Internal CONTROL preparation. Same Edit/source ID and exact single-use
    // Host are required. Timeline, wiring, bypass target, latency and probes
    // belong to each shell; no DSP state is copied, inspected, or reset here.
    juce::Result shareCompatibleRuntimeFrom(const RemoteEffect &source)
    {
        auto fail = [](const char *message) { return juce::Result::fail(message); };
        if (!isDetachedGraph() || &source == this || &source.edit != &edit || itemID != source.itemID ||
            runtimeAttached || sharingAdmissionPending || runtime->attachedShells || runtime->prepared)
            return fail(
                "effect.runtime.invalidCandidate: Requires a fresh same-ID detached shell in the same Edit");
        if (source.isRemovalShell())
            return fail("effect.runtime.removalOwner: Use the exact completed dry-removal helper");
        if (outputProgress && outputProgress != source.outputProgress)
            return fail("effect.runtime.observerOwner: Cannot replace a separately prepared observer");
        if (!source.host || !source.host->alive() || source.pluginId <= 0 || !source.runtimeAttached ||
            !source.runtime->prepared || source.runtime->attachedShells == 0 ||
            source.runtime->epoch == std::numeric_limits<std::uint64_t>::max() ||
            !source.runtimeConfigurationMatches(source.runtime->preparedRate, source.runtime->preparedBlock))
            return fail("effect.runtime.inactiveSource: Exact initialized live worker/packet owner required");
        if ((configured && host != source.host) || (configured && pluginId != source.pluginId) ||
            (host && (host->runtimeIdentity() != source.runtime->workerInstance ||
                      host->processingQuantum() != source.runtime->quantum ||
                      host->busLayout() != source.runtime->buses)))
            return fail("effect.runtime.incompatibleSource: Host instance, native buses, quantum and plugin "
                        "ID must match");
        host = source.host;
        auxiliaryChannels = source.auxiliaryChannels;
        pluginId = source.pluginId;
        if (!timeline)
            timeline = source.timeline; // Optional private graph scope remains shell-local.
        if (!configured)
        {
            bypass.store(source.bypass.load(std::memory_order_relaxed), std::memory_order_relaxed);
            initialWet = source.initialWet;
            graphLatencySamples.store(source.graphLatencySamples.load(std::memory_order_relaxed),
                                      std::memory_order_relaxed);
        }
        sharingEpoch = source.runtime->epoch;
        runtime = source.runtime; // Empty private runtime retires on CONTROL, not DSP/adoption.
        freshJoin = source.freshJoin; // Shared selected join, no live payload copied.
        outputProgress =
            source.outputProgress; // Same single selected DSP owner; never copy its live payload.
        sharingAdmissionPending = true;
        configured = true;
        return juce::Result::ok();
    }
    // CONTROL-only, before graph attachment/publication. Root/ports must call
    // beginReference/finishReference around ACTUAL child processing. Merely
    // creating this observer cannot certify a native/source interval.
    juce::Result prepareOutputObservation(PluginDryHistory::Ptr history, PluginPcmIdentity identity,
                                          const void *writer)
    {
        const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        if (!messages || !messages->isThisTheMessageThread() || runtimeAttached || sharingAdmissionPending ||
            outputProgress || !configured || !host || !host->alive() || !timeline || !history || !writer ||
            host->offline() || host->pcmIdentity() != identity || !identity.preparedTransport ||
            identity.quantum < 16 || identity.transportSamples <= 0 ||
            identity.transportSamples % identity.quantum != 0 ||
            history->phase() != PluginDryHistory::Phase::host)
            return juce::Result::fail("effects.observerOwner: Fresh prepared same-Host observation required");
        const auto native = host->declaredLatency();
        const auto delay = std::int64_t(identity.transportSamples) + native.nativeSamples;
        const auto maximum = host->maximumLiveSpan();
        if (!native.valid(identity.quantum) || delay <= 0 || delay >= PluginDryHistory::capacity ||
            delay != graphLatencySamples.load(std::memory_order_relaxed) || maximum <= 0 ||
            maximum > PluginDryHistory::capacity)
            return juce::Result::fail(
                "effects.observerConfiguration: Actual native/transport delay required");
        RemovalProgress::Binding binding{runtime.get(), history.get(), writer, identity,
                                         native,        int(delay),    maximum};
        auto next = std::shared_ptr<RemovalProgress>(
            new RemovalProgress(RemovalProgress::Mode::wet, std::move(history), binding));
        outputProgress.swap(next); // CONTROL only, no previous published observer is replaced.
        return juce::Result::ok();
    }
    std::shared_ptr<RemovalProgress> outputProgressControl() const
    {
        return outputProgress;
    }
    // CONTROL only, after configure(fresh restored Host)+prepareOutputObservation
    // and before native attachment. Canonical caller still validates the actual
    // restored Spec/state/descriptor and exact effect-slot transaction; a delay
    // match alone is deliberately NOT route/source/membership authority.
    juce::Result prepareFreshJoinFrom(const RemoteEffect &source)
    {
        const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        if (!messages || !messages->isThisTheMessageThread() || &source == this || &source.edit != &edit ||
            !isDetachedGraph() || !configured || runtimeAttached || sharingAdmissionPending ||
            runtime->prepared || runtime->attachedShells || freshJoin || !host || !host->alive() ||
            host->offline() || !outputProgress || isRemovalShell() || !source.isRemovalShell() ||
            source.outputProgress->observation().phase != RemovalProgress::Phase::dry ||
            !source.runtimeAttached || !source.runtime->prepared || source.runtime->attachedShells == 0 ||
            pluginId <= 0 || source.pluginId != pluginId || auxiliaryChannels != 0 ||
            source.auxiliaryChannels != 0 || sidechainBus || source.sidechainBus)
            return juce::Result::fail(
                "effects.freshJoinOwner: Fresh restored worker and actual dry slot required");
        const auto &before = source.outputProgress->binding;
        const auto &after = outputProgress->binding;
        const auto stats = host->statistics(); // Atomic CONTROL read, no cursor/ring read.
        const auto ramp = host->bypassTransitionSamples();
        if (outputProgress->observation().phase != RemovalProgress::Phase::prepared ||
            after.processingRuntime != runtime.get() || after.processingRuntime == before.processingRuntime ||
            after.dryHistory == before.dryHistory || after.pcm.instance == before.pcm.instance ||
            after.pcm != host->pcmIdentity() || after.native != host->declaredLatency() ||
            after.latency != before.latency || after.pcm.preparedTransport != before.pcm.preparedTransport ||
            after.pcm.quantum != before.pcm.quantum ||
            after.pcm.transportSamples != before.pcm.transportSamples ||
            after.native.nativeSamples != before.native.nativeSamples || after.maximumSpan <= 0 ||
            after.maximumSpan > before.maximumSpan || stats.submitted != 0 || stats.processed != 0 ||
            stats.missed != 0 || stats.dropped != 0 || stats.midiOverflow != 0 || host->deliveryFailed() ||
            !std::isfinite(source.runtime->preparedRate) || source.runtime->preparedRate <= 0 ||
            source.runtime->preparedBlock < 1 || ramp <= 0 ||
            double(ramp) != std::ceil(source.runtime->preparedRate * .005))
            return juce::Result::fail(
                "effects.freshJoinConfiguration: Exact same-D fresh transport required");
        auto next = std::shared_ptr<FreshJoinProgress>(
            new FreshJoinProgress(source.outputProgress, outputProgress, source.runtime->preparedRate,
                                  source.runtime->preparedBlock, after.maximumSpan, ramp));
        freshJoin.swap(next); // All fallible allocation finished before publication.
        return juce::Result::ok();
    }
    std::shared_ptr<FreshJoinProgress> freshJoinProgressControl() const
    {
        return freshJoin;
    }

    bool isRemovalShell() const noexcept
    {
        return outputProgress && outputProgress->isRemoval();
    }

    // Same effect slot, exact old stream and history. No state restore or DSP
    // clock/packet reads. Actual old readiness is checked later by selected DSP.
    juce::Result configureRemovalFrom(const RemoteEffect &source, std::shared_ptr<LiveTimeline> clock)
    {
        const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        if (!messages || !messages->isThisTheMessageThread() || !source.outputProgress ||
            source.outputProgress->isRemoval() || !clock ||
            (source.freshJoin && source.freshJoin->observation().phase != FreshJoinProgress::Phase::joined) ||
            source.outputProgress->binding.processingRuntime != source.runtime.get() || !source.host ||
            source.host->pcmIdentity() != source.outputProgress->binding.pcm ||
            source.host->declaredLatency() != source.outputProgress->binding.native || runtimeAttached ||
            sharingAdmissionPending || configured || outputProgress)
            return juce::Result::fail("effects.removalOwner: Original observed wet shell required");
        auto progress = std::shared_ptr<RemovalProgress>(
            new RemovalProgress(RemovalProgress::Mode::removal, source.outputProgress->history,
                                source.outputProgress->binding, source.outputProgress));
        configure(source.host, true, std::move(clock), source.pluginId, source.initialWet);
        if (auto shared = shareCompatibleRuntimeFrom(source); shared.failed())
            return shared;
        freshJoin.reset(); // CONTROL: removal is a new operation, never rerun a prior fresh join.
        outputProgress = std::move(progress);
        return juce::Result::ok();
    }
    // Subsequent same-config private candidates retain an already dry Runtime
    // and history without a second ring claim or reading a retired worker.
    juce::Result shareDryRemovalFrom(const RemoteEffect &source, std::shared_ptr<LiveTimeline> clock)
    {
        const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        if (!messages || !messages->isThisTheMessageThread() || !isDetachedGraph() || &edit != &source.edit ||
            itemID != source.itemID || &source == this || runtimeAttached || sharingAdmissionPending ||
            configured || outputProgress || !clock || !source.isRemovalShell() ||
            source.outputProgress->observation().phase != RemovalProgress::Phase::dry ||
            !source.runtimeAttached || !source.runtime->prepared || source.runtime->attachedShells == 0)
            return juce::Result::fail("effects.removalClone: Exact completed dry source required");
        timeline = std::move(clock);
        pluginId = source.pluginId;
        auxiliaryChannels = source.auxiliaryChannels;
        sidechainBus = source.sidechainBus;
        initialWet = source.initialWet;
        graphLatencySamples.store(source.graphLatencySamples.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
        runtime = source.runtime;
        outputProgress = source.outputProgress;
        sharingEpoch = runtime->epoch;
        sharingAdmissionPending = configured = true;
        return juce::Result::ok();
    }
    // Root-owned CONTROL after the same selected/no-reader/final-close proof.
    // The native player may cache this obsolete wet graph beyond that grace;
    // release only its exact superseded Host pin, never its live DSP payload.
    bool retainsWetPredecessorControl(const RemoteEffect &successor) const noexcept
    {
        return &edit == &successor.edit && itemID == successor.itemID && pluginId == successor.pluginId &&
               !isRemovalShell() && outputProgress && successor.isRemovalShell() &&
               runtime == successor.runtime && successor.outputProgress->previous == outputProgress &&
               host && host == successor.host;
    }
    juce::Result releaseRetiredWetWorkerControl(const RemoteEffect &successor)
    {
        const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        if (!messages || !messages->isThisTheMessageThread() || !retainsWetPredecessorControl(successor) ||
            successor.outputProgress->observation().phase != RemovalProgress::Phase::dry)
            return juce::Result::fail("effects.retiredWetOwnerChanged: Exact quiesced predecessor required");
        host.reset(); // CONTROL; the selected successor and transaction still pin this Host.
        return juce::Result::ok();
    }
    // Root-owned CONTROL calls only after exact dry transfer + selected/no old
    // readers + final source-bank close ACK. Cached PluginPtrs may remain alive:
    // explicitly retire this exact worker, never wait or release it on DSP.
    juce::Result releaseRemovalWorkerControl()
    {
        const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        if (!messages || !messages->isThisTheMessageThread() || !isRemovalShell() ||
            outputProgress->observation().phase != RemovalProgress::Phase::dry)
            return juce::Result::fail("effects.removalNotDry: Real completed dry owner required");
        if (host)
        {
            host->beginRetirement();
            host.reset();
        }
        return juce::Result::ok();
    }
    std::shared_ptr<PluginHost> removalWorkerPinControl() const
    {
        return isRemovalShell() ? host : std::shared_ptr<PluginHost>{};
    }

    // CONTROL only. Keeps no PluginPtr/Edit alive past graph retirement.
    std::shared_ptr<const void> runtimeOwnerControl() const { return runtime; }
    std::uintptr_t runtimeIdentity() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(runtime.get());
    }
    unsigned runtimeAttachmentsControl() const noexcept
    {
        return runtime->attachedShells;
    }
    std::uint64_t runtimeEpochControl() const noexcept
    {
        return runtime->epoch;
    }
    ~RemoteEffect() override
    {
        detachRuntime(); // Also balances private native-initialization unwind.
        notifyListenersOfDeletion();
    }
    // Native per-adapter CPU telemetry is unused; overall callback health is independent.
    bool shouldMeasureCpuUsage() const noexcept override
    {
        return false;
    }
    juce::String getName() const override
    {
        return getPluginName();
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool takesAudioInput() override
    {
        return true;
    }
    bool takesMidiInput() override
    {
        return false;
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        auto layout = BusLayout::singleStereoInOut();
        if (auxiliaryChannels != 0)
            layout.inputs.push_back(auxiliaryChannels == 1 ? tracktion::ChannelConfiguration::mono()
                                                           : tracktion::ChannelConfiguration::stereo());
        return layout;
    }
    void getChannelNames(juce::StringArray *ins, juce::StringArray *outs) override
    {
        if (ins)
        {
            ins->clear();
            ins->addArray({"Main L", "Main R"});
            if (auxiliaryChannels > 0)
                ins->add("Detector L/mono");
            if (auxiliaryChannels > 1)
                ins->add("Detector R");
        }
        if (outs)
        {
            outs->clear();
            outs->addArray({"Main L", "Main R"});
        }
    }
    std::optional<int> getSidechainBusIDOverride() const override
    {
        return sidechainBus;
    }
    // Prepared graph connection only; no native layout or audio-thread mutation.
    // Application connection commands/persistence are a separate required layer.
    juce::Result connectSidechain(tracktion::EditItemID source, int preparedBus)
    {
        if (runtimeAttached || auxiliaryChannels == 0 || !source.isValid() || preparedBus < 0 || sidechainBus)
            return juce::Result::fail("Sidechain needs a prepared auxiliary worker and valid graph source");
        sidechainBus = preparedBus;
        setSidechainSourceID(source);
        makeConnection(0, 0, nullptr);
        makeConnection(1, 1, nullptr);
        makeConnection(2, 2, nullptr);
        if (auxiliaryChannels == 2)
            makeConnection(3, 3, nullptr);
        return juce::Result::ok();
    }
    void configure(std::shared_ptr<PluginHost> h, bool b, std::shared_ptr<LiveTimeline> clock, Id plugin,
                   float wet = 1)
    {
        requirePrivateRuntimeConfiguration();
        if (!std::isfinite(wet) || wet < 0 || wet > 1)
            throw std::invalid_argument("effect.mix.invalid: Configure a finite wet fraction0..1");
        initialWet = wet;
        host = std::move(h);
        configured = true;
        graphLatencySamples = host ? host->latencySamples() : 0;
        auxiliaryChannels = host ? host->busLayout().auxiliaryChannels : 0;
        timeline = std::move(clock);
        pluginId = plugin;
        bypass = b;
    }
    void setBypassed(bool b)
    {
        bypass = b;
    }
    double getLatencySeconds() override
    {
        if (isRemovalShell())
            return graphLatencySamples.load(std::memory_order_relaxed) / sampleRate;
        return host ? graphLatencySamples.load(std::memory_order_relaxed) / sampleRate : 0.;
    }
    bool setGraphLatencySamples(int samples)
    {
        return graphLatencySamples.exchange(samples, std::memory_order_relaxed) != samples;
    }
    int preparedGraphLatencySamples() const noexcept
    {
        return graphLatencySamples.load(std::memory_order_relaxed);
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override {}
    void initialise(const tracktion::PluginInitialisationInfo &info) override
    {
        if (!std::isfinite(info.sampleRate) || info.sampleRate <= 0 || info.blockSizeSamples < 1)
            throw std::logic_error("effect.runtime.invalidDevice: Valid graph sample rate/block required");
        if (freshJoin &&
            (freshJoin->preparedRate != info.sampleRate || freshJoin->preparedBlock != info.blockSizeSamples))
            throw std::logic_error("effects.freshJoinDevice: Exact prepared rate/block required");
        const bool dryRemoval =
            isRemovalShell() && outputProgress->observation().phase == RemovalProgress::Phase::dry;
        if (sharingAdmissionPending &&
            (!runtime->prepared || runtime->attachedShells == 0 || runtime->epoch != sharingEpoch))
            throw std::logic_error(
                "effect.runtime.staleSource: Source initialization changed before attachment");
        if ((!runtimeAttached && runtime->attachedShells > 0) ||
            (runtimeAttached && runtime->attachedShells > 1))
        {
            if (!runtimeConfigurationMatches(info.sampleRate, info.blockSizeSamples) ||
                runtime->attachedShells == std::numeric_limits<unsigned>::max() ||
                (!dryRemoval && (!host || !host->alive())))
                throw std::logic_error(
                    "effect.runtime.configurationInUse: Shared worker/rate/block/bus identity cannot change");
            if (!runtimeAttached)
            {
                ++runtime->attachedShells;
                runtimeAttached = true;
            }
            sharingAdmissionPending = false;
            return; // Do NOT inspect/copy/reset the in-flight DSP stream.
        }
        if (dryRemoval)
            throw std::logic_error(
                "effects.removalReprepare: Retained dry history needs an actual same-config predecessor");
        // Ordinary first initialization/device reprepare retains the prior
        // stream-reset behavior. Prepare potentially fallible metadata first.
        auto instance = host ? host->runtimeIdentity() : juce::String{};
        const auto buses = host ? host->busLayout() : PluginBusRequest{};
        const auto quantum = host ? host->processingQuantum() : 0;
        runtime->stream.reset();
        runtime->effectControlsSeen = false;
        runtime->worker = host.get();
        runtime->workerInstance.swapWith(instance);
        runtime->buses = buses;
        runtime->quantum = quantum;
        runtime->plugin = pluginId;
        runtime->preparedRate = info.sampleRate;
        runtime->preparedBlock = info.blockSizeSamples;
        if (runtime->epoch != std::numeric_limits<std::uint64_t>::max())
            ++runtime->epoch;
        runtime->prepared = true;
        if (!runtimeAttached)
        {
            ++runtime->attachedShells;
            runtimeAttached = true;
        }
        sharingAdmissionPending = false;
    }
    void deinitialise() override
    {
        detachRuntime();
    }
    void applyRemoval(const tracktion::PluginRenderContext &fc) noexcept
    {
        auto &progress = *outputProgress;
        const auto clear = [&]() noexcept
        {
            if (fc.destBuffer && fc.bufferStartSample >= 0 && fc.bufferNumSamples >= 0 &&
                fc.bufferStartSample <= fc.destBuffer->getNumSamples() &&
                fc.bufferNumSamples <= fc.destBuffer->getNumSamples() - fc.bufferStartSample)
                for (int channel = 0; channel < std::min(2, fc.destBuffer->getNumChannels()); ++channel)
                    fc.destBuffer->clear(channel, fc.bufferStartSample, fc.bufferNumSamples);
        };
        if (!progress.piece(fc))
        {
            clear();
            return;
        }
        if (fc.bufferNumSamples == 0)
            return;
        // This branch precedes ANY Host/shared_ptr/runtime stream dereference.
        // A real source-bank ACK may already have retired the old worker.
        if (progress.phase == RemovalProgress::Phase::dry)
        {
            if (progress.history->render(&progress, fc.destBuffer->getWritePointer(0, fc.bufferStartSample),
                                         fc.destBuffer->getWritePointer(1, fc.bufferStartSample),
                                         fc.bufferNumSamples) != PluginDryHistory::Status::ready)
            {
                progress.fail(RemovalProgress::Fault::history);
                clear();
                return;
            }
            progress.rendered += fc.bufferNumSamples;
            return;
        }
        const auto before = progress.history->witness();
        auto lease = timeline->acquire();
        if (lease.document)
        {
            runtime->clockTempo = lease.document->tempoMap.bpmAt(
                lease.document->tempoMap.beatAt(fc.editTime.getStart().inSeconds()));
            runtime->clockNumerator = lease.document->numerator;
            runtime->clockDenominator = lease.document->denominator;
        }
        float *pointers[2]{};
        const int available = juce::jlimit(0, auxiliaryChannels, fc.destBuffer->getNumChannels() - 2);
        for (int i = 0; i < available; ++i)
            pointers[i] = fc.destBuffer->getWritePointer(2 + i);
        juce::AudioBuffer<float> detector(pointers, available, fc.destBuffer->getNumSamples());
        const auto status = runtime->stream.process(
            *host, lease.document, pluginId, *fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, {},
            fc.editTime.getStart().inSeconds(), sampleRate, runtime->clockTempo,
            fc.isPlaying || fc.isRendering, true, // Existing5ms bypass; no ramp rewrite.
            runtime->clockNumerator, runtime->clockDenominator, auxiliaryChannels ? &detector : nullptr,
            fc.bufferStartSample, runtime->effectControlsSeen ? runtime->wetTarget : initialWet,
            &progress.capture);
        if ((status != PluginAutomationStream::Status::ready &&
             status != PluginAutomationStream::Status::snapshotUnavailable) ||
            !progress.record(fc.bufferNumSamples, before))
        {
            progress.fail(RemovalProgress::Fault::packet);
            clear();
        }
    }

    void applyFreshJoin(const tracktion::PluginRenderContext &fc) noexcept
    {
        auto &join = *freshJoin;
        const auto clear = [&]() noexcept
        {
            if (fc.destBuffer && fc.bufferStartSample >= 0 && fc.bufferNumSamples >= 0 &&
                fc.bufferStartSample <= fc.destBuffer->getNumSamples() &&
                fc.bufferNumSamples <= fc.destBuffer->getNumSamples() - fc.bufferStartSample)
                for (int channel = 0; channel < std::min(2, fc.destBuffer->getNumChannels()); ++channel)
                    fc.destBuffer->clear(channel, fc.bufferStartSample, fc.bufferNumSamples);
        };
        if (!join.piece(fc))
        {
            clear();
            return;
        }
        if (fc.bufferNumSamples == 0)
            return;
        if (join.usingDry)
            for (int n = 0; n < fc.bufferNumSamples; ++n)
            {
                join.left[std::size_t(n)] = fc.destBuffer->getSample(0, fc.bufferStartSample + n);
                join.right[std::size_t(n)] = fc.destBuffer->getSample(1, fc.bufferStartSample + n);
            }
        // The actual source has run ONCE. This runs only the fresh Host ONCE;
        // its own independent dry history records that same pre-effect input.
        applyWetBuffer(fc, false);
        if (!join.blend(fc))
        {
            clear();
            return;
        }
        if (outputProbe)
            outputProbe(outputProbeContext, fc);
    }

    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    void applyToBuffer(const tracktion::PluginRenderContext &fc) override
    {
        if (freshJoin)
        {
            applyFreshJoin(fc);
            return;
        }
        if (isRemovalShell())
        {
            applyRemoval(fc);
            return;
        }
        applyWetBuffer(fc);
    }
    void applyWetBuffer(const tracktion::PluginRenderContext &fc, bool emitOutputProbe = true)
    {
        if (host && fc.destBuffer)
        {
            if (inputProbe)
                inputProbe(probeContext, fc);
            auto lease = timeline->acquire(); // An idle effect must not pin a retired document indefinitely.
            if (lease.document)
            {
                runtime->clockTempo = lease.document->tempoMap.bpmAt(
                    lease.document->tempoMap.beatAt(fc.editTime.getStart().inSeconds()));
                runtime->clockNumerator = lease.document->numerator;
                runtime->clockDenominator = lease.document->denominator;
                if (const auto *control = lease.document->effect(pluginId))
                {
                    runtime->wetTarget = control->wet;
                    runtime->bypassTarget = control->bypass;
                    runtime->effectControlsSeen = true;
                }
            }
            // External pointer storage stays inline (at most two channels).
            // Main writes cannot mutate the independent auxiliary graph channels.
            float *pointers[2]{};
            const int available = juce::jlimit(0, auxiliaryChannels, fc.destBuffer->getNumChannels() - 2);
            for (int i = 0; i < available; ++i)
                pointers[i] = fc.destBuffer->getWritePointer(2 + i);
            juce::AudioBuffer<float> detector(pointers, available, fc.destBuffer->getNumSamples());
            const bool observing = outputProgress && outputProgress->piece(fc);
            const auto before = observing ? outputProgress->history->witness() : PluginDryHistory::Witness{};
            const auto status = runtime->stream.process(
                *host, lease.document, pluginId, *fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples,
                {}, fc.editTime.getStart().inSeconds(), sampleRate, runtime->clockTempo,
                fc.isPlaying || fc.isRendering,
                runtime->effectControlsSeen ? runtime->bypassTarget : bypass.load(), runtime->clockNumerator,
                runtime->clockDenominator, auxiliaryChannels ? &detector : nullptr, fc.bufferStartSample,
                runtime->effectControlsSeen ? runtime->wetTarget : initialWet,
                observing ? &outputProgress->capture : nullptr);
            if (observing && ((status != PluginAutomationStream::Status::ready &&
                               status != PluginAutomationStream::Status::snapshotUnavailable) ||
                              !outputProgress->record(fc.bufferNumSamples, before)))
                outputProgress->fail(
                    RemovalProgress::Fault::packet); // Normal wet audio is never muted by ledger failure.
            if (emitOutputProbe && outputProbe)
                outputProbe(outputProbeContext, fc);
        }
    }
};
} // namespace vibera
