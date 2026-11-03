// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AppAudioPortMidiOwnershipAcceptance.h"
#include <thread>
namespace vibera::test::native_midi_ingress
{
using Injector = te::LiveMidiInjectingNode;
using Fault = Injector::IngressFault;
class ShortSource final : public tracktion::graph::Node
{
    unsigned& calls;
    const int events;
    const int malformedStatus;
public:
    ShortSource(unsigned& n, int count, int badStatus=0) : calls(n), events(count), malformedStatus(badStatus) {}
    tracktion::graph::NodeProperties getNodeProperties() override { return {true,true,2,0,0x494e475245535353ULL}; }
    bool isReadyToProcess() override { return true; }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo&) override
    { reserveMidiOutputForPreparation(events+(malformedStatus?1:0)); }
    void process(ProcessContext& pc) override
    {
        ++calls;
        for (choc::buffer::ChannelCount c=0;c<2;++c)
            for(choc::buffer::FrameCount n=0;n<pc.buffers.audio.getNumFrames();++n)
                pc.buffers.audio.getSample(c,n)=c ? -.0625f : .125f;
        if(malformedStatus)
            pc.buffers.midi.addMidiMessage(juce::MidiMessage(malformedStatus,255,100),0.,9);
        for(int n=0;n<events;++n)
            pc.buffers.midi.addMidiMessage(juce::MidiMessage::noteOn(1,n%128,juce::uint8(100)),0.,9);
    }
};
inline void scenario(const juce::File& folder, int mode)
{
    require(!folder.exists()&&folder.createDirectory().wasOk(), "Fresh direct ingress evidence");
    app_audio_port::Settings settings(folder.getChildFile("settings"));
    auto audio=std::make_unique<AudioEngine>(false,folder.getChildFile("guide.json"));
    te::Edit::Options options{audio->engine,te::createEmptyEdit(audio->engine),{}};
    options.numAudioTracks=1; options.role=te::Edit::forRendering;
    options.closedNativeIO=true; options.initialSourceGeneration=911;
    auto edit=te::Edit::createEdit(options); require(bool(edit),"Actual scoped closed Edit");
    auto* track=te::getAudioTracks(*edit)[0];
    tracktion::graph::PlayHead head; tracktion::graph::PlayHeadState headState(head);
    te::ProcessState process(headState,edit->tempoSequence);
    ready_graph_acceptance::Counts pools;
    ready_graph_acceptance::Player player([&](auto& p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,pools);});
    player.setNumThreads(0);player.enablePooledMemoryAllocations(false);
    unsigned calls=0;
    auto root=std::make_unique<Injector>(*track,std::make_unique<ShortSource>(calls,mode==1?513:0,mode==7?0x90:mode==8?0x80:0),&process);
    auto* injector=root.get();player.setNode(std::move(root),48000,256);
    choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2,256));
    tracktion::MidiMessageArray midi;midi.reserve(1024);
    juce::String receipt="Direct actual native injector and preallocated source/sink ONLY; no downstream PluginNode/physical-device qualification.\n";
    const auto retain=[&]
    {
        juce::FileOutputStream out(folder.getChildFile("dsp.txt"));
        require(out.openedOk()&&out.setPosition(0)&&out.truncate().wasOk()&&out.writeText(receipt,false,false,"\n"),"Retain direct ingress facts");
        out.flush();require(out.getStatus().wasOk(),"Flush direct ingress facts");
    };
    const auto inject=[&](const juce::MidiMessage& m){track->injectLiveMidiMessage(m,7);};
    if(mode==2)
    {
        const std::array<juce::uint8,4> data{1,2,3,4};
        inject(juce::MidiMessage::createSysExMessage(data.data(),int(data.size()))); // CONTROL only allocation.
    }
    if(mode==3) for(int n=0;n<513;++n) inject(juce::MidiMessage::noteOn(1,n%128,juce::uint8(100)));
    if(mode==4)
    {
       #if JUCE_DEBUG
        throw std::runtime_error("Wrong-thread native AudioTrack API is assertion-guarded in Debug; Release-only negative required");
       #else
        std::thread rejected([&]{inject(juce::MidiMessage::noteOn(1,64,juce::uint8(100)));});rejected.join();
       #endif
    }
    if(mode==5||mode==6)inject(juce::MidiMessage(mode==5?0x90:0x80,255,100)); // Genuine raw malformed bytes; never normalize.
    const std::array<int,5> spans{1,128,256,1,64};
    juce::int64 reference=0;
    for(int callback=0;callback<(mode==0?int(spans.size()):1);++callback)
    {
        const int frames=mode==0?spans[std::size_t(callback)]:256;
        if(mode==0)
        {
            inject(juce::MidiMessage::noteOn(1,64,juce::uint8(100)));
            inject(juce::MidiMessage::noteOff(1,64));
            inject(juce::MidiMessage::allNotesOff(1));
        }
        midi.clear();pcm.clear();const auto beforeCalls=calls;
        const auto allocations=callbackAllocations.load(),destructions=callbackDestructions.load();
        const auto started=juce::Time::getHighResolutionTicks();
        {
            struct Probe {const bool before=callbackAllocationProbe;Probe(){callbackAllocationProbe=true;}~Probe(){callbackAllocationProbe=before;}} probe;
            player.process({choc::buffer::FrameCount(frames),{reference,reference+frames},{pcm.getView().getStart(choc::buffer::FrameCount(frames)),midi}});
        }
        const double elapsed=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-started);
        const double deadline=double(frames)/48000.;const auto state=injector->boundedIngressObservation();
        bool audioExact=true;
        for(int c=0;c<2;++c) for(int n=0;n<frames;++n)
            audioExact &= pcm.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n))==(c?-.0625f:.125f);
        bool eventExact=midi.size()==3;
        if(eventExact) eventExact=midi[0].isNoteOn()&&midi[0].getNoteNumber()==64&&midi[1].isNoteOff()&&
            midi[1].getNoteNumber()==64&&midi[2].isAllNotesOff()&&midi[0].mpeSourceID==7&&midi[1].mpeSourceID==7&&midi[2].mpeSourceID==7;
        const auto expectedFault=mode==0?Fault::none:mode==1?Fault::sourceQuota:mode==2?Fault::unsupportedMessage:mode==3?Fault::queueFull:mode==4?Fault::wrongProducer:mode==5||mode==6?Fault::unsupportedMessage:Fault::sourceMessage;
        receipt+="callback="+juce::String(callback)+",frames="+juce::String(frames)+",fault="+juce::String(int(state.fault))+
            ",accepted="+juce::String(juce::int64(state.accepted))+",delivered="+juce::String(juce::int64(state.delivered))+
            ",events="+juce::String(midi.size())+",new="+juce::String(juce::int64(callbackAllocations.load()-allocations))+
            ",delete="+juce::String(juce::int64(callbackDestructions.load()-destructions))+
            ",seconds="+juce::String(elapsed,12)+",deadline="+juce::String(deadline,12)+",audioExact="+juce::String(int(audioExact))+"\n";
        retain();
        require(state.enabled&&state.registered&&state.scope==&process&&state.fault==expectedFault&&audioExact&&calls==beforeCalls+1&&
            callbackAllocations.load()==allocations&&callbackDestructions.load()==destructions&&elapsed<=deadline,
            "Actual injector exact audio/source/typed fault/noheap/unchanged frames-rate deadline");
        require(mode==0 ? eventExact&&state.accepted==uint64_t((callback+1)*3)&&state.delivered==state.accepted
                        : midi.isEmpty()&&state.delivered==0&&state.accepted==uint64_t(mode==3?512:0),
                "Exact accepted/delivered events or explicit fault without consuming retained queue");
        reference+=frames;
    }
}
// Real independent ProcessState instances on one actual Track: each selected
// player owns its own queue; global wasUsed must not suppress the counterpart.
inline void contextIsolation(const juce::File& folder)
{
    require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh independent context evidence");
    app_audio_port::Settings settings(folder.getChildFile("settings"));
    auto audio=std::make_unique<AudioEngine>(false,folder.getChildFile("guide.json"));
    te::Edit::Options options{audio->engine,te::createEmptyEdit(audio->engine),{}};
    options.numAudioTracks=1;options.role=te::Edit::forRendering;
    options.closedNativeIO=true;options.initialSourceGeneration=912;
    auto edit=te::Edit::createEdit(options);require(bool(edit),"Independent-context actual Edit");
    auto* track=te::getAudioTracks(*edit)[0];
    tracktion::graph::PlayHead headA,headB;
    tracktion::graph::PlayHeadState stateA(headA),stateB(headB);
    te::ProcessState scopeA(stateA,edit->tempoSequence),scopeB(stateB,edit->tempoSequence);
    ready_graph_acceptance::Counts countsA,countsB;
    ready_graph_acceptance::Player playerA([&](auto& p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,countsA);});
    ready_graph_acceptance::Player playerB([&](auto& p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,countsB);});
    playerA.setNumThreads(0);playerB.setNumThreads(0);
    playerA.enablePooledMemoryAllocations(false);playerB.enablePooledMemoryAllocations(false);
    unsigned callsA=0,callsB=0;
    auto rootA=std::make_unique<Injector>(*track,std::make_unique<ShortSource>(callsA,0),&scopeA);
    auto rootB=std::make_unique<Injector>(*track,std::make_unique<ShortSource>(callsB,0),&scopeB);
    auto* ingressA=rootA.get();auto* ingressB=rootB.get();
    playerA.setNode(std::move(rootA),48000,256);playerB.setNode(std::move(rootB),48000,256);
    choc::buffer::ChannelArrayBuffer<float> pcmA(choc::buffer::Size::create(2,256)),pcmB(choc::buffer::Size::create(2,256));
    tracktion::MidiMessageArray midiA,midiB;midiA.reserve(512);midiB.reserve(512);
    track->injectLiveMidiMessage(juce::MidiMessage::noteOn(1,65,juce::uint8(99)),13);
    playerA.process({256,{0,256},{pcmA.getView(),midiA}});
    const auto a=ingressA->boundedIngressObservation(), beforeB=ingressB->boundedIngressObservation();
    playerB.process({256,{0,256},{pcmB.getView(),midiB}});
    const auto b=ingressB->boundedIngressObservation();
    bool exact=true;
    for(int c=0;c<2;++c)for(int n=0;n<256;++n)
        exact &= pcmA.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n))==(c?-.0625f:.125f)&&
                 pcmB.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n))==(c?-.0625f:.125f);
    const juce::String facts="Distinct real ProcessState/player contexts, one actual Track injection. Functional isolation, no downstream/device timing claim.\n"
        +juce::String("Aaccepted=")+juce::String(juce::int64(a.accepted))+",Adelivered="+juce::String(juce::int64(a.delivered))+
        ",BbeforeDelivered="+juce::String(juce::int64(beforeB.delivered))+",Baccepted="+juce::String(juce::int64(b.accepted))+
        ",Bdelivered="+juce::String(juce::int64(b.delivered))+",Afault="+juce::String(int(a.fault))+",Bfault="+juce::String(int(b.fault))+"\n";
    juce::FileOutputStream out(folder.getChildFile("isolation.txt"));
    require(out.openedOk()&&out.setPosition(0)&&out.truncate().wasOk()&&out.writeText(facts,false,false,"\n"),"Retain both actual context outcomes");
    out.flush();require(out.getStatus().wasOk(),"Flush context evidence");
    require(a.runtime&&b.runtime&&a.runtime!=b.runtime&&a.scope==&scopeA&&b.scope==&scopeB&&
        a.fault==Fault::none&&b.fault==Fault::none&&a.accepted==1&&a.delivered==1&&
        beforeB.accepted==1&&beforeB.delivered==0&&b.accepted==1&&b.delivered==1&&callsA==1&&callsB==1&&exact,
        "Independent actual scopes do not steal, suppress or duplicate counterpart ingress");
    require(midiA.size()==1&&midiB.size()==1&&midiA[0].isNoteOn()&&midiB[0].isNoteOn()&&
        midiA[0].getNoteNumber()==65&&midiB[0].getNoteNumber()==65&&midiA[0].mpeSourceID==13&&midiB[0].mpeSourceID==13,
        "One exact original short event per real context");
}
inline void tests(const juce::File& folder)
{
    require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh independent ingress DSP cases");
    const std::array<const char*,9> names{"short_note_off_panic","upstream_quota","unsupported_sysex","full_retains_queue","wrong_producer_release","raw_note_on_data_refused","raw_note_off_data_refused","upstream_raw_note_on_refused","upstream_raw_note_off_refused"};
    juce::String failures;
    for(int mode=0;mode<int(names.size());++mode)
        try{scenario(folder.getChildFile(names[std::size_t(mode)]),mode);}
        catch(const std::exception& e){failures+=juce::String(names[std::size_t(mode)])+": "+e.what()+"\n";}
        catch(...){failures+=juce::String(names[std::size_t(mode)])+": unknown failure\n";}
    try{contextIsolation(folder.getChildFile("independent_contexts"));}
    catch(const std::exception& e){failures+="independent_contexts: "+juce::String(e.what())+"\n";}
    catch(...){failures+="independent_contexts: unknown failure\n";}
    juce::FileOutputStream out(folder.getChildFile("aggregate.txt"));
    require(out.openedOk()&&out.setPosition(0)&&out.truncate().wasOk()&&out.writeText(failures.isEmpty()?"PASSED all ten direct ingress cases\n":failures,false,false,"\n"),"Retain direct aggregate");
    out.flush();require(out.getStatus().wasOk(),"Flush direct aggregate");
    require(failures.isEmpty(),"All direct native ingress cases must pass");
}
} // namespace vibera::test::native_midi_ingress
