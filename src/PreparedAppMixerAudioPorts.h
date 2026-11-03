// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedMixerAudioPort.h"
#include "PreparedSourceExchangeRoot.h"
#include "PreparedMixerTrackMidiBoundary.h"
#include <map>
#include <set>

namespace vibera
{
class AudioEngine;
}

namespace vibera::experimental
{
// Internal fresh LOCAL-CLOSED app projection only. All decisions happen on
// CONTROL. Native processing never reads this registry or resolves a PluginList.
class AppMixerAudioPorts final : public std::enable_shared_from_this<AppMixerAudioPorts>
{
  public:
    struct Binding
    {
        enum class Role
        {
            unknown,
            sampler,
            channelBalance,
            mixerGain,
            guide,
            effect,
            masterGate,
            send,
            sidechain,
            auxReturn,
            disabledBalance
        };
        Role role = Role::unknown;
        std::shared_ptr<const void> runtime;
        std::uint64_t sourceEpoch = 0; // Re-probed after actual native initialization.
    };
    using Probe = std::function<Binding(tracktion::Plugin &)>;
    class PreparedRoute final
    {
        friend class AppMixerAudioPorts;
        std::shared_ptr<const tracktion::GraphOutputRouting> source, target, publicationSlot;
        PreparedSourceExchangeRoot::Request request;
        const AppMixerAudioPorts* owner = nullptr;
        std::uint64_t generation = 0;
      public:
        std::uint64_t preparedGeneration() const noexcept { return generation; }
    };
    class BuildLease final
    {
        friend class AppMixerAudioPorts;
        const std::shared_ptr<AppMixerAudioPorts> owner;
        const PreparedRoute* route;
        BuildLease(AppMixerAudioPorts& p, const PreparedRoute& r) : owner(p.shared_from_this()), route(&r)
        {
            control();
            if (owner->activeBuild || !owner->validateRoute(r))
                throw std::invalid_argument("sourceExchange.buildLease: Nested/stale route preparation");
            owner->activeBuild = &r;
        }
      public:
        BuildLease(const BuildLease&) = delete;
        BuildLease& operator=(const BuildLease&) = delete;
        ~BuildLease() { if (owner->activeBuild == route) owner->activeBuild = nullptr; }
    };
    struct ZeroQueueObservation
    {
        juce::uint64 nativeOwner = 0, build = 0;
        const void *processor = nullptr; // Diagnostic only, never dereference/persist as a project ID.
        bool certified = false, createdHere = false;
        bool preparationObserved = false, memorySharing = false, outputZero = false;
        juce::String proofTrace; // Latest CONTROL preparation only; no DSP writes/I/O.
    };
    struct FiniteSourceObservation
    {
        bool known = false, ready = false, open = false;
        std::uint32_t fault = 0;
        std::uint64_t generation = 0, deviceEpoch = 0, begins = 0, ends = 0, aborts = 0, frames = 0,
                      serial = 0;
        std::int64_t referenceStart = 0;
        const void *runtime = nullptr;
        tracktion::SourceBurstSession::Outcome native;
    };
    struct Observation
    {
        std::size_t owners = 0, runtimeOwners = 0, nativeEdges = 0, tracks = 0;
        std::uint64_t builds = 0, portsPrepared = 0, audioBlocks = 0, nonemptyAudioMidi = 0;
        std::uint64_t nativeOn = 0, nativeOff = 0, nativePanic = 0;
        unsigned nativeHeld = 0;
        bool terminalFault = false;
    };

