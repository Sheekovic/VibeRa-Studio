// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerRoutePlugins.h"
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.h>
#include <tracktion_engine/playback/graph/tracktion_PluginNode.h>
#include <array>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <limits>

namespace vibera::experimental
{
// No production source implements this experimental declaration. A caller must
// independently audit its source: native input short MIDI is preserved, no new
// arbitrary/long messages are generated, audio/voice ownership is separate.
struct ClosedShortMidiPortProducer
{
    virtual ~ClosedShortMidiPortProducer() = default;
    virtual const void *privateVoiceOwner() const noexcept = 0;
};

struct NativeMidiFaultPublisher final
{
    enum class Fault : std::uint32_t { none, ingress, terminal };
    std::atomic<Fault> value{Fault::none};
    static_assert(std::atomic<Fault>::is_always_lock_free);
    void publish(Fault reason) noexcept
    { auto expected=Fault::none;value.compare_exchange_strong(expected,reason,std::memory_order_release,std::memory_order_relaxed); }
    Fault observation() const noexcept{return value.load(std::memory_order_acquire);}
};

struct NativeMidiTerminal final
{
    enum class Fault : std::uint32_t
    {
        none,
        quota,
        unsupported,
        ownershipOverflow,
        counterExhausted
    };
    struct Event
    {
        std::array<unsigned char, 3> bytes{};
        int size = 0;
        tracktion::MPESourceID source = {};
        double time = 0;
    };
    struct Observation
    {
        Fault fault;
        std::uint64_t blocks, on, off, panicMessages, panicFlags, other;
        unsigned active;
    };
    std::array<Event, 512> lastEvents{}; // Read only with selected callback quiescent and owner pinned.
    unsigned lastCount = 0;
    std::array<std::array<unsigned, 128>, 16>
        held{}; // DSP-only accounting, never instrument/private-lane ownership.
    // Same sole DSP owner as held. CONTROL may inspect/seed only a quiescent
    // disposable fixture; production never reads these non-atomic payloads.
    std::array<unsigned, 16> channelHeld{};
    unsigned heldTotal = 0;
    std::atomic<Fault> fault{Fault::none};
    std::atomic<std::uint64_t> blocks{0}, on{0}, off{0}, panicMessages{0}, panicFlags{0}, other{0};
    std::atomic<unsigned> active{0};
    static_assert(std::atomic<Fault>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free &&
                  std::atomic<std::uint64_t>::is_always_lock_free);
    Observation observe() const noexcept
    {
        return {fault.load(),         blocks.load(),     on.load(),    off.load(),
                panicMessages.load(), panicFlags.load(), other.load(), active.load()};
    }
    void consume(const tracktion::MidiMessageArray &input) noexcept
    {
        if (fault.load(std::memory_order_relaxed) != Fault::none)
            return;
        if (input.size() > 512)
        {
            fault.store(Fault::quota);
            return;
        }
        for (const auto &m : input)
        {
            const auto n = m.getRawDataSize();
            const auto *bytes = m.getRawData();
            if (n < 1 || n > 3 || bytes[0] < 0x80 || bytes[0] == 0xf0 || bytes[0] == 0xf7 ||
                juce::MidiMessage::getMessageLengthFromFirstByte(bytes[0]) != n)
            {
                fault.store(Fault::unsupported);
                return;
            }
            for (int i = 1; i < n; ++i)
                if (bytes[i] >= 0x80)
                {
                    fault.store(Fault::unsupported);
                    return;
                }
        }
        // Counters belong to this sole DSP writer. A failed event changes no
        // key/channel/total/counter/event receipt; an earlier admitted prefix
        // remains accepted and active always describes that exact prefix.
        constexpr auto counterMax = std::numeric_limits<std::uint64_t>::max();
        const auto block = blocks.load(std::memory_order_relaxed);
        const auto panics = panicFlags.load(std::memory_order_relaxed);
        if (block == counterMax || (input.isAllNotesOff && panics == counterMax))
        {
            fault.store(Fault::counterExhausted);
            return;
        }
        lastCount = 0;
        blocks.store(block + 1, std::memory_order_relaxed);
        if (input.isAllNotesOff)
        {
            for (auto &channel : held)
                channel.fill(0); // Explicit rare global panic only.
            channelHeld.fill(0);
            heldTotal = 0;
            panicFlags.store(panics + 1, std::memory_order_relaxed);
            active.store(0, std::memory_order_release);
        }
        for (const auto &m : input)
        {
            // Only read payload bytes after the validated status proves
            // they exist. JUCE short constructors leave unused packed bytes
            // uninitialized; convenience predicates may inspect those bytes.
            const auto *bytes = m.getRawData();
            const auto kind = bytes[0] & 0xf0;
            const bool noteOn = kind == 0x90 && bytes[2] != 0;
            const bool noteOff = kind == 0x80 || (kind == 0x90 && bytes[2] == 0);
            const bool panic = kind == 0xb0 && (bytes[1] == 120 || bytes[1] == 123);
            auto &counter = noteOn ? on : noteOff ? off : panic ? panicMessages : other;
            const auto value = counter.load(std::memory_order_relaxed);
            if (value == counterMax)
            {
                fault.store(Fault::counterExhausted);
                return;
            }
            if (noteOn || noteOff)
            {
                const auto channel = std::size_t(m.getChannel() - 1), pitch = std::size_t(m.getNoteNumber());
                auto &key = held[channel][pitch];
                if (noteOn)
                {
                    constexpr auto maximum = std::numeric_limits<unsigned>::max();
                    if (key == maximum || channelHeld[channel] == maximum || heldTotal == maximum)
                    {
                        fault.store(Fault::ownershipOverflow);
                        return;
                    }
                    ++key;
                    ++channelHeld[channel];
                    ++heldTotal;
                }
                else if (key)
                {
                    if (!channelHeld[channel] || !heldTotal)
                    {
                        fault.store(Fault::ownershipOverflow);
                        return;
                    }
                    --key;
                    --channelHeld[channel];
                    --heldTotal;
                }
                active.store(heldTotal, std::memory_order_release);
            }
            else if (panic)
            {
                const auto channel = std::size_t(m.getChannel() - 1);
                if (channelHeld[channel] > heldTotal)
                {
                    fault.store(Fault::ownershipOverflow);
                    return;
                }
                heldTotal -= channelHeld[channel];
                channelHeld[channel] = 0;
                held[channel].fill(0); // Only this explicitly panicked channel.
                active.store(heldTotal, std::memory_order_release);
            }
            counter.store(value + 1, std::memory_order_relaxed);
            auto &e = lastEvents[lastCount++];
            e = {};
            e.size = m.getRawDataSize();
            e.source = m.mpeSourceID;
            e.time = m.getTimeStamp();
            for (int n = 0; n < e.size; ++n)
                e.bytes[std::size_t(n)] = m.getRawData()[n];
        }
    }
};

// Actual native ConnectedNode, not a false hasMidi override. Its only custom
// action is ordinary MIDI output reservation on CONTROL before processing.
class ReservedMidiOutputPort final : public tracktion::graph::ConnectedNode
{
  public:
    explicit ReservedMidiOutputPort(std::size_t id) : ConnectedNode(id) {}
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &info) override
    {
        ConnectedNode::prepareToPlay(info);
        reserveMidiOutputForPreparation(512);
    }
};
class OwnedNativeMidiTerminal final : public tracktion::graph::Node
{
    const std::shared_ptr<ReservedMidiOutputPort> input;
    const std::shared_ptr<NativeMidiTerminal> owner;
    const std::shared_ptr<NativeMidiFaultPublisher> faults;
    const std::size_t id;

