// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ReadyGraphAcceptance.h"
#include "../src/AudioEngine.h"
#include <tracktion_engine/playback/graph/tracktion_PluginNode.h>
#include <array>
#include <cstdint>
#include <exception>
#include <vector>

// Include after require and the existing callback allocation counters/probe.
// Native PluginNode context semantics only: no Host, worker, device or hardware.
namespace source_render_time_acceptance
{
inline float signal(int channel, std::int64_t frame) noexcept
{
    return channel == 0 ? .125f + (frame % 13 == 5 ? .25f : 0.f)
                        : -.0625f - (frame % 11 == 3 ? .125f : 0.f);
}
struct Settings
{
    const juce::String prior = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
    explicit Settings(const juce::File& folder)
    {
        require(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", folder.getFullPathName().toWideCharPointer()) == 0,
                "Own source-time fixture storage before AudioEngine construction");
    }
    ~Settings() { _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", prior.toWideCharPointer()); }
};
// Actual original37-frame audio delay, with bounded original MIDI scheduling.
// Does not use the legacy allocating native LatencyProcessor MIDI container.
class Input final : public tracktion::graph::Node
{
    const int latency;
    std::array<std::array<float, 37>, 2> ring{};
    unsigned cursor = 0;
    std::int64_t written = 0;
    const juce::MidiMessage on = juce::MidiMessage::noteOn(3, 64, juce::uint8(100));
    const juce::MidiMessage off = juce::MidiMessage::noteOff(3, 64);
public:
    unsigned calls = 0;
    explicit Input(int delay) : latency(delay) {}
    tracktion::graph::NodeProperties getNodeProperties() override
    { return {true, true, 2, latency, 0x53524354494d4549ULL}; }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo&) override
    { reserveMidiOutputForPreparation(2); }
    bool isReadyToProcess() override { return true; }
    void process(ProcessContext& pc) override
    {
        ++calls;
        for(choc::buffer::FrameCount n = 0; n < pc.numSamples; ++n)
        {
            for(int c = 0; c < 2; ++c)
            {
                const auto value = signal(c, written + std::int64_t(n));
                float delayed = value;
                if(latency)
                {
                    delayed = ring[std::size_t(c)][cursor];
                    ring[std::size_t(c)][cursor] = value;
                }
                pc.buffers.audio.getSample(choc::buffer::ChannelCount(c), n) = delayed;
            }
            if(latency) cursor = (cursor + 1) % unsigned(latency);
        }
        pc.buffers.midi.addMidiMessage(on, double(latency + 1) / 48000., 77);
        pc.buffers.midi.addMidiMessage(off, double(latency + 17) / 48000., 77);
        written += std::int64_t(pc.numSamples);
    }
};
class ProbeBase : public tracktion::Plugin
{
    const bool source;
    int nativeLatency = 31;
    std::array<std::array<float, 31>, 2> ring{};
    unsigned cursor = 0;
    std::int64_t written = 0;
public:
    struct Seen { double start = 0, end = 0; int frames = 0; };
    std::array<Seen, 3> seen{};
    unsigned calls = 0;
    bool receiptOverflow = false;
    ProbeBase(tracktion::PluginCreationInfo info, bool ownSource) : Plugin(info), source(ownSource) {}
    ~ProbeBase() override { notifyListenersOfDeletion(); }
    void configureLatencyControl(int value) { nativeLatency = value; }
    juce::String getSelectableDescription() override { return getName(); }
    bool isSynth() override { return source; }
    bool takesAudioInput() override { return !source; }
    bool takesMidiInput() override { return true; }
    bool producesAudioWhenNoAudioInput() override { return source; }
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    BusLayout getBusses() const override { return BusLayout::singleStereoInOut(); }
    double getLatencySeconds() override { return double(nativeLatency) / 48000.; }
    void initialise(const tracktion::PluginInitialisationInfo& info) override
    {
        if(info.sampleRate != 48000 || (nativeLatency != 0 && nativeLatency != 31))
            throw std::invalid_argument("Original source-time fixture configuration");
        ring = {}; cursor = 0; written = 0;
    }
    void deinitialise() override {}
    void restorePluginStateFromValueTree(const juce::ValueTree&) override {}
    void applyToBuffer(const tracktion::PluginRenderContext& fc) override
    {
        if(calls < seen.size()) seen[calls] = {fc.editTime.getStart().inSeconds(), fc.editTime.getEnd().inSeconds(), fc.bufferNumSamples};
        else receiptOverflow = true;
        ++calls;
        if(!fc.destBuffer) return;
        for(int n = 0; n < fc.bufferNumSamples; ++n)
        {
            for(int c = 0; c < 2; ++c)
            {
                const auto value = source ? signal(c, written + n)
                                          : fc.destBuffer->getSample(c, fc.bufferStartSample + n);
                float delayed = value;
                if(nativeLatency)
                {
                    delayed = ring[std::size_t(c)][cursor];
                    ring[std::size_t(c)][cursor] = value;
                }
                fc.destBuffer->setSample(c, fc.bufferStartSample + n, delayed * .5f);
            }
            if(nativeLatency) cursor = (cursor + 1) % unsigned(nativeLatency);
        }
        written += fc.bufferNumSamples;
        // Preserve the real incoming MIDI array/timestamps/source unchanged.
    }
};
class DefaultEffect final : public ProbeBase
{
public:
    static constexpr const char* xmlTypeName = "vibera_default_source_time_fixture";
    static const char* getPluginName() { return "Original default render-time fixture"; }
    explicit DefaultEffect(tracktion::PluginCreationInfo info) : ProbeBase(info, false) {}
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    // Deliberately inherits Plugin's FALSE capability, no override.
};
class SourceProbe final : public ProbeBase
{
public:
    static constexpr const char* xmlTypeName = "vibera_own_source_time_fixture";
    static const char* getPluginName() { return "Original own-source render-time fixture"; }
    mutable unsigned queries = 0, queriesDuringProbe = 0;
    explicit SourceProbe(tracktion::PluginCreationInfo info) : ProbeBase(info, true) {}
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    bool usesUnadjustedSourceRenderTime() const noexcept override
    {
        ++queries;
        if(callbackAllocationProbe) ++queriesDuringProbe;
        return true;
    }
};
inline void scenario(const juce::File& folder, bool source, bool zeroLatency)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh source-time native case");
    Settings settings(folder.getChildFile("settings"));
    auto audio = std::make_unique<vibera::AudioEngine>(false, folder.getChildFile("guide.json"));
    audio->engine.getPluginManager().createBuiltInType<DefaultEffect>();
    audio->engine.getPluginManager().createBuiltInType<SourceProbe>();
    tracktion::Edit::Options options{audio->engine, tracktion::createEmptyEdit(audio->engine), {}};
    options.numAudioTracks = 1; options.role = tracktion::Edit::forRendering;
    options.closedNativeIO = true; options.initialSourceGeneration = 3201;
    auto edit = tracktion::Edit::createEdit(options);
    require(bool(edit), "Actual owned closed Edit");
    auto plugin = edit->getPluginCache().createNewPlugin(source ? SourceProbe::xmlTypeName : DefaultEffect::xmlTypeName, {});
    auto* probe = dynamic_cast<ProbeBase*>(plugin.get());
    auto* sourceProbe = dynamic_cast<SourceProbe*>(plugin.get());
    require(probe && bool(sourceProbe) == source, "Actual registered PluginCache subtype");
    auto* track = tracktion::getAudioTracks(*edit)[0];
    track->pluginList.clear();
    track->pluginList.insertPlugin(plugin, 0, nullptr);
    require(track->pluginList.size() == 1 && track->pluginList[0] == plugin.get(),
            "Actual Edit PluginCache and PluginList retain the tested original owner");
    const int upstream = zeroLatency ? 0 : 37, own = zeroLatency ? 0 : 31;
    probe->configureLatencyControl(own);
    if(!source) require(!plugin->usesUnadjustedSourceRenderTime(), "Ordinary plugin inherits default false");
    else require(sourceProbe->queries == 0, "No source capability query before native preparation");
    tracktion::graph::PlayHead head;
    tracktion::graph::PlayHeadState headState(head);
    tracktion::ProcessState process(headState, edit->tempoSequence);
    head.playSyncedToRange({});
    ready_graph_acceptance::Counts pools;
    ready_graph_acceptance::Player player([&](auto& owner) { return std::make_unique<ready_graph_acceptance::CountingPool>(owner, pools); });
    player.setNumThreads(0); player.enableNodeMemorySharing(false); player.enablePooledMemoryAllocations(false);
    auto input = std::make_unique<Input>(upstream); auto* actualInput = input.get();
    auto root = std::make_unique<tracktion::PluginNode>(std::move(input), plugin, 48000., 128,
        nullptr, process, true, false, 2, false, 16);
    player.setNode(std::move(root), 48000., 128);
    require(!source || (sourceProbe->queries == 1 && sourceProbe->queriesDuringProbe == 0),
            "Exactly one source capability query at CONTROL preparation");
    choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2,128));
    tracktion::MidiMessageArray midi; midi.reserve(16);
    const std::array<int,3> spans{64,127,65};
    std::int64_t written = 0;
    juce::String rows = "callback,start,frames,observedStart,expectedStart,observedEnd,expectedEnd,events,inputCalls,pluginCalls,queries,new,delete,seconds\n";
    juce::String events = "callback,kind,key,channel,source,relativeSeconds,expectedSeconds\n";
    std::vector<float> captured, expectedPcm;
    const auto retain = [&]
    {
        require(folder.getChildFile("contexts.csv").replaceWithText(rows) && folder.getChildFile("events.csv").replaceWithText(events), "Retain native context/MIDI evidence before assertions");
        for(int which = 0; which < 2; ++which)
        {
            auto stream = folder.getChildFile(which ? "expected-f32.raw" : "actual-f32.raw").createOutputStream();
            require(stream && stream->setPosition(0) && stream->truncate().wasOk(), "Fresh full context PCM");
            for(float value : which ? expectedPcm : captured) require(stream->write(&value,sizeof(value)), "Retain every context PCM sample");
            stream->flush(); require(stream->getStatus().wasOk(), "Flush context PCM");
        }
    };
    for(std::size_t callback = 0; callback < spans.size(); ++callback)
    {
        const int frames = spans[callback];
        const juce::Range<juce::int64> range{96000 + written, 96000 + written + frames};
        // CONTROL fixture setup, outside the measured Player callback. Literal
        // mapping must hold; never infer an origin from the observed clock.
        process.update(48000., range, tracktion::ProcessState::UpdateContinuityFlags::yes);
        require(process.editTimeRange.getStart().inSeconds() == double(96000 + written)/48000., "Literal two-second musical origin");
        const bool expectedPanic = headState.didPlayheadJump();
        pcm.clear(); midi.clear();
        const auto allocations = callbackAllocations.load(), deletions = callbackDestructions.load();
        const auto started = juce::Time::getHighResolutionTicks();
        {
            struct Probe {const bool before=callbackAllocationProbe; Probe(){callbackAllocationProbe=true;} ~Probe(){callbackAllocationProbe=before;}} allocationScope;
            player.process({choc::buffer::FrameCount(frames), range, {pcm.getView().getStart(choc::buffer::FrameCount(frames)), midi}});
        }
        const auto seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-started);
        bool exact = true;
        const int pcmDelay = source ? own : upstream + own;
        for(int c = 0; c < 2; ++c) for(int n = 0; n < frames; ++n)
        {
            const auto frame = written + n - pcmDelay;
            const float expected = frame < 0 ? 0.f : signal(c,frame)*.5f;
            const auto actual = pcm.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n));
            captured.push_back(actual); expectedPcm.push_back(expected); exact &= actual == expected;
        }
        unsigned mask = 0;
        for(const auto& message : midi)
        {
            const int kind = message.isNoteOn() ? 0 : message.isNoteOff() ? 1 : -1;
            const double expected = double(upstream + (kind == 0 ? 1 : 17))/48000.;
            events += juce::String(int(callback))+","+juce::String(kind)+","+juce::String(message.getNoteNumber())+","+juce::String(message.getChannel())+","+juce::String(message.mpeSourceID)+","+juce::String(message.getTimeStamp(),17)+","+juce::String(expected,17)+"\n";
            exact &= kind >= 0 && message.getNoteNumber() == 64 && message.getChannel() == 3 && message.mpeSourceID == 77 && message.getTimeStamp() == expected;
            if(kind >= 0) {const auto bit = 1u << kind; exact &= (mask & bit) == 0; mask |= bit;}
        }
        const double expectedStart = double(96000 + written)/48000. - (source ? 0. : double(upstream+own)/48000.);
        const double expectedEnd = double(96000 + written + frames)/48000. - (source ? 0. : double(upstream+own)/48000.);
        const auto observed = probe->seen[callback];
        const unsigned queries = source ? sourceProbe->queries : 0;
        rows += juce::String(int(callback))+","+juce::String(range.getStart())+","+juce::String(frames)+","+juce::String(observed.start,17)+","+juce::String(expectedStart,17)+","+juce::String(observed.end,17)+","+juce::String(expectedEnd,17)+","+juce::String(midi.size())+","+juce::String(actualInput->calls)+","+juce::String(probe->calls)+","+juce::String(queries)+","+juce::String(callbackAllocations.load()-allocations)+","+juce::String(callbackDestructions.load()-deletions)+","+juce::String(seconds,12)+"\n";
        retain();
        require(exact && mask == 3 && midi.size() == 2 && midi.isAllNotesOff == expectedPanic &&
            actualInput->calls == callback+1 && probe->calls == callback+1 && !probe->receiptOverflow &&
            observed.start == expectedStart && observed.end == expectedEnd && observed.frames == frames &&
            (!source || (queries == 1 && sourceProbe->queriesDuringProbe == 0)) &&
            callbackAllocations.load() == allocations && callbackDestructions.load() == deletions && seconds <= double(frames)/48000.,
            "Literal native source/default render context, cumulative latency, input MIDI, every PCM, source once and callback safety");
        written += frames;
    }
}
inline void sourceRenderTimeTests(const juce::File& folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh source render-time suite");
    juce::String outcomes, failures;
    for(int mode = 0; mode < 4; ++mode)
    {
        const auto name = juce::String(mode < 2 ? "latency68_" : "latency0_") + (mode%2 ? "source" : "default");
        try {scenario(folder.getChildFile(name), mode%2 != 0, mode >= 2); outcomes += name+"=completed\n";}
        catch(const std::exception& error) {outcomes += name+"=FAILED: "+juce::String(error.what())+"\n"; failures += name+"\n";}
    }
    require(folder.getChildFile("outcomes.txt").replaceWithText(outcomes), "Retain all four independent native cases");
    require(failures.isEmpty(), "All original source/default native render-context cases pass");
}
} // namespace source_render_time_acceptance
