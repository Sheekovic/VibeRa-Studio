// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerRoutePlugins.h"
#include <tracktion_engine/playback/graph/tracktion_AuxSendNode.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>

namespace vibera::test::mixer_send_runtime
{
// Independent linear envelope oracle. No reads of the production smoother.
struct Envelope
{
    float current = 0, target = 0, step = 0;
    int remaining = 0, duration = 0;
    explicit Envelope(int samples) : duration(samples) {}
    void set(float value)
    {
        if (value == target)
            return;
        target = value;
        remaining = duration;
        step = (target - current) / float(duration);
    }
    float next()
    {
        if (remaining > 0)
        {
            --remaining;
            current = remaining == 0 ? target : current + step;
        }
        return current;
    }
};
class Ones final : public tracktion::graph::Node
{
  public:
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return {true, false, 1, 0, 0x76696273656e6431ULL};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &context) override
    {
        for (choc::buffer::FrameCount i = 0; i < context.buffers.audio.getNumFrames(); ++i)
            context.buffers.audio.getSample(0, i) = 1.f;
    }
};

__declspec(noinline) inline void tests(const juce::File &folder)
{
    using Player = ready_graph_acceptance::Player;
    using Node = tracktion::graph::Node;
    using Bank = tracktion::graph::SendGainState;
    require(folder.createDirectory().wasOk(), "Owned application send evidence folder");
    const auto receipt = folder.getChildFile("application-send-runtime.txt");
    require(!receipt.exists(), "Fresh application native send evidence");
    juce::String report = "Actual MixerSendPlugin/AuxSendNode/ReturnNode/selected NodePlayer PCM. "
                          "Not application routing transaction, concurrency, hardware or listening.\n";
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    tracktion::Edit::Options options{audio->engine, tracktion::createEmptyEdit(audio->engine), {}};
    options.role = tracktion::Edit::forRendering;
    options.numAudioTracks = 0;
    auto edit = tracktion::Edit::createEdit(options);
    require(bool(edit), "Disposable real Edit for application send");
    for (const auto rate : {48000., 44100.})
        for (const auto maximum : {64, 256, 512})
        {
            std::uint64_t generation = 0;
            auto scope = std::make_shared<LiveTimeline>();
            auto destination = std::make_shared<LiveTimeline>();
            auto publish = [&](const std::shared_ptr<LiveTimeline> &timeline, float value)
            {
                auto document = std::make_unique<LiveDocument>();
                document->generation = ++generation;
                document->mixerSends.push_back({1, 2, value});
                require(timeline->publish(document), "Actual prepared send target publication");
            };
            publish(scope, .25f);
            publish(destination, .75f);
            auto source = edit->getPluginCache().createNewPlugin(MixerSendPlugin::xmlTypeName, {});
            auto *incumbent = dynamic_cast<MixerSendPlugin *>(source.get());
            require(incumbent != nullptr, "Actual registered send adapter");
            incumbent->configure(scope, 1, 2, 42, .25f);
            tracktion::graph::PlayHead head;
            tracktion::graph::PlayHeadState headState(head);
            // Every retained/private graph uses this actual caller-owned clock.
            tracktion::ProcessState processState(headState);
            auto makeGraph = [&](MixerSendPlugin &plugin)
            {
                auto send = std::make_unique<tracktion::AuxSendNode>(
                    std::make_unique<Ones>(), 42, tracktion::SampleRateAndBlockSize{rate, maximum}, plugin,
                    headState, nullptr, false, plugin.isDetachedGraph(), &processState);
                std::vector<std::unique_ptr<Node>> nodes;
                nodes.push_back(std::make_unique<tracktion::graph::SinkNode>(std::move(send)));
                nodes.push_back(std::make_unique<tracktion::graph::ReturnNode>(42));
                return std::make_unique<tracktion::graph::SummingNode>(std::move(nodes));
            };
            ready_graph_acceptance::Counts counts;
            Player player([&](Player &p)
                          { return std::make_unique<ready_graph_acceptance::CountingPool>(p, counts); });
            player.setNumThreads(0);
            player.enablePooledMemoryAllocations(false);
            player.setNode(makeGraph(*incumbent), rate, maximum);
            const auto identity = incumbent->runtimeIdentity(), epoch = incumbent->runtimeEpochControl();
            require(identity != 0 && incumbent->runtimeAttachmentsControl() == 1,
                    "Actual native GainNode owns prepared bank");
            auto bank = incumbent->prepareSendGainStateForGraph();
            require(bank->rampSecondsControl() == .005, "Application sends use explicit five milliseconds");
            auto candidate = [&]()
            {
                auto value = MixerSendPlugin::createDetachedGraph(*edit, source->state.createCopy());
                auto *plugin = dynamic_cast<MixerSendPlugin *>(value.get());
                plugin->configure(destination, 1, 2, 42, .75f);
                const auto retained = plugin->shareCompatibleRuntimeFrom(*incumbent);
                require(retained.wasOk(), retained.getErrorMessage().toStdString());
                require(plugin->runtimeIdentity() == identity && plugin->runtimeEpochControl() == epoch,
                        "Retaining factory pins exact bank and epoch without copying DSP");
                return value;
            };
            choc::buffer::ChannelArrayBuffer<float> output(choc::buffer::Size::create(1, maximum));
            tracktion::MidiMessageArray midi;
            midi.reserve(128);
            Envelope envelope(std::max(1, int(std::floor(rate * .005))));
            juce::int64 reference = 0;
            double error = 0;
            unsigned blocks = 0;
            auto process = [&](int frames, float target)
            {
                envelope.set(target);
                output.clear();
                midi.clear();
                const auto allocations = callbackAllocations.load(),
                           destructions = callbackDestructions.load();
                const bool previous = callbackAllocationProbe;
                struct RestoreProbe
                {
                    bool previous;
                    ~RestoreProbe()
                    {
                        callbackAllocationProbe = previous;
                    }
                } restore{previous};
                callbackAllocationProbe = true;
                // Literal same-span ProcessState update is included in the
                // existing callback heap probe, before any graph node runs.
                processState.update(rate, {reference, reference + frames},
                                    tracktion::ProcessState::UpdateContinuityFlags::yes);
                player.process({unsigned(frames),
                                {reference, reference + frames},
                                {output.getView().getFrameRange({0, unsigned(frames)}), midi}});
                callbackAllocationProbe = previous;
                reference += frames;
                ++blocks;
                require(callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == destructions,
                        "No observed C++ new/delete in actual selected send processing");
                for (int i = 0; i < frames; ++i)
                {
                    const float expected = envelope.next(),
                                actual = output.getView().getSample(0, unsigned(i));
                    error = std::max(error, std::isfinite(actual) ? std::abs(double(actual) - expected)
                                                                  : std::numeric_limits<double>::infinity());
                }
            };
            process(7, .25f);
            publish(scope, .75f); // Old graph keeps making progress DURING private preparation.
            {
                auto discardedOwner = candidate();
                auto discarded = player.prepareFixedGraph(
                    makeGraph(*dynamic_cast<MixerSendPlugin *>(discardedOwner.get())));
                require(incumbent->runtimeAttachmentsControl() == 2, "Discard candidate adds native owner");
                process(23, .75f);
            }
            require(incumbent->runtimeAttachmentsControl() == 1 && incumbent->runtimeEpochControl() == epoch,
                    "Discard preserves selected ramp and epoch");
            auto replacementOwner = candidate();
            auto *replacement = dynamic_cast<MixerSendPlugin *>(replacementOwner.get());
            auto ready = player.prepareFixedGraph(makeGraph(*replacement));
            auto slot = player.makeGraphRetirementSlot();
            process(11, .75f);
            require(player.publishReadyGraph(ready, slot) == Player::ReadyGraphStatus::ready,
                    "Actual native prepared send graph publication");
            process(17, .75f);
            publish(destination, .125f);
            for (int i = 0; i < 30; ++i)
                process(std::array{7, 23, 36, 64}[size_t(i % 4)], .125f);
            // Posting another graph transfers the selected predecessor into an
            // explicit CONTROL retirement slot; callback selection alone does not.
            auto nextOwner = MixerSendPlugin::createDetachedGraph(*edit, source->state.createCopy());
            auto *next = dynamic_cast<MixerSendPlugin *>(nextOwner.get());
            next->configure(destination, 1, 2, 42, .125f);
            require(next->shareCompatibleRuntimeFrom(*replacement).wasOk(),
                    "Repeated successor derives from initialized actual replacement");
            auto nextGraph = player.prepareFixedGraph(makeGraph(*next));
            require(player.publishReadyGraph(nextGraph, slot) == Player::ReadyGraphStatus::ready &&
                        player.retireReadyGraph(slot),
                    "CONTROL retires predecessor after native transfer");
            process(13, .125f);
            auto staleOwner = MixerSendPlugin::createDetachedGraph(*edit, source->state.createCopy());
            auto *stale = dynamic_cast<MixerSendPlugin *>(staleOwner.get());
            stale->configure(destination, 1, 2, 42, .125f);
            require(stale->shareCompatibleRuntimeFrom(*next).wasOk(), "Prepare captured successor epoch");
            player.clearNode();
            require(bank->attachmentsControl() == 0 && bank->epochControl() != epoch,
                    "Final native graph teardown invalidates captured epoch");
            bool refused = false;
            try
            {
                stale->prepareSendGainStateForGraph();
            }
            catch (const std::logic_error &failure)
            {
                refused = juce::String(failure.what()).startsWith("mixerSend.gain.staleSource:");
            }
            require(refused, "Retained detached candidate refuses stale bank rather than renewing");
            // Existing registered plugin survives a normal quiescent rebuild.
            player.setNode(makeGraph(*incumbent), rate, maximum);
            require(incumbent->runtimeIdentity() != identity && incumbent->runtimeAttachmentsControl() == 1,
                    "Ordinary registered send obtains fresh bank after quiescent graph teardown");
            envelope = Envelope(std::max(1, int(std::floor(rate * .005))));
            process(19, .75f);
            player.clearNode();
            report += "rate=" + juce::String(rate) + ";maxBlock=" + juce::String(maximum) +
                      ";callbacks=" + juce::String(blocks) + ";maxError=" + juce::String(error, 12) + "\n";
            require(receipt.replaceWithText(report), "Retain all original application send PCM outcomes");
            require(error <= 2e-6, "Every application send sample agrees with independent five-ms ramp");
        }
}
} // namespace vibera::test::mixer_send_runtime