  public:
    OwnedNativeMidiTerminal(std::shared_ptr<ReservedMidiOutputPort> n, std::shared_ptr<NativeMidiTerminal> o,
                            std::size_t identity, std::shared_ptr<NativeMidiFaultPublisher> publisher = {})
        : input(std::move(n)), owner(std::move(o)), faults(std::move(publisher)), id(identity)
    {
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return {false, false, 0, input->getNodeProperties().latencyNumSamples, id};
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {input.get()};
    }
    bool isReadyToProcess() override
    {
        return input->hasProcessed();
    }
    void process(ProcessContext &) override
    {
        owner->consume(input->getProcessedOutput().midi);
        if(faults && owner->fault.load(std::memory_order_acquire)!=NativeMidiTerminal::Fault::none)
            faults->publish(NativeMidiFaultPublisher::Fault::terminal);
    }
};
class RegisteredAudioOutputPort final : public tracktion::graph::Node
{
    const std::shared_ptr<tracktion::graph::ConnectedNode> audio;
    const std::shared_ptr<OwnedNativeMidiTerminal> midi;
    const tracktion::Plugin::Ptr gate; // Actual initialized plugin pins its nonmoving stage Runtime.
    const void *const stage;
    const std::shared_ptr<const void> policy;
    const std::size_t id;