  private:
    struct Owner
    {
        Binding binding;
        const tracktion::Plugin *nativeShell = nullptr; // Identity only; never dereferenced after its Edit.
        juce::String type;
        int bus = -1;
        std::shared_ptr<NativeMidiTerminal> terminal = std::make_shared<NativeMidiTerminal>();
        ZeroQueueObservation zeroQueue; // Latest CONTROL prepare, not selected-graph completion.
    };
    struct Route
    {
        tracktion::AudioTrack *track;
        tracktion::Track *destination;
        bool defaultWave;
    };
    struct Counters
    {
        std::atomic<std::uint64_t> audioBlocks{0}, nonemptyAudioMidi{0};
    };
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    class AudioPort final : public tracktion::graph::Node, public tracktion::graph::PreparedAudioZeroOutput,
                            public tracktion::graph::PreparedSharedSourceOutput
    {
        friend class AppMixerAudioPorts; // CONTROL-only proof traversal of this exact private port.
        const std::shared_ptr<tracktion::graph::ConnectedNode> audio;
        const std::shared_ptr<OwnedNativeMidiTerminal> terminal;
        const tracktion::Plugin::Ptr plugin; // Actual candidate owner, ordinary native graph lifetime.
        const std::shared_ptr<const void> runtime;
        const std::shared_ptr<AppMixerAudioPorts> policy;
        const std::shared_ptr<Counters> counters;
        const void *const stage;
        const std::size_t id;
        const std::shared_ptr<tracktion::graph::Node> mixed;
        bool prepared = false, audioZero = false; // CONTROL writes only before publication.

      public:
        AudioPort(std::shared_ptr<tracktion::graph::ConnectedNode> a,
                  std::shared_ptr<OwnedNativeMidiTerminal> t, tracktion::Plugin::Ptr p,
                  std::shared_ptr<const void> r, std::shared_ptr<AppMixerAudioPorts> owner,
                  std::size_t identity, std::shared_ptr<tracktion::graph::Node> producer)
            : audio(std::move(a)), terminal(std::move(t)), plugin(std::move(p)), runtime(std::move(r)),
              policy(std::move(owner)), counters(policy->counts),
              stage(plugin->getPreparedAudioOutputStageIdentity()), id(identity), mixed(std::move(producer))
        {
            setOptimisations(
                {tracktion::graph::ClearBuffers::yes, tracktion::graph::AllocateAudioBuffer::no});
        }
        tracktion::graph::NodeProperties getNodeProperties() override
        {
            auto result = audio->getNodeProperties();
            result.nodeID = id;
            result.audioCompensationStage = stage;
            result.audioCompensationStageOrigin = stage ? this : nullptr;
            return result;
        }
        void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &info) override
        {
            ++policy->portsPrepared;
            prepared = true;
            if (policy->zeroHistoryProofs)
            {
                auto& observation = policy->owners.at(plugin->itemID.getRawID()).zeroQueue;
                observation.nativeOwner = plugin->itemID.getRawID(); observation.build = policy->builds;
                observation.preparationObserved = true; observation.memorySharing = info.enableNodeMemorySharing;
                observation.processor=nullptr; observation.certified=false; observation.createdHere=false;
                observation.proofTrace = "mixed producer; actual memorySharing=" +
                    juce::String(int(info.enableNodeMemorySharing)) + "\n";
                audioZero = !info.enableNodeMemorySharing && policy->proveAudioZero(*mixed, &observation.proofTrace);
                observation.outputZero = audioZero;
            }
            if (!stage || !info.nodeGraphToReplace)
                return;
            bool found = false;
            for (auto *node : info.nodeGraphToReplace->orderedNodes)
            {
                const auto properties = node->getNodeProperties();
                if (properties.audioCompensationStage != stage)
                    continue;
                const AudioPort *old = nullptr;
                for (auto *possible : info.nodeGraphToReplace->orderedNodes)
                    if (possible == properties.audioCompensationStageOrigin)
                        old = dynamic_cast<AudioPort *>(possible);
                if (!old || old->policy != policy || old->runtime != runtime ||
                    old->plugin->itemID != plugin->itemID)
                    throw std::invalid_argument(
                        "appAudioPort.baseline: No warm mixed/foreign stage migration");
                found = true;
            }
            if (!found)
                throw std::invalid_argument("appAudioPort.dormantStage: Previous exact audio stage required");
        }
        std::optional<tracktion::graph::PreparedSharedSourceIdentity> preparedSharedSourceIdentity() override
        {
            if (!policy->zeroHistoryProofs || !prepared || !policy->proofProbe) return {};
            const auto actual = policy->proofProbe(*plugin);
            if (actual.role != Binding::Role::sampler || !actual.sourceEpoch ||
                actual.runtime != runtime || !runtime || &plugin->edit != policy->edit) return {};
            return tracktion::graph::PreparedSharedSourceIdentity{
                runtime.get(), std::size_t(plugin->itemID.getRawID()), actual.sourceEpoch};
        }
        bool provesPreparedSharedSourceBoundary(tracktion::graph::Node& boundary) override
        {
            const auto identity = preparedSharedSourceIdentity();
            const auto p = getNodeProperties();
            if (!identity || !p.hasAudio || p.hasMidi || p.numberOfChannels != 2) return false;
            return policy->proveSingleSource(boundary, *this);
        }
        bool provesPreparedAudioZeroBoundary(tracktion::graph::Node &boundary) override
        {
            const auto p = boundary.getNodeProperties();
            if (!policy->zeroHistoryProofs || !prepared || !audioZero || !stage ||
                p.audioCompensationStage != stage || p.audioCompensationStageOrigin != this ||
                !p.hasAudio || p.hasMidi || p.numberOfChannels != 2) return false;
            auto& trace = policy->owners.at(plugin->itemID.getRawID()).zeroQueue.proofTrace;
            trace += "actual final boundary\n";
            return policy->proveAudioZero(boundary, &trace);
        }
        void observePreparedZeroQueue(const void *processor, bool certified, bool createdHere) override
        {
            if (!policy->zeroHistoryProofs)
                return;
            auto &receipt = policy->owners.at(plugin->itemID.getRawID()).zeroQueue;
            receipt.nativeOwner = plugin->itemID.getRawID(); receipt.build = policy->builds;
            receipt.processor = processor; receipt.certified = certified; receipt.createdHere = createdHere;
        }
        std::vector<Node *> getDirectInputNodes() override
        {
            return {audio.get(), terminal.get()};
        }
        bool isReadyToProcess() override
        {
            return audio->hasProcessed() && terminal->hasProcessed();
        }
        void process(ProcessContext &pc) override
        {
            // Both native ConnectedNodes select one shared mixed producer. The
            // ordinary player processes it once; MIDI has a real owned consumer.
            setAudioOutput(audio.get(), audio->getProcessedOutput().audio);
            counters->audioBlocks.fetch_add(1, std::memory_order_relaxed);
            if (!pc.buffers.midi.isEmpty() || pc.buffers.midi.isAllNotesOff)
                counters->nonemptyAudioMidi.fetch_add(1, std::memory_order_relaxed);
        }
    };
    tracktion::Edit *edit = nullptr;
    tracktion::ProcessState *scope = nullptr;
    double sampleRate = 0;
    int blockSize = 0;
    std::map<juce::uint64, Owner> owners;
    std::map<juce::uint64,std::shared_ptr<MixerTrackMidiOwner>> trackMidiOwners;
    std::shared_ptr<NativeMidiFaultPublisher> nativeMidiFault;
    std::shared_ptr<initial_source_epoch::Health> sourceEpochHealth;
    std::vector<Route> routes;
    std::shared_ptr<Counters> counts = std::make_shared<Counters>();
    std::uint64_t builds = 0, portsPrepared = 0; // CONTROL only; no callback writes.
    friend class ::vibera::AudioEngine;
    bool finiteSampleBaseline = false; // Actual separately owned LOCAL sample Session, no Host bank.
    std::uint64_t finiteGeneration = 0;
    std::function<bool(FiniteSourceObservation &)> finiteReadback; // CONTROL only, never DSP.
    Probe finiteEffectProbe; // CONTROL only; exact ORIGINAL provider capability, never a DSP callback.
    // Private: no generic caller/provider-by-name can grant a finite-tail declaration.
    static std::shared_ptr<AppMixerAudioPorts>
    createFiniteSampleBaseline(Probe exactEffect, std::uint64_t generation,
                               std::function<bool(FiniteSourceObservation &)> readback)
    {
        control();
        if (!exactEffect || !generation || !readback)
            throw std::invalid_argument("finite.baseline.capability: Original provider witness required");
        auto result = std::shared_ptr<AppMixerAudioPorts>(new AppMixerAudioPorts());
        result->finiteSampleBaseline = true;
        result->finiteGeneration = generation;
        result->finiteReadback = std::move(readback);
        result->finiteEffectProbe = std::move(exactEffect);
        result->continuity = std::make_shared<SourceExchangeContinuity>();
        result->nativeMidiFault = std::make_shared<NativeMidiFaultPublisher>();
        return result;
    }
    bool sourceExchanges = false; // Requires proof/ports from actual first baseline.
    const PreparedRoute* activeBuild = nullptr; // CONTROL stack lease only, never DSP.
    std::shared_ptr<const tracktion::GraphOutputRouting> acceptedRouting;
    std::shared_ptr<SourceExchangeContinuity> continuity;
    bool zeroHistoryProofs = false;              // Immutable before first install, internal opt-in only.
    Probe proofProbe;                            // CONTROL-only exact typed original owner classification.
    bool admitZeroPreservingPlugin(tracktion::Plugin &plugin, juce::String *proofTrace = nullptr)
    {
        const auto it = owners.find(plugin.itemID.getRawID());
        if (it == owners.end() || !proofProbe || &plugin.edit != edit)
            return false;
        const auto actual = proofProbe(plugin);
        const auto role = it->second.binding.role;
        if (proofTrace)
            *proofTrace += "actual-plugin=" + juce::String(plugin.itemID.getRawID()) +
                " type=" + plugin.getPluginType() + " enabled=" + juce::String(int(plugin.isEnabled())) +
                " role=" + juce::String(int(actual.role)) + " registeredRole=" + juce::String(int(role)) +
                " runtimeMatch=" + juce::String(int(actual.runtime == it->second.binding.runtime)) +
                " runtimePresent=" + juce::String(int(bool(actual.runtime))) + "\n";
        if (actual.role != role || actual.runtime != it->second.binding.runtime)
            return false;
        if (role == Binding::Role::disabledBalance)
        {
            // The private app Edit never exposes/enables this native shell.
            // Exact disabled native Volume is NOT ExternalPlugin: PluginNode
            // copies input/MIDI, skips apply/VCA and has no bypass/history FIFO.
            // Recheck exact identity/type/disabled state; no arbitrary disabled
            // plugin or caller-supplied role is a zero-preserving declaration.
            return !actual.runtime && it->second.nativeShell == &plugin &&
                typeid(plugin) == typeid(tracktion::VolumeAndPanPlugin) && !plugin.isEnabled();
        }
        return actual.runtime && (role == Binding::Role::channelBalance || role == Binding::Role::mixerGain ||
                                  role == Binding::Role::masterGate);
    }
    bool proveAudioZero(tracktion::graph::Node &node, juce::String* proofTrace = nullptr)
    {
        return tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(
            node,
            [this,proofTrace](tracktion::Plugin &plugin)
            { return admitZeroPreservingPlugin(plugin, proofTrace); },
            [this,proofTrace](tracktion::graph::Node &current) -> std::optional<tracktion::graph::Node *>
            {
                if (auto *port = dynamic_cast<AudioPort *>(&current))
                {
                    if (proofTrace)
                        *proofTrace += "actual-port-owner=" + juce::String(port->plugin->itemID.getRawID()) +
                            " samePolicy=" + juce::String(int(port->policy.get()==this)) +
                            " prepared=" + juce::String(int(port->prepared)) +
                            " cachedAudioZero=" + juce::String(int(port->audioZero)) + "\n";
                    return port->policy.get() == this && port->prepared && port->audioZero ? port->mixed.get()
                                                                                           : nullptr;
                }
                if(auto* boundary=dynamic_cast<PreparedMixerTrackMidiBoundary*>(&current))
                    return boundary->preparedAudioInput(scope,nativeMidiFault.get());
                return {};
            },
            [proofTrace](tracktion::graph::Node& current, const char* decision, bool zero)
            {
                if (!proofTrace) return;
                const auto p = current.getNodeProperties();
                *proofTrace += "node=" + juce::String(typeid(current).name()) +
                    " id=" + juce::String(juce::uint64(p.nodeID)) +
                    " audio=" + juce::String(int(p.hasAudio)) + " midi=" + juce::String(int(p.hasMidi)) +
                    " channels=" + juce::String(p.numberOfChannels) + " latency=" + juce::String(p.latencyNumSamples) +
                    " shareable=" + juce::String(int(current.canShareOutputBuffer())) +
                    " decision=" + juce::String(decision) + " zero=" + juce::String(int(zero)) + "\n";
            });
    }
    bool proveSingleSource(tracktion::graph::Node& boundary, AudioPort& source)
    {
        return tracktion::EditNodeBuilder::hasPreparedSingleAudioSourceOutput(boundary, source,
            [this](tracktion::Plugin& plugin)
            { return admitZeroPreservingPlugin(plugin); },
            [this](tracktion::graph::Node& current) -> std::optional<tracktion::graph::Node*>
            {
                if (auto* port = dynamic_cast<AudioPort*>(&current))
                    return port->policy.get() == this && port->prepared ? port->mixed.get() : nullptr;
                if(auto* boundary=dynamic_cast<PreparedMixerTrackMidiBoundary*>(&current))
                    return boundary->preparedAudioInput(scope,nativeMidiFault.get());
                return {};
            });
    }
    AppMixerAudioPorts() = default;
    static void control()
    {
        const auto *manager = juce::MessageManager::getInstanceWithoutCreating();
        if (!manager || !manager->isThisTheMessageThread())
            throw std::invalid_argument("appAudioPort.control: Message-thread preparation required");
    }
    static bool nativeEdge(Binding::Role role)
    {
        return role == Binding::Role::send || role == Binding::Role::sidechain ||
               role == Binding::Role::auxReturn;
    }
    static int bus(tracktion::Plugin &plugin)
    {
        if (auto *send = dynamic_cast<tracktion::AuxSendPlugin *>(&plugin))
            return int(send->busNumber);
        if (auto *receive = dynamic_cast<tracktion::AuxReturnPlugin *>(&plugin))
            return int(receive->busNumber);
        return -1;
    }
    static void validateModifiers(tracktion::Track *track)
    {
        if (track)
            if (const auto *modifiers = track->getModifierList())
                if (!modifiers->getModifiers().isEmpty())
                    throw std::invalid_argument("appAudioPort.modifier: Unaccounted native event producer");
    }
    void validateEdit(tracktion::Edit &candidate, tracktion::CreateNodeParams &params) const
    {
        if (!candidate.closedNativeIO ||
            (finiteSampleBaseline ? candidate.initialSourceGeneration != finiteGeneration
                                  : !candidate.initialSourceGeneration) ||
            params.forRendering || !params.includePlugins || !params.includeMasterPlugins ||
            params.allowedClips || params.allowedTracks || params.insertOptionalLastStageNodeForDevice ||
            !tracktion::EditNodeBuilder::usesDefaultLastStageNode() ||
            !candidate.getRackList().getTypes().isEmpty() || bool(candidate.clickTrackEnabled))
            throw std::invalid_argument(
                "appAudioPort.closedGraph: Exact initial closed live projection required");
        const auto expectedRouting = activeBuild ? activeBuild->target : acceptedRouting;
        if (params.outputRouting != expectedRouting)
            throw std::invalid_argument("sourceExchange.routeAuthority: Exact effective complete route map required");
        if (params.outputRouting) params.outputRouting->validateForBuild(candidate, true);
        if (edit && (&candidate != edit || &params.processState != scope || params.sampleRate != sampleRate ||
                     params.blockSize != blockSize))
            throw std::invalid_argument("appAudioPort.context: Exact same Edit/ProcessState required");
        for (auto *track : tracktion::getAllTracks(candidate))
        {
            if (track->isFolderTrack())
                throw std::invalid_argument("appAudioPort.folder: Native submix not admitted");
            validateModifiers(track);
        }
        validateModifiers(candidate.getMasterTrack());
        validateModifiers(candidate.getTempoTrack());
        if (edit && tracktion::getAudioTracks(candidate).size() != int(routes.size()))
            throw std::invalid_argument("appAudioPort.trackUniverse: Changed source/Mixer universe");
        std::size_t ordinal = 0;
        for (auto *track : tracktion::getAudioTracks(candidate))
        {
            if (track->hasAnyLiveInputs() || track->isFrozen(tracktion::AudioTrack::individualFreeze) ||
                (!track->getOutput().usesDefaultAudioOut() && !track->getOutput().getDestinationTrack()) ||
                !tracktion::LiveMidiInjectingNode::hasOnlyBoundedNoopOutputListeners(*track,
                                                                                     params.processState))
                throw std::invalid_argument(
                    "appAudioPort.nativeRoute: Unaccounted live/frozen/observer/output route");
            if (edit)
            {
                const auto &route = routes[ordinal];
                if (route.track != track || route.destination != track->getOutput().getDestinationTrack() ||
                    route.defaultWave != track->getOutput().usesDefaultAudioOut())
                    throw std::invalid_argument(
                        "appAudioPort.routeChanged: Initial fixed native routes required");
            }
            ++ordinal;
            // The new bookkeeping terminal intercepts THIS Mixer Track's
            // native clip/injector branch. A MIDI-only launcher can have the
            // same zero-audio/zero-channel properties as an empty anchor; it
            // is not an admitted empty source or callback-allocation path.
            if ((sourceExchanges || finiteSampleBaseline) && track->state.hasProperty("viberaMixerIndex"))
                for(auto* slot:track->getClipSlotList().getClipSlots())
                    if(slot->getClip())
                        throw std::invalid_argument("trackMidi.launcher: Intercepted Mixer Track requires empty native clip slots");
            for (auto *clip : track->getClips())
            {
                auto *anchor = dynamic_cast<tracktion::MidiClip *>(clip);
                if (!anchor || !anchor->getSequence().isEmpty() || !clip->getListeners().isEmpty() ||
                    (clip->getPluginList() && clip->getPluginList()->size() != 0))
                    throw std::invalid_argument(
                        "appAudioPort.clip: Only empty observer-free native anchors admitted");
            }
        }
    }

