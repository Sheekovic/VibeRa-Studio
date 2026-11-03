// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ReadyGraphAcceptance.h"
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <array>
#include <vector>
#include <exception>

// ORIGINAL actual native Player/legacy-queue test. No app, SDK or route claim.
// MIDI-bearing callbacks are FUNCTIONAL ONLY: ordinary native MIDI allocation
// behavior is preserved and measured, never labelled allocation-free here.
namespace zero_channel_delay_acceptance
{
using namespace tracktion::graph;
constexpr double rate = 48000.;
constexpr int delay = 287, maximumBlock = 128;
struct Counts { unsigned source = 0, anchor = 0; };
struct Shape { bool audio = false; int channels = 0; };
class MidiOnly final : public Node
{
    Counts& counts;
    const std::shared_ptr<Shape> shape;
    const bool send;
    const std::array<juce::MidiMessage,3> messages{
        juce::MidiMessage::noteOn(1,64,juce::uint8(100)),
        juce::MidiMessage::noteOff(1,64), juce::MidiMessage::allNotesOff(1)};
  public:
    MidiOnly(Counts& c, std::shared_ptr<Shape> s, bool events)
        : counts(c), shape(std::move(s)), send(events)
    { setOptimisations({ClearBuffers::yes,AllocateAudioBuffer::yes,ShareOutputBuffer::no}); }
    NodeProperties getNodeProperties() override
    { return {shape->audio,true,shape->channels,0,0x5a434d494449ULL}; }
    bool isReadyToProcess() override { return true; }
    void prepareToPlay(const PlaybackInitialisationInfo&) override
    { reserveMidiOutputForPreparation(8); }
    void process(ProcessContext& pc) override
    {
        ++counts.source;
        // Literal independent event schedule. All three enter the actual old
        // queue in callback0, then remain pending across private cancellation
        // and real graph adoption before due frames290/304/316.
        constexpr std::array<int,3> at{3,17,29};
        if (send)
            for (std::size_t n=0;n<at.size();++n)
                if (at[n]>=pc.referenceSampleRange.getStart() && at[n]<pc.referenceSampleRange.getEnd())
                    pc.buffers.midi.addMidiMessage(messages[n],
                        double(at[n]-pc.referenceSampleRange.getStart())/rate,91);
    }
};
class Anchor final : public Node
{
    Counts& counts;
  public:
    explicit Anchor(Counts& c):counts(c)
    { setOptimisations({ClearBuffers::yes,AllocateAudioBuffer::yes,ShareOutputBuffer::no}); }
    NodeProperties getNodeProperties() override { return {true,false,2,delay,0x5a43414e4348ULL}; }
    bool isReadyToProcess() override { return true; }
    void process(ProcessContext&) override { ++counts.anchor; } // Actual cleared stereo zero.
};
class UnknownAudio final : public Node
{
    const bool pulse;
  public:
    unsigned calls=0;
    explicit UnknownAudio(bool nonzero):pulse(nonzero)
    { setOptimisations({ClearBuffers::yes,AllocateAudioBuffer::yes,ShareOutputBuffer::no}); }
    NodeProperties getNodeProperties() override { return {true,false,2,0,0x5a43554e4bULL}; }
    bool isReadyToProcess() override { return true; }
    void process(ProcessContext& pc) override
    {
        ++calls;
        if(pulse) for(choc::buffer::ChannelCount c=0;c<2;++c)
            for(choc::buffer::FrameCount n=0;n<pc.numSamples;++n)
                pc.buffers.audio.getSample(c,n)=c ? -.0625f : .125f;
    }
};
inline bool audioZero(Node& node)
{
    return tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(node,
        [](tracktion::Plugin&){return false;},{});
}
inline bool singleSource(Node& root,Node& source)
{
    return tracktion::EditNodeBuilder::hasPreparedSingleAudioSourceOutput(root,source,
        [](tracktion::Plugin&){return false;},{});
}
struct Graph
{
    std::unique_ptr<Node> root;
    LatencyNode* tap=nullptr;
    Anchor* anchor=nullptr;
};
struct Fixture
{
    ready_graph_acceptance::Counts pool;
    Counts counts;
    const std::shared_ptr<Shape> shape=std::make_shared<Shape>();
    LockFreeMultiThreadedNodePlayer player;
    LatencyNode* tap=nullptr;
    Node* root=nullptr;
    Anchor* anchor=nullptr;
    const bool events;
    const juce::File folder;
    choc::buffer::ChannelArrayBuffer<float> pcm{choc::buffer::Size::create(2,maximumBlock)};
    tracktion::MidiMessageArray midi;
    std::int64_t reference=0;
    unsigned receivedMask=0;
    std::vector<float> actual;
    juce::String rows="phase,start,frames,sourceCalls,anchorCalls,zeroMetadata,certificate,selection,new,delete,seconds,midiCount\n";
    juce::String eventRows="phase,start,kind,key,source,relativeFrames,expectedRelativeFrames\n";
    Fixture(juce::File output,bool withEvents)
        :player([this](auto& p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,pool);}),
         events(withEvents),folder(std::move(output))
    {
        require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh zero-channel evidence");
        player.setNumThreads(0);player.enablePooledMemoryAllocations(false);player.enableNodeMemorySharing(false);
        auto initial=graph();
        require(!initial.tap->hasPreparedNoAudioOutput()&&!audioZero(*initial.tap),
            "Advertised no-audio/zero-channel properties BEFORE actual prepare are insufficient");
        tap=initial.tap;root=initial.root.get();anchor=initial.anchor;
        player.setNode(std::move(initial.root),rate,maximumBlock);
        midi.reserve(16);actual.reserve(8192);
        require(tap->hasPreparedNoAudioOutput()&&audioZero(*tap)&&singleSource(*root,*anchor)&&
            !tap->hasStructuralZeroHistory()&&tap->getNodeProperties().hasMidi,
            "Final zero-channel legacy processor proves AUDIO zero WITHOUT MIDI/history certification");
    }
    Graph graph()
    {
        Graph next;
        std::unique_ptr<Node> source=std::make_unique<MidiOnly>(counts,shape,events);
        auto latency=std::make_unique<LatencyNode>(std::move(source),delay);
        next.tap=latency.get();
        auto zero=std::make_unique<Anchor>(counts);next.anchor=zero.get();
        std::vector<std::unique_ptr<Node>> inputs;
        inputs.push_back(std::move(latency));inputs.push_back(std::move(zero));
        next.root=std::make_unique<SummingNode>(std::move(inputs));return next;
    }
    void retain()
    {
        require(folder.getChildFile("callbacks.csv").replaceWithText(rows)&&
            folder.getChildFile("midi.csv").replaceWithText(eventRows),"Retain actual metadata/events before assertions");
        auto stream=folder.getChildFile("actual-call-planar-f32.raw").createOutputStream();
        require(stream&&stream->setPosition(0)&&stream->truncate().wasOk()&&
            stream->write(actual.data(),actual.size()*sizeof(float)),"Truncate/write all actual zero PCM");
        stream->flush();require(stream->getStatus().wasOk(),"Flush actual zero PCM");
    }
    void drive(const char* phase,int frames)
    {
        pcm.clear();midi.clear();const auto before=counts;
        const auto allocations=callbackAllocations.load(),destructions=callbackDestructions.load();
        const auto started=juce::Time::getHighResolutionTicks();
        {
            struct Probe { bool previous=callbackAllocationProbe;Probe(){callbackAllocationProbe=true;}
                ~Probe(){callbackAllocationProbe=previous;} } probe;
            player.process({choc::buffer::FrameCount(frames),{reference,reference+frames},
                {pcm.getView().getStart(choc::buffer::FrameCount(frames)),midi}});
        }
        const double seconds=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-started);
        bool exact=true;
        for(int c=0;c<2;++c)for(int n=0;n<frames;++n)
        {
            const float value=pcm.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n));
            actual.push_back(value);exact&=value==0.f;
        }
        constexpr std::array<int,3> due{290,304,316};
        unsigned expectedMask=0,observedMask=0;
        for(std::size_t n=0;n<due.size();++n)
            if(events&&due[n]>=reference&&due[n]<reference+frames)expectedMask|=1u<<n;
        bool correct=true;
        for(const auto& event:midi)
        {
            const int kind=event.isNoteOn()?0:event.isNoteOff()?1:event.isAllNotesOff()?2:-1;
            const int key=kind==0||kind==1?event.getNoteNumber():-1;
            const double actualFrame=event.getTimeStamp()*rate;
            const int wanted=kind>=0?due[std::size_t(kind)]-int(reference):-1;
            eventRows+=juce::String(phase)+","+juce::String(reference)+","+juce::String(kind)+","+
                juce::String(key)+","+juce::String(event.mpeSourceID)+","+
                juce::String(actualFrame,12)+","+juce::String(wanted)+"\n";
            correct &= kind>=0 && event.mpeSourceID==91 &&
                (kind==2||key==64) && std::abs(actualFrame-wanted)<=1.e-7;
            if(kind>=0) {const auto bit=1u<<kind;correct&=(observedMask&bit)==0;observedMask|=bit;}
        }
        correct &= observedMask==expectedMask&&!midi.isAllNotesOff&&(receivedMask&observedMask)==0;
        receivedMask|=observedMask;
        rows+=juce::String(phase)+","+juce::String(reference)+","+juce::String(frames)+","+
            juce::String(counts.source)+","+juce::String(counts.anchor)+","+
            juce::String(int(tap->hasPreparedNoAudioOutput()))+","+juce::String(int(tap->hasStructuralZeroHistory()))+","+
            juce::String(player.graphSelection().selectedSerial)+","+
            juce::String(callbackAllocations.load()-allocations)+","+juce::String(callbackDestructions.load()-destructions)+","+
            juce::String(seconds,9)+","+juce::String(midi.size())+"\n";
        reference+=frames;retain();
        require(exact&&correct&&counts.source==before.source+1&&counts.anchor==before.anchor+1&&
            tap->hasPreparedNoAudioOutput()&&!tap->hasStructuralZeroHistory()&&seconds<=double(frames)/rate,
            "Every zero PCM sample, independent delayed On/Off/CC123/time/source, producer once and unchanged deadline");
        if(!events)require(callbackAllocations.load()==allocations&&callbackDestructions.load()==destructions,
            "Empty-MIDI original callbacks allocate/destroy no C++ owners");
    }
};
inline void reuse(const juce::File& folder,bool events)
{
    Fixture f(folder,events);f.drive("initial_pending",128);
    const auto* original=f.tap->structuralZeroHistoryRuntimeToken();
    {
        auto next=f.graph();const auto before=f.counts;
        auto candidate=f.player.prepareFixedGraph(std::move(next.root));
        require(next.tap->hasPreparedNoAudioOutput()&&next.tap->structuralZeroHistoryRuntimeToken()==original&&
            !next.tap->hasStructuralZeroHistory()&&f.counts.source==before.source,
            "Actual FINAL inherited zero-channel processor; private preparation does not consume MIDI");
    }
    f.drive("FIRST_after_cancel_pending",65);
    auto next=f.graph();auto* selectedRoot=next.root.get();auto* selectedTap=next.tap;auto* selectedAnchor=next.anchor;
    auto candidate=f.player.prepareFixedGraph(std::move(next.root));const auto serial=candidate.publicationSerial();
    require(selectedTap->hasPreparedNoAudioOutput()&&selectedTap->structuralZeroHistoryRuntimeToken()==original,
        "Second real prepare retains exact pending processor");
    auto retirement=f.player.makeGraphRetirementSlot();
    require(f.player.publishReadyGraph(candidate,retirement)==ready_graph_acceptance::Status::ready,
        "Actual fixed holder publication");
    f.root=selectedRoot;f.tap=selectedTap;f.anchor=selectedAnchor;
    f.drive("FIRST_adopted_pending_delivery",127);
    require(f.player.graphSelection().selectedSerial==serial,"Exact actual successor selection");
    f.drive("post_delivery_tail",64);f.drive("tail",128);
    require(f.receivedMask==(events?7u:0u),"Exactly one of every genuine delayed event across cancel/adoption");
    const bool retired=f.player.retireReadyGraph(retirement);
    require(folder.getChildFile("retirement.txt").replaceWithText(
        "actualControlRetired="+juce::String(int(retired))+"\nselectedSerial="+
        juce::String(f.player.graphSelection().selectedSerial)+"\n"),
        "Retain actual old-graph retirement outcome before assertion");
    require(retired,"Actual old holder explicitly retired on CONTROL after successor processing");
}
inline void negatives(const juce::File& folder)
{
    Fixture f(folder,false);
    f.shape->audio=true;
    require(!f.tap->hasPreparedNoAudioOutput()&&!audioZero(*f.tap),"Re-read current input hasAudio; cached shape alone cannot authorize");
    f.shape->audio=false;f.shape->channels=2;
    require(!f.tap->hasPreparedNoAudioOutput(),"Current input channel mismatch refuses");
    f.shape->channels=0;
    auto* processor=const_cast<LatencyProcessor*>(static_cast<const LatencyProcessor*>(f.tap->structuralZeroHistoryRuntimeToken()));
    processor->prepareToPlay(rate,maximumBlock,2); // Actual task-owned public config fault; no callback races.
    require(processor->hasConfiguration(delay,rate,2)&&!f.tap->hasPreparedNoAudioOutput()&&!audioZero(*f.tap),
        "Actual FINAL processor late two-channel reprepare refuses despite unchanged zero input properties");
    // This deliberately broken fixture is retired without processing/resetting
    // it. Nothing is cleared/re-certified to manufacture successful playback.
}
inline void unknownStereo(const juce::File& folder,bool pulse)
{
    require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh unknown stereo negative");
    ready_graph_acceptance::Counts pool;
    LockFreeMultiThreadedNodePlayer player([&](auto& p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,pool);});
    player.setNumThreads(0);player.enablePooledMemoryAllocations(false);player.enableNodeMemorySharing(false);
    auto original=std::make_unique<UnknownAudio>(pulse);auto* observedSource=original.get();
    std::unique_ptr<Node> source=std::move(original);
    auto graph=std::make_unique<LatencyNode>(std::move(source),delay);auto* tap=graph.get();
    player.setNode(std::move(graph),rate,maximumBlock);
    choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2,maximumBlock));
    tracktion::MidiMessageArray midi;midi.reserve(16);
    std::vector<float> actual,expected;actual.reserve(1280);expected.reserve(1280);
    juce::String rows="start,error,sourceCalls,new,delete,seconds,metadata,certificate\n";
    for(int callback=0;callback<5;++callback)
    {
        pcm.clear();midi.clear();const auto calls=observedSource->calls;
        const auto allocations=callbackAllocations.load(),destructions=callbackDestructions.load();
        const auto started=juce::Time::getHighResolutionTicks();
        {
            struct Probe {bool previous=callbackAllocationProbe;Probe(){callbackAllocationProbe=true;}
                ~Probe(){callbackAllocationProbe=previous;}} probe;
            player.process({maximumBlock,{callback*maximumBlock,(callback+1)*maximumBlock},{pcm.getView(),midi}});
        }
        const auto seconds=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-started);
        double error=0;
        for(int c=0;c<2;++c)for(int n=0;n<maximumBlock;++n)
        {
            const auto value=pcm.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n));
            const float wanted=pulse&&callback*maximumBlock+n>=delay?(c?-.0625f:.125f):0.f;
            actual.push_back(value);expected.push_back(wanted);
            error=std::isfinite(value)?std::max(error,std::abs(double(value)-wanted)):INFINITY;
        }
        const bool refused=!tap->hasPreparedNoAudioOutput()&&!audioZero(*tap)&&!tap->hasStructuralZeroHistory();
        rows+=juce::String(callback*maximumBlock)+","+juce::String(error,12)+","+
            juce::String(observedSource->calls)+","+juce::String(callbackAllocations.load()-allocations)+","+
            juce::String(callbackDestructions.load()-destructions)+","+juce::String(seconds,9)+","+
            juce::String(int(tap->hasPreparedNoAudioOutput()))+","+juce::String(int(tap->hasStructuralZeroHistory()))+"\n";
        require(folder.getChildFile("callbacks.csv").replaceWithText(rows),"Retain every warm unknown-queue callback");
        for(int which=0;which<2;++which)
        {
            const auto& values=which?expected:actual;
            auto stream=folder.getChildFile(which?"expected-call-planar-f32.raw":"actual-call-planar-f32.raw").createOutputStream();
            require(stream&&stream->setPosition(0)&&stream->truncate().wasOk()&&
                stream->write(values.data(),values.size()*sizeof(float)),"Retain unknown-queue complete PCM before assert");
            stream->flush();require(stream->getStatus().wasOk(),"Flush unknown-queue PCM");
        }
        require(refused&&error==0&&observedSource->calls==calls+1&&midi.isEmpty()&&!midi.isAllNotesOff&&
            callbackAllocations.load()==allocations&&callbackDestructions.load()==destructions&&seconds<=maximumBlock/rate,
            "Unknown warm zero/nonzero stereo queue still refuses; literal full delay PCM/once/heap/deadline retained");
    }
}
inline void tests(const juce::File& folder)
{
    require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh original zero-channel aggregate");
    std::exception_ptr first;juce::String outcomes;
    for(int mode=0;mode<5;++mode)
    {
        try
        {
            const auto child=folder.getChildFile("case"+juce::String(mode));
            if(mode<2)reuse(child,mode==1);else if(mode==2)negatives(child);else unknownStereo(child,mode==4);
            outcomes+=juce::String(mode)+"=completed\n";
        }
        catch(const std::exception& error)
        {if(!first)first=std::current_exception();outcomes+=juce::String(mode)+"=failed:"+juce::String(error.what())+"\n";}
        require(folder.getChildFile("cases.txt").replaceWithText(outcomes),"Retain all independent original case outcomes");
    }
    if(first)std::rethrow_exception(first);
}
} // namespace zero_channel_delay_acceptance