  public:
    RegisteredAudioOutputPort(std::shared_ptr<tracktion::graph::ConnectedNode> a,
                              std::shared_ptr<OwnedNativeMidiTerminal> m, tracktion::Plugin::Ptr g,
                              std::size_t identity, std::shared_ptr<const void> p)
        : audio(std::move(a)), midi(std::move(m)), gate(std::move(g)),
          stage(gate->getPreparedAudioOutputStageIdentity()), policy(std::move(p)), id(identity)
    {
        setOptimisations({tracktion::graph::ClearBuffers::yes, tracktion::graph::AllocateAudioBuffer::no});
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto p = audio->getNodeProperties();
        p.nodeID = id;
        p.audioCompensationStage = stage;
        p.audioCompensationStageOrigin = this;
        return p;
    }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &info) override
    {
        if (!info.nodeGraphToReplace)
            return; // Cold creation only; no queue retro-certification.
        bool predecessor = false;
        for (auto *node : info.nodeGraphToReplace->orderedNodes)
        {
            const auto p = node->getNodeProperties();
            if (p.audioCompensationStage != stage)
                continue;
            const RegisteredAudioOutputPort *origin = nullptr;
            for (auto *possible : info.nodeGraphToReplace->orderedNodes)
                if (possible == p.audioCompensationStageOrigin)
                    origin = dynamic_cast<RegisteredAudioOutputPort *>(possible);
            if (!origin || origin->policy.get() != policy.get() || origin->stage != stage)
                throw std::invalid_argument(
                    "audioPort.baseline: Warm mixed/foreign stage cannot migrate into audio port");
            predecessor = true;
        }
        if (!predecessor)
            throw std::invalid_argument("audioPort.baseline: Exact prior registered audio port required");
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {audio.get(), midi.get()};
    }
    bool isReadyToProcess() override
    {
        return audio->hasProcessed() && midi->hasProcessed();
    }
    void process(ProcessContext &) override
    {
        setAudioOutput(audio.get(), audio->getProcessedOutput().audio);
    }
};

// Fresh baseline + exact same-policy private successor experiment ONLY. This
// is deliberately not admission for the full app's eighteen-insert projection.
class SingleTrackMixerAudioPort final : public std::enable_shared_from_this<SingleTrackMixerAudioPort>
{
    tracktion::AudioTrack &track;
    tracktion::ProcessState &scope;
    const tracktion::Plugin::Ptr source, gate;
    const void *const sourceOwner;
    const void *const stageOwner;
    explicit SingleTrackMixerAudioPort(tracktion::AudioTrack &t, tracktion::ProcessState &p,
                                       tracktion::Plugin::Ptr s, tracktion::Plugin::Ptr g)
        : track(t), scope(p), source(std::move(s)), gate(std::move(g)),
          sourceOwner(dynamic_cast<ClosedShortMidiPortProducer &>(*source).privateVoiceOwner()),
          stageOwner(gate->getPreparedAudioOutputStageIdentity())
    {
    }

