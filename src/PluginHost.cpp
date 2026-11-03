// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synth/SynthRegistry.h"
#include "PluginHost.h"
#include "effects/NativeEffectsRegistry.h"
#include "PluginDryHistory.h"
#include "PluginEditorClock.h"
#include "PluginStateEncoding.h"
#include "PluginPolicy.h"
#include "HostWetMix.h"
#include "WorkerProcess.h"
#include "PluginControlRequests.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>
#include <cmath>
#include <set>
#include <optional>
#include <limits>
#include <stdexcept>

namespace vibera
{
juce::File pluginWorkerExecutable()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile("VibeRa Plugin Worker.exe");
}
struct PluginHost::Impl
{
    HANDLE mapping = nullptr, wake = nullptr;
    ipc::Shared *shared = nullptr;
    WorkerProcess child;
    juce::File descriptorFile;
    bool offline = false, instrument = false;
    PluginBusRequest buses;
    PluginBusCatalogue busCatalogue;
    bool busCatalogueAvailable = false;
    std::atomic<std::uint64_t> auxiliaryRejected{0};
    int bypassFrames = 1;
    double bypassMix = 0, bypassStep = 1;
    bool bypassInitialised = false; // Sole processing owner.
    std::atomic<bool> bypassAvailable{false};
    std::atomic<float> bypassReadback{0}, wetReadback{1};
    HostWetMix wetMix; // Single processing owner, retained with host.
    static_assert(std::atomic<float>::is_always_lock_free);
    const juce::Uuid runtimeUuid; // Immutable CONTROL construction; no DSP conversion/allocation.
    juce::String descriptor, controlError, recoveryState, runtimeIdentity = runtimeUuid.toString();
    juce::String capturedState;
    unsigned captureSerial = 0, capturedSerial = 0, captureGeneration = 0, capturedGeneration = 0,
             capturedRevision = 0;
    bool capturedStateValid = false, captureEditorClosed = false;
    std::atomic<bool> failed{false};
    juce::uint32 lastProgress = 0;
    std::optional<juce::uint32> shutdownStarted;
    bool terminationSent = false, retiring = false, editorClockFailed = false;
    std::uint64_t observedProcessed = 0;
    // Finalized on CONTROL before first DSP (constructor or private fresh
    // promotion). Immutable for every graph/process reader thereafter.
    PreparedPluginTransport::Ptr preparedTransport;
    int transportPackets;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> transportFault{0}; // Packed Host/source status, never allocated text.
    bool burstActive = false; // SINGLE processing owner, no CONTROL reads of these fields.
    std::uint64_t burstSerial = 0, lastBurstSerial = 0;
    int burstQuota = 0, burstProcessed = 0;
    explicit Impl(TransportPolicy policy) : transportPackets(int(policy))
    {
        if (policy != TransportPolicy::oneQuantum && policy != TransportPolicy::twoQuanta)
            throw std::invalid_argument("Unsupported prepared plugin transport policy");
    }
    explicit Impl(PreparedPluginTransport::Ptr prepared)
        : preparedTransport(std::move(prepared)),
          transportPackets(preparedTransport ? preparedTransport->budget().packets : 0)
    {
        if (!preparedTransport || transportPackets < 2 || transportPackets > ipc::slots)
            throw std::invalid_argument("A validated owned plugin transport plan is required");
    }
    PluginTransportOutcome
    refuseTransport(PluginTransportStatus status,
                    transport::BurstStatus source = transport::BurstStatus::ready) noexcept
    {
        const auto code = std::uint64_t(unsigned(status)) | (std::uint64_t(unsigned(source)) << 32);
        std::uint64_t empty = 0;
        transportFault.compare_exchange_strong(empty, code, std::memory_order_acq_rel);
        PluginTransportOutcome result;
        const auto actual = transportFault.load(std::memory_order_acquire);
        result.status = PluginTransportStatus(unsigned(actual & 0xffffffffu));
        result.source.status = transport::BurstStatus(unsigned(actual >> 32));
        return result; // Sticky until new CONTROL-prepared Host; reslicing cannot erase a fault.
    }
    PluginTransportOutcome inspectPhysicalSpan(int count) const noexcept
    {
        PluginTransportOutcome result;
        result.source.status = transport::BurstStatus::ready;
        const auto fault = transportFault.load(std::memory_order_acquire);
        if (fault != 0)
        {
            result.status = PluginTransportStatus(unsigned(fault & 0xffffffffu));
            result.source.status = transport::BurstStatus(unsigned(fault >> 32));
            return result;
        }
        if (!burstActive)
        {
            result.status = PluginTransportStatus::burstMissing;
            return result;
        }
        juce::AudioDeviceManager::PhysicalCallbackSnapshot snapshot;
        const bool known = preparedTransport->read(snapshot);
        const auto admission =
            transport::admitSourceBurst(known, snapshot, preparedTransport->budget(), burstQuota);
        if (admission.status != transport::BurstStatus::ready)
        {
            result.status = PluginTransportStatus::sourceRefused;
            result.source = admission;
            return result;
        }
        if (snapshot.callbackSerial != burstSerial)
        {
            result.status = PluginTransportStatus::serialReused;
            return result;
        }
        if (count < 0 || count > burstQuota - burstProcessed)
        {
            result.status = PluginTransportStatus::sourceSpanExceeded;
            return result;
        }
        result.status = PluginTransportStatus::ready;
        return result;
    }
    bool validatePhysicalSpan(int count) noexcept
    {
        const auto result = inspectPhysicalSpan(count);
        if (!result.succeeded())
        {
            refuseTransport(result.status, result.source.status);
            return false;
        }
        return true;
    }
    PluginPcmReceiptStatus inspectProcessInput(const juce::AudioBuffer<float> &buffer, int offset, int count,
                                               float wetTarget) const noexcept
    {
        // Shared ordinary span/mix/readiness checks, in original order. The
        // null-capture process path needs no identity or large receipt value.
        if (offset < 0 || count < 0 || offset > buffer.getNumSamples() ||
            count > buffer.getNumSamples() - offset ||
            (!offline && !preparedTransport && transportPackets == 2 && count > blockSize))
            return PluginPcmReceiptStatus::invalidSpan;
        if (!std::isfinite(wetTarget) || wetTarget < 0 || wetTarget > 1)
            return PluginPcmReceiptStatus::invalidMix;
        if (!shared)
            return PluginPcmReceiptStatus::workerUnavailable;
        if (!dryHistory->hostOwns(this))
            return PluginPcmReceiptStatus::historyDetached;
        return PluginPcmReceiptStatus::empty;
    }

