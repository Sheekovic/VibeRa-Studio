// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "InitialAppSourceTransportAcceptance.h"
#include "ZeroHistoryCreationAcceptance.h"
#include "EmptyMixerLineageAcceptance.h"
#include <tracktion_engine/playback/graph/tracktion_ChannelRemappingNode.h>

// Additive actual-app prerequisite only. Does NOT authorize or select a reroute.
// Include after existing original SDK/physical/heap helpers, unchanged.
namespace initial_app_zero_history_acceptance
{
inline void memorySharingPolicyContract(const juce::File& folder)
{
    using namespace vibera;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned sharing-policy settings");
    initial_app_source_acceptance::Storage storage(folder.getChildFile("settings"));
    AudioEngine audio(false,folder.getChildFile("guide.json"));
    te::Edit::Options ordinary{audio.engine,te::createEmptyEdit(audio.engine),{}};
    ordinary.role=te::Edit::forRendering;
    require(!ordinary.disableNodeMemorySharing,"Default Options preserve native sharing policy");
    auto before=te::Edit::createEdit(ordinary);
    auto excludedOptions=ordinary;
    excludedOptions.editState=te::createEmptyEdit(audio.engine);
    excludedOptions.disableNodeMemorySharing=true;
    auto excluded=te::Edit::createEdit(excludedOptions);
    ordinary.editState=te::createEmptyEdit(audio.engine);
    auto after=te::Edit::createEdit(ordinary);
    require(before && excluded && after && !before->disableNodeMemorySharing &&
        excluded->disableNodeMemorySharing && !after->disableNodeMemorySharing,
        "Actual Edit owns immutable per-instance sharing exclusion");
    for(bool nativeDefault : {false,true})
        require(before->shouldUseNodeMemorySharing(nativeDefault)==nativeDefault &&
            after->shouldUseNodeMemorySharing(nativeDefault)==nativeDefault &&
            !excluded->shouldUseNodeMemorySharing(nativeDefault),
            "Independent ordinary Edit policies unchanged before/after explicit excluded instance");
    require(!excluded->state.hasProperty("disableNodeMemorySharing"),
        "Engineering sharing policy is not serialized into musical project state");
    // Actual EPC/Node preparation exclusion is independently required by the
    // full app queue certificate below; this unit is not counterpart DSP proof.
}
inline void nativeProofNegatives()
{
    using namespace tracktion::graph;
    struct Port final : Node
    {
        Node *input;
        explicit Port(Node &node) : input(&node) {}
        NodeProperties getNodeProperties() override
        {
            return input->getNodeProperties();
        }
        std::vector<Node *> getDirectInputNodes() override
        {
            return {input};
        }
        bool isReadyToProcess() override
        {
            return true;
        }
        void process(ProcessContext &) override {}
    };
    struct Unknown final : Node
    {
        NodeProperties getNodeProperties() override
        {
            return {true, false, 2, 0, 0x5a554e4b4e4f574eULL};
        }
        bool isReadyToProcess() override
        {
            return true;
        }
        void process(ProcessContext &) override {} // Emits zero in a cleared block, but declares NO proof.
    };
    const auto plugins = [](tracktion::Plugin &) { return false; };
    const auto ports = [](Node &node) -> std::optional<Node *>
    {
        if (auto *port = dynamic_cast<Port *>(&node))
            return port->input;
        return {};
    };
    SilentNode silent(2);
    Unknown unknown;
    require(tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(silent, plugins, ports) &&
                !tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(unknown, plugins, ports),
            "Actual declared native silence and unknown silent-looking node stay distinct");
    tracktion::ChannelRemappingNode remap(std::make_unique<SilentNode>(2),
                                          tracktion::ChannelMap::identity(2));
    require(!tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(remap, plugins, ports),
            "Even a finite identity remapper is unknown until its hidden-source/mapping contract is "
            "independently admitted");
    zero_history_creation_acceptance::Fixture fixture(zero_history_creation_acceptance::Source::certifiedZero,
                                                      48000.);
    Port port(*fixture.tap);
    require(fixture.tap->hasStructuralZeroHistory() &&
                tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(port, plugins, ports),
            "Prepared port follows actual FINAL queue certificate");
    const auto *token = fixture.tap->structuralZeroHistoryRuntimeToken();
    {
        auto unused = fixture.player.prepareFixedGraph(
            fixture.graph(zero_history_creation_acceptance::Source::unknownZero));
        require(!fixture.tap->hasStructuralZeroHistory() &&
                    fixture.tap->structuralZeroHistoryRuntimeToken() == token &&
                    !tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(port, plugins, ports),
                "Unknown private preparation invalidates exact shared proof; port may not shortcut with "
                "cached true");
    }
    require(!fixture.tap->hasStructuralZeroHistory() &&
                !tracktion::EditNodeBuilder::hasPreparedAudioZeroOutput(port, plugins, ports),
            "Cancel never resurrects actual queue proof");
}
inline void run(const juce::File &folder, const vibera::PluginSpec &stored, bool patternMode)
{
    using namespace vibera;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual zero-history attempt");
    initial_app_source_acceptance::Storage storage(folder.getChildFile("settings"));
    Controller controller;
    auto &project = controller.project;
    project.channels.resize(1);
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
    project.patterns.resize(1);
    auto &pattern = project.patterns.front();
    pattern.length = 64;
    pattern.notes.clear();
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
    juce::String csv = "phase,start,frames,error,ms,new,delete,missed,dropped,cursor,sourceSerial\n";
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
                            const float wanted =
                                held
                                    ? float(.25 * (c == 0 ? 61. / 512 + 1. / 4096 : -(68. / 512 + 1. / 8192)))
                                    : 0.f;
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
                           juce::String(source.native.physicalSerial) + "\n";
                    require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                            "Every callback retained before oracle");
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
                {
                    const auto beforeProject = controller.project.state();
                    const auto beforeRevision = controller.revision;
                    const auto beforeSerial = context.getGraphSelection().selectedSerial;
                    CommandArgs route;
                    route.channel = channelId;
                    route.mixer = 2;
                    std::unique_ptr<PreparedControllerChange> command;
                    require(controller.prepareStructuralCommand("channel.mixer", route, command).wasOk() &&
                                command && !command->isNoOp() && command->destinationBeforeCommit() &&
                                command->destinationBeforeCommit()->channels.front().mixer == 2,
                            "Genuine canonical destination prepared before audio admission");
                    std::unique_ptr<AudioEngine::PreparedChannelRouting> rejected;
                    const auto outcome =
                        audio->prepareChannelRouting(*command->destinationBeforeCommit(), rejected);
                    require(outcome.status == AudioEngine::ChannelRoutingStatus::unsupportedDelta &&
                                !rejected && outcome.message.startsWith("source.initialOnly:") &&
                                !command->wasCommitted() && controller.revision == beforeRevision &&
                                controller.project.state().isEquivalentTo(beforeProject) &&
                                !controller.undo.canUndo() &&
                                context.getGraphSelection().selectedSerial == beforeSerial,
                            "A sound zero certificate alone MUST NOT waive whole-inventory/canonical "
                            "membership admission");
                    drive("first_pcm_after_canonical_reroute_refusal", 256);
                }
                initial_app_ports_acceptance::privateGraph(*audio, edit, context, drive);
                const auto retained = retainQueues("after_actual_cancel_and_gate_adoption");
                bool matched = false;
                for (const auto &item : retained)
                    if (item.nativeOwner == coldGate)
                        matched = item.processor == queue && item.certified && !item.createdHere;
                require(matched, "Final actual reused B processor retains its OWN creation proof, never "
                                 "copies temporary proof");
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
                require(nativeParameter(catalogue, 90).value == 1. / 1024 &&
                            nativeParameter(catalogue, 91).value == 1. / 1024,
                        "Actual single native On/Off, no duplicate source or lost owner");
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
    memorySharingPolicyContract(folder.getChildFile("memory-policy"));
    empty_mixer_lineage_acceptance::tests(folder.getChildFile("empty-lineage"));
    nativeProofNegatives();
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
} // namespace initial_app_zero_history_acceptance