  public:
    const std::shared_ptr<NativeMidiTerminal> terminal = std::make_shared<NativeMidiTerminal>();
    static std::shared_ptr<SingleTrackMixerAudioPort> prepare(tracktion::AudioTrack &t,
                                                              tracktion::ProcessState &p,
                                                              tracktion::Plugin::Ptr s,
                                                              tracktion::Plugin::Ptr g)
    {
        if (!s || !g || !dynamic_cast<ClosedShortMidiPortProducer *>(s.get()) ||
            !dynamic_cast<MixerMasterGate *>(g.get()))
            throw std::invalid_argument(
                "audioPort.producer: Audited short source and actual MixerMasterGate required");
        auto result = std::shared_ptr<SingleTrackMixerAudioPort>(
            new SingleTrackMixerAudioPort(t, p, std::move(s), std::move(g)));
        result->validate();
        return result;
    }
    void validate() const
    {
        const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        if (!messages || !messages->isThisTheMessageThread())
            throw std::invalid_argument("audioPort.controlOwner: Message-thread graph preparation required");
        if (!track.edit.closedNativeIO || !track.edit.initialSourceGeneration ||
            tracktion::getAudioTracks(track.edit).size() != 1 || track.hasAnyLiveInputs() ||
            !track.getOutput().usesDefaultAudioOut() || track.getOutput().getDestinationTrack())
            throw std::invalid_argument(
                "audioPort.nativeRouting: Closed single-track default-wave route required");
        if (!tracktion::LiveMidiInjectingNode::hasOnlyBoundedNoopOutputListeners(track, scope))
            throw std::invalid_argument(
                "audioPort.nativeObserver: Unaccounted real recorded-output listener");
        if (track.pluginList.size() != 2 || track.pluginList[0] != source.get() ||
            track.pluginList[1] != gate.get() || &source->edit != &track.edit || &gate->edit != &track.edit ||
            !source->isEnabled() || !gate->isEnabled() || source->getSidechainSourceID().isValid() ||
            gate->getSidechainSourceID().isValid() || !sourceOwner ||
            dynamic_cast<ClosedShortMidiPortProducer &>(*source).privateVoiceOwner() != sourceOwner ||
            !stageOwner || gate->getPreparedAudioOutputStageIdentity() != stageOwner ||
            source->getNumOutputChannelsGivenInputs(2) != 2 || gate->getNumOutputChannelsGivenInputs(2) != 2)
            throw std::invalid_argument("audioPort.chain: Exact immutable two-owner stereo chain required");
        for (auto *clip : track.getClips())
        {
            auto *midi = dynamic_cast<tracktion::MidiClip *>(clip);
            if (!midi || !midi->getSequence().isEmpty() || !clip->getListeners().isEmpty() ||
                (clip->getPluginList() && clip->getPluginList()->size() != 0))
                throw std::invalid_argument(
                    "audioPort.clip: Only observer-free empty native MIDI anchors admitted");
        }
    }
    void install(tracktion::CreateNodeParams &params)
    {
        validate();
        if (&params.processState != &scope || params.includeMasterPlugins || !params.forRendering ||
            params.pluginListResolver || params.preparePluginFinalOutput ||
            params.omitAccountedNoopMidiObserver)
            throw std::invalid_argument("audioPort.build: Exact unbound closed rendering factory required");
        auto self = shared_from_this();
        params.boundedNativeMidiIngress = true;
        params.omitAccountedNoopMidiObserver = [self](tracktion::AudioTrack &t)
        {
            self->validate();
            if (&t != &self->track)
                throw std::invalid_argument("audioPort.foreignTrack");
            return true;
        };
        params.preparePluginFinalOutput = [self](tracktion::Plugin &plugin)
            -> std::optional<tracktion::CreateNodeParams::PluginFinalOutputPlan>
        {
            self->validate();
            if (&plugin != self->source.get() && &plugin != self->gate.get())
                throw std::invalid_argument("audioPort.unknownPlugin: Unaccounted producer/control owner");
            tracktion::CreateNodeParams::PluginFinalOutputPlan plan;
            plan.shortMidiCapacity = 512;
            if (&plugin == self->gate.get())
            {
                plan.moveAudioCompensationStage = true;
                plan.wrap = [self](std::unique_ptr<tracktion::graph::Node> final)
                    -> std::unique_ptr<tracktion::graph::Node>
                {
                    self->validate();
                    if (!final || final->getNodeProperties().numberOfChannels != 2 ||
                        final->getNodeProperties().audioCompensationStage)
                        throw std::invalid_argument(
                            "audioPort.final: Exact stereo post-remap unregistered producer required");
                    auto mixed = std::shared_ptr<tracktion::graph::Node>(std::move(final));
                    const auto base = std::size_t(self->gate->itemID.getRawID());
                    auto audio =
                        std::make_shared<tracktion::graph::ConnectedNode>(base ^ 0x415544494f504f52ULL);
                    auto midi = std::make_shared<ReservedMidiOutputPort>(base ^ 0x4d494449504f5254ULL);
                    if (!audio->addAudioConnection(mixed, {0, 0}) ||
                        !audio->addAudioConnection(mixed, {1, 1}) || !midi->addMidiConnection(mixed))
                        throw std::invalid_argument(
                            "audioPort.connections: Native port construction refused");
                    auto consumer = std::make_shared<OwnedNativeMidiTerminal>(midi, self->terminal,
                                                                              base ^ 0x4d49444953494e4bULL);
                    return std::make_unique<RegisteredAudioOutputPort>(audio, consumer, self->gate,
                                                                       base ^ 0x415544494f535447ULL, self);
                };
            }
            return plan;
        };
    }
};
} // namespace vibera::experimental
