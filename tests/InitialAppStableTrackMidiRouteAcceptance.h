// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "InitialAppZeroHistoryAcceptance.h"
#include "../src/PreparedMixerTrackMidiBoundary.h"
#include "../src/PreparedSourceExchangeRoot.h"
#include "ZeroHistoryCreationAcceptance.h"
#include "EmptyMixerLineageAcceptance.h"
#include <tracktion_engine/playback/graph/tracktion_ChannelRemappingNode.h>

// Additive actual canonical Controller + prepared whole-inventory routing experiment.
// Explicit initial option ONLY; source-authored, not production/default activation.
// Include after existing original SDK/physical/heap helpers, unchanged.
namespace initial_app_stable_track_midi_route_acceptance
{
inline void sourceMultiplicityContracts()
{
    using namespace tracktion::graph;
    struct Unknown final : Node
    {
        NodeProperties getNodeProperties() override { return {true,false,2,0,0x554e4b4e4f574eULL}; }
        bool isReadyToProcess() override { return true; }
        void process(ProcessContext&) override {}
    };
    struct Port final : Node
    {
        Node* input;
        explicit Port(Node& n):input(&n){}
        NodeProperties getNodeProperties() override { return input->getNodeProperties(); }
        std::vector<Node*> getDirectInputNodes() override { return {input}; }
        bool isReadyToProcess() override { return true; }
        void process(ProcessContext&) override {}
    };
    Unknown actualSource,unaccounted;
    const auto plugins=[](tracktion::Plugin&){return false;};
    const auto ports=[](Node& node)->std::optional<Node*>
    { if(auto* p=dynamic_cast<Port*>(&node)) return p->input; return {}; };
    const auto prove=[&](Node& root)
    { return tracktion::EditNodeBuilder::hasPreparedSingleAudioSourceOutput(root,actualSource,plugins,ports); };
    std::vector<std::unique_ptr<Node>> single;
    single.push_back(std::make_unique<Port>(actualSource)); single.push_back(std::make_unique<SilentNode>(2));
    SummingNode good(std::move(single));
    require(prove(good),"Actual native sum: one designated typed path plus independent zero");
    std::vector<std::unique_ptr<Node>> repeated;
    repeated.push_back(std::make_unique<Port>(actualSource)); repeated.push_back(std::make_unique<Port>(actualSource));
    SummingNode twice(std::move(repeated));
    require(!prove(twice),"One shared source on TWO contributing edges refuses despite DAG-node deduplication");
    std::vector<std::unique_ptr<Node>> extra;
    extra.push_back(std::make_unique<Port>(actualSource));extra.push_back(std::make_unique<Port>(unaccounted));
    SummingNode other(std::move(extra));
    require(!prove(other),"Unknown silent-looking extra audio contributor cannot join source proof");
    tracktion::ChannelRemappingNode remap(std::make_unique<Port>(actualSource),tracktion::ChannelMap::identity(2));
    require(!prove(remap),"Actual finite remapper still refuses hidden-input/mapping authority");
    Port cycle(actualSource);cycle.input=&cycle;
    require(!prove(cycle),"Active cycle refuses within bounded CONTROL traversal");
}
inline void run(const juce::File &folder, const vibera::PluginSpec &stored, bool patternMode)
{
    using namespace vibera;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual zero-history attempt");
    initial_app_source_acceptance::Storage storage(folder.getChildFile("settings"));
    Controller controller;
    auto &project = controller.project;
    project.channels.resize(1);
    project.tempo = 120;
    auto &channel = project.channels.front();
    channel.instrument = stored;
    channel.instrument->id = project.allocate();
    channel.gain = 1;
    channel.pan = 0;
    channel.mixer = 1;
    const auto channelId = channel.id, instrumentId = channel.instrument->id;
    for (auto &insert : project.mixer)
    {
        insert.effects.clear();
        insert.gain = 1;
        insert.pan = 0;
        insert.routes.clear();
        if (insert.index)
            insert.routes.push_back({0, 1.f});
    }
    project.mixer[1].gain=.25f;
    project.mixer[2].gain=.5f;
    project.patterns.resize(1);
    auto &pattern = project.patterns.front();
    pattern.length = 64;
    pattern.notes.clear();
    // Independent score pulse at literal sourceframes6000..9000 (120bpm/48k).
    // Velocity1 maps exactly to native1; no fitted phase/cursor-derived origin.
    pattern.notes.push_back({project.allocate(),channelId,.25,.125,72,1.f});
    const auto patternId = pattern.id;
    project.placements = {{project.allocate(), patternId, 0, 0, 64, 0}};
    require(project.validate().wasOk(), "Genuine one-instrument/no-FX/no-send Project");
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    auto &manager = audio->engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio->engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(device && device->getCurrentBufferSizeSamples() == 256, "Actual unchanged device Q256");
    AudioEngine::InitialSourceTransportOptions options;
    options.quantum = 256;
    options.explicitProviderFrames = 256;
    options.sessionGeneration = patternMode ? 401 : 403;
    options.membershipGeneration = patternMode ? 407 : 409;
    options.audioZeroHistoryProofs = true;
    options.zeroStageSourceRoutes = true;
    options.routingController = &controller;
    require(audio->prepareInitialSourceTransport(project, options).status ==
                    AudioEngine::InitialSourceTransportStatus::invalidProject &&
                audio->initialSourceTransportReadback().status ==
                    AudioEngine::InitialSourceTransportStatus::none,
            "Zero-history proof without genuine ports refuses atomically");
    options.audioOnlyMixerPorts = true;
    require(audio->prepareInitialSourceTransport(project, options).status ==
                    AudioEngine::InitialSourceTransportStatus::configured &&
                awaitProjection(*audio, project, patternMode, patternId).wasOk(),
            "Actual initial instrument+18-Mixer proof graph");
    const auto host = audio->parameterHost(instrumentId);
    require(host && host->latencySamples() == 512 && host->transportPacketCount() == 2 &&
                host->declaredLatency().nativeSamples == 0,
            "Literal K2Q512 source declaration");
    const auto pid = host->processId();
    const auto identity = host->runtimeIdentity();
    const auto reset = host->midiResetGeneration();
    const auto initialStats = host->statistics();
    const auto starts = device->starts, stops = device->stops;
    std::array<juce::AudioBuffer<float>, 4> buffers;
    const std::array<int, 4> lengths{64, 65, 127, 256};
    for (std::size_t i = 0; i < lengths.size(); ++i)
        buffers[i].setSize(2, lengths[i]);
    juce::AudioBuffer<float> actual(2, 16384), expected(2, 16384);
    actual.clear();
    expected.clear();
    int written = 0, on = -1, off = -1;
    float routeGain=.25f;
    juce::String routeLog="phase,beforeSelected,posted,afterSelected,revision,from,to\n";
    juce::String csv = "phase,start,frames,error,ms,new,delete,missed,dropped,cursor,sourceSerial,sourceEditFrame,leadSkip\n";
    juce::String queues = "phase,nativeOwner,build,processor,certified,createdHere,preparationObserved,memorySharing,outputZero\n";
    std::exception_ptr failure;
    const auto retainQueues = [&](const char *phase)
    {
        const auto observations = audio->initialAudioZeroQueues();
        for (const auto &item : observations)
        {
            if (item.preparationObserved)
                require(folder.getChildFile(juce::String(phase)+"-owner"+juce::String(item.nativeOwner)+"-proof.txt")
                    .replaceWithText(item.proofTrace),"Retain actual CONTROL proof tree before queue assertion");
            queues += juce::String(phase) + "," + juce::String(item.nativeOwner) + "," +
                      juce::String(item.build) + "," +
                      juce::String::toHexString(juce::pointer_sized_int(item.processor)) + "," +
                      juce::String(int(item.certified)) + "," + juce::String(int(item.createdHere)) + "," + juce::String(int(item.preparationObserved)) +
                "," + juce::String(int(item.memorySharing)) + "," + juce::String(int(item.outputZero)) + "\n";
        }
        require(folder.getChildFile("actual-final-queues.csv").replaceWithText(queues),
                "Actual queue/certificate identities retained before assertions");
        return observations;
    };
    try
    {
        audio->withInitialSourceTransportContext(
            [&](te::Edit &edit, te::EditPlaybackContext &context)
            {
                require(edit.disableNodeMemorySharing && !edit.shouldUseNodeMemorySharing(true),
                "Actual app Edit/context construction explicitly excludes memory sharing");
            juce::uint64 coldGate = 0;
                for (auto *track : te::getAudioTracks(edit))
                    if (int(track->state["viberaMixerIndex"]) == 2)
                        for (auto *plugin : track->pluginList)
                            if (dynamic_cast<MixerMasterGate *>(plugin))
                                coldGate = plugin->itemID.getRawID();
                require(coldGate != 0, "Actual insert B final original MasterGate identity");
                const auto born = retainQueues("baseline");
                const void *queue = nullptr;
                for (const auto &item : born)
                    if (item.nativeOwner == coldGate)
                    {
                        require(item.certified && item.createdHere,
                                "Actual FINAL B queue born structurally zero");
                        queue = item.processor;
                    }
                require(queue, "No fabricated absent-queue certificate");
                auto *head = context.getNodePlayHead();
                require(head, "Actual local app PlayHead");
                audio->setMidiTarget(channelId);
                audio->setLooping(false);
                audio->playStop();
                dispatchMessages(20);
                const auto origin = head->getReferenceSampleRange().getEnd();
                tracktion::AudioTrack* nativeA=nullptr;tracktion::AudioTrack* nativeB=nullptr;
                for(auto* track:tracktion::getAudioTracks(edit))
                    if(track->state.hasProperty("viberaMixerIndex"))
                    {if(int(track->state["viberaMixerIndex"])==1)nativeA=track;
                     if(int(track->state["viberaMixerIndex"])==2)nativeB=track;}
                require(nativeA&&nativeB&&nativeA!=nativeB,"Two actual native Mixer Track ingress owners");
                juce::String nativeRows="phase,mixer,nativeId,runtime,scope,readers,accepted,delivered,on,off,panics,held,ingressFault,terminalFault\n";
                const auto readNative=[&](const char* phase,int mixer)
                {
                    const auto observations=audio->initialMixerTrackMidiObservations();
                    const auto found=std::find_if(observations.begin(),observations.end(),
                        [mixer](const auto& x){return x.mixer==mixer;});
                    require(found!=observations.end(),"Actual CONTROL per-Track runtime snapshot exists");
                    const auto x=*found;
                    nativeRows+=juce::String(phase)+","+juce::String(mixer)+","+juce::String(x.nativeId)+","+
                        juce::String::toHexString(juce::pointer_sized_int(x.runtime))+","+
                        juce::String::toHexString(juce::pointer_sized_int(x.scope))+","+juce::String(x.readers)+","+
                        juce::String(x.accepted)+","+juce::String(x.delivered)+","+juce::String(x.on)+","+
                        juce::String(x.off)+","+juce::String(x.panics)+","+juce::String(x.held)+","+
                        juce::String(x.ingressFault)+","+juce::String(x.terminalFault)+"\n";
                    require(folder.getChildFile("native-track-inboxes.csv").replaceWithText(nativeRows),
                        "Retain actual inbox/terminal facts BEFORE assertions");
                    require(x.enabled&&x.registered&&x.runtime&&x.scope&&x.readers>0&&
                        !x.ingressFault&&!x.terminalFault,"Actual prepared healthy native Track/scope");
                    return x;
                };
                const auto baseA=readNative("baseline",1),baseB=readNative("baseline",2);
                require(baseA.runtime!=baseB.runtime&&baseA.scope==baseB.scope&&
                    baseA.nativeId==nativeA->itemID.getRawID()&&baseB.nativeId==nativeB->itemID.getRawID(),
                    "Different actual Track owners share only the real ProcessState");
                const auto injectNative=[&](int mixer,const juce::MidiMessage& message)
                {
                    const auto before=readNative("before_native_enqueue",mixer);
                    (mixer==1?nativeA:nativeB)->injectLiveMidiMessage(message,tracktion::MPESourceID(700+mixer));
                    const auto after=readNative("accepted_unconsumed",mixer);
                    require(after.runtime==before.runtime&&after.accepted==before.accepted+1&&
                        after.delivered==before.delivered,"Genuine accepted/pending native inbox, not assumed silence");
                };
                const auto checkNative=[&](const char* phase,int mixer,std::uint64_t ons,std::uint64_t offs,
                    std::uint64_t panics,unsigned held)
                {
                    const auto x=readNative(phase,mixer);const auto& original=mixer==1?baseA:baseB;
                    require(x.runtime==original.runtime&&x.nativeId==original.nativeId&&x.scope==original.scope&&
                        x.accepted==x.delivered&&x.on==original.on+ons&&x.off==original.off+offs&&
                        x.panics==original.panics+panics&&x.held==held,
                        "Each original Track terminal receives once; no inbox swap to routed destination");
                };
                const auto drive = [&](const char *phase, int count)
                {
                    juce::AudioBuffer<float> *block = nullptr;
                    for (std::size_t i = 0; i < lengths.size(); ++i)
                        if (lengths[i] == count)
                            block = &buffers[i];
                    require(block && written + count <= actual.getNumSamples(),
                            "Actual prepared physical span");
                    const auto before = audio->projectionDiagnostics();
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    const auto start = juce::Time::getHighResolutionTicks();
                    {
                        struct Probe
                        {
                            bool old = callbackAllocationProbe;
                            Probe()
                            {
                                callbackAllocationProbe = true;
                            }
                            ~Probe()
                            {
                                callbackAllocationProbe = old;
                            }
                        } probe;
                        device->process(*block);
                    }
                    const auto ms =
                        SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
                    double error = 0;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < count; ++n)
                        {
                            const auto source = written + n - 512;
                            const bool held = on >= 0 && source >= on && (off < 0 || source < off);
                            const bool pulse=source>=6000 && source<9000;
                            const double ons=source>=6000 ? 2. : 1.;
                            const double amplitude=c==0 ?
                                ((held ? 61./512 : 0.)+(pulse ? 73./512 : 0.)+ons/4096.) :
                                -((held ? 68./512 : 0.)+(pulse ? 56./512 : 0.)+ons/8192.);
                            const float wanted=(held || pulse) ? float(routeGain*float(.25*amplitude)) : 0.f;
                            const float observed = block->getSample(c, n);
                            actual.setSample(c, written + n, observed);
                            expected.setSample(c, written + n, wanted);
                            error = std::isfinite(observed)
                                        ? std::max(error, std::abs(double(observed) - wanted))
                                        : std::numeric_limits<double>::infinity();
                        }
                    written += count;
                    dispatchMessages(6);
                    host->service();
                    const auto after = audio->projectionDiagnostics();
                    const auto source = audio->initialSourceTransportReadback();
                    const auto stats = host->statistics();
                    const auto range = head->getReferenceSampleRange();
                    csv += juce::String(phase) + "," + juce::String(written - count) + "," +
                           juce::String(count) + "," + juce::String(error, 12) + "," + juce::String(ms, 6) +
                           "," + juce::String(callbackAllocations.load() - allocations) + "," +
                           juce::String(callbackDestructions.load() - destructions) + "," +
                           juce::String(juce::uint64(stats.missed)) + "," +
                           juce::String(juce::uint64(stats.dropped)) + "," +
                           juce::String(host->packetFrame()) + "," +
                           juce::String(source.native.physicalSerial) + "," +
                           juce::String(after.channels[0].lastTime * 48000., 12) + "," +
                           juce::String(after.channels[0].lastLeadSkip) + "\n";
                    require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                            "Every callback retained before oracle");
                    if (error != 0)
                    {
                        const auto native = parameterCatalogue(*host, true);
                        require(folder.getChildFile("first-pcm-mismatch-native.txt").replaceWithText(
                                    "sourceEditFrame=" + juce::String(after.channels[0].lastTime * 48000., 12) +
                                    "\nsourceSpan=" + juce::String(count) +
                                    "\nnativeOn=" + juce::String(nativeParameter(native, 90).value, 12) +
                                    "\nnativeOff=" + juce::String(nativeParameter(native, 91).value, 12)),
                                "Retain actual native score/clock facts; no oracle adjustment");
                    }
                    require(
                        error == 0 && ms <= 1000. * count / 48000. &&
                            callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == destructions && before.edit == after.edit &&
                            before.playbackContext == after.playbackContext && after.channels.size() == 1 &&
                            after.channels[0].renderCalls == before.channels[0].renderCalls + 1 &&
                            after.headPlaying && after.transportPlaying &&
                            range.getEnd() == origin + written &&
                            range.getStart() == origin + written - count &&
                            host->packetFrame() == written % 256 &&
                            stats.submitted == initialStats.submitted + std::uint64_t(written / 256) &&
                            stats.missed == initialStats.missed && stats.dropped == initialStats.dropped &&
                            stats.parametersRejected == initialStats.parametersRejected &&
                            host->processId() == pid && host->runtimeIdentity() == identity &&
                            host->midiResetGeneration() == reset && source.pairedClosed &&
                            source.status == AudioEngine::InitialSourceTransportStatus::ready &&
                            context.getGraphSelection().latencySamples == 512 && source.memberCount == 1 &&
                            source.native.sourceProcessed == count && source.native.sourceQuota == count &&
                            device->starts == starts && device->stops == stops,
                        "Literal full startup/held/release PCM, source once, native "
                        "clock/scope/lifetime/heap/deadline");
                };
                for (int n = 0; n < 8; ++n)
                    drive("startup", 256);
                audio->midiInput.capture.begin();
                audio->typingInput.capture.begin();
                on = written;
                audio->midiInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("physical_on", 127);
                audio->typingInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                for (int frames : {64, 64, 65, 256})
                    drive("both_held", frames);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 2,
                        "Real physical+typing held ownership");
                for (int malformed = 0; malformed < 3; ++malformed)
                {
                    auto invalid = project;
                    const auto nan = std::numeric_limits<float>::quiet_NaN();
                    if (malformed == 0)
                        invalid.mixer[2].gain = nan;
                    else if (malformed == 1)
                        invalid.mixer[2].routes.front().gain = nan;
                    else
                        invalid.channels.front().pan = nan;
                    const auto selection = context.getGraphSelection().selectedSerial;
                    const auto state = controller.project.state();
                    const auto revision = controller.revision;
                    require(invalid.validate().failed() &&
                                audio->rebuild(invalid, patternMode, patternId).failed() &&
                                context.getGraphSelection().selectedSerial == selection &&
                                controller.project.state().isEquivalentTo(state) &&
                                controller.revision == revision,
                            "Nonfinite gain/route/pan refuses before graph, document or history mutation");
                    const auto observations = retainQueues("after_nonfinite_document_refusal");
                    bool unchanged = false;
                    for (const auto &item : observations)
                        if (item.nativeOwner == coldGate)
                            unchanged = item.processor == queue && item.certified;
                    require(unchanged, "Malformed document cannot replace or taint actual certified queue");
                    drive("first_pcm_after_nonfinite_document_refusal", 256);
                }

                const auto prepare = [&](int target, std::unique_ptr<PreparedControllerChange>& command,
                                         std::unique_ptr<AudioEngine::PreparedChannelRouting>& graph)
                {
                    CommandArgs args;args.channel=channelId;args.mixer=target;
                    require(controller.prepareStructuralCommand("channel.mixer",args,command).wasOk() &&
                        command && !command->isNoOp(),"Actual canonical route mutation/history prepared privately");
                    const auto result=audio->prepareInitialSourceChannelRouting(controller,*command,graph);
                    require(folder.getChildFile("last-prepare.txt").replaceWithText(
                        juce::String(int(result.status))+" "+result.message),"Retain actual native prepare refusal");
                    require(result.status==AudioEngine::ChannelRoutingStatus::ready && graph && !graph->wasPosted(),
                        "Whole final inventory, actual zero queues, exact source/session/Host prepared");
                };
                const auto publish = [&](const char* phase, std::unique_ptr<PreparedControllerChange>& command,
                                         std::unique_ptr<AudioEngine::PreparedChannelRouting>& graph, int target)
                {
                    const auto beforeSerial=context.getGraphSelection().selectedSerial;
                    const auto serial=graph->publicationSerial();
                    const auto from=project.channel(channelId)->mixer;
                    require(audio->validateInitialSourceChannelRouting(controller,*command,*graph).status==
                        AudioEngine::ChannelRoutingStatus::ready,"Final SAME Controller+audio admission");
                    require(audio->postChannelRouting(*graph)==AudioEngine::ChannelRoutingStatus::posted &&
                        controller.commitPreparedChange(*command),"Native post plus canonical noexcept commit");
                    // Applies on FIRST actually adopted output frame, independent of
                    // source phase512. Both bus smoothers settled before held input.
                    routeGain=target==1 ? .25f : .5f;
                    // Queued native events are owned by A/B, independent of the
                    // musical Channel route. All accepted before native post.
                    drive(phase,127);
                    if(juce::String(phase)=="actual_A_to_B")
                    {checkNative(phase,1,1,1,0,0);checkNative(phase,2,1,0,1,0);}
                    else if(juce::String(phase)=="actual_Undo_B_to_A")
                    {checkNative(phase,1,2,1,0,1);checkNative(phase,2,2,0,1,1);}
                    else if(juce::String(phase)=="actual_Redo_A_to_B")
                    {checkNative(phase,1,2,1,1,0);checkNative(phase,2,2,1,1,0);}
                    const auto selected=audio->observeChannelRoutingSelection();
                    routeLog+=juce::String(phase)+","+juce::String(beforeSerial)+","+juce::String(serial)+","+
                        juce::String(selected.selectedSerial)+","+juce::String(controller.revision)+","+
                        juce::String(from)+","+juce::String(target)+"\n";
                    require(folder.getChildFile("canonical-routes.csv").replaceWithText(routeLog),
                        "Retain model/native selection facts before joint assertion");
                    retainQueues(phase);
                    require(selected.status==AudioEngine::ChannelRoutingStatus::selected &&
                        selected.selectedSerial==serial && serial>beforeSerial && graph->wasPosted() &&
                        command->wasCommitted() && project.channel(channelId)->mixer==target &&
                        audio->projectionDiagnostics().channels[0].heldInputKeys==2,
                        "Actual selection AND successful root use, unchanged held source ownership");
                    graph.reset(); command.reset(); // CONTROL, after actual selection/retirement.
                    controller.deliverPendingNotifications();
                };
                for(int authority=0;authority<2;++authority)
                {
                    const auto state=project.state();
                    const auto revision=controller.revision;
                    const auto selected=context.getGraphSelection().selectedSerial;
                    const auto originalIdentity=controller.projectIdentity;
                    Controller foreign;
                    foreign.project=project; // Deliberately identical disposable MODEL, distinct owner.
                    auto& refusedOwner=authority==0 ? foreign : controller;
                    if(authority==1) controller.projectIdentity=juce::Uuid().toString();
                    CommandArgs args;args.channel=channelId;args.mixer=2;
                    std::unique_ptr<PreparedControllerChange> refused;
                    std::unique_ptr<AudioEngine::PreparedChannelRouting> absent;
                    require(refusedOwner.prepareStructuralCommand("channel.mixer",args,refused).wasOk() && refused,
                        "Genuine canonical candidate under foreign owner/changed session");
                    const auto outcome=audio->prepareInitialSourceChannelRouting(refusedOwner,*refused,absent);
                    // Restore only the injected test session TAG; no native/model
                    // rollback/recovery API is claimed. Real Project replace stays
                    // a separate lifecycle operation, outside this experiment.
                    if(authority==1) controller.projectIdentity=originalIdentity;
                    require(outcome.status==AudioEngine::ChannelRoutingStatus::staleProjection &&
                        outcome.message.startsWith("source.canonicalOwner:") && !absent && !refused->wasCommitted() &&
                        project.state().isEquivalentTo(state) && controller.revision==revision &&
                        !controller.undo.canUndo() && context.getGraphSelection().selectedSerial==selected,
                        "Identical foreign model/changed canonical session refuses BEFORE native prep/mutation");
                    drive("FIRST_incumbent_after_canonical_authority_refusal",256);
                }
                {
                    const auto state=project.state();
                    const auto revision=controller.revision;
                    const auto selected=context.getGraphSelection().selectedSerial;
                    std::unique_ptr<PreparedControllerChange> command;
                    std::unique_ptr<AudioEngine::PreparedChannelRouting> candidate;
                    const auto readersA=readNative("before_private_prepare",1).readers;
                    const auto readersB=readNative("before_private_prepare",2).readers;
                    prepare(2,command,candidate);
                    injectNative(1,juce::MidiMessage::noteOn(3,67,juce::uint8(100)));
                    injectNative(2,juce::MidiMessage::noteOn(5,71,juce::uint8(110)));
                    candidate.reset();command.reset();
                    require(readNative("after_cancel_before_callback",1).readers==readersA&&
                        readNative("after_cancel_before_callback",2).readers==readersB,
                        "Cancelled graph releases only its own CONTROL readers/view pin");
                    require(project.state().isEquivalentTo(state) && controller.revision==revision &&
                        !controller.undo.canUndo() && context.getGraphSelection().selectedSerial==selected,
                        "Private cancel changes no route/model/history/native authority");
                    drive("FIRST_incumbent_after_cancel",256);
                    checkNative("FIRST_incumbent_after_cancel",1,1,0,0,1);
                    checkNative("FIRST_incumbent_after_cancel",2,1,0,0,1);
                }
                std::unique_ptr<PreparedControllerChange> staleCommand,command;
                std::unique_ptr<AudioEngine::PreparedChannelRouting> staleGraph,candidate;
                prepare(2,staleCommand,staleGraph);
                prepare(2,command,candidate);
                injectNative(1,juce::MidiMessage::noteOff(3,67));
                injectNative(2,juce::MidiMessage::allNotesOff(5));
                publish("actual_A_to_B",command,candidate,2);
                const auto stale=audio->validateInitialSourceChannelRouting(controller,*staleCommand,*staleGraph);
                require(stale.status!=AudioEngine::ChannelRoutingStatus::ready && !staleCommand->wasCommitted() &&
                    project.channel(channelId)->mixer==2,"Old canonical/native prepared route refuses after selection");
                staleGraph.reset();staleCommand.reset();
                drive("FIRST_incumbent_after_stale_refusal",65);
                require(controller.prepareHistoryChange(HistoryDirection::undo,command).wasOk() && command,
                    "Actual JUCE history destination prepared without cursor movement");
                require(audio->prepareInitialSourceChannelRouting(controller,*command,candidate).status==
                    AudioEngine::ChannelRoutingStatus::ready,"Exact inverse zero exchange from real Undo ticket");
                injectNative(1,juce::MidiMessage::noteOn(3,69,juce::uint8(100)));
                injectNative(2,juce::MidiMessage::noteOn(5,73,juce::uint8(100)));
                publish("actual_Undo_B_to_A",command,candidate,1);
                require(controller.prepareHistoryChange(HistoryDirection::redo,command).wasOk() && command &&
                    audio->prepareInitialSourceChannelRouting(controller,*command,candidate).status==
                        AudioEngine::ChannelRoutingStatus::ready,"Actual Redo whole inventory preparation");
                injectNative(1,juce::MidiMessage::allNotesOff(3));
                injectNative(2,juce::MidiMessage::noteOff(5,73));
                publish("actual_Redo_A_to_B",command,candidate,2);
                struct ForeignObserver final:tracktion::AudioTrack::Listener
                {
                    void injectLiveMidiMessage(tracktion::AudioTrack&,const tracktion::MidiMessageWithSource&,bool&)override{}
                    void recordedMidiMessageSentToPlugins(tracktion::AudioTrack&,const juce::MidiMessage&)override{}
                } observer;
                const auto beforeRefusal=project.state();const auto revisionRefusal=controller.revision;
                const auto selectionRefusal=context.getGraphSelection().selectedSerial;
                CommandArgs refusedArgs;refusedArgs.channel=channelId;refusedArgs.mixer=1;
                std::unique_ptr<PreparedControllerChange> refusedCommand;
                std::unique_ptr<AudioEngine::PreparedChannelRouting> refusedGraph;
                require(controller.prepareStructuralCommand("channel.mixer",refusedArgs,refusedCommand).wasOk(),
                    "Genuine actual canonical negative candidate");
                injectNative(1,juce::MidiMessage::noteOn(3,75,juce::uint8(100)));
                struct ObserverLease
                {
                    tracktion::AudioTrack& track;tracktion::AudioTrack::Listener& listener;
                    ObserverLease(tracktion::AudioTrack& t,tracktion::AudioTrack::Listener& l):track(t),listener(l)
                    {track.addListener(&listener);}
                    ~ObserverLease(){track.removeListener(&listener);}
                };
                const auto rejected=[&]{ObserverLease lease(*nativeA,observer);
                    return audio->prepareInitialSourceChannelRouting(controller,*refusedCommand,refusedGraph);}();
                require(folder.getChildFile("foreign-observer.txt").replaceWithText(
                    juce::String(int(rejected.status))+" "+rejected.message),"Retain actual unknown-consumer refusal");
                require(rejected.status!=AudioEngine::ChannelRoutingStatus::ready&&!refusedGraph&&
                    !refusedCommand->wasCommitted()&&project.state().isEquivalentTo(beforeRefusal)&&
                    controller.revision==revisionRefusal&&context.getGraphSelection().selectedSerial==selectionRefusal,
                    "Unknown legitimate observer refuses with accepted native inbox/model/history intact");
                drive("FIRST_after_foreign_observer_refusal",127);
                checkNative("FIRST_after_foreign_observer_refusal",1,3,1,1,1);
                injectNative(1,juce::MidiMessage::noteOff(3,75));drive("native_release_after_refusal",64);
                checkNative("native_release_after_refusal",1,3,2,1,0);
                {
                    const auto canonical=project.state();const auto revision=controller.revision;
                    const auto serial=context.getGraphSelection().selectedSerial;
                    const auto inbox=readNative("before_hidden_launcher",1);
                    auto& slots=nativeA->getClipSlotList();const int oldSlots=slots.getClipSlots().size();
                    slots.ensureNumberOfSlots(oldSlots+1);
                    auto* slot=slots.getClipSlots().getLast();
                    auto launcher=tracktion::insertMIDIClip(*slot,"Original hidden MIDI launcher negative",
                        {tracktion::TimePosition::fromSeconds(0),tracktion::TimePosition::fromSeconds(1)});
                    require(bool(launcher)&&slot->getClip()==launcher.get(),"Genuine actual ClipSlot-owned native MidiClip");
                    struct LauncherLease
                    {
                        tracktion::ClipSlotList& slots;const int oldCount;tracktion::MidiClip::Ptr clip;
                        ~LauncherLease(){if(clip)clip->removeFromParent();clip=nullptr;slots.setNumberOfSlots(oldCount);}
                    };
                    AudioEngine::ChannelRoutingOutcome result;
                    bool arrangerContainsLauncher=false;
                    for(auto* clip:nativeA->getClips())arrangerContainsLauncher|=clip==launcher.get();
                    std::unique_ptr<PreparedControllerChange> refused;
                    std::unique_ptr<AudioEngine::PreparedChannelRouting> noGraph;
                    {
                        LauncherLease cleanup{slots,oldSlots,std::move(launcher)};
                        auto& notes=cleanup.clip->getSequence();
                        require(notes.addNote(74,tracktion::BeatPosition::fromBeats(0),
                            tracktion::BeatDuration::fromBeats(.5),100,0,nullptr)!=nullptr&&!notes.isEmpty(),
                            "Actual hidden MIDI-only launcher has a genuine sounding note, not a shape label");
                        require(!arrangerContainsLauncher,"Existing arranger-only validation cannot see this ClipSlot source");
                        CommandArgs args;args.channel=channelId;args.mixer=1;
                        require(controller.prepareStructuralCommand("channel.mixer",args,refused).wasOk()&&refused,
                            "Real Controller candidate for hidden native-source refusal");
                        injectNative(1,juce::MidiMessage::noteOn(3,76,juce::uint8(100)));
                        result=audio->prepareInitialSourceChannelRouting(controller,*refused,noGraph);
                        const auto after=readNative("hidden_launcher_refused_pending",1);
                        require(folder.getChildFile("hidden-launcher.txt").replaceWithText(
                            juce::String(int(result.status))+" "+result.message+"\nactualSlot="+
                            juce::String(slot->itemID.getRawID())+" actualNotes="+juce::String(notes.getNumNotes())+
                            " accepted="+juce::String(after.accepted)+" delivered="+juce::String(after.delivered)+
                            " sameRuntime="+juce::String(int(after.runtime==inbox.runtime))),
                            "Retain actual hidden source, typed refusal and unconsumed inbox facts BEFORE assertions");
                        require(result.status==AudioEngine::ChannelRoutingStatus::preparationFailed&&
                            result.message.contains("trackMidi.launcher:")&&!noGraph&&!refused->wasCommitted()&&
                            project.state().isEquivalentTo(canonical)&&controller.revision==revision&&
                            context.getGraphSelection().selectedSerial==serial&&after.runtime==inbox.runtime&&
                            after.readers==inbox.readers&&after.accepted==inbox.accepted+1&&after.delivered==inbox.delivered,
                            "Hidden launcher refuses before graph/queue/model/history movement; genuine native event remains pending");
                    } // Remove only the explicit test-injected native clip/slot on CONTROL, no message pump.
                    require(slots.getClipSlots().size()==oldSlots,"Fault-injected native launcher ownership retired before next callback");
                    drive("FIRST_incumbent_after_hidden_launcher_refusal",127);
                    checkNative("FIRST_incumbent_after_hidden_launcher_refusal",1,4,2,1,1);
                    injectNative(1,juce::MidiMessage::noteOff(3,76));drive("native_release_after_launcher_refusal",64);
                    checkNative("native_release_after_launcher_refusal",1,4,3,1,0);
                }
                while(written<10000) drive("literal_score_pulse_and_held_source",256);
                audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("physical_off_typing_retained", 64);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 1,
                        "Physical release preserves independent held typing owner");
                off = written;
                audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("typing_off", 65);
                for (int n = 0; n < 5; ++n)
                    drive("full_release_and_silence", 256);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 0,
                        "Final typing release retires the last actual held owner");
                const auto catalogue = parameterCatalogue(*host, true);
                require(nativeParameter(catalogue, 90).value == 2. / 1024 &&
                            nativeParameter(catalogue, 91).value == 2. / 1024,
                        "Actual held note plus literal timeline pulse: exactly2 native On/Off each");
                initial_app_ports_acceptance::captured(audio->midiInput,channelId);
                initial_app_ports_acceptance::captured(audio->typingInput,channelId);
                audio->playStop();
            });
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    for (int which = 0; which < 2; ++which)
    {
        const auto &data = which ? expected : actual;
        auto output = folder.getChildFile(which ? "expected-planar-f32.raw" : "actual-planar-f32.raw")
                          .createOutputStream();
        require(output && output->setPosition(0) && output->truncate().wasOk(),
                "Fresh complete actual PCM evidence");
        for (int c = 0; c < 2; ++c)
            require(output->write(data.getReadPointer(c), std::size_t(written) * sizeof(float)),
                    "Write raw PCM");
        output->flush();
        require(output->getStatus().wasOk(), "Flush/status actual evidence");
    }
    if (failure)
        std::rethrow_exception(failure);
}
// Two additional ORIGINAL native ownership negatives. They do not qualify a
// production MIDI migration or arbitrary-provider callback path.
class EmptyNativeInput final : public tracktion::graph::Node
{
    unsigned& calls;
public:
    explicit EmptyNativeInput(unsigned& count):calls(count){}
    tracktion::graph::NodeProperties getNodeProperties() override
    {return {false,false,0,0,0x4d54454d505459ULL};}
    bool isReadyToProcess() override{return true;}
    void process(ProcessContext&) override{++calls;}
};
inline void warmMigrationRefusal(const juce::File& folder)
{
    require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh paired warm MIDI boundary evidence");
    struct Event
    {
        std::int64_t blockStart;
        int frames,kind,key,channel;
        std::uint32_t source;
        double relative,absolute;
        bool operator==(const Event&) const=default;
    };
    struct Callback
    {
        std::int64_t start;
        int frames;
        unsigned sourceCalls;
        std::uint64_t accepted,delivered;
        unsigned events;
        bool operator==(const Callback&) const=default;
    };
    struct Capture
    {
        std::vector<float> pcm;
        std::vector<Event> events;
        std::vector<Callback> callbacks;
        bool completed=false;
    };
    const auto runCase=[&](const juce::File& caseFolder,bool rejectPreparation)->Capture
    {
    using namespace vibera;
    using namespace vibera::experimental;
    using namespace tracktion::graph;
    require(!caseFolder.exists()&&caseFolder.createDirectory().wasOk(),"Fresh actual warm MIDI refusal evidence");
    initial_app_source_acceptance::Storage settings(caseFolder.getChildFile("settings"));
    auto engine=std::make_unique<AudioEngine>(false,caseFolder.getChildFile("guide.json"));
    tracktion::Edit::Options options{engine->engine,tracktion::createEmptyEdit(engine->engine),{}};
    options.numAudioTracks=1;options.role=tracktion::Edit::forRendering;
    options.closedNativeIO=true;options.initialSourceGeneration=1201;
    auto edit=tracktion::Edit::createEdit(options);
    require(bool(edit),"Real closed native Edit, no audio device");
    auto* track=tracktion::getAudioTracks(*edit)[0];
    PlayHead head;PlayHeadState headState(head);tracktion::ProcessState scope(headState,edit->tempoSequence);
    ready_graph_acceptance::Counts pool;
    ready_graph_acceptance::Player player([&](auto& p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,pool);});
    player.setNumThreads(0);player.enablePooledMemoryAllocations(false);player.enableNodeMemorySharing(false);
    unsigned calls=0;
    auto injection=std::make_unique<tracktion::LiveMidiInjectingNode>(*track,std::make_unique<EmptyNativeInput>(calls),&scope);
    auto* ingress=injection.get();std::unique_ptr<Node> mixed=std::move(injection);
    auto queued=std::make_unique<LatencyNode>(std::move(mixed),512);auto* delay=queued.get();
    player.setNode(std::move(queued),48000.,128);
    const auto* originalQueue=delay->structuralZeroHistoryRuntimeToken();
    const auto initial=ingress->boundedIngressObservation();
    choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2,128));
    tracktion::MidiMessageArray midi;midi.reserve(16);
    std::int64_t position=0;unsigned received=0;
    Capture captured;
    auto& actual=captured.pcm;juce::String rows="phase,start,frames,accepted,delivered,events,new,delete,seconds\n";
    juce::String eventRows="phase,start,kind,key,source,frame,expectedFrame\n";
    const auto retain=[&]
    {
        require(caseFolder.getChildFile("callbacks.csv").replaceWithText(rows)&&
            caseFolder.getChildFile("events.csv").replaceWithText(eventRows),"Retain warm refusal facts before assertions");
        for(int which=0;which<2;++which)
        {
            auto out=caseFolder.getChildFile(which?"expected-planar-call-f32.raw":"actual-planar-call-f32.raw").createOutputStream();
            require(out&&out->setPosition(0)&&out->truncate().wasOk(),"Fresh complete warm-refusal PCM");
            for(const auto value:actual){const float retained=which?0.f:value;
                require(out->write(&retained,sizeof(retained)),"Retain every warm-refusal sample");}
            out->flush();require(out->getStatus().wasOk(),"Flush warm-refusal PCM");
        }
    };
    const auto drive=[&](const char* phase,int frames)
    {
        midi.clear();pcm.clear();const auto count=calls;
        const auto allocations=callbackAllocations.load(),destructions=callbackDestructions.load();
        const auto start=juce::Time::getHighResolutionTicks();
        {
            struct Probe{const bool old=callbackAllocationProbe;Probe(){callbackAllocationProbe=true;}
                ~Probe(){callbackAllocationProbe=old;}} probe;
            player.process({choc::buffer::FrameCount(frames),{position,position+frames},
                {pcm.getView().getStart(choc::buffer::FrameCount(frames)),midi}});
        }
        const double seconds=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-start);
        bool exact=true;unsigned mask=0;
        for(int c=0;c<2;++c)for(int n=0;n<frames;++n)
        {const auto v=pcm.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n));actual.push_back(v);exact&=v==0.f;}
        for(const auto& m:midi)
        {
            const int kind=m.isNoteOn()?0:m.isNoteOff()?1:-1;
            const auto absolute=double(position)+m.getTimeStamp()*48000.;
            const int expected=kind==0?512:kind==1?576:-1;
            eventRows+=juce::String(phase)+","+juce::String(position)+","+juce::String(kind)+","+
                juce::String(kind>=0?m.getNoteNumber():-1)+","+juce::String(m.mpeSourceID)+","+
                juce::String(absolute,12)+","+juce::String(expected)+"\n";
            exact&=kind>=0&&m.getNoteNumber()==67&&m.getChannel()==4&&m.mpeSourceID==101&&
                std::abs(absolute-double(expected))<=1.e-7;
            captured.events.push_back({position,frames,kind,kind>=0?m.getNoteNumber():-1,
                m.getChannel(),m.mpeSourceID,m.getTimeStamp(),absolute});
            if(kind>=0){const unsigned bit=1u<<kind;exact&=(mask&bit)==0&&(received&bit)==0;mask|=bit;}
        }
        unsigned expectedMask=0;
        // Actual LatencyProcessor::readMIDI uses <= block duration. These
        // positive deadlines belong to EXACTLY the right-closed callback;
        // do not accept either neighbor or widen the literal timing oracle.
        if(position<512&&512<=position+frames)expectedMask|=1;
        if(position<576&&576<=position+frames)expectedMask|=2;
        exact&=mask==expectedMask&&!midi.isAllNotesOff;received|=mask;
        const auto observation=ingress->boundedIngressObservation();
        rows+=juce::String(phase)+","+juce::String(position)+","+juce::String(frames)+","+
            juce::String(observation.accepted)+","+juce::String(observation.delivered)+","+juce::String(midi.size())+","+
            juce::String(callbackAllocations.load()-allocations)+","+juce::String(callbackDestructions.load()-destructions)+","+
            juce::String(seconds,12)+"\n";
        captured.callbacks.push_back({position,frames,calls-count,observation.accepted,
            observation.delivered,unsigned(midi.size())});
        retain();position+=frames;
        require(exact&&calls==count+1&&observation.runtime==initial.runtime&&observation.registered&&
            observation.fault==tracktion::LiveMidiInjectingNode::IngressFault::none&&
            delay->structuralZeroHistoryRuntimeToken()==originalQueue&&seconds<=double(frames)/48000.,
            "Original warm MIDI queue retains exact On/Off due time/source and first post-refusal PCM");
        // Legacy LatencyProcessor MIDI allocation is measured, not waived or
        // labelled safe: this refusal-only functional case preserves it.
    };
    track->injectLiveMidiMessage(juce::MidiMessage::noteOn(4,67,juce::uint8(99)),101);
    drive("real_on_in_old_delay",64);
    track->injectLiveMidiMessage(juce::MidiMessage::noteOff(4,67),101);
    const auto pending=ingress->boundedIngressObservation();
    require(pending.accepted==2&&pending.delivered==1&&received==0,"Genuine old delayed On and unconsumed Off");
    if(rejectPreparation)
    {
    auto publisher=std::make_shared<NativeMidiFaultPublisher>();
    auto owner=std::make_shared<MixerTrackMidiOwner>(*track,scope,1,publisher);
    juce::String diagnostic;
    try
    {
        auto next=std::make_unique<PreparedMixerTrackMidiBoundary>(
            std::make_unique<tracktion::LiveMidiInjectingNode>(*track,std::make_unique<EmptyNativeInput>(calls),&scope),owner);
        auto refused=player.prepareFixedGraph(std::move(next));
    }
    catch(const std::exception& error){diagnostic=error.what();}
    const auto after=ingress->boundedIngressObservation();
    require(caseFolder.getChildFile("refusal.txt").replaceWithText(diagnostic+"\naccepted="+juce::String(after.accepted)+
        " delivered="+juce::String(after.delivered)+" readers="+juce::String(owner->readers)),"Retain actual typed migration refusal");
    require(diagnostic.startsWith("trackMidi.migration:")&&after.runtime==pending.runtime&&
        after.accepted==pending.accepted&&after.delivered==pending.delivered&&owner->readers==0&&
        !owner->view.observation().enabled,"No late boundary abandons or inspects existing queued native MIDI");
    }
    else
    {
        const auto unchanged=ingress->boundedIngressObservation();
        require(caseFolder.getChildFile("refusal.txt").replaceWithText(
            "BASELINE: no candidate constructed or prepared\naccepted="+juce::String(unchanged.accepted)+
            " delivered="+juce::String(unchanged.delivered)),"Retain genuine no-preparation control");
        require(unchanged.runtime==pending.runtime&&unchanged.accepted==pending.accepted&&
            unchanged.delivered==pending.delivered&&calls==1,
            "Baseline leaves the same literal old On queued and native Off pending");
    }
    drive("FIRST_old_output_after_refusal",64);
    while(position<640)drive("unchanged_old_delay_tail",128);
    require(received==3&&ingress->boundedIngressObservation().delivered==2,
        "Both original delayed messages delivered exactly once after refused migration");
    require(captured.events.size()==2&&captured.events[0].blockStart==384&&
        captured.events[0].frames==128&&captured.events[0].kind==0&&
        captured.events[1].blockStart==512&&captured.events[1].frames==128&&
        captured.events[1].kind==1,
        "Native right endpoint: On512 in [384,512], Off576 in [512,640], each exactly once");
    captured.completed=true;
    return captured;
    };
    Capture baseline,refused;
    juce::String failures;
    try{baseline=runCase(folder.getChildFile("baseline_no_preparation"),false);}
    catch(const std::exception& error){failures+="baseline: "+juce::String(error.what())+"\n";}
    try{refused=runCase(folder.getChildFile("refused_preparation"),true);}
    catch(const std::exception& error){failures+="refused: "+juce::String(error.what())+"\n";}
    const bool samePcm=baseline.pcm==refused.pcm;
    const bool sameEvents=baseline.events==refused.events;
    const bool sameCallbacks=baseline.callbacks==refused.callbacks;
    require(folder.getChildFile("paired.txt").replaceWithText(
        "baselineCompleted="+juce::String(int(baseline.completed))+" refusedCompleted="+
        juce::String(int(refused.completed))+" exactPcm="+juce::String(int(samePcm))+
        " exactEvents="+juce::String(int(sameEvents))+" exactCallbacks="+
        juce::String(int(sameCallbacks))+"\n"+failures),
        "Retain independent baseline/refused outcomes before paired assertions");
    require(failures.isEmpty()&&baseline.completed&&refused.completed&&samePcm&&sameEvents&&sameCallbacks,
        "Actual no-preparation baseline and refused preparation preserve identical literal MIDI/PCM/source callbacks");
}
class TerminalFaultSource final:public tracktion::graph::Node
{
    unsigned& calls;
public:
    explicit TerminalFaultSource(unsigned& n):calls(n){}
    tracktion::graph::NodeProperties getNodeProperties() override{return {true,true,2,0,0x4d544641554c5453ULL};}
    bool isReadyToProcess() override{return true;}
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo&) override{reserveMidiOutputForPreparation(4);}
    void process(ProcessContext& pc) override
    {
        ++calls;
        for(choc::buffer::ChannelCount c=0;c<2;++c)for(choc::buffer::FrameCount n=0;n<pc.numSamples;++n)
            pc.buffers.audio.getSample(c,n)=c?-.0625f:.125f;
        pc.buffers.midi.addMidiMessage(juce::MidiMessage::noteOn(2,66,juce::uint8(99)),0.,7);
    }
};
class TerminalFaultJoin final:public tracktion::graph::Node
{
    const std::shared_ptr<tracktion::graph::ConnectedNode> audio;
    const std::shared_ptr<vibera::experimental::OwnedNativeMidiTerminal> terminal;
public:
    TerminalFaultJoin(std::shared_ptr<tracktion::graph::ConnectedNode> a,
        std::shared_ptr<vibera::experimental::OwnedNativeMidiTerminal> t):audio(std::move(a)),terminal(std::move(t)){}
    tracktion::graph::NodeProperties getNodeProperties() override
    {auto p=audio->getNodeProperties();p.nodeID=0x4d544641554c544aULL;return p;}
    std::vector<Node*> getDirectInputNodes() override{return {audio.get(),terminal.get()};}
    bool isReadyToProcess() override{return audio->hasProcessed()&&terminal->hasProcessed();}
    void process(ProcessContext&) override{setAudioOutput(audio.get(),audio->getProcessedOutput().audio);}
};
inline void lateTerminalFailure(const juce::File& folder)
{
    using namespace vibera::experimental;using namespace tracktion::graph;
    require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh actual late terminal failure");
    ready_graph_acceptance::Counts pools;ready_graph_acceptance::Player player(
        [&](auto& p){return std::make_unique<ready_graph_acceptance::CountingPool>(p,pools);});
    player.setNumThreads(0);player.enableNodeMemorySharing(false);player.enablePooledMemoryAllocations(false);
    auto publisher=std::make_shared<NativeMidiFaultPublisher>();auto terminal=std::make_shared<NativeMidiTerminal>();
    auto continuity=std::make_shared<vibera::experimental::SourceExchangeContinuity>();unsigned calls=0;
    auto source=std::make_shared<TerminalFaultSource>(calls);
    auto audio=std::make_shared<ConnectedNode>(0x4d544641554c5441ULL);
    auto midi=std::make_shared<ReservedMidiOutputPort>(0x4d544641554c544dULL);
    require(audio->addAudioConnection(source,{0,0})&&audio->addAudioConnection(source,{1,1})&&midi->addMidiConnection(source),
        "Real original mixed source to genuine native audio/MIDI selections");
    auto sink=std::make_shared<OwnedNativeMidiTerminal>(midi,terminal,0x4d544641554c5454ULL,publisher);
    std::unique_ptr<Node> input=std::make_unique<TerminalFaultJoin>(audio,sink);
    auto root=std::make_unique<vibera::experimental::PreparedSourceExchangeRoot>(std::move(input),continuity,1,
        vibera::experimental::PreparedSourceExchangeRoot::Request{},publisher);
    player.setNode(std::move(root),48000.,128);
    // Explicit quiescent counter-boundary fault injection, not a product flag.
    // consume() detects this DURING child processing, after root prefetch.
    terminal->blocks.store(std::numeric_limits<std::uint64_t>::max());
    choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2,128));
    tracktion::MidiMessageArray output;output.reserve(512);
    const auto allocations=callbackAllocations.load(),destructions=callbackDestructions.load();
    const auto start=juce::Time::getHighResolutionTicks();
    {struct Probe{const bool old=callbackAllocationProbe;Probe(){callbackAllocationProbe=true;}
        ~Probe(){callbackAllocationProbe=old;}} probe;player.process({128,{0,128},{pcm.getView(),output}});}
    const double seconds=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-start);
    bool exact=true;
    for(int which=0;which<2;++which)
    {
        auto stream=folder.getChildFile(which?"expected-f32.raw":"actual-f32.raw").createOutputStream();
        require(stream&&stream->setPosition(0)&&stream->truncate().wasOk(),"Fresh full fault output evidence");
        for(choc::buffer::ChannelCount c=0;c<2;++c)for(choc::buffer::FrameCount n=0;n<128;++n)
        {const float value=which?0.f:pcm.getView().getSample(c,n);exact&=value==0.f;
         require(stream->write(&value,sizeof(value)),"Record every late-fault sample");}
        stream->flush();require(stream->getStatus().wasOk(),"Flush late-fault output evidence");
    }
    const auto facts="Explicit quiescent terminal counter seed; actual terminal rejects after root prefetch.\nsourceCalls="+
        juce::String(calls)+" publisher="+juce::String(int(publisher->observation()))+
        " terminal="+juce::String(int(terminal->observe().fault))+" root="+juce::String(int(continuity->observation()))+
        " completed="+juce::String(continuity->completedGeneration())+
        " baselineReady="+juce::String(int(continuity->hasSuccessfulBaseline()))+
        " selected="+juce::String(player.graphSelection().selectedSerial)+
        " new="+juce::String(callbackAllocations.load()-allocations)+" delete="+juce::String(callbackDestructions.load()-destructions)+
        " seconds="+juce::String(seconds,12)+" deadline="+juce::String(128./48000.,12)+"\n";
    require(folder.getChildFile("late-fault.txt").replaceWithText(facts),"Retain actual typed failure before assertions");
    require(exact&&output.isEmpty()&&!output.isAllNotesOff&&calls==1&&
        terminal->observe().fault==NativeMidiTerminal::Fault::counterExhausted&&
        publisher->observation()==NativeMidiFaultPublisher::Fault::terminal&&
        continuity->observation()==vibera::experimental::SourceExchangeContinuity::Fault::nativeProofFailed&&
        !continuity->hasSuccessfulBaseline()&&continuity->completedGeneration()==0&&
        callbackAllocations.load()==allocations&&callbackDestructions.load()==destructions&&seconds<=128./48000.,
        "Failed real child terminal cannot publish successful root generation or partial audible output");
}
inline void supportNegatives(const juce::File& folder)
{
    require(!folder.exists()&&folder.createDirectory().wasOk(),"Fresh independent native negatives");
    std::exception_ptr first;juce::String outcomes;
    for(int n=0;n<2;++n)
    {
        const auto name=juce::String(n?"post_child_fault":"warm_migration");
        try{if(n)lateTerminalFailure(folder.getChildFile(name));else warmMigrationRefusal(folder.getChildFile(name));outcomes+=name+"=completed\n";}
        catch(const std::exception& error){if(!first)first=std::current_exception();outcomes+=name+"=failed:"+juce::String(error.what())+"\n";}
        require(folder.getChildFile("cases.txt").replaceWithText(outcomes),"Retain both independent negatives");
    }
    if(first)std::rethrow_exception(first);
}

