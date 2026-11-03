// SPDX-License-Identifier: AGPL-3.0-or-later
// Unbuilt internal native-clock experiment. No canonical send automation target.
// Include after AudioEngine/require/probes and current Tracktion module headers.
#pragma once
#include "SendAutomationBlockProbe.h"
#include <tracktion_graph/tracktion_graph/tracktion_TestUtilities.h>
#include <tracktion_graph/tracktion_graph/tracktion_TestNodes.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionNodePlayer.h>
#include <tracktion_engine/playback/graph/tracktion_AuxSendNode.h>
#include <array>

namespace vibera::test::send_automation_native_experiment
{
using Node = tracktion::graph::Node;
using Bank = tracktion::graph::SendGainState;
struct Counts
{
    unsigned source = 0, scalar = 0, block = 0, clocks = 0;
};
class Signal final : public Node
{
    Counts &counts;

  public:
    explicit Signal(Counts &value) : counts(value) {}
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return {true, false, 2, 0, 0x76696273656e6443ULL};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &pc) override
    {
        ++counts.source;
        auto left = int(pc.referenceSampleRange.getStart() % 251);
        auto right = int(pc.referenceSampleRange.getStart() % 127);
        for (unsigned frame = 0; frame < pc.buffers.audio.getNumFrames(); ++frame)
        {
            pc.buffers.audio.getSample(0, frame) = float(left + 1) / 512.f;
            pc.buffers.audio.getSample(1, frame) = -float(right + 1) / 1024.f;
            if (++left == 251)
                left = 0;
            if (++right == 127)
                right = 0;
        }
    }
};
class OriginalSend final : public tracktion::AuxSendPlugin
{
    experimental::SendAutomationBlockProbe evaluator;
    const std::shared_ptr<Bank> bank;
    Counts &counts;

