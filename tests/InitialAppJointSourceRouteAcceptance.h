// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "InitialAppZeroHistoryAcceptance.h"
#include "ZeroHistoryCreationAcceptance.h"
#include "EmptyMixerLineageAcceptance.h"
#include <tracktion_engine/playback/graph/tracktion_ChannelRemappingNode.h>

// Additive actual canonical Controller + prepared whole-inventory routing experiment.
// Explicit initial option ONLY; source-authored, not production/default activation.
// Include after existing original SDK/physical/heap helpers, unchanged.
namespace initial_app_joint_source_route_acceptance
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
                    drive(phase,127);
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
                    prepare(2,command,candidate);
                    candidate.reset();command.reset();
                    require(project.state().isEquivalentTo(state) && controller.revision==revision &&
                        !controller.undo.canUndo() && context.getGraphSelection().selectedSerial==selected,
                        "Private cancel changes no route/model/history/native authority");
                    drive("FIRST_incumbent_after_cancel",256);
                }
                std::unique_ptr<PreparedControllerChange> staleCommand,command;
                std::unique_ptr<AudioEngine::PreparedChannelRouting> staleGraph,candidate;
                prepare(2,staleCommand,staleGraph);
                prepare(2,command,candidate);
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
                publish("actual_Undo_B_to_A",command,candidate,1);
                require(controller.prepareHistoryChange(HistoryDirection::redo,command).wasOk() && command &&
                    audio->prepareInitialSourceChannelRouting(controller,*command,candidate).status==
                        AudioEngine::ChannelRoutingStatus::ready,"Actual Redo whole inventory preparation");
                publish("actual_Redo_A_to_B",command,candidate,2);
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
} // namespace initial_app_joint_source_route_acceptance
