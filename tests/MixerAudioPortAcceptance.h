// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <array>
#include <vector>
#include <memory>
#include <cmath>
#include <limits>

// Include after ReadyGraphAcceptance.h and actual callback heap/timing helpers.
// Original finite source/port-consumer experiment; no application/provider/device.
namespace mixer_audio_port_acceptance
{
using namespace tracktion::graph;
struct Owner {};
struct Counts { unsigned source = 0, control = 0, consumer = 0, stage = 0, anchor = 0; };
struct Event { juce::int64 frame; unsigned char status, key, velocity; };
inline constexpr std::array<Event, 6> events{{
    {0, 0x90, 60, 100}, {64, 0x80, 60, 0}, {127, 0x90, 64, 90},
    {128, 0x80, 64, 0}, {273, 0x90, 67, 80}, {511, 0x80, 67, 0}}};
inline float sourceSample(int channel, juce::int64 reference) noexcept
{
    return channel == 0 ? .125f + (reference % 53 == 3 ? .25f : 0.f)
                        : -.0625f - (reference % 47 == 17 ? .125f : 0.f);
}
class MixedSource final : public Node
{
    Counts &counts;
    const double rate;
public:
    MixedSource(Counts &c, double r) : counts(c), rate(r)
    { setOptimisations({ClearBuffers::yes, AllocateAudioBuffer::yes, ShareOutputBuffer::no}); }
    NodeProperties getNodeProperties() override { return {true, true, 2, 0, 0x41504d4958535243ULL}; }
    bool isReadyToProcess() override { return true; }
    void process(ProcessContext &pc) override
    {
        ++counts.source;
        for (choc::buffer::ChannelCount c = 0; c < 2; ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
                pc.buffers.audio.getSample(c,n) = sourceSample(int(c),pc.referenceSampleRange.getStart()+n);
        for (const auto &e : events)
            if (pc.referenceSampleRange.contains(e.frame))
                pc.buffers.midi.addMidiMessage(juce::MidiMessage(e.status,e.key,e.velocity),
                    double(e.frame-pc.referenceSampleRange.getStart())/rate, 7);
        pc.buffers.midi.isAllNotesOff = pc.referenceSampleRange.contains(256);
    }
};
class Control final : public Node
{
    const std::shared_ptr<Node> input;
    Counts &counts;
public:
    Control(std::shared_ptr<Node> n, Counts &c) : input(std::move(n)), counts(c)
    { setOptimisations({ClearBuffers::yes, AllocateAudioBuffer::yes, ShareOutputBuffer::no}); }
    NodeProperties getNodeProperties() override { return {true,true,2,0,0x4150434f4e54524cULL}; }
    std::vector<Node*> getDirectInputNodes() override { return {input.get()}; }
    bool isReadyToProcess() override { return input->hasProcessed(); }
    void process(ProcessContext &pc) override
    {
        ++counts.control;
        const auto source=input->getProcessedOutput();
        for (choc::buffer::ChannelCount c=0;c<2;++c)
            for(choc::buffer::FrameCount n=0;n<pc.buffers.audio.getNumFrames();++n)
                pc.buffers.audio.getSample(c,n)=source.audio.getSample(c,n)*.5f;
        pc.buffers.midi.copyFrom(source.midi); // Actual combined control output preserves ALL events/panic.
    }
};
// A real terminal consumer, not a metadata-only filter. It records each routed
// event/panic into fixed storage. It produces no MIDI output by definition.
class MidiConsumer final : public Node
{
    const std::shared_ptr<Node> input;
    Counts &counts;
    const double rate;
public:
    std::array<Event,16> received{};
    std::array<int,16> sources{};
    unsigned count=0;
    bool panic=false, invalid=false;
    MidiConsumer(std::shared_ptr<Node> n, Counts &c, double r) : input(std::move(n)), counts(c), rate(r) {}
    NodeProperties getNodeProperties() override { return {false,false,0,0,0x41504d494449534bULL}; }
    std::vector<Node*> getDirectInputNodes() override { return {input.get()}; }
    bool isReadyToProcess() override { return input->hasProcessed(); }
    void process(ProcessContext &pc) override
    {
        ++counts.consumer; count=0; invalid=false;
        const auto data=input->getProcessedOutput(); panic=data.midi.isAllNotesOff;
        for (const auto &m:data.midi)
        {
            const auto frame=std::llround(m.getTimeStamp()*rate);
            if(count>=received.size() || m.getRawDataSize()!=3 || frame<0 || frame>=pc.referenceSampleRange.getLength())
            { invalid=true; continue; }
            const auto *bytes=m.getRawData();
            received[count]={pc.referenceSampleRange.getStart()+frame,bytes[0],bytes[1],bytes[2]};
            sources[count]=int(m.mpeSourceID); ++count;
        }
    }
};
// The output stage starts AFTER actual audio-port selection. It does not
// override hasMidi: getNodeProperties inherits the real ConnectedNode result.
class AudioStage final : public Node
{
    const std::shared_ptr<ConnectedNode> audioPort;
    const std::shared_ptr<MidiConsumer> consumer;
    const std::shared_ptr<Owner> owner;
    Counts &counts;
public:
    AudioStage(std::shared_ptr<ConnectedNode> a,std::shared_ptr<MidiConsumer> m,std::shared_ptr<Owner> o,Counts &c)
        : audioPort(std::move(a)),consumer(std::move(m)),owner(std::move(o)),counts(c)
    { setOptimisations({ClearBuffers::yes,AllocateAudioBuffer::yes,ShareOutputBuffer::no}); }
    NodeProperties getNodeProperties() override
    {
        auto p=audioPort->getNodeProperties(); p.nodeID=0x4150415544494f53ULL;
        p.audioCompensationStage=owner.get(); p.audioCompensationStageOrigin=this; return p;
    }
    std::vector<Node*> getDirectInputNodes() override { return {audioPort.get(),consumer.get()}; }
    bool isReadyToProcess() override { return audioPort->hasProcessed() && consumer->hasProcessed(); }
    void process(ProcessContext &pc) override
    {
        ++counts.stage;
        const auto source=audioPort->getProcessedOutput();
        for(choc::buffer::ChannelCount c=0;c<2;++c)
            for(choc::buffer::FrameCount n=0;n<pc.buffers.audio.getNumFrames();++n)
                pc.buffers.audio.getSample(c,n)=source.audio.getSample(c,n);
        // Audio connection has no MIDI; no supplied MIDI buffer is cleared here.
    }
};
class ZeroAnchor final : public Node
{
    Counts &counts;
    std::array<std::array<float,128>,2> delay{};
    unsigned cursor=0;
public:
    explicit ZeroAnchor(Counts &c):counts(c){}
    NodeProperties getNodeProperties() override {return {true,false,2,128,0x4150414e43484f52ULL};}
    bool isReadyToProcess() override{return true;}
    void process(ProcessContext &pc) override
    {
        ++counts.anchor;
        for(choc::buffer::FrameCount n=0;n<pc.buffers.audio.getNumFrames();++n)
        {
            for(choc::buffer::ChannelCount c=0;c<2;++c)
            {pc.buffers.audio.getSample(c,n)=delay[c][cursor];delay[c][cursor]=0.f;}
            cursor=(cursor+1)%128;
        }
    }
};
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),"Fresh audio-port proof output");
    juce::String report="Actual native ConnectedNode audio/MIDI ports; no app/worker/device/loop claim.\n";
    std::vector<float> actual,expected;actual.reserve(32768);expected.reserve(32768);
    const auto retain=[&]
    {
        for(int which=0;which<2;++which)
        {
            const auto &data=which?expected:actual;
            juce::FileOutputStream out(folder.getChildFile(which?"expected-f32.raw":"actual-f32.raw"));
            require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                out.write(data.data(),data.size()*sizeof(float)),"Retain every audio-port sample");
            out.flush();require(out.getStatus().wasOk(),"Flush audio-port samples");
        }
        juce::FileOutputStream out(folder.getChildFile("ports.txt"));
        require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
            out.writeText(report,false,false,"\n"),"Retain all actual port evidence");
        out.flush();require(out.getStatus().wasOk(),"Flush port evidence");
    };
    try
    {
        for(const double rate:{48000.,44100.})
        {
            ready_graph_acceptance::Counts pool;
            Counts counts;
            auto owner=std::make_shared<Owner>();
            auto source=std::make_shared<MixedSource>(counts,rate);
            auto control=std::make_shared<Control>(source,counts);
            auto audio=std::make_shared<ConnectedNode>(0x4150415544494f50ULL);
            auto midi=std::make_shared<ConnectedNode>(0x41504d494449504fULL);
            require(audio->addAudioConnection(control,{0,0}) && audio->addAudioConnection(control,{1,1}) &&
                midi->addMidiConnection(control),"Real explicit audio and independent MIDI connections");
            auto consumer=std::make_shared<MidiConsumer>(midi,counts,rate);
            auto stage=std::make_unique<AudioStage>(audio,consumer,owner,counts);
            auto *stageRaw=stage.get();
            std::vector<std::unique_ptr<Node>> inputs;inputs.push_back(std::move(stage));
            inputs.push_back(std::make_unique<ZeroAnchor>(counts));
            auto root=std::make_unique<SummingNode>(std::move(inputs));auto *rootRaw=root.get();
            // Existing explicit reservation only for this bounded ORIGINAL
            // producer/fan-in. No audio-history policy, no legacy MIDI queue.
            root->setMidiCompensationHistory({64,4,64,1,1024*1024});
            LockFreeMultiThreadedNodePlayer player([&](auto &p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,pool);});
            player.setNumThreads(0);player.setNode(std::move(root),rate,256);
            LatencyNode *tap=nullptr;
            for(auto *node:getNodes(*rootRaw,VertexOrdering::postordering))
                if(auto *d=dynamic_cast<LatencyNode*>(node);d && d->getNodeProperties().audioCompensationStage==owner.get())
                {require(!tap,"Unique actual audio-stage PDC queue");tap=d;}
            require(tap && tap->compensationSamples()==128 && !tap->getNodeProperties().hasMidi &&
                !stageRaw->getNodeProperties().hasMidi && !audio->getNodeProperties().hasMidi &&
                midi->getNodeProperties().hasMidi && source->getNodeProperties().hasMidi && control->getNodeProperties().hasMidi &&
                !source->getNodeProperties().audioCompensationStage && !control->getNodeProperties().audioCompensationStage,
                "Stage begins after real port; mixed source stays unregistered/MIDI-capable");
            choc::buffer::ChannelArrayBuffer<float> output{choc::buffer::Size::create(2,256)};
            tracktion::MidiMessageArray sink;sink.reserve(64);
            std::array<std::array<float,128>,2> oracle{};unsigned cursor=0;
            // Actual native reference gaps/backward jumps. This Player has no
            // musical PlayHead; these are NOT claimed transport-loop tests.
            constexpr std::array<int,15> sizes{128,1,63,64,17,111,256,31,97,128,64,1,127,128,256};
            juce::int64 reference=0;
            for(size_t call=0;call<sizes.size();++call)
            {
                if(call==8)reference+=41;
                if(call==10)reference=120;
                const int frames=sizes[call];const auto before=counts;
                const auto allocations=callbackAllocations.load(),destructions=callbackDestructions.load();
                output.clear();sink.clear();
                const auto began=juce::Time::getHighResolutionTicks();
                {struct Probe{bool old=callbackAllocationProbe;Probe(){callbackAllocationProbe=true;}~Probe(){callbackAllocationProbe=old;}}probe;
                    player.process({choc::buffer::FrameCount(frames),{reference,reference+frames},
                        {output.getView().getStart(choc::buffer::FrameCount(frames)),sink}});}
                const auto elapsed=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-began);
                bool exact=true;
                for(int n=0;n<frames;++n)
                {
                    for(int c=0;c<2;++c)
                    {
                        const float wanted=oracle[c][cursor];
                        oracle[c][cursor]=sourceSample(c,reference+n)*.5f;
                        const auto value=output.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n));
                        actual.push_back(value);expected.push_back(wanted);exact&=std::isfinite(value)&&value==wanted;
                    }
                    cursor=(cursor+1)%128;
                }
                unsigned expectedEvents=0;
                for(const auto &e:events)if(e.frame>=reference && e.frame<reference+frames)
                {
                    const auto &got=consumer->received[expectedEvents];
                    exact&=expectedEvents<consumer->count && got.frame==e.frame && got.status==e.status &&
                        got.key==e.key && got.velocity==e.velocity && consumer->sources[expectedEvents]==7;
                    ++expectedEvents;
                }
                const auto port=audio->getProcessedOutput(),delayed=tap->getProcessedOutput();
                const bool clean=counts.source==before.source+1 && counts.control==before.control+1 &&
                    counts.consumer==before.consumer+1 && counts.stage==before.stage+1 && counts.anchor==before.anchor+1 &&
                    consumer->count==expectedEvents && !consumer->invalid &&
                    consumer->panic==(reference<=256 && reference+frames>256) &&
                    port.midi.isEmpty()&&!port.midi.isAllNotesOff && delayed.midi.isEmpty()&&!delayed.midi.isAllNotesOff &&
                    sink.isEmpty()&&!sink.isAllNotesOff && callbackAllocations.load()==allocations &&
                    callbackDestructions.load()==destructions && elapsed<=double(frames)/rate;
                report+="callback,rate="+juce::String(rate)+",reference="+juce::String(reference)+",frames="+juce::String(frames)+
                    ",events="+juce::String(consumer->count)+",wantedEvents="+juce::String(expectedEvents)+
                    ",panic="+juce::String(int(consumer->panic))+",source="+juce::String(counts.source)+
                    ",control="+juce::String(counts.control)+",seconds="+juce::String(elapsed,9)+
                    ",consumer="+juce::String(counts.consumer)+",stage="+juce::String(counts.stage)+
                    ",anchor="+juce::String(counts.anchor)+
                    ",portMidiEmpty="+juce::String(int(port.midi.isEmpty()&&!port.midi.isAllNotesOff))+
                    ",delayMidiEmpty="+juce::String(int(delayed.midi.isEmpty()&&!delayed.midi.isAllNotesOff))+
                    ",rootMidiEmpty="+juce::String(int(sink.isEmpty()&&!sink.isAllNotesOff))+
                    ",new="+juce::String(callbackAllocations.load()-allocations)+",delete="+
                    juce::String(callbackDestructions.load()-destructions)+",exact="+juce::String(int(exact))+"\n";
                for(unsigned i=0;i<consumer->count;++i)
                    report+="event,"+juce::String(consumer->received[i].frame)+","+juce::String(int(consumer->received[i].status))+","+
                        juce::String(int(consumer->received[i].key))+","+juce::String(int(consumer->received[i].velocity))+"\n";
                retain();require(exact&&clean,"Actual separated MIDI delivery/audio queue PCM/source-once/heap/deadline");
                reference+=frames;
            }
        }
        report+="PASS actual ports/consumer/nativePDC only; no app/worker/physical MIDI/transport-loop claim.\n";retain();
    }
    catch(...){report+="FAILED; retained complete attempted PCM/port observations.\n";retain();throw;}
}
} // namespace mixer_audio_port_acceptance
