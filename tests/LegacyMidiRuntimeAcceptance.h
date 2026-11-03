// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "NativeMidiIngressDspAcceptance.h"
#include <cstring>

// Additive ownership tests. Keep all original six default + six bounded factory
// cases unchanged. Generic policy is NOT callback allocation/lock qualification.
namespace vibera::test::legacy_midi_runtime
{
using namespace tracktion::graph;
using Player = ready_graph_acceptance::Player;
using Injector = te::LiveMidiInjectingNode;
struct Fixture
{
    static juce::File own(const juce::File &output)
    {
        require(!output.exists() && output.createDirectory().wasOk(),
                "Fresh direct generic Runtime ownership case");
        return output;
    }
    app_audio_port::Settings settings;
    std::unique_ptr<AudioEngine> audio;
    std::unique_ptr<te::Edit> edit;
    te::AudioTrack *track = nullptr;
    PlayHead head;
    PlayHeadState headState{head};
    std::unique_ptr<te::ProcessState> scope;
    ready_graph_acceptance::Counts counts;
    Player player;
    unsigned calls = 0;
    std::int64_t reference = 0;
    juce::String report;
    const juce::File folder;
    explicit Fixture(const juce::File &output, bool initial = true)
        : settings(own(output).getChildFile("settings")),
          audio(std::make_unique<AudioEngine>(false, output.getChildFile("guide.json"))),
          player([&](auto &p) { return std::make_unique<ready_graph_acceptance::CountingPool>(p, counts); }),
          folder(output)
    {
        require(folder.isDirectory(), "Owned direct generic Runtime output");
        te::Edit::Options options{audio->engine, te::createEmptyEdit(audio->engine), {}};
        options.numAudioTracks = 1;
        options.role = te::Edit::forRendering;
        edit = te::Edit::createEdit(options);
        require(bool(edit), "Actual ordinary Edit, no bounded/local opt-in");
        track = te::getAudioTracks(*edit)[0];
        scope = std::make_unique<te::ProcessState>(headState, edit->tempoSequence);
        player.setNumThreads(0);
        player.enablePooledMemoryAllocations(false);
        player.setNode(graph(initial), 48000, 256);
    }
    std::unique_ptr<Node> graph(bool injector = true, bool compatibility = false)
    {
        std::unique_ptr<Node> source = std::make_unique<native_midi_ingress::ShortSource>(calls, 0);
        if (injector)
            source = std::make_unique<Injector>(*track, std::move(source), nullptr,
                                                compatibility ? nullptr : scope.get());
        return source;
    }
    int listeners() const
    {
        return track->getListeners().getListeners().size();
    }
    void retain(const char *phase, const te::MidiMessageArray *data = nullptr)
    {
        const auto selection = player.graphSelection();
        report += juce::String(phase) + ",listeners=" + juce::String(listeners()) +
                  ",selected=" + juce::String(selection.selectedSerial) +
                  ",posted=" + juce::String(selection.postedSerial) + ",sourceCalls=" + juce::String(calls) +
                  "\n";
        if (data)
            for (const auto &event : *data)
                report += event.getDescription() + ",source=" + juce::String(event.mpeSourceID) +
                          ",time=" + juce::String(event.getTimeStamp(), 12) + "\n";
        require(folder.getChildFile("ownership.txt").replaceWithText(report),
                "Retain actual listener/selection/event facts BEFORE assertions");
    }
    void inject()
    {
        track->injectLiveMidiMessage(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 91);
        track->injectLiveMidiMessage(juce::MidiMessage::noteOff(1, 64), 91);
        track->injectLiveMidiMessage(juce::MidiMessage::allNotesOff(1), 91);
    }
    void render(const char *phase, int expected, int frames = 256, double rate = 48000)
    {
        choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2, uint32_t(frames)));
        te::MidiMessageArray midi;
        const auto before = calls;
        const juce::Range<std::int64_t> range{reference, reference + frames};
        scope->update(rate, range, te::ProcessState::UpdateContinuityFlags::yes);
        player.process({choc::buffer::FrameCount(frames), range, {pcm.getView(), midi}});
        reference += frames;
        retain(phase, &midi);
        bool exact = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < frames; ++n)
                exact &= pcm.getView().getSample(uint32_t(c), uint32_t(n)) == (c ? -.0625f : .125f);
        require(exact && calls == before + 1 && midi.size() == expected,
                "Actual source once, literal PCM and exact delivered generic event count");
        if (expected == 3)
            require(midi[0].isNoteOn() && midi[1].isNoteOff() && midi[2].isAllNotesOff() &&
                        midi[0].mpeSourceID == 91 && midi[1].mpeSourceID == 91 && midi[2].mpeSourceID == 91 &&
                        midi[0].getTimeStamp() == 0 && midi[1].getTimeStamp() == 0 &&
                        midi[2].getTimeStamp() == 0,
                    "Original generic On/Off/panic/source/timestamp0 order");
    }
};
inline void pendingGap(const juce::File &folder)
{
    Fixture f(folder);
    f.render("initial_owner_actually_selected", 0);
    require(f.player.graphSelection().selectedSerial != 0, "A is actually adopted before B is posted");
    f.inject();
    const auto count = f.listeners();
    auto gap = f.player.prepareFixedGraph(f.graph(false));
    auto retired = f.player.makeGraphRetirementSlot();
    require(f.player.publishReadyGraph(gap, retired) == Player::ReadyGraphStatus::ready,
            "Actual posted gap before adoption");
    bool refused = false;
    try
    {
        auto candidate = f.player.prepareFixedGraph(f.graph());
    }
    catch (const std::exception &e)
    {
        refused = juce::String(e.what()).startsWith("nativeMidi.legacyPendingGap:");
    }
    f.retain("posted_not_adopted_gap_refusal");
    require(refused && f.listeners() == count,
            "Missing posted predecessor with surviving actual scoped owner refuses before registration");
    f.render("gap_actually_selected", 0);
    refused = false;
    try
    {
        auto candidate = f.player.prepareFixedGraph(f.graph());
    }
    catch (const std::exception &e)
    {
        refused = juce::String(e.what()).startsWith("nativeMidi.legacyPendingGap:");
    }
    f.retain("selected_gap_old_holder_refusal");
    require(refused && f.listeners() == count,
            "Actual adopted gap still refuses while old retirement owner survives");
    require(f.player.retireReadyGraph(retired), "First publication retirement slot drained on CONTROL");
    // A was already selected when B was posted. LockFreeObject therefore
    // returns an empty slot at that post, then holds A in pending storage when
    // DSP adopts B. The next CONTROL publication returns the actual A owner;
    // draining the first, empty slot must never be mistaken for retiring A.
    require(f.listeners() == count, "Selected predecessor remains in native pending retirement storage");
    auto nextGap = f.player.prepareFixedGraph(f.graph(false));
    retired = f.player.makeGraphRetirementSlot();
    require(f.player.publishReadyGraph(nextGap, retired) == Player::ReadyGraphStatus::ready,
            "Next native publication returns the actual selected predecessor owner");
    require(f.player.retireReadyGraph(retired), "Actual old generic queue/listener retired on CONTROL");
    f.retain("old_holder_retired");
    require(f.listeners() == count - 1, "Exactly one old Runtime listener removed");
    auto fresh = f.player.prepareFixedGraph(f.graph());
    retired = f.player.makeGraphRetirementSlot();
    f.inject();
    require(f.player.publishReadyGraph(fresh, retired) == Player::ReadyGraphStatus::ready,
            "True fresh reintroduction after exact old owner retirement");
    f.render("fresh_has_only_new_events", 3);
    f.render("no_replay", 0);
    require(f.player.retireReadyGraph(retired), "Gap holder retired");
    f.player.clearNode();
    f.retain("all_graphs_destroyed");
    require(f.listeners() == 0, "No stale registered Runtime after graph teardown");
}
inline void multipleCandidates(const juce::File &folder)
{
    Fixture f(folder);
    const auto count = f.listeners();
    f.inject();
    Player::ReadyGraph first = f.player.prepareFixedGraph(f.graph()),
                       second = f.player.prepareFixedGraph(f.graph());
    f.retain("two_exact_predecessor_candidates");
    require(f.listeners() == count, "Two private candidates share one listener without queue movement");
    first = Player::ReadyGraph{};
    f.render("first_incumbent_after_one_cancel", 3);
    auto retired = f.player.makeGraphRetirementSlot();
    require(f.player.publishReadyGraph(second, retired) == Player::ReadyGraphStatus::ready,
            "Other genuine candidate posted");
    f.render("other_candidate_selected_no_replay", 0);
    require(f.player.retireReadyGraph(retired), "Explicit native predecessor retirement");
}
inline void coldCandidatesAndMismatch(const juce::File &folder)
{
    Fixture f(folder, false);
    auto first = f.player.prepareFixedGraph(f.graph());
    const auto count = f.listeners();
    bool refused = false;
    try
    {
        auto second = f.player.prepareFixedGraph(f.graph());
    }
    catch (const std::exception &e)
    {
        refused = juce::String(e.what()).startsWith("nativeMidi.legacyPendingGap:");
    }
    f.retain("second_cold_owner_refused");
    require(refused && f.listeners() == count,
            "No posted predecessor never acquires a private candidate listener");
    first = Player::ReadyGraph{};
    auto selected = f.player.prepareFixedGraph(f.graph());
    auto retired = f.player.makeGraphRetirementSlot();
    require(f.player.publishReadyGraph(selected, retired) == Player::ReadyGraphStatus::ready,
            "Cold candidate after prior cancellation");
    f.render("cold_candidate_selected", 0);
    require(f.player.retireReadyGraph(retired), "Empty predecessor retired");
    f.inject();
    const auto before = f.listeners();
    refused = false;
    try
    {
        auto incompatible = f.player.prepareFixedGraph(f.graph(true, true));
    }
    catch (const std::exception &e)
    {
        refused = juce::String(e.what()).startsWith("nativeMidi.policy:");
    }
    f.retain("null_compatibility_migration_refused");
    require(refused && f.listeners() == before,
            "Null compatibility candidate destructor removes only its own listener, no scoped queue swap");
    f.render("first_scoped_callback_after_mismatch", 3);
}
inline void sameShellReprepare(const juce::File &folder)
{
    Fixture f(folder);
    const auto count = f.listeners();
    f.inject();
    f.player.prepareToPlay(44100, 128);
    f.retain("same_shell_real_rate_block_reprepare");
    require(f.listeners() == count, "Reprepare retains exact Runtime/listener without queue reset");
    f.render("first_reprepared_callback_pending_events", 3, 128, 44100);
    f.render("no_reprepare_replay", 0, 128, 44100);
}
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh additive generic ownership tests");
    using Test = void (*)(const juce::File &);
    const std::array<Test, 4> cases{pendingGap, multipleCandidates, coldCandidatesAndMismatch,
                                    sameShellReprepare};
    std::exception_ptr first;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto output = folder.getChildFile(juce::String(int(i)));
        try
        {
            cases[i](output);
        }
        catch (const std::exception &e)
        {
            output.getChildFile("failure.txt").replaceWithText(e.what());
            if (!first)
                first = std::current_exception();
        }
    }
    if (first)
        std::rethrow_exception(first);
}
} // namespace vibera::test::legacy_midi_runtime