  public:
    mutable std::array<tracktion::graph::SendGainBlockContext, 128> clocks{};
    OriginalSend(tracktion::PluginCreationInfo info, std::shared_ptr<LiveTimeline> scope, double rate,
                 Counts &value)
        : AuxSendPlugin(info),
          evaluator(std::move(scope), 1, 2, {AutomationParameter::mixerGain, 1, 0}, rate),
          bank(std::make_shared<Bank>(Bank::EdgeIdentity{1, 2, 42}, .005)), counts(value)
    {
        busNumber = 42;
    }
    float getRealtimeSendGain() const noexcept override
    {
        ++counts.scalar;
        return .25f;
    }
    bool hasRealtimeSendGainBlock() const noexcept override
    {
        return true;
    }
    tracktion::graph::SendGainBlockResult
    getRealtimeSendGainBlock(const tracktion::graph::SendGainBlockContext &clock,
                             std::span<float> output) const noexcept override
    {
        ++counts.block;
        if (counts.clocks == clocks.size())
            return {};
        clocks[counts.clocks++] = clock; // Pre-owned bounded plain records, never callback logging.
        return evaluator.fill(clock, output);
    }
    std::shared_ptr<Bank> prepareSendGainStateForGraph() const override
    {
        return bank;
    }
};
inline void raw(const juce::File &file, const juce::AudioBuffer<float> &pcm, int frames)
{
    auto stream = file.createOutputStream();
    require(bool(stream) && stream->setPosition(0) && stream->truncate().wasOk(),
            "Fresh owned send PCM evidence");
    for (int channel = 0; channel < 2; ++channel)
        require(stream->write(pcm.getReadPointer(channel), size_t(frames) * sizeof(float)),
                "Every actual/expected send PCM sample");
    stream->flush();
    require(stream->getStatus().wasOk(), "Complete send PCM evidence flush");
}
__declspec(noinline) inline bool scenario(AudioEngine &engine, const juce::File &folder, bool pattern,
                                          bool rendering, bool uneven, int latency)
{
    constexpr int rate = 48000, maximum = 256, total = 1152, loop = 384, ramp = 240;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned send clock scenario");
    auto project = Project::empty();
    project.tempo = 120;
    project.mixer[1].routes = {{2, .25f}};
    const AutomationKey key{AutomationParameter::mixerGain, 1, 0};
    AutomationSource source;
    source.id = project.allocate();
    source.length = 256. / 24000.;
    source.points = {{project.allocate(), 0, 0}, {project.allocate(), source.length, 1}};
    source.links = {{key, 0, 2}};
    project.automation.initialValues.push_back({key, 0});
    project.automation.placements.push_back({project.allocate(), source.id, 0, source.length, 0, 0, false});
    project.automation.sources.push_back(std::move(source));
    require(project.validate().wasOk(),
            "Legal existing gain-key clock probe, not unsupported send-kind masquerading");
    auto document = compileLiveDocument(project, pattern, project.patterns.front().id, 1);
    auto timeline = std::make_shared<LiveTimeline>();
    require(document && timeline->publish(document), "One actual immutable compiled automation document");
    tracktion::Edit::Options options{engine.engine, tracktion::createEmptyEdit(engine.engine), {}};
    options.role = tracktion::Edit::forRendering;
    options.numAudioTracks = 0;
    auto edit = tracktion::Edit::createEdit(options);
    Counts counts;
    // Original private fixture type uses the normal lifecycle. The production
    // detached-mode allowlist deliberately accepts only registered app types.
    tracktion::Plugin::Ptr owner = new OriginalSend(
        {*edit, tracktion::AuxSendPlugin::create(), true}, timeline, rate, counts);
    auto *plugin = dynamic_cast<OriginalSend *>(owner.get());
    tracktion::graph::PlayHead head;
    tracktion::graph::PlayHeadState headState(head);
    tracktion::ProcessState state(headState);
    tracktion::TracktionNodePlayer player(state);
    player.setNumThreads(0);
    player.enablePooledMemoryAllocations(false);
    const auto makeGraph = [&]() -> std::unique_ptr<Node>
    {
        std::unique_ptr<Node> input = std::make_unique<Signal>(counts);
        if (latency)
            input = std::make_unique<tracktion::graph::LatencyNode>(std::move(input), latency);
        auto send = std::make_unique<tracktion::AuxSendNode>(
            std::move(input), 42, tracktion::SampleRateAndBlockSize{rate, maximum}, *plugin, headState,
            nullptr, false, !plugin->baseClassNeedsInitialising(), &state, rendering);
        std::vector<std::unique_ptr<Node>> roots;
        roots.push_back(std::make_unique<tracktion::graph::SinkNode>(std::move(send)));
        roots.push_back(std::make_unique<tracktion::graph::ReturnNode>(42));
        return std::make_unique<tracktion::graph::SummingNode>(std::move(roots));
    };
    player.setNode(makeGraph(), rate, maximum);
    if (rendering)
        head.playSyncedToRange({0, total});
    else
        head.play({0, loop}, true);
    auto bank = plugin->prepareSendGainStateForGraph();
    juce::AudioBuffer<float> actual(2, total + 64), expected(2, total + 64);
    actual.clear();
    expected.clear();
    choc::buffer::ChannelArrayBuffer<float> output(choc::buffer::Size::create(2, maximum));
    tracktion::MidiMessageArray midi;
    midi.reserve(128);
    bool clean = counts.scalar == 0, clockOk = true, discarded = false;
    double error = 0, maximumMs = 0;
    unsigned overruns = 0, callbacks = 0;
    int done = 0;
    const auto drive = [&](int frames, bool stopped)
    {
        output.clear();
        midi.clear();
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const auto sourceBefore = counts.source, blocksBefore = counts.block, clocksBefore = counts.clocks;
        const auto began = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            player.process({unsigned(frames),
                            {done, done + frames},
                            {output.getView().getFrameRange({0, unsigned(frames)}), midi}});
        }
        const auto elapsed =
            SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        maximumMs = std::max(maximumMs, elapsed);
        overruns += elapsed > 1000. * frames / rate;
        const bool split = !rendering && !stopped && done / loop != (done + frames - 1) / loop;
        clean &= callbackAllocations.load() == allocations && callbackDestructions.load() == destructions &&
                 counts.source == sourceBefore + (split ? 2u : 1u) &&
                 counts.block == blocksBefore + (split ? 2u : 1u) && counts.scalar == 0;
        for (unsigned index = clocksBefore; index < counts.clocks; ++index)
        {
            const auto &clock = plugin->clocks[index];
            const auto music =
                rendering ? clock.referenceRange.getStart() : clock.referenceRange.getStart() % loop;
            clockOk &= clock.referenceRange.getLength() == clock.frames && clock.sampleRate == rate &&
                       clock.rendering == rendering;
            if (!stopped)
                clockOk &= clock.playing &&
                           std::abs(clock.editStartSeconds - double(music - latency) / rate) < 1.e-12 &&
                           std::abs(clock.secondsPerFrame - 1. / rate) < 1.e-15;
            else
                clockOk &= !clock.playing && clock.secondsPerFrame == 0;
        }
        for (int frame = 0; frame < frames; ++frame)
        {
            const int absolute = done + frame, input = absolute - latency;
            const int musical = rendering ? absolute : absolute % loop;
            // Independent absolute-clock formula. No reads of provider/block
            // context, lane evaluator, GainState or captured PCM to fit phase.
            const double curve = std::clamp(musical - latency, 0, 256) / 128.;
            const double manual = .25 * std::min(absolute + 1, ramp) / ramp;
            const double gain = pattern || stopped ? manual : curve;
            const double left = input < 0 ? 0 : double(input % 251 + 1) / 512.;
            const double right = input < 0 ? 0 : -double(input % 127 + 1) / 1024.;
            for (int channel = 0; channel < 2; ++channel)
            {
                const double wanted = (channel == 0 ? left : right) * gain;
                const auto value = output.getView().getSample(unsigned(channel), unsigned(frame));
                actual.setSample(channel, absolute, value);
                expected.setSample(channel, absolute, float(wanted));
                error = std::isfinite(value) ? std::max(error, std::abs(double(value) - wanted))
                                             : std::numeric_limits<double>::infinity();
            }
        }
        done += frames;
        ++callbacks;
    };
    constexpr std::array<int, 4> shapes{64, 1, 127, 64};
    while (done < total)
    {
        drive(std::min(uneven ? shapes[callbacks % shapes.size()] : maximum, total - done), false);
        if (!discarded && done >= 256)
        {
            raw(folder.getChildFile("actual.raw"), actual, done);
            raw(folder.getChildFile("expected.raw"), expected, done);
            const auto calls = counts.block;
            {
                auto candidate = player.prepareFixedGraph(makeGraph());
                require(bank->attachmentsControl() == 2 && counts.block == calls,
                        "Private candidate never invokes provider or copies/advances gain DSP");
            }
            require(bank->attachmentsControl() == 1, "Discard leaves incumbent send runtime attached");
            discarded = true;
        }
    }
    if (!rendering)
    {
        head.stop();
        drive(64, true);
    }
    std::uint64_t faults = 0;
    unsigned gainNodes = 0;
    tracktion::graph::visitNodes(
        *player.getNode(),
        [&](Node &node)
        {
            if (auto *gain = dynamic_cast<tracktion::graph::GainNode *>(&node))
            {
                ++gainNodes;
                faults += gain->getExactGainFaults();
            }
        },
        false);
    raw(folder.getChildFile("actual.raw"), actual, done);
    raw(folder.getChildFile("expected.raw"), expected, done);
    const bool passed =
        clean && clockOk && faults == 0 && gainNodes == 1 && discarded && error <= 2.e-6 && overruns == 0;
    require(folder.getChildFile("result.txt")
                .replaceWithText(
                    "passed=" + juce::String(int(passed)) + " completeFrames=" + juce::String(done) +
                    " maxError=" + juce::String(error, 12) + " maxMs=" + juce::String(maximumMs, 6) +
                    " overruns=" + juce::String(overruns) + " clean=" + juce::String(int(clean)) + " clock=" +
                    juce::String(int(clockOk)) + " faults=" + juce::String(juce::int64(faults)) + "\n"),
            "Retain full native experiment before aggregate assertion");
    player.clearNode();
    owner = nullptr;
    edit.reset(); // Native Plugin/Edit lifetime, CONTROL only.
    return passed;
}
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned send experiment root");
    auto engine = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    bool passed = true;
    for (const bool pattern : {false, true})
        for (const bool render : {false, true})
            for (const bool uneven : {false, true})
                for (const int latency : {0, 31})
                {
                    const auto name = juce::String(pattern ? "pattern" : "song") +
                                      (render ? "_render" : "_live_loop") + (uneven ? "_uneven" : "_fixed") +
                                      "_latency" + juce::String(latency);
                    try
                    {
                        passed =
                            scenario(*engine, folder.getChildFile(name), pattern, render, uneven, latency) &&
                            passed;
                    }
                    catch (const std::exception &error)
                    {
                        passed = false;
                        require(
                            folder.getChildFile(name)
                                .getChildFile("failure.txt")
                                .replaceWithText(error.what()),
                            "Retain independent native send case exception before continuing all16 cases");
                    }
                }
    require(passed, "All16 original actual native send-clock/PCM/manual/partition cases");
}
} // namespace vibera::test::send_automation_native_experiment
