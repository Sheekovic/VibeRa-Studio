// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedMixerAudioPort.h"
#include <type_traits>
#include <utility>

namespace vibera::experimental
{
// No Node/PluginPtr pin. The opaque actual Runtime view exists only while true
// graph readers remain; the LAST CONTROL lease disarms it before Track/Edit
// teardown. Registry scalar identity/terminal facts may then outlive the Edit.
struct MixerTrackMidiOwner final
{
    const tracktion::AudioTrack* const track;
    const tracktion::ProcessState* const scope;
    const juce::uint64 nativeId;
    const int mixer;
    const std::shared_ptr<NativeMidiTerminal> terminal=std::make_shared<NativeMidiTerminal>();
    const std::shared_ptr<NativeMidiFaultPublisher> faults;
    const void* runtime=nullptr; // CONTROL identity only, never dereferenced.
    unsigned readers=0; // CONTROL graph leases only, not processing calls.
    tracktion::LiveMidiInjectingNode::BoundedIngressRuntimeView view;
    static_assert(std::is_nothrow_move_assignable_v<decltype(view)>);
    MixerTrackMidiOwner(tracktion::AudioTrack& t,const tracktion::ProcessState& s,
        int stableMixer,std::shared_ptr<NativeMidiFaultPublisher> fault)
        :track(&t),scope(&s),nativeId(t.itemID.getRawID()),mixer(stableMixer),faults(std::move(fault)){}
};

// A genuine empty audio selection plus a genuine MIDI selection/consumer.
// Default native factories never construct this original private app node.
class PreparedMixerTrackMidiBoundary final : public tracktion::graph::Node
{
    const std::shared_ptr<tracktion::graph::ConnectedNode> audio;
    const std::shared_ptr<tracktion::LiveMidiInjectingNode> producer;
    const std::shared_ptr<OwnedNativeMidiTerminal> terminal;
    const std::shared_ptr<MixerTrackMidiOwner> owner;
    const std::size_t id;
    bool prepared=false;
  public:
    PreparedMixerTrackMidiBoundary(std::unique_ptr<tracktion::graph::Node> input,
                                  std::shared_ptr<MixerTrackMidiOwner> stable)
        :audio(std::make_shared<tracktion::graph::ConnectedNode>(checkedId(stable)^0x4d5452415544ULL)),
         producer(checked(std::move(input))),
         terminal(makeTerminal(producer,stable)),owner(std::move(stable)),
         id(std::size_t(owner->nativeId)^0x4d5452424f554e44ULL)
    {
        if(!owner->faults||!owner->nativeId)throw std::invalid_argument("trackMidi.owner: Actual stable scope/fault owner required");
        const auto p=producer->getNodeProperties();
        if(p.hasAudio||p.numberOfChannels!=0||p.latencyNumSamples!=0)
            throw std::invalid_argument("trackMidi.audioInput: No audio may be disconnected by this boundary");
        setOptimisations({tracktion::graph::ClearBuffers::yes,tracktion::graph::AllocateAudioBuffer::no,
            tracktion::graph::ShareOutputBuffer::no});
    }
    static std::size_t checkedId(const std::shared_ptr<MixerTrackMidiOwner>& stable)
    {
        if(!stable||!stable->faults||!stable->nativeId)
            throw std::invalid_argument("trackMidi.owner: Actual stable scope/fault owner required");
        return std::size_t(stable->nativeId);
    }
    ~PreparedMixerTrackMidiBoundary() override
    {
        if(prepared)
        {
            jassert(owner->readers>0);
            if(--owner->readers==0)
                owner->view={}; // CONTROL, before producer/Track and Edit teardown.
        }
    }
    static std::shared_ptr<tracktion::LiveMidiInjectingNode> checked(std::unique_ptr<tracktion::graph::Node> input)
    {
        auto* actual=dynamic_cast<tracktion::LiveMidiInjectingNode*>(input.get());
        if(!actual)throw std::invalid_argument("trackMidi.input: Actual native injector required");
        input.release();return std::shared_ptr<tracktion::LiveMidiInjectingNode>(actual);
    }
    static std::shared_ptr<OwnedNativeMidiTerminal> makeTerminal(
        const std::shared_ptr<tracktion::LiveMidiInjectingNode>& source,const std::shared_ptr<MixerTrackMidiOwner>& stable)
    {
        auto midi=std::make_shared<ReservedMidiOutputPort>(std::size_t(stable->nativeId)^0x4d54524d494449ULL);
        if(!midi->addMidiConnection(source))throw std::invalid_argument("trackMidi.connection: Actual native MIDI port refused");
        return std::make_shared<OwnedNativeMidiTerminal>(midi,stable->terminal,
            std::size_t(stable->nativeId)^0x4d54525445524dULL,stable->faults);
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {auto p=audio->getNodeProperties();p.nodeID=id;return p;}
    std::vector<Node*> getDirectInputNodes() override{return {audio.get(),terminal.get()};}
    bool isReadyToProcess() override{return audio->hasProcessed()&&terminal->hasProcessed();}
    tracktion::graph::Node* preparedAudioInput(const tracktion::ProcessState* exactScope,
                                              const NativeMidiFaultPublisher* exactPublisher) const noexcept
    {return prepared&&owner->scope==exactScope&&owner->faults.get()==exactPublisher?audio.get():nullptr;}
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo& info) override
    {
        const auto* messages=juce::MessageManager::getInstanceWithoutCreating();
        if(!messages||!messages->isThisTheMessageThread()||prepared)throw std::invalid_argument("trackMidi.control: One actual CONTROL preparation required");
        const auto native=producer->boundedIngressObservation();
        if(!native.enabled||!native.registered||native.scope!=owner->scope||!native.runtime||
           native.fault!=tracktion::LiveMidiInjectingNode::IngressFault::none)
            throw std::invalid_argument("trackMidi.ingress: Exact prepared healthy native scope required");
        std::size_t copies=0,previous=0;
        for(auto* node:info.nodeGraph.orderedNodes)
            if(auto* p=dynamic_cast<PreparedMixerTrackMidiBoundary*>(node);p&&p->owner->track==owner->track)
            {if(p->owner!=owner)throw std::invalid_argument("trackMidi.owner: Duplicate/foreign current scope");++copies;}
        if(copies!=1)throw std::invalid_argument("trackMidi.reader: Exactly one selected reader per Track/scope");
        if(info.nodeGraphToReplace)
            for(auto* node:info.nodeGraphToReplace->orderedNodes)
                if(auto* p=dynamic_cast<PreparedMixerTrackMidiBoundary*>(node);p&&p->owner->track==owner->track)
                {if(p->owner!=owner||p->owner->scope!=owner->scope)throw std::invalid_argument("trackMidi.predecessor: Exact retained owner required");++previous;}
        if((info.nodeGraphToReplace&&previous!=1)||(!info.nodeGraphToReplace&&owner->runtime))
            throw std::invalid_argument("trackMidi.migration: First graph only; no warm mixed queue abandonment");
        if(owner->runtime&&owner->runtime!=native.runtime)throw std::invalid_argument("trackMidi.runtime: Native inbox changed");
        if(owner->readers==std::numeric_limits<unsigned>::max())
            throw std::invalid_argument("trackMidi.readers: CONTROL lease count exhausted");
        auto view=producer->boundedIngressRuntimeControl();
        if(view.ownerTrackControl()!=owner->track||view.observation().scope!=owner->scope)
            throw std::invalid_argument("trackMidi.view: Actual native Track/scope required");
        owner->runtime=native.runtime;owner->view=std::move(view);++owner->readers;prepared=true;
    }
    void process(ProcessContext&) override
    {
        // Dependency reaches the real producer through the actual MIDI port.
        // Never copy/release an owning handle or inspect a native inbox here.
        if(producer->boundedIngressObservation().fault!=tracktion::LiveMidiInjectingNode::IngressFault::none)
            owner->faults->publish(NativeMidiFaultPublisher::Fault::ingress);
        setAudioOutput(audio.get(),audio->getProcessedOutput().audio);
    }
};
} // namespace vibera::experimental
