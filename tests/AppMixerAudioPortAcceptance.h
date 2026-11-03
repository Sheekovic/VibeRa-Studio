// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AppAudioPortMidiOwnershipAcceptance.h"
#include "PreparedMixerAudioPort.h"
#include "Sampler.h"
#include <tracktion_engine/playback/graph/tracktion_LiveMidiOutputNode.h>
#include <set>

namespace vibera::test::app_mixer_audio_port
{
// Original actual VoiceBank-backed plugin. Its two pre-held banks represent
// independent private sample owners; they are NOT physical keyboard hardware,
// FixedSampler integration, MIDI capture, or arbitrary plugin qualification.
class PrivateSample final : public tracktion::Plugin, public experimental::ClosedShortMidiPortProducer
{
    Sample sample;
    VoiceBank laneA, laneB;

  public:
    static constexpr const char *xmlTypeName = "vibera_private_sample_port_fixture";
    static const char *getPluginName()
    {
        return "Original private sample port fixture";
    }
    unsigned calls = 0;
    std::uint64_t frames = 0;
    bool heldA = true, heldB = true;
    static float pcm(int c, std::uint64_t n) noexcept
    {
        return c == 0 ? .125f + (n % 53 == 3 ? .25f : 0.f) : -.0625f - (n % 47 == 17 ? .125f : 0.f);
    }
    explicit PrivateSample(tracktion::PluginCreationInfo info) : Plugin(info)
    {
        sample.data.setSize(2, 65536);
        sample.oneShot = false;
        sample.rootNote = 60;
        sample.sampleRate = 48000;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < sample.data.getNumSamples(); ++n)
                sample.data.setSample(c, n, pcm(c, std::uint64_t(n)));
        sample.prepareNormalization();
        SamplerSettings settings;
        settings.envelope = true;
        settings.attack = 0;
        settings.decay = 0;
        settings.sustain = 1;
        settings.release = 0;
        for (auto *bank : {&laneA, &laneB})
        {
            bank->prepare(sample, 48000);
            bank->configure(PreparedSamplerSettings(settings));
            bank->noteOn(60, .5f);
        }
    }
    ~PrivateSample() override
    {
        notifyListenersOfDeletion();
    }
    const void *privateVoiceOwner() const noexcept override
    {
        return &laneA;
    }
    unsigned held() const noexcept
    {
        return unsigned(laneA.activeVoices() + laneB.activeVoices());
    }
    void releaseA() noexcept
    {
        laneA.noteOff(60);
        heldA = false;
    }
    void releaseB() noexcept
    {
        laneB.noteOff(60);
        heldB = false;
    }
    juce::String getName() const override
    {
        return getPluginName();
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool isSynth() override
    {
        return true;
    }
    bool takesMidiInput() override
    {
        return true;
    }
    bool takesAudioInput() override
    {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return true;
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    void initialise(const tracktion::PluginInitialisationInfo &info) override
    {
        if (info.sampleRate != 48000)
            throw std::invalid_argument("Original fixture has exact48000 sample rate");
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override {}
    void deinitialise() override {}
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    void applyToBuffer(const tracktion::PluginRenderContext &context) override
    {
        ++calls;
        if (!context.destBuffer)
            return;
        context.destBuffer->clear(context.bufferStartSample, context.bufferNumSamples);
        laneA.render(*context.destBuffer, context.bufferStartSample, context.bufferNumSamples);
        laneB.render(*context.destBuffer, context.bufferStartSample, context.bufferNumSamples);
        frames += std::uint64_t(context.bufferNumSamples);
        // Native input MIDI is intentionally preserved to the owned terminal.
        // Neither terminal panic nor native preview touches these private banks.
    }
};
inline juce::ValueTree state(tracktion::Edit &edit, const char *type)
{
    juce::ValueTree result(tracktion::IDs::PLUGIN);
    result.setProperty(tracktion::IDs::type, type, nullptr);
    tracktion::EditItemID::readOrCreateNewID(edit, result);
    return result;
}
struct Observer final : tracktion::AudioTrack::Listener
{
    unsigned delivered = 0;
    void injectLiveMidiMessage(tracktion::AudioTrack &, const tracktion::MidiMessageWithSource &,
                               bool &) override
    {
    }
    void recordedMidiMessageSentToPlugins(tracktion::AudioTrack &, const juce::MidiMessage &) override
    {
        ++delivered;
    }
};
inline void scenario(const juce::File &folder, int mode)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual Mixer audio-port case");
    app_audio_port::Settings settings(folder.getChildFile("settings"));
    auto engine = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    tracktion::Edit::Options options{engine->engine, tracktion::createEmptyEdit(engine->engine), {}};
    options.numAudioTracks = 1;
    options.role = tracktion::Edit::forRendering;
    options.closedNativeIO = true;
    options.initialSourceGeneration = 2101;
    auto edit = tracktion::Edit::createEdit(options);
    require(bool(edit), "Real closed local Edit");
    auto *track = tracktion::getAudioTracks(*edit)[0];
    track->getOutput().setOutputToDefaultDevice(false);
    track->pluginList.clear(); // Owned disposable fixture BEFORE graph construction only.
    auto anchor = track->insertMIDIClip(
        "Empty original source anchor",
        {tracktion::TimePosition::fromSeconds(0), tracktion::TimePosition::fromSeconds(4)}, nullptr);
    require(bool(anchor) && anchor->getSequence().isEmpty(),
            "Actual empty native anchor, not proof of no injection");
    anchor->setUsesProxy(false);
    engine->engine.getPluginManager().createBuiltInType<PrivateSample>();
    auto source = edit->getPluginCache().createNewPlugin(PrivateSample::xmlTypeName, {});
    auto *sample = dynamic_cast<PrivateSample *>(source.get());
    auto gate = edit->getPluginCache().createNewPlugin(MixerMasterGate::xmlTypeName, {});
    require(sample && dynamic_cast<MixerMasterGate *>(gate.get()),
            "Actual registered cached source and gate");
    auto timeline = std::make_shared<LiveTimeline>();
    // Real immutable unity Master edge, published on CONTROL before native
    // adapter preparation. There is no automated curve in this PCM fixture.
    auto document = std::make_unique<LiveDocument>();
    document->generation = 1;
    document->tempo = 120;
    document->mixerSends = {{1, 0, 1.f}};
    require(timeline->publish(document), "Actual authored Master-edge document before gate preparation");
    dynamic_cast<MixerMasterGate *>(gate.get())->configure(timeline, 1, 1.f);
    track->pluginList.insertPlugin(source, 0, nullptr);
    track->pluginList.insertPlugin(gate, 1, nullptr);
    require(track->pluginList.size() == 2 && track->pluginList[0] == source.get() &&
                track->pluginList[1] == gate.get(),
            "Actual native PluginList retains both exact configured PluginCache owners");
    tracktion::graph::PlayHead head;
    tracktion::graph::PlayHeadState headState(head);
    tracktion::ProcessState process(headState, edit->tempoSequence);
    head.playSyncedToRange({});
    tracktion::CreateNodeParams params{process};
    params.sampleRate = 48000;
    params.blockSize = 256;
    params.forRendering = true;
    params.includeMasterPlugins = false;
    auto policy = experimental::SingleTrackMixerAudioPort::prepare(*track, process, source, gate);
    if (mode == 4)
    {
        params.boundedNativeMidiIngress = true;
        // Audited same exact source/gate; reserve without moving mixed stage.
        params.preparePluginFinalOutput =
            [&](tracktion::Plugin &p) -> std::optional<tracktion::CreateNodeParams::PluginFinalOutputPlan>
        {
            require(&p == source.get() || &p == gate.get(), "Original fixture reserve universe");
            tracktion::CreateNodeParams::PluginFinalOutputPlan result;
            result.shortMidiCapacity = 512;
            return result;
        };
    }
    else
        policy->install(params);
    ready_graph_acceptance::Counts pools;
    ready_graph_acceptance::Player player(
        [&](auto &p) { return std::make_unique<ready_graph_acceptance::CountingPool>(p, pools); });
    player.setNumThreads(0);
    player.enablePooledMemoryAllocations(false);
    juce::String receipt =
        "Actual Edit/createNodeForEdit, real MixerMasterGate, original private VoiceBank sample fixture.\n"
        "No production FixedSampler/physical typing router, external MIDI, worker, hardware, or full app "
        "activation claim.\n";
    std::vector<float> actual, expected;
    actual.reserve(32768);
    expected.reserve(32768);
    const auto retain = [&]
    {
        for (int which = 0; which < 2; ++which)
        {
            const auto &data = which ? expected : actual;
            juce::FileOutputStream out(folder.getChildFile(which ? "expected.raw" : "actual.raw"));
            require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                        out.write(data.data(), data.size() * sizeof(float)),
                    "Retain every port PCM sample");
            out.flush();
            require(out.getStatus().wasOk(), "Flush port PCM");
        }
        juce::FileOutputStream out(folder.getChildFile("actual.txt"));
        require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                    out.writeText(receipt, false, false, "\n"),
                "Retain port facts");
        out.flush();
        require(out.getStatus().wasOk(), "Flush port facts");
    };
    tracktion::LiveMidiInjectingNode *legacyIngress = nullptr;
    const auto graph = [&](const tracktion::CreateNodeParams &selected)
    {
        const auto before = sample->calls;
        auto result = tracktion::createNodeForEdit(*edit, selected);
        require(bool(result) && sample->calls == before, "Actual native construction performs no source DSP");
        unsigned nativeObservers = 0, stages = 0, pluginStages = 0;
        std::set<const void *> origins;
        for (auto *node : tracktion::graph::getNodes(*result, tracktion::graph::VertexOrdering::postordering))
        {
            const auto p = node->getNodeProperties();
            if (mode == 4 || mode >= 5)
                if (auto *ingress = dynamic_cast<tracktion::LiveMidiInjectingNode *>(node))
                    legacyIngress = ingress;
            if (dynamic_cast<tracktion::LiveMidiOutputNode *>(node))
                ++nativeObservers;
            if (dynamic_cast<experimental::RegisteredAudioOutputPort *>(node))
                ++stages;
            if (auto *pluginNode = dynamic_cast<tracktion::PluginNode *>(node);
                pluginNode && p.audioCompensationStage)
                ++pluginStages;
            if (p.audioCompensationStage)
            {
                origins.insert(p.audioCompensationStageOrigin);
                if (mode != 4)
                    require(!p.hasMidi, "Every actually registered audio stage is structurally MIDI-empty");
            }
        }
        receipt += "constructedStages=" + juce::String(stages) +
                   ",nativeObservers=" + juce::String(nativeObservers) +
                   ",pluginStages=" + juce::String(pluginStages) + "\n";
        retain();
        if (mode != 4)
            require(stages == 1 && pluginStages == 0 && nativeObservers == 0 && origins.size() == 1 &&
                        !result->getNodeProperties().hasMidi,
                    "One actual post-port stage, no mixed duplicate, no suppressed real observer, "
                    "structurally empty root");
        return result;
    };
    player.setNode(graph(params), 48000, 256);
    choc::buffer::ChannelArrayBuffer<float> output(choc::buffer::Size::create(2, 256));
    tracktion::MidiMessageArray midi;
    midi.reserve(512);
    juce::int64 reference = 0;
    const auto render = [&](const char *phase)
    {
        const auto start = sample->frames;
        const auto calls = sample->calls;
        const auto held = unsigned(sample->heldA) + unsigned(sample->heldB);
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        midi.clear();
        output.clear();
        process.update(48000, {reference, reference + 256},
                       tracktion::ProcessState::UpdateContinuityFlags::yes);
        const auto begin = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                bool prior = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = prior;
                }
            } probe;
            player.process({256, {reference, reference + 256}, {output.getView(), midi}});
        }
        const auto seconds =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - begin);
        bool exact = true;
        for (int n = 0; n < 256; ++n)
            for (int c = 0; c < 2; ++c)
            {
                const auto got =
                    output.getView().getSample(choc::buffer::ChannelCount(c), choc::buffer::FrameCount(n));
                const auto wanted =
                    PrivateSample::pcm(c, std::uint64_t(reference) + std::uint64_t(n)) * (held == 2   ? 1.f
                                                                                          : held == 1 ? .5f
                                                                                                      : 0.f);
                actual.push_back(got);
                expected.push_back(wanted);
                exact &= got == wanted;
            }
        const auto terminal = policy->terminal->observe();
        const auto selected = player.graphSelection();
        receipt += juce::String(phase) + ",reference=" + juce::String(reference) +
                   ",selected=" + juce::String(juce::int64(selected.selectedSerial)) +
                   ",frames=" + juce::String(juce::int64(sample->frames)) +
                   ",sourceCalls=" + juce::String(sample->calls) + ",held=" + juce::String(sample->held()) +
                   ",on=" + juce::String(juce::int64(terminal.on)) +
                   ",off=" + juce::String(juce::int64(terminal.off)) +
                   ",panicMessages=" + juce::String(juce::int64(terminal.panicMessages)) +
                   ",panicFlags=" + juce::String(juce::int64(terminal.panicFlags)) +
                   ",fault=" + juce::String(int(terminal.fault)) + ",rootMidi=" + juce::String(midi.size()) +
                   ",rootPanic=" + juce::String(int(midi.isAllNotesOff)) +
                   ",new=" + juce::String(juce::int64(callbackAllocations.load() - allocations)) +
                   ",delete=" + juce::String(juce::int64(callbackDestructions.load() - destructions)) +
                   ",seconds=" + juce::String(seconds, 12) + ",exact=" + juce::String(int(exact)) + "\n";
        retain();
        require(exact && start == std::uint64_t(reference) && sample->calls == calls + 1 &&
                    sample->frames == std::uint64_t(reference) + 256 && sample->held() == held &&
                    selected.referenceStart == reference && selected.referenceEnd == reference + 256 &&
                    callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions && seconds <= 256. / 48000.,
                "Full exact signed sample PCM, private owners, source once, clock, noheap and "
                "unchanged256-frame deadline");
        if (mode != 4)
            require(midi.isEmpty() && !midi.isAllNotesOff &&
                        terminal.fault == experimental::NativeMidiTerminal::Fault::none,
                    "Actual terminal consumption leaves real audio-root MIDI and panic empty");
        reference += 256;
    };
    const auto refused = [&](const char *code, const auto &action)
    {
        const auto model = edit->state.toXmlString();
        const auto calls = sample->calls;
        const auto frames = sample->frames;
        const auto selection = player.graphSelection().selectedSerial;
        bool failed = false;
        try
        {
            action();
        }
        catch (const std::invalid_argument &error)
        {
            receipt += "refusal=" + juce::String(error.what()) + "\n";
            failed = juce::String(error.what()).startsWith(code);
        }
        retain();
        require(failed && edit->state.toXmlString() == model && sample->calls == calls &&
                    sample->frames == frames && player.graphSelection().selectedSerial == selection,
                "Typed refusal leaves native model/selected source/clock unchanged");
    };
    try
    {
        render("baseline");
        if (mode == 0)
        {
            track->injectLiveMidiMessage(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 7);
            render("native_on");
            require(policy->terminal->observe().on == 1,
                    "One actual native event accounted by owned terminal");
            track->injectLiveMidiMessage(juce::MidiMessage::noteOff(1, 64), 7);
            {
                auto cancelled = player.prepareFixedGraph(graph(params));
                require(!cancelled.empty(), "Real private successor cancellation");
            }
            render("queued_off_after_cancel");
            require(policy->terminal->observe().off == 1,
                    "Queued native release survives candidate cancellation");
            auto candidate = player.prepareFixedGraph(graph(params));
            track->injectLiveMidiMessage(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 7);
            render("old_after_prepare");
            const auto serial = candidate.publicationSerial();
            auto retired = player.makeGraphRetirementSlot();
            require(player.publishReadyGraph(candidate, retired) ==
                        ready_graph_acceptance::Player::ReadyGraphStatus::ready,
                    "Actual native post");
            render("selected_successor");
            require(player.graphSelection().selectedSerial == serial && policy->terminal->observe().on == 2 &&
                        player.retireReadyGraph(retired),
                    "Actual selection preserves single ingress/consumer/held-source and retires old graph "
                    "CONTROL");
            track->injectLiveMidiMessage(juce::MidiMessage::allNotesOff(1), 7);
            render("native_panic");
            require(policy->terminal->observe().panicMessages == 1 &&
                        policy->terminal->observe().active == 0 && sample->held() == 2,
                    "Native panic consumed and explicitly leaves both private sample owners held");
            sample->releaseA();
            render("one_private_release");
            sample->releaseB();
            render("both_private_release");
        }
        else if (mode == 1)
        {
            Observer observer;
            track->addListener(&observer);
            try
            {
                refused("audioPort.nativeObserver",
                        [&] { auto denied = player.prepareFixedGraph(graph(params)); });
            }
            catch (...)
            {
                track->removeListener(&observer);
                throw;
            }
            track->removeListener(&observer);
            require(observer.delivered == 0,
                    "Unknown real output observer was not called or silently redirected");
            render("after_observer_refusal");
        }
        else if (mode == 2)
        {
            track->getOutput().setOutputToDefaultDevice(true);
            refused("audioPort.nativeRouting",
                    [&] { auto denied = player.prepareFixedGraph(graph(params)); });
            track->getOutput().setOutputToDefaultDevice(false);
            render("after_external_midi_refusal");
        }
        else if (mode == 3)
        {
            tracktion::Plugin::Ptr unknown =
                new RoutingSignalProbe({*edit, state(*edit, "vibera_routing_signal_probe"), false}, 1, 1);
            refused("audioPort.producer",
                    [&]
                    {
                        auto denied =
                            experimental::SingleTrackMixerAudioPort::prepare(*track, process, unknown, gate);
                    });
            render("after_unsupported_source_refusal");
        }
        else if (mode >= 5)
        {
            require(legacyIngress != nullptr, "Actual selected bounded ingress for malformed-data refusal");
            const auto ingressBefore = legacyIngress->boundedIngressObservation();
            const auto terminalBefore = policy->terminal->observe();
            track->injectLiveMidiMessage(juce::MidiMessage(mode == 5 ? 0x90 : 0x80, 255, 100), 7);
            const auto refusedIngress = legacyIngress->boundedIngressObservation();
            receipt += "malformedRuntimeFault=" + juce::String(int(refusedIngress.fault)) +
                       ",accepted=" + juce::String(juce::int64(refusedIngress.accepted)) +
                       ",delivered=" + juce::String(juce::int64(refusedIngress.delivered)) + "\n";
            retain();
            require(refusedIngress.runtime == ingressBefore.runtime &&
                        refusedIngress.accepted == ingressBefore.accepted &&
                        refusedIngress.delivered == ingressBefore.delivered &&
                        refusedIngress.fault ==
                            tracktion::LiveMidiInjectingNode::IngressFault::unsupportedMessage,
                    "Raw invalid note data refused before actual ingress queue mutation");
            render("after_raw_note_data_refusal");
            const auto terminalAfter = policy->terminal->observe();
            require(
                terminalAfter.on == terminalBefore.on && terminalAfter.off == terminalBefore.off &&
                    terminalAfter.other == terminalBefore.other &&
                    terminalAfter.panicMessages == terminalBefore.panicMessages &&
                    terminalAfter.active == terminalBefore.active && sample->held() == 2,
                "Malformed native input never indexes/changes terminal notes or private sample ownership");
        }
        else
        {
            tracktion::CreateNodeParams port{process};
            port.sampleRate = 48000;
            port.blockSize = 256;
            port.forRendering = true;
            port.includeMasterPlugins = false;
            policy->install(port);
            require(legacyIngress != nullptr, "Actual selected legacy ingress retained");
            track->injectLiveMidiMessage(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 7);
            const auto before = legacyIngress->boundedIngressObservation();
            refused("audioPort.baseline", [&]
                    { auto denied = player.prepareFixedGraph(tracktion::createNodeForEdit(*edit, port)); });
            const auto after = legacyIngress->boundedIngressObservation();
            receipt += "legacyQueueAccepted=" + juce::String(juce::int64(after.accepted)) +
                       ",delivered=" + juce::String(juce::int64(after.delivered)) + "\n";
            retain();
            require(before.runtime == after.runtime && before.accepted == 1 && before.delivered == 0 &&
                        after.accepted == before.accepted && after.delivered == before.delivered &&
                        after.fault == tracktion::LiveMidiInjectingNode::IngressFault::none &&
                        sample->held() == 2,
                    "Refused warm conversion preserves actual queued ingress and held private owners without "
                    "candidate DSP");
            // Do not claim legacy downstream allocation safety: its unreserved
            // post-plugin TrackMutingNode still lies outside this opt-in.
            // The owned disposable queue is retired unprocessed on CONTROL.
        }
        receipt += "PASSED actual narrow factory audio-port case\n";
        retain();
    }
    catch (...)
    {
        receipt += "FAILED actual narrow factory case; all captured PCM and facts retained\n";
        retain();
        throw;
    }
}
inline void malformedTerminalData(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh direct terminal validation evidence");
    juce::String facts = "Original component terminal validation, not native input/provider/hardware.\n";
    for (const int status : {0x90, 0x80})
    {
        experimental::NativeMidiTerminal terminal;
        tracktion::MidiMessageArray valid, invalid;
        valid.reserve(1);
        invalid.reserve(1);
        valid.addMidiMessage(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 0., 7);
        terminal.consume(valid);
        const auto before = terminal.observe();
        const auto heldBefore = terminal.held;
        const auto eventBefore = terminal.lastEvents[0];
        const auto countBefore = terminal.lastCount;
        invalid.addMidiMessage(juce::MidiMessage(status, 255, 100), 0., 7);
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        {
            struct Probe
            {
                bool prior = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = prior;
                }
            } probe;
            terminal.consume(invalid);
        }
        const auto after = terminal.observe();
        facts +=
            "rawStatus=" + juce::String(status) + ",rawKey=" + juce::String(invalid[0].getNoteNumber()) +
            ",fault=" + juce::String(int(after.fault)) + ",beforeOn=" + juce::String(juce::int64(before.on)) +
            ",afterOn=" + juce::String(juce::int64(after.on)) +
            ",beforeActive=" + juce::String(before.active) + ",afterActive=" + juce::String(after.active) +
            ",new=" + juce::String(juce::int64(callbackAllocations.load() - allocations)) +
            ",delete=" + juce::String(juce::int64(callbackDestructions.load() - destructions)) + "\n";
        juce::FileOutputStream out(folder.getChildFile("actual.txt"));
        require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                    out.writeText(facts, false, false, "\n"),
                "Retain raw terminal refusal facts");
        out.flush();
        require(out.getStatus().wasOk(), "Flush terminal refusal facts");
        require(after.fault == experimental::NativeMidiTerminal::Fault::unsupported &&
                    after.blocks == before.blocks && after.on == before.on && after.off == before.off &&
                    after.panicMessages == before.panicMessages && after.panicFlags == before.panicFlags &&
                    after.other == before.other && after.active == before.active &&
                    terminal.held == heldBefore && terminal.lastCount == countBefore &&
                    terminal.lastEvents[0].bytes == eventBefore.bytes &&
                    terminal.lastEvents[0].source == eventBefore.source && invalid.size() == 1 &&
                    invalid[0].getNoteNumber() == 255 && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions,
                "Both malformed raw note types refuse before indexing/accounting/ownership/input mutation or "
                "heap activity");
    }
}
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh independent actual factory port aggregate");
    const std::array<const char *, 7> names{
        "actual_baseline_cancel_select", "unaccounted_listener", "external_midi_route",
        "unsupported_producer",          "warm_mixed_refused",   "malformed_raw_note_on",
        "malformed_raw_note_off"};
    juce::String failures;
    for (int mode = 0; mode < int(names.size()); ++mode)
        try
        {
            scenario(folder.getChildFile(names[std::size_t(mode)]), mode);
        }
        catch (const std::exception &e)
        {
            failures += juce::String(names[std::size_t(mode)]) + ": " + e.what() + "\n";
        }
        catch (...)
        {
            failures += juce::String(names[std::size_t(mode)]) + ": unknown failure\n";
        }
    try
    {
        malformedTerminalData(folder.getChildFile("terminal_raw_data_refused"));
    }
    catch (const std::exception &e)
    {
        failures += "terminal_raw_data_refused: " + juce::String(e.what()) + "\n";
    }
    catch (...)
    {
        failures += "terminal_raw_data_refused: unknown failure\n";
    }
    juce::FileOutputStream out(folder.getChildFile("aggregate.txt"));
    require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                out.writeText(failures.isEmpty() ? "PASSED all seven narrow actual factory cases and two raw "
                                                   "terminal component negatives\n"
                                                 : failures,
                              false, false, "\n"),
            "Retain all independent cases");
    out.flush();
    require(out.getStatus().wasOk(), "Flush factory aggregate");
    require(failures.isEmpty(), "All narrow actual factory cases must pass");
}
} // namespace vibera::test::app_mixer_audio_port