    // Fixed existing prepared dry storage; NEVER clamp an unsupported native
    // declaration into a seemingly valid KQ delay. Native Q-bound alone is not enough.
    static constexpr int dryCapacity = PluginDryHistory::capacity;
    bool supportedLatency(PluginLatencyStamp stamp, int quantum) const noexcept
    {
        return stamp.valid(quantum) && stamp.nativeSamples < dryCapacity - transportPackets * quantum;
    }
    int cursor = 0, blockSize = 256;
    std::uint64_t sequence = 0;
    ipc::Block *input = nullptr;
    float output[2][ipc::frames]{};
    // Same existing samples/algorithm, with an independently retained owner.
    // All shared-owner operations are CONTROL construction/retirement only.
    const PluginDryHistory::Ptr dryHistory = std::make_shared<PluginDryHistory>(this);
    int consumedNativeSamples = 0; // Sole processing owner; dry tap follows consumed wet PCM.
    PluginPcmPacketKind emittedPacket = PluginPcmPacketKind::startup;
    std::optional<PluginConsumedLatency> emittedConsumed; // Same single DSP owner, not CONTROL-readable.
    bool latencyPacketFailed = false;
    std::atomic<std::uint64_t> latencyRejected{0};
    std::atomic<std::uint64_t> consumedRevision{0}, consumedStamp{0}, consumedSequence{0};
    std::array<ipc::MidiEvent, ipc::events> midi;
    int midiCount = 0;
    std::array<ipc::ParameterEvent, ipc::parameterEvents> parameters;
    int parameterCount = 0;
    unsigned parameterGeneration = 0;
    bool parametersInvalid = false;
    PluginControlRequests controlRequests;
    PluginEditorRequests editorRequests;
    std::optional<PluginParameterCatalogue> catalogue;
    std::optional<PluginParameterCatalogue> initialCatalogue;
    StartupDiagnostics startup;
    PluginGestureTracker gestures;
    PluginCompletedGestures completedGestures;
    unsigned completedGeneration = 0, completedEpoch = 0;
    std::uint64_t gestureLossSeen = 0;
    std::atomic<std::uint64_t> submitted{0}, missed{0}, dropped{0}, overflow{0};
    std::atomic<AutomationStatus> automationLast{AutomationStatus::ready};
    std::array<std::atomic<std::uint64_t>, 4> automationFailures{};
    void requestShutdown()
    {
        if (shutdownStarted)
            return; // Repeated retirement/failure must not extend the original grace budget.
        shutdownStarted = juce::Time::getMillisecondCounter();
        if (shared)
            shared->shutdown.store(1, std::memory_order_release);
        child.requestGracefulShutdown();
    }
    void failEditorClock()
    {
        // No diagnostic String allocation while recovering physical mailbox
        // ownership. error() formats the stable reason on later control readback.
        editorClockFailed = true;
        editorRequests.failPending(PluginEditorOutcome::workerUnavailable);
        failed = true;
        requestShutdown(); // Existing exact-owned worker grace/recovery path.
    }
    ~Impl()
    {
        if (shared)
            shared->shutdown.store(1);
        if (child.isRunning() && !child.waitForProcessToFinish(2000) && allowForcedWorkerTermination)
            child.kill(); // only our owned worker
        if (shared)
            UnmapViewOfFile(shared);
        if (mapping)
            CloseHandle(mapping);
        if (wake)
            CloseHandle(wake);
        if (descriptorFile.existsAsFile())
            descriptorFile.deleteFile();
    }
    double ppqAnchor = -1, ppqAnchorSeconds = 0; // Audio thread only (setBlockPpq).
    void begin(double seconds, double tempo, bool playing, int numerator, int denominator)
    {
        std::fill_n(output[0], blockSize, 0.f);
        std::fill_n(output[1], blockSize, 0.f);
        // Full-Q atomic input cannot execute before the final logical slice.
        // Prepared K=1+ceil(total source quota/Q) leaves a separate physical
        // callback turn for each owed result. This does not prove OS scheduling.
        // Legacy1Q/explicit2Q remain unchanged; no live wait is introduced.
        const bool due = sequence >= transportPackets;
        const auto wanted = due ? sequence - transportPackets : 0;
        emittedPacket = due ? PluginPcmPacketKind::missing : PluginPcmPacketKind::startup;
        emittedConsumed.reset(); // Do not present an older wet packet after a miss/startup.
        bool found = !due;       // The first K startup packets are analytical silence, not misses.
        for (auto &packet : shared->blocks)
        {
            auto status = packet.status.load(std::memory_order_acquire);
            if (due && packet.sequence == wanted && (status == 1 || status == 2) && offline)
            {
                const auto deadline = juce::Time::getMillisecondCounter() + 10000;
                while ((status = packet.status.load(std::memory_order_acquire)) != 3 &&
                       shared->status.load() == 1 && child.isRunning() &&
                       juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep(1);
            }
            // A fast worker can already finish wanted+1. Keep that PCM in its
            // owned IPC slot until its ACTUAL output deadline; never free it early.
            if (status == 3 && due && packet.sequence <= wanted)
            {
                if (due && packet.sequence == wanted)
                {
                    if (!supportedLatency(packet.latency, blockSize))
                    {
                        emittedPacket = PluginPcmPacketKind::invalidLatency;
                        latencyPacketFailed = true;
                        ++latencyRejected;
                        failed.store(true, std::memory_order_release);
                    }
                    else
                    {
                        for (int c = 0; c < 2; ++c)
                            std::copy_n(packet.output[c], blockSize, output[c]);
                        consumedNativeSamples = packet.latency.nativeSamples;
                        emittedPacket = PluginPcmPacketKind::completed;
                        emittedConsumed = PluginConsumedLatency{packet.latency, packet.sequence};
                        consumedRevision.fetch_add(1);
                        consumedStamp.store(packet.latency.packed());
                        consumedSequence.store(packet.sequence);
                        consumedRevision.fetch_add(1);
                    }
                    found = true;
                }
                packet.status.store(0, std::memory_order_release);
            }
        }
        if (!found)
        {
            ++missed;
            // Late PCM is discarded, but a processed note must retain its voice.
            // The worker requests gate reconciliation if it actually skips a MIDI packet.
        }
        if (!offline)
            shared->oldestNeeded.store(sequence >= transportPackets - 1 ? sequence - (transportPackets - 1)
                                                                        : 0,
                                       std::memory_order_release);
        input = nullptr;
        for (auto &packet : shared->blocks)
            if (packet.status.load(std::memory_order_acquire) == 0)
            {
                input = &packet;
                break;
            }
        if (input)
        {
            input->sequence = sequence;
            input->count = blockSize;
            input->parameterCount = 0;
            input->seconds = seconds;
            input->tempo = tempo;
            // Song beat from the tempo map anchor, advanced at this packet's tempo.
            input->ppq = ppqAnchor >= 0 ? ppqAnchor + (seconds - ppqAnchorSeconds) * tempo / 60. : -1.;
            input->playing = playing;
            input->numerator = numerator;
            input->denominator = denominator;
        }
        midiCount = 0;
        parameterCount = 0;
        parameterGeneration = 0;
        parametersInvalid = false;
    }
    void rejectParameters(std::uint64_t incoming = 0) noexcept
    {
        shared->parameterEventsRejected.fetch_add(incoming + std::uint64_t(parameterCount),
                                                  std::memory_order_relaxed);
        parameterCount = 0;
        parameterGeneration = 0;
        parametersInvalid = true;
    }
};
PluginHost::PluginHost(TransportPolicy policy) : impl(std::make_unique<Impl>(policy)) {}
PluginHost::PluginHost(PreparedPluginTransport::Ptr prepared)
    : impl(std::make_unique<Impl>(std::move(prepared)))
{
}
PluginHost::TransportPolicy PluginHost::transportPolicy() const noexcept
{
    return impl->preparedTransport ? TransportPolicy::preparedBurst : TransportPolicy(impl->transportPackets);
}
PluginTransportOutcome PluginHost::beginPhysicalBurst(int sourceFrameUpperBound) noexcept
{
    auto &p = *impl;
    PluginTransportOutcome result;
    if (!p.preparedTransport || !p.shared || p.offline)
        return result;
    if (auto fault = transportOutcome(); !fault.succeeded())
        return fault;
    if (p.failed.load(std::memory_order_acquire) || p.shared->status.load(std::memory_order_acquire) != 1)
        return p.refuseTransport(PluginTransportStatus::workerUnavailable);
    if (p.burstActive)
        return p.refuseTransport(PluginTransportStatus::burstAlreadyActive);
    juce::AudioDeviceManager::PhysicalCallbackSnapshot snapshot;
    const bool known = p.preparedTransport->read(snapshot);
    result.source =
        transport::admitSourceBurst(known, snapshot, p.preparedTransport->budget(), sourceFrameUpperBound);
    if (result.source.status != transport::BurstStatus::ready)
        return p.refuseTransport(PluginTransportStatus::sourceRefused, result.source.status);
    if (snapshot.callbackSerial <= p.lastBurstSerial)
        return p.refuseTransport(PluginTransportStatus::serialReused);
    const auto completions = (std::uint64_t(p.cursor) + std::uint64_t(sourceFrameUpperBound)) / p.blockSize;
    if (completions > std::numeric_limits<std::uint64_t>::max() - p.sequence)
        return p.refuseTransport(PluginTransportStatus::sequenceExhausted);
    p.burstSerial = snapshot.callbackSerial;
    p.lastBurstSerial = snapshot.callbackSerial;
    p.burstQuota = sourceFrameUpperBound;
    p.burstProcessed = 0;
    p.burstActive = true;
    result.status = PluginTransportStatus::ready; // Accepted scope, NOT a completed DSP/worker result.
    result.sourceQuota = sourceFrameUpperBound;
    return result;
}
PluginTransportOutcome PluginHost::endPhysicalBurst() noexcept
{
    auto &p = *impl;
    if (auto fault = transportOutcome(); !fault.succeeded())
        return fault;
    if (!p.preparedTransport || p.offline || !p.burstActive)
        return p.refuseTransport(PluginTransportStatus::burstMissing);
    if (!p.validatePhysicalSpan(0))
        return transportOutcome();
    p.burstActive = false;
    PluginTransportOutcome result;
    result.status = PluginTransportStatus::ready;
    result.source.status = transport::BurstStatus::ready;
    result.sourceQuota = p.burstQuota;
    result.processedFrames = p.burstProcessed;
    // Muted/pruned processors may receive FEWER samples than the source bound.
    // Closing metadata scope never asserts that all graph/worker work completed.
    return result;
}
void PluginHost::abortPhysicalBurst() noexcept
{
    auto &p = *impl;
    p.burstActive = false;
    p.burstSerial = 0;
    p.burstQuota = 0;
    p.burstProcessed = 0;
    // Deliberately retain lastBurstSerial and transportFault: closing metadata
    // cannot admit the same outer twice or turn failed audio into success.
}
PluginTransportOutcome PluginHost::transportOutcome() const noexcept
{
    const auto code = impl->transportFault.load(std::memory_order_acquire);
    PluginTransportOutcome result;
    result.status = PluginTransportStatus(unsigned(code & 0xffffffffu));
    result.source.status = transport::BurstStatus(unsigned(code >> 32));
    return result; // Immutable config + coherent latched fault; no DSP-owned counters read on CONTROL.
}
int PluginHost::transportPacketCount() const noexcept
{
    return impl->transportPackets;
}
PluginHost::FreshTransportPromotion
PluginHost::promoteFreshPreparedTransport(const PluginSpec &spec, double rate, int block,
                                          const PreparedPluginTransport::Ptr &prepared,
                                          PluginBusRequest buses) noexcept
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread())
        return FreshTransportPromotion::wrongThread;
    auto &p = *impl;
    if (!prepared || p.preparedTransport)
        return FreshTransportPromotion::invalidPlan;
    if (!p.shared || p.offline || p.retiring || p.shutdownStarted || p.terminationSent || !alive())
        return FreshTransportPromotion::workerUnavailable;
    if (!std::isfinite(rate) || rate != prepared->sampleRate() || rate != p.shared->rate ||
        block != p.blockSize || block != p.shared->blockSize || block != prepared->budget().quantum ||
        p.descriptor != spec.descriptor || p.instrument != spec.instrument || p.buses != buses ||
        p.shared->requestedBuses != buses || p.shared->negotiatedBuses != buses)
        return FreshTransportPromotion::configurationMismatch;

    // Caller has excluded ALL possible graph/process readers, not merely
    // stopped playback. These are therefore quiescent DSP-owned reads.
    // bypassInitialised is permanent after any admitted nonzero process call,
    // including a partial first packet. No new callback counter/store is needed.
    if (p.bypassInitialised || p.bypassAvailable.load(std::memory_order_acquire) || p.cursor != 0 ||
        p.sequence != 0 || p.input || p.dryHistory->hostCursor() != 0 ||
        p.emittedPacket != PluginPcmPacketKind::startup || p.emittedConsumed || p.latencyPacketFailed ||
        p.consumedRevision.load(std::memory_order_acquire) != 0 ||
        p.consumedStamp.load(std::memory_order_acquire) != 0 ||
        p.consumedSequence.load(std::memory_order_acquire) != 0 ||
        p.submitted.load(std::memory_order_acquire) != 0 ||
        p.shared->processed.load(std::memory_order_acquire) != 0 || p.observedProcessed != 0 ||
        p.shared->oldestNeeded.load(std::memory_order_acquire) != 0 ||
        p.shared->midiResetGeneration.load(std::memory_order_acquire) != 0 || p.burstActive ||
        p.burstSerial || p.lastBurstSerial || p.burstQuota || p.burstProcessed || p.midiCount ||
        p.parameterCount || p.parameterGeneration || p.parametersInvalid || deliveryFailed())
        return FreshTransportPromotion::notFresh;
    for (const auto &packet : p.shared->blocks)
        if (packet.status.load(std::memory_order_acquire) != 0)
            return FreshTransportPromotion::notFresh;

    const auto &budget = prepared->budget();
    const auto transportSamples = std::int64_t(budget.packets) * block;
    const auto native = declaredLatency();
    if (budget.packets < 2 || budget.packets > ipc::slots || transportSamples != budget.transportSamples ||
        transportSamples <= 0 || transportSamples >= Impl::dryCapacity || !native.valid(block) ||
        native.nativeSamples >= Impl::dryCapacity - transportSamples)
        return FreshTransportPromotion::unsupportedLatency;
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    const bool known = prepared->read(physical);
    const auto source = transport::admitPhysicalBurst(known, physical, budget);
    if (!known || physical.maximumOuterFrames > budget.physicalFrames ||
        (source.status != transport::BurstStatus::ready &&
         !(source.status == transport::BurstStatus::noPhysicalCallback && physical.outerFrames == 0 &&
           physical.maximumOuterFrames == 0)))
        return FreshTransportPromotion::sourceRefused;

    // All admission precedes this CONTROL-only publication. No native IPC K
    // exists: the worker's packet quantum/layout are unchanged. Initial dry
    // storage is still zero; seed its unconsumed native phase just as start().
    // Preserve every counter, native state/control/editor request and panic.
    p.consumedNativeSamples = native.nativeSamples;
    p.transportPackets = budget.packets;
    p.preparedTransport = prepared;
    return FreshTransportPromotion::promoted;
}

