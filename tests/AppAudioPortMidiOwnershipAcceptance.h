// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "GraphOutputRoutingAcceptance.h"
#include <tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.h>
#include <array>
#include <exception>
#include <cstdint>

// Original application-native queue ownership prerequisite. NOT another
// ConnectedNode mechanism test; no audio-port policy is enabled here.
namespace vibera::test::app_audio_port
{
using Player = ready_graph_acceptance::Player;
struct Settings
{
    const juce::String previous = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
    explicit Settings(const juce::File& folder)
    { require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", folder.getFullPathName().toWideCharPointer()) == 0,
              "Own disposable native MIDI prerequisite settings"); }
    ~Settings() { _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", previous.toWideCharPointer()); }
};
inline void scenario(const juce::File& folder, int mode, bool boundedIngress = false)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual Edit MIDI ownership case");
    // CONTROL-only diagnostic. Markers bracket the existing reverse destruction
    // order without moving/resetting any graph, Edit, engine or listener owner.
    const auto stageFile = folder.getChildFile("teardown-stages.txt");
    const auto stageOrigin = juce::Time::getHighResolutionTicks();
    const auto stage = [&](const char *name) noexcept
    {
        try
        {
            const auto now = juce::Time::getHighResolutionTicks();
            // Pinned JUCE FileOutputStream opens an existing file at its end.
            juce::FileOutputStream out(stageFile);
            if (!out.openedOk())
                return;
            const auto line =
                juce::String(name) + ",qpc=" + juce::String(now) + ",elapsedMs=" +
                juce::String(1000. * juce::Time::highResolutionTicksToSeconds(now - stageOrigin), 6) + "\n";
            out.writeText(line, false, false, "\n");
            out.flush();
        }
        catch (...)
        {
        } // Diagnostic I/O must not replace the original failure during unwind.
    };
    struct TeardownStage
    {
        const decltype(stage) &write;
        const char *name;
        bool active = true;
        ~TeardownStage() noexcept
        {
            if (active)
                write(name);
        }
    };
    stage("scenario.enter");
    Settings settings(folder.getChildFile("settings"));
    TeardownStage audioDestroyed{stage, "AudioEngine.destroy.end", false};
    stage("AudioEngine.construct.begin");
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    audioDestroyed.active = true;
    stage("AudioEngine.construct.end");
    TeardownStage audioDestroyBegin{stage, "AudioEngine.destroy.begin"};
    te::Edit::Options options{audio->engine, te::createEmptyEdit(audio->engine), {}};
    options.numAudioTracks = 1;
    options.role = te::Edit::forRendering;
    options.closedNativeIO = true;
    options.initialSourceGeneration = 701;
    TeardownStage editDestroyed{stage, "Edit.destroy.end", false};
    stage("Edit.construct.begin");
    auto edit = te::Edit::createEdit(options);
    editDestroyed.active = bool(edit);
    stage("Edit.construct.end");
    TeardownStage editDestroyBegin{stage, "Edit.destroy.begin", bool(edit)};
    require(bool(edit), "Actual closed/local Edit");
    auto tracks = te::getAudioTracks(*edit);
    require(tracks.size() == 1, "One exact application-like anchor source track");
    auto* track = tracks[0];
    track->getOutput().setOutputToDefaultDevice(false);
    auto clip = track->insertMIDIClip("Empty application anchor",
        {te::TimePosition::fromSeconds(0), te::TimePosition::fromSeconds(4)}, nullptr);
    require(bool(clip) && clip->getSequence().isEmpty(), "Real empty anchor, never infer no injection from it");
    clip->setUsesProxy(false);
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, "vibera_routing_signal_probe", nullptr);
    te::EditItemID::readOrCreateNewID(*edit, state);
    te::Plugin::Ptr probe = new RoutingSignalProbe({*edit, state, false}, 1, 1);
    track->pluginList.insertPlugin(probe, 0, nullptr);
    auto* signal = dynamic_cast<RoutingSignalProbe*>(probe.get());
    tracktion::graph::PlayHead head;
    tracktion::graph::PlayHeadState headState(head);
    te::ProcessState process(headState, edit->tempoSequence);
    head.playSyncedToRange({});
    te::CreateNodeParams params{process};
    params.sampleRate = 48000; params.blockSize = 256;
    params.forRendering = true; params.includeMasterPlugins = false;
    params.boundedNativeMidiIngress = boundedIngress; // Explicit diagnostic opt-in only.
    params.pluginListResolver = [track, probe](const te::PluginList& list)
    {
        te::Plugin::Array result;
        if (&list == &track->pluginList) result.add(probe);
        return result;
    };
    ready_graph_acceptance::Counts pools;
    TeardownStage playerDestroyed{stage, "Player.destroy.end", false};
    stage("Player.construct.begin");
    Player player([&](auto& owner) { return std::make_unique<ready_graph_acceptance::CountingPool>(owner, pools); });
    playerDestroyed.active = true;
    stage("Player.construct.end");
    TeardownStage playerDestroyBegin{stage, "Player.destroy.begin"};
    player.setNumThreads(0); player.enablePooledMemoryAllocations(false);
    unsigned lastInjectorCount = 0;
    std::size_t lastInjectorId = 0, selectedInjectorId = 0;
    te::LiveMidiInjectingNode* lastInjector = nullptr;
    te::LiveMidiInjectingNode* selectedInjector = nullptr;
    te::LiveMidiInjectingNode* pendingInjector = nullptr;
    std::uint64_t pendingInjectorSerial = 0;
    const void* selectedRuntime = nullptr;
    const auto graph = [&]
    {
        auto result = te::createNodeForEdit(*edit, params); // Actual native factory, not synthetic injected nodes.
        require(bool(result), "Actual source graph factory");
        lastInjectorCount = 0;
        for (auto* node : tracktion::graph::getNodes(*result, tracktion::graph::VertexOrdering::postordering))
            if (auto* injector = dynamic_cast<te::LiveMidiInjectingNode*>(node))
            { ++lastInjectorCount; lastInjectorId = node->getNodeProperties().nodeID; lastInjector = injector; }
        require(lastInjectorCount == 1, "Actual factory installed exactly one native injector per graph");
        return result;
    };
    player.setNode(graph(), 48000, 256);
    selectedInjectorId = lastInjectorId;
    selectedInjector = lastInjector;
    selectedRuntime = selectedInjector->boundedIngressObservation().runtime;
    choc::buffer::ChannelArrayBuffer<float> output(choc::buffer::Size::create(2, 256));
    tracktion::MidiMessageArray midi;
    midi.reserve(128);
    juce::int64 reference = 0;
    juce::String receipt = "Actual native factory/Track injection/ready-holder ownership only.\n"
        "No port enabled; no RT allocation/deadline/worker/hardware qualification.\n";
    unsigned on = 0, off = 0, panic = 0;
    const auto retain = [&]
    {
        juce::FileOutputStream out(folder.getChildFile("midi-ownership.txt"));
        require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                    out.writeText(receipt, false, false, "\n"), "Retain actual queue ownership evidence");
        out.flush(); require(out.getStatus().wasOk(), "Flush ownership evidence");
    };
    const auto render = [&](const char* phase)
    {
        const auto calls = signal->calls;
        const juce::Range<juce::int64> range{reference, reference + 256};
        process.update(48000, range, te::ProcessState::UpdateContinuityFlags::yes);
        midi.clear(); output.clear(); player.process({256, range, {output.getView(), midi}});
        for (const auto& event : midi)
        {
            const auto& message = event;
            if (message.isNoteOn() && message.getNoteNumber() == 64) ++on;
            if (message.isNoteOff() && message.getNoteNumber() == 64) ++off;
            if (message.isAllNotesOff()) ++panic;
            receipt += juce::String(phase) + ",actualEvent=" + message.getDescription() +
                ",timestamp=" + juce::String(message.getTimeStamp(), 12) + "\n";
        }
        const auto selected = player.graphSelection();
        if (pendingInjector && selected.selectedSerial == pendingInjectorSerial)
        { selectedInjector = pendingInjector; pendingInjector = nullptr; pendingInjectorSerial = 0; }
        receipt += juce::String(phase) + ",start=" + juce::String(reference) +
            ",selected=" + juce::String(juce::int64(selected.selectedSerial)) +
            ",on=" + juce::String(on) + ",off=" + juce::String(off) + ",panic=" + juce::String(panic) +
            ",sourceCalls=" + juce::String(signal->calls) + ",nativePanic=" + juce::String(int(midi.isAllNotesOff)) + "\n";
        if (boundedIngress)
        {
            const auto state = selectedInjector->boundedIngressObservation();
            receipt += "runtime=" + juce::String::toHexString(juce::int64(reinterpret_cast<std::uintptr_t>(state.runtime))) +
                ",scope=" + juce::String::toHexString(juce::int64(reinterpret_cast<std::uintptr_t>(state.scope))) +
                ",fault=" + juce::String(int(state.fault)) + ",accepted=" + juce::String(juce::int64(state.accepted)) +
                ",delivered=" + juce::String(juce::int64(state.delivered)) + "\n";
            retain();
            require(state.enabled && state.registered && state.scope == &process && state.runtime == selectedRuntime &&
                        state.fault == te::LiveMidiInjectingNode::IngressFault::none,
                    "Actual selected shell retains exact healthy per-context Runtime");
        }
        reference += 256; retain();
        require(signal->calls == calls + 1 && selected.referenceStart == range.getStart() &&
                    selected.referenceEnd == range.getEnd(), "Native source once and exact range across ownership cases");
    };
    const auto inject = [&](const juce::MidiMessage& message)
    {
        track->injectLiveMidiMessage(message, {}); // Actual CONTROL application/native entry point.
        receipt += "injected=" + message.getDescription() + "\n"; retain();
    };
    try
    {
        render("baseline_start");
        if (mode == 0)
        {
            inject(juce::MidiMessage::noteOn(1, 64, juce::uint8(100))); render("baseline_on");
            inject(juce::MidiMessage::noteOff(1, 64)); render("baseline_off");
            inject(juce::MidiMessage::allNotesOff(1)); render("baseline_panic");
            retain(); require(on == 1 && off == 1 && panic == 1, "Baseline has a genuine native delivery path");
        }
        else if (mode >= 1 && mode <= 3)
        {
            if (mode == 2) { inject(juce::MidiMessage::noteOn(1, 64, juce::uint8(100))); render("held_before_cancel"); }
            const auto model = edit->state.toXmlString();
            if (mode == 1) inject(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)));
            else if (mode == 2) inject(juce::MidiMessage::noteOff(1, 64));
            else inject(juce::MidiMessage::allNotesOff(1));
            const auto sourceBefore = signal->calls;
            {
                auto candidate = player.prepareFixedGraph(graph());
                receipt += "candidatePreparedNotPosted=1\n"; retain();
                require(!candidate.empty() && signal->calls == sourceBefore,
                        "Actual private graph preparation performs no source DSP");
            } // Actual ready-holder cancellation, never fake Undo or node queue manipulation.
            render("after_candidate_cancel");
            retain(); require(edit->state.toXmlString() == model &&
                (mode == 1 ? on == 1 : mode == 2 ? on == 1 && off == 1 : panic == 1),
                "Cancelled actual native successor must retain exact queued on/off/panic once");
        }
        else if (mode == 5)
        {
            inject(juce::MidiMessage::noteOn(1, 64, juce::uint8(100))); render("held_before_first_adoption");
            auto first = player.prepareFixedGraph(graph());
            receipt += "firstOldInjector=" + juce::String(juce::int64(selectedInjectorId)) +
                ",firstNewInjector=" + juce::String(juce::int64(lastInjectorId)) +
                ",matched=" + juce::String(int(selectedInjectorId == lastInjectorId)) + "\n"; retain();
            auto retired = player.makeGraphRetirementSlot();
            const auto firstSerial = first.publicationSerial();
            require(player.publishReadyGraph(first, retired) == Player::ReadyGraphStatus::ready,
                    "First actual successor selected before second-generation cancellation");
            pendingInjector = lastInjector; pendingInjectorSerial = firstSerial;
            render("first_successor_selected");
            require(on == 1 && player.retireReadyGraph(retired), "Held original event once; first holder retired CONTROL");
            selectedInjectorId = lastInjectorId;
            inject(juce::MidiMessage::noteOff(1, 64));
            {
                auto second = player.prepareFixedGraph(graph());
                receipt += "secondOldInjector=" + juce::String(juce::int64(selectedInjectorId)) +
                    ",secondNewInjector=" + juce::String(juce::int64(lastInjectorId)) +
                    ",matched=" + juce::String(int(selectedInjectorId == lastInjectorId)) + "\n"; retain();
                require(!second.empty(), "Actual second-generation private successor");
            }
            render("second_generation_cancel_queued_off");
            retain(); require(on == 1 && off == 1, "Second-generation cancellation preserves exactly one queued release");
        }
        else
        {
            auto candidate = player.prepareFixedGraph(graph());
            inject(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)));
            render("old_selected_with_private_successor");
            require(on == 1, "One actual delivery while old graph remains selected");
            auto retired = player.makeGraphRetirementSlot();
            const auto serial = candidate.publicationSerial();
            require(player.publishReadyGraph(candidate, retired) == Player::ReadyGraphStatus::ready,
                    "Actual native successor publication");
            pendingInjector = lastInjector; pendingInjectorSerial = serial;
            render("new_graph_selected_after_old_delivery");
            retain(); require(player.graphSelection().selectedSerial == serial && on == 1,
                    "Same native event must not be replayed by private successor listener");
            require(player.retireReadyGraph(retired), "Old actual holder reclaimed on CONTROL");
        }
        receipt += "PASSED ownership scenario\n"; retain();
        stage("scenario.assertions.complete");
    }
    catch (...) { receipt += "FAILED ownership scenario; actual failure retained\n"; retain(); throw; }
}
inline void tests(const juce::File& folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh app port ownership prerequisite aggregate");
    const std::array<const char*,6> names{"native_baseline", "cancel_note_on", "cancel_note_off", "cancel_panic", "overlap_once", "second_generation_cancel_off"};
    juce::String failures;
    for (int mode=0; mode<int(names.size()); ++mode)
        try { scenario(folder.getChildFile(names[std::size_t(mode)]), mode); }
        catch (const std::exception& error) { failures += juce::String(names[std::size_t(mode)]) + ": " + error.what() + "\n"; }
        catch (...) { failures += juce::String(names[std::size_t(mode)]) + ": non-standard exception\n"; }
    juce::FileOutputStream out(folder.getChildFile("aggregate.txt"));
    require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                out.writeText(failures.isEmpty() ? "PASSED all six actual native ownership cases\n" : failures, false, false, "\n"),
            "Retain every independent native ownership outcome");
    out.flush(); require(out.getStatus().wasOk(), "Flush aggregate");
    require(failures.isEmpty(), "Actual native queue ownership must qualify before app audio-only port admission");
}
// Separate explicit branch: original legacy cases above remain unchanged.
inline void boundedTests(const juce::File& folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh bounded native ingress ownership aggregate");
    const std::array<const char*,6> names{"native_baseline", "cancel_note_on", "cancel_note_off", "cancel_panic", "overlap_once", "second_generation_cancel_off"};
    juce::String failures;
    for (int mode=0; mode<int(names.size()); ++mode)
        try { scenario(folder.getChildFile(names[std::size_t(mode)]), mode, true); }
        catch (const std::exception& error) { failures += juce::String(names[std::size_t(mode)]) + ": " + error.what() + "\n"; }
        catch (...) { failures += juce::String(names[std::size_t(mode)]) + ": non-standard exception\n"; }
    juce::FileOutputStream out(folder.getChildFile("aggregate.txt"));
    require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                out.writeText(failures.isEmpty() ? "PASSED six bounded native ownership cases\n" : failures, false, false, "\n"), "Retain bounded aggregate");
    out.flush(); require(out.getStatus().wasOk(), "Flush bounded aggregate");
    require(failures.isEmpty(), "Bounded ingress ownership cases must all pass");
}
} // namespace vibera::test::app_audio_port