inline void tests(const juce::File &folder, const vibera::PluginSpec &instrument)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh proof aggregate attempt");
    require(instrument.instrument && instrument.name == "VibeRa Instrument Runtime Fixture",
            "Exact original SDK instrument; no arbitrary-provider PCM claim");
    juce::String identities;
    const auto retainIdentity = [&](const juce::File &file)
    {
        require(file.existsAsFile(), "Exact real SDK/application/worker file");
        identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    };
    retainIdentity(juce::File::getSpecialLocation(juce::File::currentExecutableFile));
    retainIdentity(vibera::pluginWorkerExecutable());
    const auto xml = juce::parseXML(instrument.descriptor);
    juce::PluginDescription actual;
    require(xml && actual.loadFromXml(*xml) && actual.name == instrument.name && actual.isInstrument &&
                actual.pluginFormatName == "VST3" && juce::File::isAbsolutePath(actual.fileOrIdentifier),
            "Actual cached genuine original VST3 descriptor");
    retainIdentity(juce::File(actual.fileOrIdentifier));
    require(folder.getChildFile("binary-identities.txt").replaceWithText(identities) &&
                folder.getChildFile("instrument-descriptor.xml").replaceWithText(instrument.descriptor),
            "Retain actual app/worker/fixture/descriptor identities");
    sourceMultiplicityContracts();
    const auto stored = initial_app_source_acceptance::capturedOriginal(instrument);
    std::exception_ptr first;
    juce::String cases;
    try{supportNegatives(folder.getChildFile("support-negatives"));cases+="native-support=completed\n";}
    catch(const std::exception& error){first=std::current_exception();cases+="native-support=failed:"+juce::String(error.what())+"\n";}
    require(folder.getChildFile("cases.txt").replaceWithText(cases),"Retain independent native support outcome");
    for (bool pattern : {true, false})
    {
        const auto name = juce::String(pattern ? "pattern" : "song");
        try
        {
            run(folder.getChildFile(name), stored, pattern);
            cases += name + "=completed\n";
        }
        catch (const std::exception &error)
        {
            if (!first)
                first = std::current_exception();
            cases += name + "=failed:" + juce::String(error.what()) + "\n";
        }
        require(folder.getChildFile("cases.txt").replaceWithText(cases),
                "Retain every independent actual case");
    }
    if (first)
        std::rethrow_exception(first);
}
} // namespace initial_app_stable_track_midi_route_acceptance