bool PluginHost::matchesPrepared(const PluginSpec &spec, double rate, int block,
                                 const PreparedPluginTransport::Ptr &prepared, PluginBusRequest buses) const
{
    return prepared && impl->preparedTransport == prepared && impl->shared && impl->shared->rate == rate &&
           impl->blockSize == block && impl->descriptor == spec.descriptor && impl->buses == buses && alive();
}

PluginHost::~PluginHost() = default;
juce::Result PluginHost::start(const PluginSpec &spec, double rate, int block, bool offline,
                               const std::function<bool()> &cancelled, PluginBusRequest buses)
{
    const auto startupBegan = juce::Time::getMillisecondCounterHiRes();
    if (cancelled && cancelled())
        return juce::Result::fail("Plugin preparation cancelled");
    if (impl->retiring || impl->shared || impl->mapping || impl->wake || impl->child.processId() != 0)
        return juce::Result::fail("Plugin host startup is single-use; prepare a new owned host");
    if (block < 16 || block > ipc::frames || !std::isfinite(rate) || rate < 8000 || rate > 192000)
        return juce::Result::fail("Unsupported VST3 device configuration");
    if (impl->preparedTransport)
    {
        const auto &plan = *impl->preparedTransport;
        if (rate != plan.sampleRate() || block != plan.budget().quantum)
        {
            impl->refuseTransport(PluginTransportStatus::invalidPlan);
            return juce::Result::fail("Prepared plugin transport rate/quantum mismatch");
        }
        juce::AudioDeviceManager::PhysicalCallbackSnapshot snapshot;
        const bool known = plan.read(snapshot);
        // Startup is CONTROL and need not occur during a callback. Epoch and
        // ownership must remain valid; callback serial/outer frames may be zero.
        transport::BurstStatus source = transport::BurstStatus::ready;
        if (!known)
            source = transport::BurstStatus::telemetryUnavailable;
        else if (!snapshot.ownerAlive)
            source = transport::BurstStatus::managerReleased;
        else if (snapshot.writerConflict)
            source = transport::BurstStatus::writerConflict;
        else if (snapshot.countersExhausted)
            source = transport::BurstStatus::counterExhausted;
        else if (!snapshot.active)
            source = transport::BurstStatus::deviceStopped;
        else if (snapshot.deviceEpoch != plan.budget().deviceEpoch ||
                 snapshot.configuredFrames != plan.budget().configuredFrames)
            source = transport::BurstStatus::deviceChanged;
        else if (snapshot.maximumOuterFrames > plan.budget().physicalFrames)
            source = transport::BurstStatus::physicalBurstExceeded;
        if (source != transport::BurstStatus::ready)
        {
            impl->refuseTransport(PluginTransportStatus::sourceRefused, source);
            return juce::Result::fail("Prepared plugin transport source identity is no longer admissible");
        }
    }
    if (!buses.valid() || (spec.instrument && buses.auxiliaryChannels != 0))
        return juce::Result::fail("pluginIo.invalidRequest: Unsupported VST3 main/auxiliary bus request");
    juce::PluginDescription description;
    auto xml = juce::XmlDocument::parse(spec.descriptor);
    if (!xml || !description.loadFromXml(*xml) ||
        (description.pluginFormatName != "VST3" && (!effects::isNativeDescription(description) && !synth::isNativeDescription(description))))
        return juce::Result::fail("Invalid effect or VST3 descriptor");
    if (description.isInstrument && buses.auxiliaryChannels != 0)
        return juce::Result::fail("Auxiliary input on instruments is not supported");
    if ((!effects::isNativeDescription(description) && !synth::isNativeDescription(description)) && !juce::File(description.fileOrIdentifier).exists())
        return juce::Result::fail("Missing VST3: " + spec.name);
    auto exe = pluginWorkerExecutable();
    if (!exe.existsAsFile())
        return juce::Result::fail("Missing isolated VST3 worker executable");
    juce::MemoryBlock state;
    if (!decodePluginState(spec.state, state))
        return juce::Result::fail("Invalid or oversized VST3 state");
    auto name = "Local\\VibeRa-" + juce::Uuid().toString();
    impl->wake = CreateEventW(nullptr, FALSE, FALSE, (name + "-audio").toWideCharPointer());
    if (!impl->wake)
        return juce::Result::fail("Cannot create plugin audio notification");
    impl->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                       DWORD(sizeof(ipc::Shared)), name.toWideCharPointer());
    if (!impl->mapping)
        return juce::Result::fail("Cannot create plugin shared memory");
    impl->shared = static_cast<ipc::Shared *>(
        MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ipc::Shared)));
    if (!impl->shared)
        return juce::Result::fail("Cannot map plugin shared memory");
    new (impl->shared) ipc::Shared;
    auto &shared = *impl->shared;
    shared.parentPid = GetCurrentProcessId();
    shared.instrument = spec.instrument;
    shared.rate = rate;
    shared.blockSize = block;
    shared.requestedBuses = buses;
    shared.stateSize = int(state.getSize());
    if (!state.isEmpty())
        std::memcpy(shared.state.data(), state.getData(), state.getSize());
    impl->descriptorFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getNonexistentChildFile("vibera-vst3-", ".xml");
    if (!impl->descriptorFile.replaceWithText(spec.descriptor))
        return juce::Result::fail("Cannot prepare plugin descriptor");
    if (!impl->child.start({exe.getFullPathName(), "--host", name, impl->descriptorFile.getFullPathName()},
                           0))
        return juce::Result::fail("Cannot start plugin worker");
    const auto launchCompleted = juce::Time::getMillisecondCounterHiRes();
    auto deadline = juce::Time::getMillisecondCounter() + 15000;
    while (shared.status.load(std::memory_order_acquire) == 0 && impl->child.isRunning() &&
           juce::Time::getMillisecondCounter() < deadline)
    {
        if (cancelled && cancelled())
            return juce::Result::fail("Plugin preparation cancelled");
        juce::Thread::sleep(5); // preparation only, never an audio callback
    }
    if (cancelled && cancelled())
        return juce::Result::fail("Plugin preparation cancelled");
    if (shared.status.load() != 1)
        return juce::Result::fail(error().isNotEmpty() ? error()
                                                       : "Plugin worker initialization timed out or crashed");
    const auto initialLatency = PluginLatencyStamp::unpack(shared.latency.load(std::memory_order_acquire));
    if (!impl->supportedLatency(initialLatency, block))
    {
        if (impl->transportPackets >= 2)
        {
            impl->blockSize = block; // Preserve actual rejected prepared identity for readback.
            impl->latencyRejected.fetch_add(1, std::memory_order_relaxed);
            impl->failed.store(true, std::memory_order_release);
        }
        return juce::Result::fail("VST3 latency exceeds supported delay capacity");
    }
    if (shared.negotiatedBuses != buses || !shared.busCatalogue.valid())
        return juce::Result::fail("pluginIo.invalidAcknowledgement: VST3 worker returned a different/invalid "
                                  "main or auxiliary layout");
    const auto readyCompleted = juce::Time::getMillisecondCounterHiRes();
    impl->blockSize = block;
    impl->consumedNativeSamples = initialLatency.nativeSamples;
    impl->descriptor = spec.descriptor;
    impl->recoveryState = spec.state;
    impl->offline = offline;
    impl->instrument = spec.instrument;
    impl->buses = shared.negotiatedBuses;
    impl->busCatalogue = shared.busCatalogue;
    impl->bypassFrames = std::max(1, int(std::ceil(rate * .005)));
    impl->bypassStep = 1. / impl->bypassFrames;
    impl->wetMix.prepare(impl->bypassFrames);
    // The worker publishes this slot before adding its listener/starting DSP.
    // No reader has access to this unpublished host during preparation. Keep
    // the first serial-zero snapshot separate from all later controller values.
    if (refreshParameterCatalogue() && impl->catalogue && impl->catalogue->serial == 0)
        impl->initialCatalogue = impl->catalogue;
    // This Host has not been published to any processing reader. Capture its
    // actual post-restore baseline here, so the first structural operation need
    // not suspend an already audible worker just to learn its default state.
    // Keep the existing total startup deadline and cancellation ownership.
    if (!offline)
    {
        const auto serial = requestState();
        juce::String baseline;
        bool captured = false;
        while (serial && !(captured = readState(serial, baseline)) && impl->child.isRunning() &&
               juce::Time::getMillisecondCounter() < deadline)
        {
            if (cancelled && cancelled())
                return juce::Result::fail("Plugin preparation cancelled");
            juce::Thread::sleep(2); // Unpublished preparation only, never DSP.
        }
        if (cancelled && cancelled())
            return juce::Result::fail("Plugin preparation cancelled");
        if (!captured)
            return juce::Result::fail("Initial native plugin state capture failed or timed out");
        // Membership must still prove exact restoration of the supplied spec.
        // Keep the separate actual capture for a later explicit structural read.
        impl->recoveryState = spec.state;
    }
    impl->lastProgress = juce::Time::getMillisecondCounter();
    const auto catalogueCompleted = juce::Time::getMillisecondCounterHiRes();
    impl->startup = {launchCompleted - startupBegan, readyCompleted - launchCompleted,
                     catalogueCompleted - readyCompleted, catalogueCompleted - startupBegan};
    impl->busCatalogueAvailable = true;
    return juce::Result::ok();
}
void PluginHost::editor(bool open)
{
    const auto request =
        requestEditor(runtimeIdentity(), open ? PluginEditorAction::open : PluginEditorAction::close);
    if (request.serial)
        abandonEditorRequest(runtimeIdentity(),
                             request.serial); // Old callers cannot leak retained owner receipts.
}
PluginEditorAdmission PluginHost::requestEditor(const juce::String &instance, PluginEditorAction action,
                                                std::uint64_t wrapperSelection)
{
    if (instance != runtimeIdentity())
        return {0, PluginEditorAdmissionCode::instanceChanged};
    serviceEditorRequests();
    if (!pluginEditorOpens(action) && action != PluginEditorAction::close)
        return {0, PluginEditorAdmissionCode::invalidAction};
    auto *shared = impl->shared;
    if (!shared || !alive() || impl->offline)
        return {0, PluginEditorAdmissionCode::unavailable};
    refreshParameterCatalogue(); // Drain metadata into its own cache, not another owner's receipt.
    if (!impl->catalogue ||
        impl->catalogue->serial != shared->catalogueRequest.load(std::memory_order_acquire))
        return {0, PluginEditorAdmissionCode::busy};
    if (shared->stateRequest.load(std::memory_order_acquire) !=
            shared->stateReady.load(std::memory_order_acquire) ||
        shared->parameterControl.status.load(std::memory_order_acquire) != 0)
        return {0, PluginEditorAdmissionCode::busy};
    unsigned admittedAt = 0;
    try
    {
        admittedAt = pluginEditorMilliseconds();
    }
    catch (...)
    {
        impl->failEditorClock();
        return {0, PluginEditorAdmissionCode::unavailable};
    }
    auto &wire = shared->editorControl;
    auto expected = PluginEditorPhase::idle;
    if (!wire.phase.compare_exchange_strong(expected, PluginEditorPhase::hostWriting,
                                            std::memory_order_acquire))
        return {0, PluginEditorAdmissionCode::busy};
    const auto admission = impl->editorRequests.reserve(action, admittedAt);
    if (!admission.serial)
    {
        wire.phase.store(PluginEditorPhase::idle, std::memory_order_release);
        return admission;
    }
    impl->capturedStateValid = false;  // Opening OR closing native UI invalidates opaque cache.
    impl->captureEditorClosed = false; // Also invalidate a completed, not-yet-read state capture.
    wire.serial = admission.serial;
    wire.admittedAt = admittedAt;
    wire.completedAt = 0;
    wire.action = action;
    wire.outcome = PluginEditorOutcome::invalidRequest;
    wire.window = 0;
    wire.wrapperSelection = wrapperSelection;
    wire.phase.store(PluginEditorPhase::pending, std::memory_order_release);
    return admission;
}
void PluginHost::serviceEditorRequests()
{
    auto *shared = impl->shared;
    if (!shared || impl->failed.load() || impl->retiring || impl->shutdownStarted)
    {
        // Quarantine late replies after a terminal host failure. They cannot
        // overwrite a retained timeout/cancel/failure or revive an abandoned owner.
        impl->editorRequests.failPending(PluginEditorOutcome::workerUnavailable);
        return;
    }
    auto &wire = shared->editorControl;
    auto fail = [&](PluginEditorOutcome outcome, const juce::String &reason)
    {
        impl->editorRequests.failPending(outcome);
        if (!impl->failed.exchange(true))
        {
            impl->controlError = reason;
            impl->requestShutdown();
        }
    };
    auto drainReply = [&]
    {
        auto expected = PluginEditorPhase::reply;
        if (!wire.phase.compare_exchange_strong(expected, PluginEditorPhase::hostReading,
                                                std::memory_order_acquire))
            return false;
        PluginEditorResult result{wire.serial, wire.action, wire.outcome,
                                  PluginEditorWindowState::unpack(wire.window), true};
        result.admittedAt = wire.admittedAt;
        result.completedAt = wire.completedAt;
        // Read the host observation tick AFTER acquiring the published worker
        // completion. Both clocks use the same Windows performance-counter epoch.
        unsigned observedAt = 0;
        try
        {
            observedAt = pluginEditorMilliseconds();
        }
        catch (...)
        {
            // Native work may already have happened. Release physical ownership
            // but preserve explicit failure/uncertainty; never invent a tick.
            wire.phase.store(PluginEditorPhase::idle, std::memory_order_release);
            impl->failEditorClock();
            return true;
        }
        const auto disposition = PluginEditorWindowState::validPacked(wire.window)
                                     ? impl->editorRequests.completeNative(result, observedAt)
                                     : PluginEditorReplyDisposition::invalid;
        wire.phase.store(PluginEditorPhase::idle, std::memory_order_release);
        if (disposition == PluginEditorReplyDisposition::invalid)
            fail(PluginEditorOutcome::invalidAcknowledgement,
                 "Invalid VST3 native editor acknowledgement; saved state retained (serial=" +
                     juce::String(result.serial) + ", action=" + juce::String(int(result.action)) +
                     ", native=" + juce::String(int(result.outcome)) +
                     ", window=" + juce::String(juce::uint64(wire.window)) + ", admitted=" +
                     juce::String(result.admittedAt) + ", completed=" + juce::String(result.completedAt) +
                     ", observed=" + juce::String(observedAt) + ")");
        else if (disposition == PluginEditorReplyDisposition::timeout)
            fail(PluginEditorOutcome::timeout,
                 "Plugin native editor completed after deadline; saved state retained");
        return true;
    };
    const auto phase = wire.phase.load(std::memory_order_acquire);
    if (phase == PluginEditorPhase::nativeRunning || phase == PluginEditorPhase::reply)
        impl->editorRequests.markDispatched(wire.serial);
    drainReply(); // A timely completed native request may be polled after its deadline.
    if (!alive())
        impl->editorRequests.failPending(PluginEditorOutcome::workerUnavailable);
    else
    {
        unsigned now = 0;
        try
        {
            now = pluginEditorMilliseconds();
        }
        catch (...)
        {
            impl->failEditorClock();
            return;
        }
        if (!impl->editorRequests.expired(now, pluginEditorTimeoutMs))
            return;
        // Recheck a publication racing the first poll before timing out ongoing work.
        if (drainReply())
            return;
        auto expected = PluginEditorPhase::pending;
        if (wire.phase.compare_exchange_strong(expected, PluginEditorPhase::hostWriting,
                                               std::memory_order_acquire))
            wire.phase.store(PluginEditorPhase::idle, std::memory_order_release);
        else if (expected == PluginEditorPhase::reply)
        {
            if (drainReply())
                return;
        }
        else if (expected == PluginEditorPhase::nativeRunning)
            impl->editorRequests.markDispatched(wire.serial);
        fail(PluginEditorOutcome::timeout, "Plugin native editor timed out; saved state retained");
    }
}
bool PluginHost::editorResultReady(const juce::String &instance, unsigned request)
{
    if (instance != runtimeIdentity())
        return false;
    serviceEditorRequests();
    return impl->editorRequests.ready(request);
}
bool PluginHost::readEditorResult(const juce::String &instance, unsigned request, PluginEditorResult &out)
{
    if (instance != runtimeIdentity())
        return false;
    serviceEditorRequests();
    return impl->editorRequests.consume(request, out);
}
PluginEditorCancellation PluginHost::cancelEditorRequest(const juce::String &instance, unsigned request)
{
    if (instance != runtimeIdentity())
        return PluginEditorCancellation::instanceChanged;
    serviceEditorRequests();
    if (!impl->editorRequests.known(request))
        return PluginEditorCancellation::unknownRequest;
    if (impl->editorRequests.ready(request))
        return PluginEditorCancellation::alreadyCompleted;
    if (auto *shared = impl->shared)
    {
        auto &wire = shared->editorControl;
        if (wire.serial == request)
        {
            auto expected = PluginEditorPhase::pending;
            if (wire.phase.compare_exchange_strong(expected, PluginEditorPhase::hostWriting,
                                                   std::memory_order_acquire))
            {
                const bool cancelled =
                    impl->editorRequests.cancelBeforeDispatch(request, editorWindowState());
                wire.phase.store(PluginEditorPhase::idle, std::memory_order_release);
                return cancelled ? PluginEditorCancellation::cancelled : PluginEditorCancellation::tooLate;
            }
            if (expected == PluginEditorPhase::nativeRunning || expected == PluginEditorPhase::reply)
                impl->editorRequests.markDispatched(request);
        }
    }
    return PluginEditorCancellation::tooLate;
}
void PluginHost::abandonEditorRequest(const juce::String &instance, unsigned request)
{
    if (instance != runtimeIdentity())
        return;
    serviceEditorRequests();
    impl->editorRequests.abandon(request);
}
unsigned PluginHost::pendingEditorRequests() const noexcept
{
    return impl->editorRequests.outstanding(); // Includes completed unconsumed receipts.
}
bool PluginHost::editorBusy() const
{
    return impl->shared && alive() &&
           pluginEditorPhaseBusy(impl->shared->editorControl.phase.load(std::memory_order_acquire));
}
PluginEditorWindowState PluginHost::editorWindowState() const
{
    if (!impl->shared || !alive())
        return {};
    const auto value = impl->shared->editorWindow.load(std::memory_order_acquire);
    return PluginEditorWindowState::validPacked(value) ? PluginEditorWindowState::unpack(value)
                                                       : PluginEditorWindowState{};
}
bool PluginHost::takeWrapperRequest(PluginWrapperRequest &request)
{
    if (!impl->shared || !alive()) return false;
    auto &mailbox = impl->shared->wrapperControl;
    auto expected = PluginWrapperPhase::submitted;
    if (!mailbox.phase.compare_exchange_strong(expected, PluginWrapperPhase::accepted,
                                               std::memory_order_acq_rel)) return false;
    const auto desired = mailbox.desired.load(std::memory_order_acquire);
    request = {mailbox.serial,
               {mailbox.action, mailbox.parameter, mailbox.generation, PluginWrapperMailbox::value(desired),
                PluginWrapperMailbox::gestureOpen(desired), mailbox.io}};
    return true;
}
bool PluginHost::readWrapperDesired(unsigned serial, float &value, bool &gestureOpen) const
{
    if (!impl->shared || !alive()) return false;
    const auto &mailbox = impl->shared->wrapperControl;
    if (mailbox.phase.load(std::memory_order_acquire) != PluginWrapperPhase::accepted ||
        mailbox.serial != serial) return false;
    const auto desired = mailbox.desired.load(std::memory_order_acquire);
    value = PluginWrapperMailbox::value(desired);
    gestureOpen = PluginWrapperMailbox::gestureOpen(desired);
    return true;
}
bool PluginHost::completeWrapperRequest(unsigned serial, PluginWrapperOutcome outcome,
                                         const juce::String &operation, const juce::String &code,
                                         const juce::String &message)
{
    if (!impl->shared || !alive()) return false;
    auto &mailbox = impl->shared->wrapperControl;
    if (mailbox.phase.load(std::memory_order_acquire) != PluginWrapperPhase::accepted ||
        mailbox.serial != serial) return false;
    mailbox.outcome = outcome;
    operation.copyToUTF8(mailbox.operation, sizeof(mailbox.operation));
    code.copyToUTF8(mailbox.code, sizeof(mailbox.code));
    message.copyToUTF8(mailbox.message, sizeof(mailbox.message));
    mailbox.phase.store(PluginWrapperPhase::terminal, std::memory_order_release);
    return true;
}
void PluginHost::publishWrapperState(bool ready, bool bypassed, float mix, std::uint64_t lastTweakedSelection)
{
    if (!impl->shared) return;
    auto &mailbox = impl->shared->wrapperControl;
    if (!std::isfinite(mix) || mix < 0 || mix > 1)
    { mailbox.ready.store(false, std::memory_order_release); return; }
    mailbox.bypassed.store(bypassed, std::memory_order_release);
    mailbox.mix.store(mix, std::memory_order_release);
    mailbox.lastTweakedSelection.store(lastTweakedSelection, std::memory_order_release);
    mailbox.ready.store(ready && alive(), std::memory_order_release);
}
PluginWrapperState PluginHost::wrapperState() const
{
    if (!impl->shared || !alive()) return {};
    const auto &mailbox = impl->shared->wrapperControl;
    const auto phase = mailbox.phase.load(std::memory_order_acquire);
    return {mailbox.ready.load(std::memory_order_acquire), impl->shared->instrument,
            mailbox.bypassed.load(std::memory_order_acquire),
            phase != PluginWrapperPhase::idle && phase != PluginWrapperPhase::terminal,
            mailbox.mix.load(std::memory_order_acquire), mailbox.view.load(std::memory_order_acquire),
            parameterNotificationsEnabled(), mailbox.lastTweakedSelection.load(std::memory_order_acquire)};
}
std::uint64_t PluginHost::wrapperSelection() const
{
    return impl->shared && alive() ? impl->shared->wrapperControl.selection.load(std::memory_order_acquire)
                                   : 0;
}
PluginNativeMeterReading PluginHost::nativeOutputMeter() const noexcept
{
    if (!impl->shared || !alive())
        return {};
    return impl->shared->nativeOutputMeter.read();
}
std::optional<float> PluginHost::wetBlend() const noexcept
{
    return impl->bypassAvailable.load(std::memory_order_acquire)
               ? std::optional<float>{impl->wetReadback.load(std::memory_order_relaxed)}
               : std::nullopt;
}
void PluginHost::resetNotes() noexcept
{
    if (impl->shared)
        impl->shared->panic.store(1);
}
unsigned PluginHost::midiResetGeneration() const noexcept
{
    return impl->shared ? impl->shared->midiResetGeneration.load() : 0;
}
bool PluginHost::offline() const noexcept
{
    return impl->offline;
}
PluginBusRequest PluginHost::busLayout() const noexcept
{
    return impl->buses;
}
bool PluginHost::readBusCatalogue(PluginBusCatalogue &output) const
{
    if (!impl->busCatalogueAvailable)
        return false;
    output = impl->busCatalogue;
    return true;
}
int PluginHost::bypassTransitionSamples() const noexcept
{
    return impl->bypassFrames;
}
std::optional<float> PluginHost::bypassBlend() const noexcept
{
    if (!impl->bypassAvailable.load(std::memory_order_acquire))
        return {};
    return impl->bypassReadback.load(std::memory_order_relaxed);
}
bool PluginHost::matches(const PluginSpec &spec, double rate, int block, PluginBusRequest buses) const
{
    return !impl->preparedTransport && impl->shared && impl->shared->rate == rate &&
           impl->blockSize == block && impl->descriptor == spec.descriptor && impl->buses == buses && alive();
}
juce::uint32 PluginHost::processId() const
{
    return impl->child.processId();
}
unsigned PluginHost::requestState()
{
    if (!alive() || editorBusy())
        return 0; // Refusal is never an omitted successful capture.
    if (!impl->shared)
        return 0;
    impl->captureGeneration = impl->shared->catalogueGeneration.load(std::memory_order_acquire);
    impl->captureEditorClosed = !editorWindowState().exists;
    impl->captureSerial = impl->shared->stateRequest.fetch_add(1) + 1;
    return impl->captureSerial;
}
unsigned PluginHost::requestParameterCatalogue()
{
    if (!alive() || editorBusy())
        return 0;
    return impl->shared ? impl->shared->catalogueRequest.fetch_add(1) + 1 : 0;
}
bool PluginHost::refreshParameterCatalogue()
{
    auto *shared = impl->shared;
    if (!shared)
        return false;
    auto &source = shared->catalogue;
    int expected = 2;
    if (!source.status.compare_exchange_strong(expected, 3, std::memory_order_acquire))
        return false;
    struct Release
    {
        ipc::ParameterCatalogue &data;
        ~Release()
        {
            data.status.store(0, std::memory_order_release);
        }
    } release{source};
    impl->catalogue.reset(); // Never reuse a previous snapshot after malformed input.
    if (source.count < 0 || source.count > ipc::parameters || source.reportedCount < source.count ||
        source.error < 0 || source.error > 3 || source.generation == 0 ||
        (source.error == 0 && source.count != source.reportedCount) ||
        (source.error != 0 && source.count != 0))
        return false;
    PluginParameterCatalogue next;
    next.serial = source.serial;
    next.generation = source.generation;
    next.reportedCount = source.reportedCount;
    next.error = source.error;
    if (!std::memchr(source.diagnostic, 0, sizeof(source.diagnostic)) ||
        !juce::CharPointer_UTF8::isValidString(source.diagnostic, int(sizeof(source.diagnostic))))
        return false;
    next.diagnostic = juce::String::fromUTF8(source.diagnostic);
    std::set<std::uint32_t> ids;
    for (int i = 0; i < source.count; ++i)
    {
        const auto &p = source.entries[size_t(i)];
        if (!std::memchr(p.name, 0, sizeof(p.name)) || !std::memchr(p.label, 0, sizeof(p.label)) ||
            !juce::CharPointer_UTF8::isValidString(p.name, int(sizeof(p.name))) ||
            !juce::CharPointer_UTF8::isValidString(p.label, int(sizeof(p.label))) ||
            !std::isfinite(p.value) || p.value < 0 || p.value > 1 || !std::isfinite(p.defaultValue) ||
            p.defaultValue < 0 || p.defaultValue > 1 || p.stepCount < 0 || (p.availability & ~3u) != 0 ||
            !ids.insert(p.id).second)
            return false;
        next.parameters.push_back({p.id, p.flags, p.availability, p.stepCount, p.unit, p.value,
                                   p.defaultValue, juce::String::fromUTF8(p.name),
                                   juce::String::fromUTF8(p.label)});
    }
    impl->catalogue = std::move(next);
    return true;
}
bool PluginHost::readParameterCatalogue(PluginParameterCatalogue &out)
{
    refreshParameterCatalogue();
    const auto &cached = impl->catalogue;
    if (!cached || !alive() ||
        cached->generation != impl->shared->catalogueGeneration.load(std::memory_order_acquire) ||
        (out.generation == cached->generation && out.serial == cached->serial))
        return false;
    auto copy = *cached; // Allocation/copy and reclamation are control-thread work.
    if (copy.generation != impl->shared->catalogueGeneration.load(std::memory_order_acquire))
        return false;
    out = std::move(copy);
    return true;
}
bool PluginHost::readInitialParameterCatalogue(PluginParameterCatalogue &out) const
{
    const auto &initial = impl->initialCatalogue;
    if (!initial || initial->error || !alive() ||
        initial->generation != impl->shared->catalogueGeneration.load(std::memory_order_acquire))
        return false;
    auto copy = *initial; // Prepared/control work only, never called by DSP.
    if (copy.generation != impl->shared->catalogueGeneration.load(std::memory_order_acquire))
        return false;
    out = std::move(copy);
    return true;
}
PluginHost::StartupDiagnostics PluginHost::startupDiagnostics() const noexcept
{
    return impl->startup;
}
PluginBindingAdmission PluginHost::validateAutomationBindings(std::span<const PluginBinding> bindings)
{
    PluginParameterCatalogue snapshot;
    if (!readParameterCatalogue(snapshot))
        return {};
    return admitPluginBindings(bindings, snapshot,
                               impl->shared->catalogueGeneration.load(std::memory_order_acquire));
}
unsigned PluginHost::setParameter(std::uint32_t id, float value, unsigned generation)
{
    if (editorBusy())
        return 0;
    auto *shared = impl->shared;
    if (!shared || !alive() || generation == 0 || !std::isfinite(value) || value < 0 || value > 1)
        return 0;
    auto &request = shared->parameterControl;
    int expected = 0;
    if (!request.status.compare_exchange_strong(expected, -1, std::memory_order_acquire))
        return 0;
    request.serial = impl->controlRequests.reserve(id, juce::Time::getMillisecondCounter());
    if (!request.serial)
    {
        request.status.store(0, std::memory_order_release);
        return 0;
    }
    request.generation = generation;
    request.id = id;
    request.requested = value;
    request.result = 0;
    request.previousValue = 0;
    request.beforeKnown = 0;
    request.patchCount = 0;
    request.status.store(1, std::memory_order_release);
    return request.serial;
}
unsigned PluginHost::setParameterPatch(std::span<const std::pair<std::uint32_t,float>> values, unsigned generation)
{
    if(values.empty() || values.size()>ipc::patchValues || editorBusy()) return 0;
    auto *shared=impl->shared;
    if(!shared || !alive() || generation==0) return 0;
    for(std::size_t i=0;i<values.size();++i)
    {
        if(!std::isfinite(values[i].second) || values[i].second<0 || values[i].second>1) return 0;
        for(std::size_t j=0;j<i;++j) if(values[j].first==values[i].first) return 0;
    }
    auto &request=shared->parameterControl;
    int expected=0;
    if(!request.status.compare_exchange_strong(expected,-1,std::memory_order_acquire)) return 0;
    request.serial=impl->controlRequests.reserve(values.front().first,juce::Time::getMillisecondCounter());
    if(!request.serial) {request.status.store(0,std::memory_order_release);return 0;}
    request.generation=generation;request.id=values.front().first;request.requested=values.front().second;
    request.result=0;request.previousValue=0;request.beforeKnown=0;request.patchCount=unsigned(values.size());
    for(std::size_t i=0;i<values.size();++i) request.patch[i]={values[i].first,values[i].second,0};
    request.status.store(1,std::memory_order_release);
    return request.serial;
}
void PluginHost::serviceParameterRequests()
{
    auto *shared = impl->shared;
    if (!shared)
        return;
    const auto fail = [&](int outcome, const juce::String &reason)
    {
        impl->controlRequests.failPending(outcome);
        if (!impl->failed.exchange(true))
        {
            impl->controlError = reason;
            impl->requestShutdown();
        }
    };
    auto &result = shared->parameterControl;
    int expected = 2;
    if (result.status.compare_exchange_strong(expected, 3, std::memory_order_acquire))
    {
        const bool validBefore =
            (result.beforeKnown == 0 || result.beforeKnown == 1) &&
            (!result.beforeKnown || (result.result == 0 && std::isfinite(result.previousValue) &&
                                     result.previousValue >= 0 && result.previousValue <= 1));
        bool validPatch=result.patchCount<=result.patch.size();
        for(unsigned i=0;validPatch && i<result.patchCount;++i)
        {
            const auto &entry=result.patch[i];
            validPatch=std::isfinite(entry.value) && entry.value>=0 && entry.value<=1 &&
                std::isfinite(entry.previousValue) && entry.previousValue>=0 && entry.previousValue<=1;
            for(unsigned j=0;j<i;++j) if(result.patch[j].id==entry.id) validPatch=false;
        }
        PluginParameterResult reply{result.serial,result.id,result.actual,result.result,result.previousValue,result.beforeKnown==1};
        reply.patchCount=result.patchCount;reply.patch=result.patch;
        const bool valid=validBefore && validPatch && impl->controlRequests.complete(reply);
        result.status.store(0, std::memory_order_release);
        if (!valid)
            fail(5, "Invalid VST3 control acknowledgement; saved state retained");
    }
    if (!alive())
        impl->controlRequests.failPending(4);
    else if (impl->controlRequests.expired(juce::Time::getMillisecondCounter(), 2000))
        fail(6, "Plugin control timed out; saved state retained");
}
bool PluginHost::readParameterResult(unsigned request, PluginParameterResult &out)
{
    serviceParameterRequests();
    return impl->controlRequests.consume(request, out);
}
bool PluginHost::parameterResultReady(unsigned request)
{
    serviceParameterRequests();
    return impl->controlRequests.ready(request);
}
void PluginHost::abandonParameterRequest(unsigned request)
{
    serviceParameterRequests();
    impl->controlRequests.abandon(request);
}
unsigned PluginHost::pendingParameterRequests() const noexcept
{
    return impl->controlRequests.outstanding();
}
bool PluginHost::readState(unsigned serial, juce::String &base64, unsigned *revision)
{
    auto *s = impl->shared;
    if (!s || serial == 0 || impl->captureSerial != serial ||
        s->stateRequest.load(std::memory_order_acquire) != serial ||
        s->stateReady.load(std::memory_order_acquire) != serial || s->stateSize < 0 ||
        s->stateSize > ipc::stateBytes)
        return false;
    juce::MemoryBlock state(s->state.data(), size_t(s->stateSize));
    base64 = state.toBase64Encoding();
    impl->recoveryState = base64;
    impl->capturedState = base64;
    impl->capturedSerial = serial;
    impl->capturedRevision = s->stateParameterRevision;
    impl->capturedGeneration = impl->captureGeneration;
    impl->capturedStateValid =
        impl->captureSerial == serial && impl->captureEditorClosed && !editorWindowState().exists &&
        impl->capturedGeneration == s->catalogueGeneration.load(std::memory_order_acquire);
    if (revision)
        *revision = s->stateParameterRevision;
    return true;
}
bool PluginHost::reuseCapturedState(juce::String &base64, unsigned &revision)
{
    const auto *shared = impl->shared;
    if (!shared || !impl->capturedStateValid || !alive() || editorBusy() || editorWindowState().exists ||
        pendingParameterRequests() || impl->capturedSerial != impl->captureSerial ||
        shared->stateRequest.load(std::memory_order_acquire) !=
            shared->stateReady.load(std::memory_order_acquire) ||
        impl->capturedRevision != parameterRevision() ||
        impl->capturedGeneration != shared->catalogueGeneration.load(std::memory_order_acquire))
        return false;
    base64 = impl->capturedState;
    impl->recoveryState = base64;
    revision = impl->capturedRevision;
    return true;
}
unsigned PluginHost::stateCaptureSerial() const noexcept
{
    return impl->shared ? impl->shared->stateRequest.load(std::memory_order_acquire) : 0;
}
const juce::String &PluginHost::recoveryState() const noexcept
{
    return impl->recoveryState;
}
const juce::String &PluginHost::runtimeIdentity() const noexcept
{
    return impl->runtimeIdentity;
}
unsigned PluginHost::parameterRevision() const noexcept
{
    return impl->shared ? impl->shared->parameterRevision.load() : 0;
}
std::optional<PluginGesture> PluginHost::lastParameterGesture()
{
    auto *shared = impl->shared;
    if (!shared || !alive())
        return {};
    const auto generation = shared->catalogueGeneration.load(std::memory_order_acquire);
    const auto epoch = shared->gestureEpoch.load(std::memory_order_acquire);
    const auto lost = shared->gestures.dropped.load(std::memory_order_acquire);
    if (impl->completedGeneration != generation || impl->completedEpoch != epoch)
    {
        if (impl->completedGeneration)
            impl->completedGestures.invalidate();
        impl->completedGeneration = generation;
        impl->completedEpoch = epoch;
    }
    impl->gestures.synchronize(generation, epoch, lost);
    PluginGestureEvent event;
    if (impl->gestureLossSeen != lost)
    {
        impl->completedGestures.invalidate(lost - impl->gestureLossSeen);
        impl->gestureLossSeen = lost;
        for (int i = 0; i < 256 && shared->gestures.pop(event); ++i)
        {
        }
        impl->gestures.reset();
        return {}; // Loss quarantines the complete queued prefix.
    }
    // Bound work even if a nonconforming plugin sends continuous notifications.
    bool overflow = false;
    for (int i = 0; i < 256 && shared->gestures.pop(event); ++i)
    {
        const auto invalid = impl->gestures.invalidatedCompletions();
        const auto completed = impl->gestures.consume(event);
        if (impl->gestures.invalidatedCompletions() != invalid)
            impl->completedGestures.invalidate();
        if (completed && parameterNotificationsEnabled())
        {
            if (overflow)
                impl->completedGestures.invalidate();
            else
                overflow = !impl->completedGestures.push(*completed);
        }
    }
    if (lost != shared->gestures.dropped.load(std::memory_order_acquire))
    {
        impl->gestures.reset();
        impl->completedGestures.invalidate();
    }
    return parameterNotificationsEnabled() ? impl->gestures.latest() : std::nullopt;
}
bool PluginHost::popCompletedParameterGesture(PluginGesture &output)
{
    lastParameterGesture(); // The same sole IPC drain, never a second consumer.
    if (!alive() || !parameterNotificationsEnabled())
        return false;
    return impl->completedGestures.pop(output);
}
std::uint64_t PluginHost::completedParameterGestureLosses() const noexcept
{
    const auto retained = impl->completedGestures.losses();
    const auto pending = impl->shared
                             ? impl->shared->gestures.dropped.load(std::memory_order_acquire) -
                                   impl->gestureLossSeen
                             : 0;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return pending > maximum - retained ? maximum : retained + pending;
}
bool PluginHost::parameterNotificationsEnabled() const
{
    return impl->shared && impl->shared->gestureNotifications.load(std::memory_order_acquire);
}
void PluginHost::setParameterNotificationsEnabled(bool enabled)
{
    if (auto *shared = impl->shared; shared && parameterNotificationsEnabled() != enabled)
    {
        shared->gestureNotifications.store(enabled, std::memory_order_release);
        shared->gestureEpoch.fetch_add(1, std::memory_order_acq_rel);
        impl->gestures.reset();
        impl->completedGestures.invalidate();
    }
}
std::uint64_t PluginHost::droppedParameterGestures() const noexcept
{
    return impl->shared ? impl->shared->gestures.dropped.load(std::memory_order_acquire) : 0;
}
void PluginHost::service()
{
    auto &p = *impl;
    if (!p.shared)
        return;
    serviceParameterRequests();
    serviceEditorRequests();
    lastParameterGesture(); // Drain native notifications even while its inspector is closed.
    auto now = juce::Time::getMillisecondCounter();
    auto completed = p.shared->processed.load();
    if (completed != p.observedProcessed)
    {
        p.observedProcessed = completed;
        p.lastProgress = now;
    }
    // An idle transport owes no packets. Start the timeout budget with pending
    // work, rather than treating time spent stopped as a stalled processor.
    if (p.submitted.load() <= completed)
        p.lastProgress = now;
    if (!p.child.isRunning())
    {
        p.failed = true;
        if (p.controlError.isEmpty())
            p.controlError = "Plugin worker exited; saved state retained";
        return;
    }
    if (p.shutdownStarted)
    {
        if (!p.terminationSent && now - *p.shutdownStarted >= 2000)
            p.terminationSent = p.child.kill(); // Exact owned handle; process may exit on a later tick.
        return;
    }
    if (p.transportFault.load(std::memory_order_acquire) != 0)
    {
        p.controlError = "Prepared plugin transport admission failed; saved state retained";
        p.requestShutdown(); // CONTROL-owned containment; never terminate/release a worker in DSP.
        p.failed = true;
        return;
    }
    const auto reportedLatency = declaredLatency();
    if (!p.supportedLatency(reportedLatency, p.blockSize) ||
        p.latencyRejected.load(std::memory_order_relaxed) != 0)
    {
        p.controlError = "Plugin latency exceeds supported delay capacity; saved state retained";
        p.requestShutdown();
        p.failed = true;
        return;
    }
    if (p.submitted.load() > completed && now - p.lastProgress > 2000)
    {
        const auto diagnostics = statistics();
        p.controlError = "Plugin processing timed out; saved state retained (phase=" +
                         juce::String(ipc::dspPhaseName(diagnostics.workerPhase)) +
                         ", processed=" + juce::String(diagnostics.processed) +
                         ", heartbeat=" + juce::String(diagnostics.workerHeartbeat) + ")";
        p.requestShutdown();
        p.failed = true;
    }
}
void PluginHost::beginRetirement()
{
    serviceEditorRequests();
    impl->retiring = true;
    impl->failed = true;
    impl->controlRequests.failPending(4);
    impl->editorRequests.failPending(PluginEditorOutcome::workerUnavailable);
    impl->requestShutdown();
}
bool PluginHost::retirementComplete() const
{
    return impl->retiring && impl->child.waitForProcessToFinish(0);
}
int PluginHost::latencySamples() const noexcept
{
    if (!impl->shared)
        return 0;
    if (impl->preparedTransport && impl->transportFault.load(std::memory_order_acquire) != 0)
        return -1;
    const auto stamp = declaredLatency();
    if (impl->transportPackets >= 2 && impl->latencyRejected.load(std::memory_order_relaxed) != 0)
        return -1;
    if (impl->transportPackets == 1)
        return impl->blockSize + juce::jlimit(0, 131071 - impl->blockSize, stamp.nativeSamples);
    // -1 is explicit unsupported/unavailable, not a plausible clamped delay.
    // service() contains failure and preserved consumed dry epoch remains in DSP.
    return impl->supportedLatency(stamp, impl->blockSize) ? transportLatencySamples() + stamp.nativeSamples
                                                          : -1;
}
PluginLatencyStamp PluginHost::declaredLatency() const noexcept
{
    return impl->shared ? PluginLatencyStamp::unpack(impl->shared->latency.load(std::memory_order_acquire))
                        : PluginLatencyStamp{};
}
std::optional<PluginConsumedLatency> PluginHost::consumedLatency() const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = impl->consumedRevision.load();
        if (before == 0)
            return {};
        if ((before & 1) != 0)
            continue;
        const auto stamp = impl->consumedStamp.load();
        const auto sequence = impl->consumedSequence.load();
        if (impl->consumedRevision.load() == before)
            return PluginConsumedLatency{PluginLatencyStamp::unpack(stamp), sequence};
    }
    return {};
}
int PluginHost::processingQuantum() const noexcept
{
    return impl->blockSize;
}
int PluginHost::transportLatencySamples() const noexcept
{
    return impl->shared ? impl->transportPackets * impl->blockSize : 0;
}
int PluginHost::maximumLiveSpan() const noexcept
{
    return impl->preparedTransport       ? impl->preparedTransport->budget().sourceFrames
           : impl->transportPackets == 2 ? impl->blockSize
                                         : std::numeric_limits<int>::max();
}
int PluginHost::packetFrame() const noexcept
{
    return impl->cursor;
}
juce::String PluginHost::error() const
{
    if (impl->transportFault.load(std::memory_order_acquire) != 0)
        return "Prepared plugin transport admission failed; saved state retained";
    if (impl->editorClockFailed)
        return "Native editor performance clock unavailable; native outcome is unknown; saved state retained";
    if (impl->controlError.isNotEmpty())
        return impl->controlError;
    if (impl->shared && impl->shared->status.load(std::memory_order_acquire) == -1)
        return juce::String::fromUTF8(impl->shared->error);
    if (impl->latencyRejected.load(std::memory_order_relaxed) != 0)
        return "Invalid completed plugin latency packet; saved state retained";
    if (impl->shared && impl->transportPackets >= 2 &&
        !impl->supportedLatency(declaredLatency(), impl->blockSize))
        return impl->preparedTransport ? "Plugin native latency plus prepared KQ exceeds supported delay "
                                         "capacity; saved state retained"
                                       : "Plugin native latency plus prepared2Q turnaround exceeds supported "
                                         "delay capacity; saved "
                                         "state retained";
    return impl->auxiliaryRejected.load() != 0
               ? "Invalid auxiliary input buffer; detector samples replaced by silence"
               : juce::String();
}
bool PluginHost::alive() const
{
    return impl->shared && !impl->failed.load() &&
           impl->transportFault.load(std::memory_order_acquire) == 0 && impl->shared->status.load() == 1 &&
           (impl->transportPackets == 1 || impl->supportedLatency(declaredLatency(), impl->blockSize)) &&
           impl->child.isRunning();
}
PluginHost::Statistics PluginHost::statistics() const noexcept
{
    Statistics result{impl->submitted.load(), impl->missed.load(), impl->dropped.load(),
                      impl->overflow.load()};
    result.auxiliaryRejected = impl->auxiliaryRejected.load();
    if (impl->shared)
    {
        result.processed = impl->shared->processed.load(std::memory_order_acquire);
        result.workerHeartbeat = impl->shared->dspHeartbeat.load(std::memory_order_acquire);
        result.workerPhase = impl->shared->dspPhase.load(std::memory_order_acquire);
        result.parametersApplied = impl->shared->parameterEventsApplied.load();
        result.parametersRejected = impl->shared->parameterEventsRejected.load();
    }
    return result;
}
void PluginHost::recordAutomationStatus(AutomationStatus status) noexcept
{
    const auto index = unsigned(status);
    if (index > 0 && index <= impl->automationFailures.size())
        impl->automationFailures[index - 1].fetch_add(1, std::memory_order_relaxed);
    impl->automationLast.store(status, std::memory_order_relaxed);
}
PluginHost::AutomationStatistics PluginHost::automationStatistics() const noexcept
{
    return {impl->automationLast.load(std::memory_order_relaxed),
            impl->automationFailures[0].load(std::memory_order_relaxed),
            impl->automationFailures[1].load(std::memory_order_relaxed),
            impl->automationFailures[2].load(std::memory_order_relaxed),
            impl->automationFailures[3].load(std::memory_order_relaxed)};
}
bool PluginHost::deliveryFailed() const noexcept
{
    const auto s = statistics();
    return s.missed != 0 || s.dropped != 0 || s.midiOverflow != 0 || s.parametersRejected != 0 ||
           s.auxiliaryRejected != 0 || impl->latencyRejected.load(std::memory_order_relaxed) != 0 ||
           impl->transportFault.load(std::memory_order_acquire) != 0 || automationStatistics().failed();
}
PluginPcmIdentity PluginHost::pcmIdentity() const noexcept
{
    const auto &p = *impl;
    PluginPcmIdentity result;
    std::copy_n(p.runtimeUuid.getRawData(), result.instance.size(), result.instance.begin());
    result.host = reinterpret_cast<std::uintptr_t>(this);
    result.preparedTransport = reinterpret_cast<std::uintptr_t>(p.preparedTransport.get());
    result.quantum = p.blockSize;
    result.transportSamples = p.shared ? p.transportPackets * p.blockSize : 0;
    return result;
}
bool PluginHost::dryHistoryCountersPermit(const Statistics &stats, const AutomationStatistics &automation,
                                          std::uint64_t rejectedLatency, std::uint64_t sourceFault) noexcept
{
    // A historical PCM miss/drop remains diagnostic/export failure. It does
    // not invalidate a healthy Host's CONTROL ring pin after structural work.
    // Actual selected DSP continuity and dry ownership are still checked later.
    return stats.midiOverflow == 0 && stats.parametersRejected == 0 && stats.auxiliaryRejected == 0 &&
           rejectedLatency == 0 && sourceFault == 0 && !automation.failed();
}
std::optional<PluginHost::DryHistoryPreparation> PluginHost::prepareDryHistoryRetention() const
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || !impl->shared || impl->instrument ||
        impl->retiring || !alive())
        return {};
    if (!dryHistoryCountersPermit(statistics(), automationStatistics(),
                                  impl->latencyRejected.load(std::memory_order_relaxed),
                                  impl->transportFault.load(std::memory_order_acquire)))
        return {};
    return DryHistoryPreparation{impl->dryHistory, pcmIdentity(), impl.get()};
}
PluginPcmPreflight PluginHost::preflightProcess(const juce::AudioBuffer<float> &buffer, int offset, int count,
                                                float wetTarget,
                                                const PluginPcmCapture *capture) const noexcept
{
    const auto &p = *impl;
    PluginPcmPreflight result;
    if (capture)
        result.identity = pcmIdentity();
    result.status = p.inspectProcessInput(buffer, offset, count, wetTarget);
    if (result.status == PluginPcmReceiptStatus::empty)
    {
        if (capture)
        {
            if (p.blockSize <= 0 || p.cursor < 0 || p.cursor >= p.blockSize)
            {
                result.status = PluginPcmReceiptStatus::invalidSpan;
                return result;
            }
            // Promote BEFORE adding, including a multi-Q/offline whole span.
            result.required =
                count == 0
                    ? 0
                    : std::size_t(1 + (std::int64_t(p.cursor) + std::int64_t(count) - 1) / p.blockSize);
            if (capture->expectedIdentity && *capture->expectedIdentity != result.identity)
            {
                result.status = PluginPcmReceiptStatus::identityMismatch;
                return result;
            }
            if (result.required > capture->storage.size())
            {
                result.status = PluginPcmReceiptStatus::capacityExceeded;
                return result;
            }
            if (result.required > 0)
            {
                const auto begin = reinterpret_cast<std::uintptr_t>(capture->storage.data());
                const auto maximum = std::numeric_limits<std::uintptr_t>::max();
                if (!begin || begin % alignof(PluginPcmSegment) != 0 ||
                    capture->storage.size() > maximum / sizeof(PluginPcmSegment) ||
                    begin > maximum - capture->storage.size() * sizeof(PluginPcmSegment))
                {
                    result.status = PluginPcmReceiptStatus::invalidStorage;
                    return result;
                }
                const auto end = begin + capture->storage.size() * sizeof(PluginPcmSegment);
                // Real writable disjoint storage is still a caller contract.
                // Reject detectable alias with any destination channel span.
                for (int c = 0; c < buffer.getNumChannels(); ++c)
                {
                    const auto audio = reinterpret_cast<std::uintptr_t>(buffer.getReadPointer(c, offset));
                    if (std::uintptr_t(count) > maximum / sizeof(float))
                    {
                        result.status = PluginPcmReceiptStatus::invalidStorage;
                        return result;
                    }
                    const auto bytes = std::uintptr_t(count) * sizeof(float);
                    if (audio > maximum - bytes || (begin < audio + bytes && audio < end))
                    {
                        result.status = PluginPcmReceiptStatus::invalidStorage;
                        return result;
                    }
                }
            }
        }
        if (!p.offline && p.preparedTransport && count > 0)
        {
            result.transport = p.inspectPhysicalSpan(count);
            if (!result.transport.succeeded())
            {
                result.status = PluginPcmReceiptStatus::transportRefused;
                return result;
            }
        }
        result.admitted = true;
    }
    return result;
}
void PluginHost::setBlockPpq(double beat, double seconds) noexcept
{
    impl->ppqAnchor = std::isfinite(beat) && beat >= 0 ? beat : -1.;
    impl->ppqAnchorSeconds = seconds;
}
void PluginHost::process(juce::AudioBuffer<float> &buffer, int offset, int count,
                         const ipc::MidiEvent *events, int numEvents, double seconds, double tempo,
                         bool playing, bool bypass, int numerator, int denominator,
                         const ipc::ParameterEvent *parameters, int parameterCount,
                         unsigned parameterGeneration, bool parameterBatchValid,
                         const juce::AudioBuffer<float> *auxiliary, int auxiliaryOffset, float wetTarget,
                         PluginPcmCapture *capture) noexcept
{
    auto &p = *impl;
    if (capture)
    {
        const auto admission = preflightProcess(buffer, offset, count, wetTarget, capture);
        capture->status = admission.status;
        capture->count = 0;
        capture->required = admission.required;
        capture->identity = admission.identity;
        if (!admission.admitted)
        {
            if (admission.status == PluginPcmReceiptStatus::invalidSpan ||
                admission.status == PluginPcmReceiptStatus::invalidMix)
                recordAutomationStatus(AutomationStatus::invalidTiming);
            else if (admission.status == PluginPcmReceiptStatus::workerUnavailable && p.instrument)
                buffer.clear(offset, count);
            else if (admission.status == PluginPcmReceiptStatus::transportRefused)
                p.refuseTransport(admission.transport.status, admission.transport.source.status);
            else if (admission.status == PluginPcmReceiptStatus::historyDetached)
                p.refuseTransport(PluginTransportStatus::sourceRefused);
            return; // Direct Host retains its sticky refusal after earlier original checks.
        }
    }
    else
    {
        const auto status = p.inspectProcessInput(buffer, offset, count, wetTarget);
        if (status == PluginPcmReceiptStatus::invalidSpan || status == PluginPcmReceiptStatus::invalidMix)
        {
            recordAutomationStatus(AutomationStatus::invalidTiming);
            return;
        }
        if (status == PluginPcmReceiptStatus::historyDetached)
        {
            p.refuseTransport(PluginTransportStatus::sourceRefused); // Source fault, not worker shutdown.
            return; // No input/cursor/MIDI/parameter/ramp or ring mutation.
        }
        if (status == PluginPcmReceiptStatus::workerUnavailable)
        {
            if (p.instrument)
                buffer.clear(offset, count);
            return;
        }
        if (!p.offline && p.preparedTransport && count > 0 && !p.validatePhysicalSpan(count))
            return; // No new identity/storage work on the existing null path.
    }
    if (count > 0)
        p.wetMix.setTarget(wetTarget);
    const bool failed = p.failed.load() || p.shared->status.load(std::memory_order_acquire) != 1;
    bypass = bypass || failed;
    if (count > 0 && (!p.bypassInitialised || failed))
    {
        // Initial project state has no startup fade. A failed worker must leave
        // its output immediately; only healthy runtime toggles crossfade.
        p.bypassMix = bypass ? 1. : 0.;
        p.bypassInitialised = true;
    }
    const bool validAuxiliary = auxiliary && p.buses.auxiliaryChannels != 0 && auxiliaryOffset >= 0 &&
                                auxiliaryOffset <= auxiliary->getNumSamples() && count >= 0 &&
                                count <= auxiliary->getNumSamples() - auxiliaryOffset &&
                                auxiliary->getNumChannels() >= p.buses.auxiliaryChannels;
    if (auxiliary && !validAuxiliary)
        p.auxiliaryRejected.fetch_add(1, std::memory_order_relaxed);
    // Bound the callback work and reject a malformed batch atomically, including unreachable offsets.
    bool validParameters =
        parameterBatchValid && parameterCount >= 0 && parameterCount <= ipc::parameterEvents &&
        (parameterCount == 0 ||
         (parameters != nullptr && parameterGeneration != 0 &&
          parameterGeneration == p.shared->catalogueGeneration.load(std::memory_order_acquire)));
    if (validParameters)
        for (int i = 0; i < parameterCount; ++i)
        {
            const auto &e = parameters[i];
            if (e.frame < 0 || e.frame >= count || (i > 0 && e.frame < parameters[i - 1].frame) ||
                !std::isfinite(e.value) || e.value < 0 || e.value > 1)
            {
                validParameters = false;
                break;
            }
        }
    if (!validParameters)
    {
        p.shared->parameterEventsRejected.fetch_add(std::uint64_t(std::max(1, parameterCount)),
                                                    std::memory_order_relaxed);
        parameterCount = 0;
    }
    int event = 0;
    int parameter = 0;
    for (int frame = 0; frame < count; ++frame)
    {
        if (p.cursor == 0)
            p.begin(seconds + frame / p.shared->rate, tempo, playing, numerator, denominator);
        if (p.latencyPacketFailed)
        {
            bypass = true;
            p.bypassMix = 1.; // Invalid packets never enter a healthy transition.
        }
        if (!validParameters && !p.parametersInvalid)
            p.rejectParameters(); // Invalidate an earlier partial prefix of this packet too.
        while (event < numEvents && events[event].frame <= frame)
        {
            if (p.midiCount < ipc::events)
            {
                auto &e = p.midi[size_t(p.midiCount++)];
                e = events[event];
                e.frame = p.cursor;
            }
            else
                ++p.overflow;
            ++event;
        }
        while (parameters && parameter < parameterCount && parameters[parameter].frame <= frame)
        {
            const auto &e = parameters[parameter++];
            if (p.parametersInvalid || p.parameterCount >= ipc::parameterEvents ||
                (p.parameterGeneration != 0 && p.parameterGeneration != parameterGeneration))
                p.rejectParameters(1);
            else
            {
                p.parameterGeneration = parameterGeneration;
                auto copy = e;
                copy.frame = p.cursor;
                p.parameters[size_t(p.parameterCount++)] = copy;
            }
        }
        p.bypassMix =
            bypass ? std::min(1., p.bypassMix + p.bypassStep) : std::max(0., p.bypassMix - p.bypassStep);
        const float mix = float(p.bypassMix), wet = p.wetMix.next();
        const auto dryDelay =
            p.transportPackets * p.blockSize + p.consumedNativeSamples; // This packet's wet/dry epoch.
        if (capture)
        {
            const auto kind = mix == 1 || wet == 0   ? PluginPcmMixKind::delayedDry
                              : mix == 0 && wet == 1 ? PluginPcmMixKind::wet
                                                     : PluginPcmMixKind::mixed;
            if (frame == 0 || p.cursor == 0)
            {
                auto &segment = capture->storage[capture->count++];
                segment = {};
                segment.offset = frame;
                segment.packetFrame = p.cursor;
                segment.outputPacket = p.sequence;
                segment.packet = p.emittedPacket;
                segment.consumed = p.emittedConsumed;
                segment.dryDelaySamples = dryDelay;
                segment.forcedDry = failed || p.latencyPacketFailed;
                segment.mix = kind;
                segment.firstWet = wet;
                segment.firstBypass = mix;
            }
            auto &segment = capture->storage[capture->count - 1];
            ++segment.count;
            if (segment.mix != kind)
                segment.mix = PluginPcmMixKind::mixed;
            segment.lastWet = wet;
            segment.lastBypass = mix;
        }
        for (int c = 0; c < 2; ++c)
        {
            float source = buffer.getNumChannels() > c ? buffer.getSample(c, offset + frame) : 0;
            p.dryHistory->write(c, source);
            if (p.input)
                p.input->input[c][p.cursor] = source;
            if (c < buffer.getNumChannels())
            {
                const auto processed = p.output[c][p.cursor];
                const auto dry = p.instrument ? 0.f : p.dryHistory->tap(c, dryDelay);
                buffer.setSample(c, offset + frame, HostWetMix::render(processed, dry, wet, mix));
            }
        }
        if (p.input)
            for (int c = 0; c < 2; ++c)
                p.input->input[c + 2][p.cursor] = validAuxiliary && c < p.buses.auxiliaryChannels
                                                      ? auxiliary->getSample(c, auxiliaryOffset + frame)
                                                      : 0.f;
        p.dryHistory->advance();
        if (++p.cursor == p.blockSize)
        {
            if (p.input)
            {
                p.input->midiCount = p.midiCount;
                p.input->parameterCount = p.parameterCount;
                p.input->parameterGeneration = p.parameterGeneration;
                std::copy_n(p.parameters.data(), p.parameterCount, p.input->parameters.data());
                std::copy_n(p.midi.data(), p.midiCount, p.input->midi.data());
                p.input->status.store(1, std::memory_order_release);
                SetEvent(p.wake); // signal only; the live host never waits for its worker
                ++p.submitted;
            }
            else
            {
                ++p.dropped;
                p.shared->parameterEventsRejected.fetch_add(std::uint64_t(p.parameterCount),
                                                            std::memory_order_relaxed);
                if (p.instrument && p.midiCount > 0)
                    p.shared->panic.store(1);
            }
            ++p.sequence;
            p.cursor = 0;
        }
    }
    if (count > 0)
    {
        if (!p.offline && p.preparedTransport)
            p.burstProcessed += count;
        p.dryHistory->record(count, p.transportPackets * p.blockSize + p.consumedNativeSamples,
                             p.emittedConsumed ? p.emittedConsumed->stamp : PluginLatencyStamp{},
                             !p.instrument && !failed && !p.latencyPacketFailed && p.emittedConsumed &&
                                 p.emittedPacket == PluginPcmPacketKind::completed,
                             p.bypassMix == 1.);
        p.bypassReadback.store(float(p.bypassMix), std::memory_order_relaxed);
        p.wetReadback.store(p.wetMix.value(), std::memory_order_relaxed);
        p.bypassAvailable.store(true, std::memory_order_release);
    }
    if (capture)
        capture->status = PluginPcmReceiptStatus::complete;
}
} // namespace vibera