  public:
    static std::shared_ptr<AppMixerAudioPorts> create(bool enableZeroHistoryProofs = false,
                                                    bool enableSourceExchanges = false,
                                                    std::shared_ptr<initial_source_epoch::Health> fixedHealth = {})
    {
        auto result = std::shared_ptr<AppMixerAudioPorts>(new AppMixerAudioPorts());
        if (enableSourceExchanges && !enableZeroHistoryProofs)
            throw std::invalid_argument("sourceExchange.baseline: Initial zero proofs required");
        result->zeroHistoryProofs = enableZeroHistoryProofs;
        if(enableSourceExchanges&&!fixedHealth)
            throw std::invalid_argument("sourceEpoch.owner: Initial source exchange requires prepared health owner");
        result->sourceExchanges = enableSourceExchanges;
        result->sourceEpochHealth=std::move(fixedHealth);
        if (enableSourceExchanges)
        {
            result->continuity = std::make_shared<SourceExchangeContinuity>();
            result->nativeMidiFault = std::make_shared<NativeMidiFaultPublisher>();
        }
        return result;
    }
    FiniteSourceObservation finiteSourceObservation() const
    {
        control();
        FiniteSourceObservation out;
        out.known = finiteReadback && finiteReadback(out);
        return out; // Source/session metrics are CONTROL-safe; not a graph commit receipt.
    }
    std::shared_ptr<PreparedRoute> prepareRoute(std::shared_ptr<const tracktion::GraphOutputRouting> target,
                                               const void* addDelay, const void* removeDelay)
    {
        control();
        if (!sourceExchanges || !edit || !continuity || activeBuild || !target ||
            !continuity->hasSuccessfulBaseline() || terminalFaultControl() ||
            continuity->observation() != SourceExchangeContinuity::Fault::none ||
            !addDelay || !removeDelay || addDelay == removeDelay)
            throw std::invalid_argument("sourceExchange.route: Healthy initial exact two-stage request required");
        target->validateForBuild(*edit, true);
        auto r = std::shared_ptr<PreparedRoute>(new PreparedRoute());
        r->owner=this; r->source=acceptedRouting; r->target=target;
        if (builds >= std::numeric_limits<std::uint64_t>::max()-1)
            throw std::invalid_argument("sourceExchange.generationExhausted: Fresh projection required");
        r->publicationSlot=std::move(target); r->request={addDelay,removeDelay}; r->generation=builds+1;
        return r;
    }
    bool validateRoute(const PreparedRoute& r) const noexcept
    {
        return r.owner == this && sourceExchanges && continuity && r.target &&
            r.publicationSlot == r.target && r.source == acceptedRouting &&
            !terminalFaultControl() && continuity->observation() == SourceExchangeContinuity::Fault::none;
    }
    std::unique_ptr<BuildLease> leaseRoute(const PreparedRoute& r)
    { control(); return std::unique_ptr<BuildLease>(new BuildLease(*this,r)); }
    void publishRoute(PreparedRoute& r) noexcept
    {
        // Sole CONTROL after native publication, no fallible work/reentry.
        // The ticket retains old map; no last-owner destruction at commit.
        acceptedRouting.swap(r.publicationSlot);
    }
    SourceExchangeContinuity::Fault exchangeFault() const noexcept
    { return continuity ? continuity->observation() : SourceExchangeContinuity::Fault::none; }
    std::uint64_t completedExchangeGeneration() const noexcept
    { return continuity ? continuity->completedGeneration() : 0; }
    bool terminalFaultControl() const noexcept
    {
        // Sole CONTROL registry reader; callbacks only write terminal atomics.
        // Never called from root/DSP, never scan/follow mutable owners there.
        if(sourceEpochHealth&&!sourceEpochHealth->healthy())return true;
        if(nativeMidiFault&&nativeMidiFault->observation()!=NativeMidiFaultPublisher::Fault::none)return true;
        for(const auto& [id,owner]:trackMidiOwners)
            if(owner->view.observation().fault!=tracktion::LiveMidiInjectingNode::IngressFault::none ||
               owner->terminal->fault.load(std::memory_order_acquire)!=NativeMidiTerminal::Fault::none)return true;
        for (const auto& [id,owner]:owners)
            if (owner.terminal->fault.load(std::memory_order_acquire) != NativeMidiTerminal::Fault::none)
                return true;
        return false;
    }
    struct TrackMidiObservation
    {
        juce::uint64 nativeId=0;int mixer=-1;
        const void* scope=nullptr;const void* runtime=nullptr;
        unsigned readers=0;
        tracktion::LiveMidiInjectingNode::IngressObservation ingress;
        NativeMidiTerminal::Observation terminal;
    };
    std::vector<TrackMidiObservation> trackMidiObservations() const
    {
        control();std::vector<TrackMidiObservation> result;
        for(const auto& [id,owner]:trackMidiOwners)
            result.push_back({id,owner->mixer,owner->scope,owner->runtime,owner->readers,
                owner->view.observation(),owner->terminal->observe()});
        return result;
    }
    std::vector<ZeroQueueObservation> zeroHistoryObservations() const
    {
        control();
        std::vector<ZeroQueueObservation> result;
        for (const auto &[id, owner] : owners)
            if (owner.zeroQueue.processor || owner.zeroQueue.preparationObserved)
                result.push_back(owner.zeroQueue);
        return result; // Latest prepared actual queues, not current selection/capability.
    }
    // CONTROL readback; callback counters/terminal fields are independent atomic
    // observations, not a coherent asynchronous application-completion receipt.
    Observation observe() const
    {
        control();
        Observation result;
        result.owners = owners.size();
        result.tracks = routes.size();
        result.builds = builds;
        result.portsPrepared = portsPrepared;
        for (const auto &[id, owner] : owners)
        {
            (void)id;
            if (owner.binding.runtime)
                ++result.runtimeOwners;
            if (nativeEdge(owner.binding.role))
                ++result.nativeEdges;
            const auto terminal = owner.terminal->observe();
            result.terminalFault |= terminal.fault != NativeMidiTerminal::Fault::none;
            result.nativeOn += terminal.on;
            result.nativeOff += terminal.off;
            result.nativePanic += terminal.panicMessages;
            result.nativeHeld += terminal.active;
        }
        result.audioBlocks = counts->audioBlocks.load();
        result.nonemptyAudioMidi = counts->nonemptyAudioMidi.load();
        return result;
    }
    void install(tracktion::Edit &candidate, tracktion::CreateNodeParams &params, Probe probe)
    {
        control();
        validateEdit(candidate, params);
        if (!probe || params.preparePluginFinalOutput || params.omitAccountedNoopMidiObserver ||
            ((sourceExchanges || finiteSampleBaseline) &&
             (params.prepareWholeGraphOutput || params.prepareNativeTrackClipOutput)))
            throw std::invalid_argument("appAudioPort.existingPolicy: Refuse policy replacement/composition");
        const auto upstream = params.pluginListResolver;
        auto resolved = std::make_shared<std::map<const tracktion::PluginList *, tracktion::Plugin::Array>>();
        const auto add = [&](const tracktion::PluginList &list)
        { resolved->emplace(&list, upstream ? upstream(list) : list.getPlugins()); };
        add(candidate.getMasterPluginList());
        for (auto *track : tracktion::getAudioTracks(candidate))
        {
            add(track->pluginList);
            for (auto *clip : track->getClips())
                if (auto *list = clip->getPluginList())
                    add(*list);
        }
        std::map<juce::uint64, Owner> first;
        std::set<juce::uint64> found;
        const auto admit = [&](tracktion::Plugin &plugin)
        {
            const auto id = plugin.itemID.getRawID();
            const auto binding = probe(plugin);
            if (&plugin.edit != &candidate || !id || !found.insert(id).second ||
                binding.role == Binding::Role::unknown || plugin.isFrozen() ||
                (binding.role != Binding::Role::disabledBalance && !plugin.isEnabled()))
                throw std::invalid_argument(
                    "appAudioPort.owner: Unknown/frozen/duplicate/unavailable processor");
            if(sourceExchanges && (binding.role==Binding::Role::effect || binding.role==Binding::Role::send ||
                                    binding.role==Binding::Role::sidechain))
                throw std::invalid_argument("trackMidi.closedScope: FX/sends need separate native MIDI admission");
            if (finiteSampleBaseline &&
                (binding.role == Binding::Role::send || binding.role == Binding::Role::sidechain ||
                 (binding.role == Binding::Role::effect &&
                  (!finiteEffectProbe || finiteEffectProbe(plugin).runtime != binding.runtime))))
                throw std::invalid_argument(
                    "finite.baseline.capability: Exact original fixed-state finite anchor required");
            const bool original = !nativeEdge(binding.role) && binding.role != Binding::Role::disabledBalance;
            if (original && !binding.runtime)
                throw std::invalid_argument(
                    "appAudioPort.runtime: Exact nonmoving source/control owner required");
            if (edit)
            {
                const auto old = owners.find(id);
                if (old == owners.end() || old->second.binding.role != binding.role ||
                    old->second.type != plugin.getPluginType() ||
                    old->second.binding.runtime != binding.runtime || old->second.bus != bus(plugin) ||
                    (!binding.runtime && old->second.nativeShell != &plugin))
                    throw std::invalid_argument(
                        "appAudioPort.ownerChanged: Same-ID retained Runtime/native edge required");
            }
            else
                first.emplace(id, Owner{binding, &plugin, plugin.getPluginType(), bus(plugin)});
        };
        for (const auto &[list, plugins] : *resolved)
        {
            (void)list;
            for (auto *plugin : plugins)
            {
                if (!plugin)
                    throw std::invalid_argument("appAudioPort.nullOwner");
                admit(*plugin);
            }
        }
        auto master = candidate.getMasterVolumePlugin();
        if (master && !found.contains(master->itemID.getRawID()))
            admit(*master);
        if (edit && found.size() != owners.size())
            throw std::invalid_argument("appAudioPort.ownerUniverse: Processor membership changed");
        if (!edit)
        {
            std::vector<Route> firstRoutes;
            for (auto *track : tracktion::getAudioTracks(candidate))
                firstRoutes.push_back({track, track->getOutput().getDestinationTrack(),
                                       track->getOutput().usesDefaultAudioOut()});
            owners = std::move(first);
            routes = std::move(firstRoutes);
            edit = &candidate;
            scope = &params.processState;
            sampleRate = params.sampleRate;
            blockSize = params.blockSize;
        }
        if (sourceExchanges && (builds >= std::numeric_limits<std::uint64_t>::max()-1 ||
            (activeBuild && activeBuild->generation != builds+1)))
            throw std::invalid_argument("sourceExchange.buildGeneration: Exhausted/reentered candidate build");
        ++builds;
        if (zeroHistoryProofs || finiteSampleBaseline)
            proofProbe = probe;
        const auto self = shared_from_this();
        if (sourceExchanges || finiteSampleBaseline)
        {
            // Every Mixer terminal is installed from the first factory. The
            // source/instrument/Master path is deliberately not intercepted.
            for(auto* track:tracktion::getAudioTracks(candidate))
            {
                if(!track->state.hasProperty("viberaMixerIndex"))continue;
                const int mixer=int(track->state["viberaMixerIndex"]);
                if(mixer<=0)throw std::invalid_argument("trackMidi.identity: Non-Master app Mixer Track required");
                const auto& plugins=resolved->at(&track->pluginList);
                bool gain=false,gate=false,receive=false;
                for(auto* plugin:plugins)
                {
                    const auto binding=probe(*plugin);
                    if(binding.role==Binding::Role::mixerGain && !gain && admitZeroPreservingPlugin(*plugin))gain=true;
                    else if(binding.role==Binding::Role::masterGate && !gate && admitZeroPreservingPlugin(*plugin))gate=true;
                    else if(binding.role==Binding::Role::disabledBalance && admitZeroPreservingPlugin(*plugin)){}
                    else if(binding.role==Binding::Role::auxReturn && !receive &&
                            typeid(*plugin)==typeid(tracktion::AuxReturnPlugin))receive=true;
                    else if (finiteSampleBaseline && binding.role == Binding::Role::effect &&
                             finiteEffectProbe && finiteEffectProbe(*plugin).runtime == binding.runtime)
                    {
                    }
                    else throw std::invalid_argument("trackMidi.consumer: Only exact native MIDI-noop Mixer controls admitted");
                }
                if(!gain||!gate||!receive)throw std::invalid_argument("trackMidi.consumer: Complete original Mixer chain required");
                const auto id=track->itemID.getRawID();
                if(!trackMidiOwners.contains(id))
                    trackMidiOwners.emplace(id,std::make_shared<MixerTrackMidiOwner>(*track,*scope,mixer,nativeMidiFault));
                else if(trackMidiOwners.at(id)->track!=track||trackMidiOwners.at(id)->scope!=scope||
                        trackMidiOwners.at(id)->mixer!=mixer)
                    throw std::invalid_argument("trackMidi.owner: Exact initial Track/scope required");
            }
            if (builds == std::numeric_limits<std::uint64_t>::max())
                throw std::invalid_argument("sourceExchange.generationExhausted: Fresh projection required");
            const auto request = activeBuild ? activeBuild->request : PreparedSourceExchangeRoot::Request{};
            const auto generation = builds;
            params.prepareWholeGraphOutput = [owner=continuity,generation,request,fault=nativeMidiFault,health=sourceEpochHealth](std::unique_ptr<tracktion::graph::Node> root)
                -> std::unique_ptr<tracktion::graph::Node>
            { return std::make_unique<PreparedSourceExchangeRoot>(std::move(root), owner, generation, request, fault, health); };
        }
        if (sourceExchanges || finiteSampleBaseline)
            params.prepareNativeTrackClipOutput=[self](tracktion::AudioTrack& track,std::unique_ptr<tracktion::graph::Node> input)
                ->std::unique_ptr<tracktion::graph::Node>
            {
                if(!track.state.hasProperty("viberaMixerIndex"))return input;
                const auto found=self->trackMidiOwners.find(track.itemID.getRawID());
                if(found==self->trackMidiOwners.end()||found->second->track!=&track)
                    throw std::invalid_argument("trackMidi.owner: No native-original fallback");
                return std::make_unique<PreparedMixerTrackMidiBoundary>(std::move(input),found->second);
            };
        params.boundedNativeMidiIngress = true;
        params.pluginListResolver = [resolved](const tracktion::PluginList &list)
        {
            const auto found = resolved->find(&list);
            if (found == resolved->end())
                throw std::invalid_argument("appAudioPort.list: No fallback owner list");
            return found->second;
        };
        params.omitAccountedNoopMidiObserver = [self](tracktion::AudioTrack &track)
        {
            if (&track.edit != self->edit ||
                !tracktion::LiveMidiInjectingNode::hasOnlyBoundedNoopOutputListeners(track, *self->scope))
                throw std::invalid_argument(
                    "appAudioPort.observer: Legitimate/unknown observer cannot be suppressed");
            return true;
        };
        params.preparePluginFinalOutput = [self, probe](tracktion::Plugin &plugin)
            -> std::optional<tracktion::CreateNodeParams::PluginFinalOutputPlan>
        {
            const auto item = self->owners.find(plugin.itemID.getRawID());
            const auto binding = probe(plugin);
            if (item == self->owners.end() || binding.role != item->second.binding.role ||
                binding.runtime != item->second.binding.runtime || nativeEdge(binding.role) ||
                (!binding.runtime && item->second.nativeShell != &plugin))
                throw std::invalid_argument("appAudioPort.plugin: Factory encountered unadmitted producer");
            tracktion::CreateNodeParams::PluginFinalOutputPlan plan;
            plan.shortMidiCapacity = 512;
            plan.moveAudioCompensationStage = plugin.getPreparedAudioOutputStageIdentity() != nullptr;
            const auto terminal = item->second.terminal;
            plan.wrap =
                [self, terminal, binding, p = tracktion::Plugin::Ptr(&plugin)](
                    std::unique_ptr<tracktion::graph::Node> output) -> std::unique_ptr<tracktion::graph::Node>
            {
                if (!output || output->getNodeProperties().numberOfChannels != 2 ||
                    output->getNodeProperties().audioCompensationStage)
                    throw std::invalid_argument(
                        "appAudioPort.final: Exact post-remap stereo unregistered output required");
                auto mixed = std::shared_ptr<tracktion::graph::Node>(std::move(output));
                const auto id = std::size_t(p->itemID.getRawID());
                auto audio = std::make_shared<tracktion::graph::ConnectedNode>(id ^ 0x415050415544494fULL);
                auto midi = std::make_shared<ReservedMidiOutputPort>(id ^ 0x4150504d49444950ULL);
                if (!audio->addAudioConnection(mixed, {0, 0}) || !audio->addAudioConnection(mixed, {1, 1}) ||
                    !midi->addMidiConnection(mixed))
                    throw std::invalid_argument("appAudioPort.connection: Native connection refused");
                auto sink =
                    std::make_shared<OwnedNativeMidiTerminal>(midi, terminal, id ^ 0x4150504d4944534bULL,self->nativeMidiFault);
                return std::make_unique<AudioPort>(audio, sink, p, binding.runtime, self,
                                                   id ^ 0x4150504f55545054ULL, mixed);
            };
            return plan;
        };
    }
};
} // namespace vibera::experimental
