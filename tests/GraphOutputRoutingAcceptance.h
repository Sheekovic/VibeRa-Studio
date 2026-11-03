// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AudioEngine.h"
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>

namespace vibera::test
{
// Independent original signal/gain fixture. This is native graph routing DSP,
// not a sampler, MIDI ownership, worker, or production transaction qualification.
class RoutingSignalProbe final : public tracktion::Plugin
{
    const int source;
    const float gain;

  public:
    unsigned calls = 0;
    RoutingSignalProbe(tracktion::PluginCreationInfo info, int s, float g) : Plugin(info), source(s), gain(g)
    {
    }
    ~RoutingSignalProbe() override
    {
        notifyListenersOfDeletion();
    }
    juce::String getName() const override
    {
        return "Original routing signal probe";
    }
    juce::String getPluginType() override
    {
        return "vibera_routing_signal_probe";
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool isSynth() override
    {
        return source != 0;
    }
    bool takesAudioInput() override
    {
        return source == 0;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return source != 0;
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    void initialise(const tracktion::PluginInitialisationInfo &) override {}
    void deinitialise() override {}
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    static float signal(int kind, juce::int64 frame) noexcept
    {
        return kind == 1 ? .125f + float(frame % 251 - 125) / 8192.f
                         : -.0625f + float(frame % 257 - 128) / 16384.f;
    }
    void applyToBuffer(const tracktion::PluginRenderContext &context) override
    {
        ++calls;
        if (!context.destBuffer)
            return;
        const auto first = juce::int64(std::llround(context.editTime.getStart().inSeconds() * sampleRate));
        for (int c = 0; c < context.destBuffer->getNumChannels(); ++c)
            for (int i = 0; i < context.bufferNumSamples; ++i)
            {
                auto &sample = context.destBuffer->getWritePointer(c, context.bufferStartSample)[i];
                if (source)
                    sample = signal(source, first + i);
                else
                    sample *= gain;
            }
    }
};

__declspec(noinline) inline void graphOutputRoutingPcmTests(const juce::File &folder)
{
    using R = tracktion::GraphOutputRouting;
    using T = R::Target;
    struct Storage
    {
        juce::String previous =
            juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
        explicit Storage(const juce::File &path)
        {
            require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                               path.getFullPathName().toWideCharPointer()) == 0,
                    "Use disposable routing PCM preferences");
        }
        ~Storage()
        {
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", previous.toWideCharPointer());
        }
    } storage(folder.getChildFile("pcm-storage"));
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("pcm-guide.json"));
    tracktion::Edit::Options options{audio->engine, tracktion::createEmptyEdit(audio->engine), {}};
    options.numAudioTracks = 4;
    options.role = tracktion::Edit::forRendering;
    auto edit = tracktion::Edit::createEdit(options);
    require(bool(edit), "Actual four-track render Edit for independent routing PCM");
    const auto tracks = tracktion::getAudioTracks(*edit);
    require(tracks.size() == 4, "Exact source A/B and gain bus fixture");
    for (auto *track : tracks)
        track->getOutput().setOutputToDefaultDevice(false);
    std::array<tracktion::Plugin::Ptr, 4> probes;
    for (size_t i = 0; i < probes.size(); ++i)
    {
        juce::ValueTree state(tracktion::IDs::PLUGIN);
        state.setProperty(tracktion::IDs::type, "vibera_routing_signal_probe", nullptr);
        tracktion::EditItemID::readOrCreateNewID(*edit, state);
        probes[i] =
            new RoutingSignalProbe({*edit, state, false}, i < 2 ? int(i) + 1 : 0, i == 2 ? .25f : .5f);
        // Native source admission examines the real disposable list before the
        // optional resolver. A generator must be present there to form a root.
        tracks[int(i)]->pluginList.insertPlugin(probes[i], 0, nullptr);
    }
    const auto before = edit->state.toXmlString();
    juce::String report = "Actual native render graph: two original prime251/257 signals and distinct .25/.5 "
                          "buses. No worker/MIDI/live timing qualification.\n";
    const auto receipt = folder.getChildFile("output-routing-pcm.txt");
    require(!receipt.exists(), "Routing PCM evidence is fresh");
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        std::vector<R::Route> routes{
            {tracks[0]->itemID, T::destinationTrack, tracks[scenario == 1 ? 3 : 2]->itemID},
            {tracks[1]->itemID, T::destinationTrack, tracks[scenario == 1 ? 2 : 3]->itemID},
            {tracks[2]->itemID, T::defaultWaveDevice, {}},
            {tracks[3]->itemID, scenario == 2 ? T::disconnected : T::defaultWaveDevice, {}}};
        auto map = R::prepare(*edit, routes);
        tracktion::graph::PlayHead head;
        tracktion::graph::PlayHeadState headState(head);
        tracktion::ProcessState processState(headState, edit->tempoSequence);
        head.playSyncedToRange({});
        tracktion::CreateNodeParams params{processState};
        params.sampleRate = 48000;
        params.blockSize = 128;
        params.forRendering = true;
        params.includeMasterPlugins = false;
        params.outputRouting = map;
        params.pluginListResolver = [tracks, probes](const tracktion::PluginList &list)
        {
            tracktion::Plugin::Array result;
            for (int i = 0; i < tracks.size(); ++i)
                if (&list == &tracks[i]->pluginList)
                    result.add(probes[size_t(i)]);
            return result;
        };
        ready_graph_acceptance::Counts poolCounts;
        ready_graph_acceptance::Player player(
            [&](auto &p) { return std::make_unique<ready_graph_acceptance::CountingPool>(p, poolCounts); });
        player.setNumThreads(0);
        player.enablePooledMemoryAllocations(false);
        auto node = tracktion::createNodeForEdit(*edit, params);
        require(bool(node), "Actual native factory accepts complete graph-local output map");
        player.setNode(std::move(node), 48000, 128);
        choc::buffer::ChannelArrayBuffer<float> output(choc::buffer::Size::create(2, 128));
        tracktion::MidiMessageArray midi;
        double maximumError = 0;
        for (int block = 0; block < 8; ++block)
        {
            const juce::Range<juce::int64> range{block * 128, (block + 1) * 128};
            processState.update(48000, range, tracktion::ProcessState::UpdateContinuityFlags::yes);
            output.clear();
            midi.clear();
            player.process({128, range, {output.getView(), midi}});
            for (unsigned c = 0; c < 2; ++c)
                for (unsigned i = 0; i < 128; ++i)
                {
                    const auto frame = range.getStart() + i;
                    const auto a = RoutingSignalProbe::signal(1, frame),
                               b = RoutingSignalProbe::signal(2, frame);
                    const float expected = scenario == 1   ? .5f * a + .25f * b
                                           : scenario == 2 ? .25f * a
                                                           : .25f * a + .5f * b;
                    const auto observed = output.getView().getSample(c, i);
                    if (!std::isfinite(observed))
                        maximumError = std::numeric_limits<double>::infinity();
                    else
                        maximumError = std::max(maximumError, std::abs(double(observed) - expected));
                }
        }
        player.clearNode();
        report += "scenario=" + juce::String(scenario) + ";maxError=" + juce::String(maximumError, 12) + "\n";
        require(receipt.replaceWithText(report), "Retain all native routing PCM including failures");
        require(maximumError == 0, "All exact original native routing samples match independent musical sum");
        for (const auto &probe : probes)
            require(
                dynamic_cast<RoutingSignalProbe *>(probe.get())->calls == unsigned(8 * (scenario + 1)),
                "Each original source and gain bus processes once, including disconnected processing bus");
        require(edit->state.toXmlString() == before,
                "Native graph-local render routing never rewrites actual TrackOutput or Edit state");
    }
}
// Original actual native Edit admission checks. No device, DSP or user project.
__declspec(noinline) inline void graphOutputRoutingTests(const juce::File &folder)
{
    using R = tracktion::GraphOutputRouting;
    using T = R::Target;
    using S = R::Status;
    const auto receipt = folder.getChildFile("output-routing.txt");
    require(!receipt.exists(), "Routing evidence must be fresh");
    juce::String report =
        "Actual native Edit and immutable map validation; not routed PCM or device qualification.\n";
    auto check = [&](bool condition, const char *name)
    {
        report += juce::String(name) + "=" + juce::String(int(condition)) + "\n";
        require(receipt.replaceWithText(report), "Retain routing result before assertion");
        require(condition, name);
    };
    struct Storage
    {
        juce::String previous =
            juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
        explicit Storage(const juce::File &path)
        {
            require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                               path.getFullPathName().toWideCharPointer()) == 0,
                    "Use disposable engine preferences");
        }
        ~Storage()
        {
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", previous.toWideCharPointer());
        }
    } storage(folder.getChildFile("storage"));
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    tracktion::Edit::Options options{audio->engine, tracktion::createEmptyEdit(audio->engine), {}};
    options.role = tracktion::Edit::forEditing;
    options.numAudioTracks = 3;
    auto edit = tracktion::Edit::createEdit(options);
    check(bool(edit), "actual_disposable_edit_created");
    const auto tracks = tracktion::getAudioTracks(*edit);
    check(tracks.size() == 3, "exact_three_track_fixture");
    for (auto *track : tracks)
        track->getOutput().setOutputToDefaultDevice(false);
    const std::vector<R::Route> routes{{tracks[0]->itemID, T::destinationTrack, tracks[1]->itemID},
                                       {tracks[1]->itemID, T::defaultWaveDevice, {}},
                                       {tracks[2]->itemID, T::disconnected, {}}};
    const auto before = edit->state.toXmlString();
    const auto cache = edit->getPluginCache().getPlugins();
    auto map = R::prepare(*edit, routes);
    map->validateForBuild(*edit, false);
    check(map->resolve(*tracks[0]).destination == tracks[1] &&
              map->resolve(*tracks[0]).target == T::destinationTrack &&
              map->resolve(*tracks[1]).target == T::defaultWaveDevice &&
              map->resolve(*tracks[2]).target == T::disconnected && map->getRoutes().size() == 3,
          "complete_distinct_targets_resolve_actual_native_identity");
    auto refusal = [&](auto operation, S expected, const char *name)
    {
        bool refused = false;
        try
        {
            operation();
        }
        catch (const R::Error &error)
        {
            refused = error.status == expected && juce::String(error.what()).startsWith("outputRouting.");
        }
        check(refused, name);
    };
    auto malformed = [&](auto mutation, S expected, const char *name)
    {
        auto candidate = routes;
        mutation(candidate);
        refusal([&] { R::prepare(*edit, candidate); }, expected, name);
    };
    malformed([](auto &r) { r[0].source = {}; }, S::invalidSource, "invalid_source_refused");
    malformed([](auto &r) { r.push_back(r.front()); }, S::duplicateSource, "duplicate_source_refused");
    malformed([](auto &r) { r.pop_back(); }, S::incompleteMap, "missing_explicit_track_refused");
    malformed([](auto &r) { r[0].destination = r[0].source; }, S::invalidDestination, "self_route_refused");
    malformed([](auto &r) { r[0].destination = {}; }, S::invalidDestination, "missing_destination_refused");
    malformed([&](auto &r) { r[1].destination = tracks[2]->itemID; }, S::invalidDestination,
              "default_target_cannot_smuggle_destination");
    malformed([&](auto &r) { r[2].destination = tracks[1]->itemID; }, S::invalidDestination,
              "disconnected_target_cannot_smuggle_destination");
    malformed([](auto &r) { r[0].target = static_cast<T>(99); }, S::invalidTarget, "unknown_target_refused");
    malformed([&](auto &r) { r[1] = {tracks[1]->itemID, T::destinationTrack, tracks[0]->itemID}; }, S::cycle,
              "actual_two_track_cycle_refused_before_nodes");
    malformed(
        [&](auto &r)
        {
            r[1] = {tracks[1]->itemID, T::destinationTrack, tracks[2]->itemID};
            r[2] = {tracks[2]->itemID, T::destinationTrack, tracks[0]->itemID};
        },
        S::cycle, "actual_three_track_cycle_refused_before_nodes");
    // Options.editState is a shared JUCE ValueTree, not a copy-on-create source.
    tracktion::Edit::Options foreignOptions{audio->engine, tracktion::createEmptyEdit(audio->engine), {}};
    foreignOptions.role = tracktion::Edit::forEditing;
    foreignOptions.numAudioTracks = 3;
    auto foreign = tracktion::Edit::createEdit(foreignOptions);
    check(bool(foreign), "independently_owned_foreign_edit_created");
    refusal([&] { map->validateForBuild(*foreign, false); }, S::foreignEdit, "foreign_edit_refused");
    const auto foreignTrack = tracktion::getAudioTracks(*foreign)[0];
    refusal([&] { map->resolve(*foreignTrack); }, S::foreignEdit, "foreign_track_resolution_refused");
    malformed([&](auto &r) { r[0].destination = foreignTrack->itemID; }, S::invalidDestination,
              "foreign_destination_refused");
    check(edit->state.toXmlString() == before && edit->getPluginCache().getPlugins() == cache &&
              !tracks[0]->getOutput().getDestinationTrack(),
          "preparation_and_all_refusals_leave_active_edit_cache_output_unchanged");
    check(audio->engine.getDeviceManager().getDefaultWaveOutDevice() == nullptr,
          "fixture_has_no_physical_default_output");
    refusal([&] { map->validateForBuild(*edit, true); }, S::changedDefaultDevice,
            "live_build_requires_captured_available_device");
    tracks[0]->getOutput().setOutputToTrack(tracks[2]);
    refusal([&] { map->validateForBuild(*edit, false); }, S::changedNativeOutput,
            "changed_native_output_invalidates_ticket");
    tracks[0]->getOutput().setOutputToDefaultDevice(false);
    map->validateForBuild(*edit, false);
    check(map->resolve(*tracks[0]).destination == tracks[1], "restored_native_output_keeps_private_route");
    const auto first = edit->state.indexOf(tracks[0]->state);
    const auto second = edit->state.indexOf(tracks[1]->state);
    check(first >= 0 && second >= 0, "actual_track_nodes_are_top_level");
    edit->state.moveChild(first, second, nullptr);
    refusal([&] { map->validateForBuild(*edit, false); }, S::changedUniverse,
            "native_track_reorder_invalidates_fixed_universe");
}
} // namespace vibera::test
