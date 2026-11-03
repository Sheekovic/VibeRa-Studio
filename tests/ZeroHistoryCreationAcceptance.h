// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <memory>
#include <vector>
#include <cmath>

// Include after ReadyGraphAcceptance.h. Actual original native Player/queue
// metadata experiment. No joint-route admission, SDK, app or hardware claim.
namespace zero_history_creation_acceptance
{
using namespace tracktion::graph;
enum class Source
{
    certifiedZero,
    unknownZero,
    pulse,
    midiUnknown,
    unmarkedWrapper,
    sharedZero
};
struct Owner
{
};
struct Counters
{
    unsigned source = 0, anchor = 0;
};
inline float pulse(int channel, juce::int64 frame) noexcept
{
    if (frame < 0)
        return 0;
    return channel == 0 ? .125f + (frame % 251 == 11 ? .25f : 0.f)
                        : -.0625f - (frame % 257 == 23 ? .125f : 0.f);
}
class Leaf final : public Node, public StructuralAudioSilence
{
    const Source mode;
    const std::shared_ptr<Owner> owner;
    Counters &counts;

  public:
    Leaf(Source m, std::shared_ptr<Owner> o, Counters &c) : mode(m), owner(std::move(o)), counts(c)
    {
        setOptimisations({ClearBuffers::yes, AllocateAudioBuffer::yes,
                          mode == Source::sharedZero ? ShareOutputBuffer::yes : ShareOutputBuffer::no});
    }
    Kind structuralAudioSilenceKind() const noexcept override
    {
        return mode == Source::certifiedZero || mode == Source::sharedZero ? Kind::zeroLeaf : Kind::unknown;
    }
    NodeProperties getNodeProperties() override
    {
        return {true, mode == Source::midiUnknown, 2, 0, 0x5a484c454146ULL, owner.get(), this};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &pc) override
    {
        ++counts.source;
        for (choc::buffer::ChannelCount c = 0; c < 2; ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
                pc.buffers.audio.getSample(c, n) =
                    mode == Source::pulse ? pulse(int(c), pc.referenceSampleRange.getStart() + juce::int64(n))
                                          : 0.f;
        // Immutable certifiedZero mode ALWAYS emits zero audio and empty MIDI.
        // midiUnknown advertises unqualified MIDI; it is not called a MIDI test.
    }
};
class Pass final : public Node, public StructuralAudioSilence
{
    std::unique_ptr<Node> input;

  public:
    explicit Pass(std::unique_ptr<Node> n) : input(std::move(n))
    {
        setOptimisations({ClearBuffers::no, AllocateAudioBuffer::no, ShareOutputBuffer::no});
    }
    Kind structuralAudioSilenceKind() const noexcept override
    {
        return Kind::zeroPreservingInputs;
    }
    NodeProperties getNodeProperties() override
    {
        return input->getNodeProperties();
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {input.get()};
    }
    bool isReadyToProcess() override
    {
        return input->hasProcessed();
    }
    void process(ProcessContext &) override
    {
        setAudioOutput(input.get(), input->getProcessedOutput().audio);
    }
};
class Anchor final : public Node
{
    Counters &counts;

  public:
    explicit Anchor(Counters &c) : counts(c) {}
    NodeProperties getNodeProperties() override
    {
        return {true, false, 2, 256, 0x5a48414e4348ULL};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &) override
    {
        ++counts.anchor;
    } // Node-cleared zero.
};
// An unreviewed wrapper must block inference even over a certified zero leaf.
class OpaquePass final : public Node
{
    std::unique_ptr<Node> input;

  public:
    explicit OpaquePass(std::unique_ptr<Node> n) : input(std::move(n))
    {
        setOptimisations({ClearBuffers::no, AllocateAudioBuffer::no});
    }
    NodeProperties getNodeProperties() override
    {
        return input->getNodeProperties();
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {input.get()};
    }
    bool isReadyToProcess() override
    {
        return input->hasProcessed();
    }
    void process(ProcessContext &) override
    {
        setAudioOutput(input.get(), input->getProcessedOutput().audio);
    }
};
struct Fixture
{
    ready_graph_acceptance::Counts pool;
    Counters counts;
    const std::shared_ptr<Owner> owner = std::make_shared<Owner>();
    const double rate;
    const Source mode;
    LockFreeMultiThreadedNodePlayer player;
    Node *root = nullptr;
    LatencyNode *tap = nullptr;
    const void *injectedUnpreparedProcessor = nullptr;
    choc::buffer::ChannelArrayBuffer<float> pcm{choc::buffer::Size::create(2, 256)};
    tracktion::MidiMessageArray midi;
    juce::int64 reference = 0;
    Fixture(Source m, double r, int preFirstMidi = -1)
        : rate(r), mode(m),
          player([this](auto &p) { return std::make_unique<ready_graph_acceptance::CountingPool>(p, pool); })
    {
        player.setNumThreads(0);
        auto initial = graph(mode, preFirstMidi);
        root = initial.get();
        player.setNode(std::move(initial), rate, 256);
        tap = find(*root);
        require(!injectedUnpreparedProcessor ||
                    injectedUnpreparedProcessor == tap->structuralZeroHistoryRuntimeToken(),
                "Actual injected unprepared processor survives its FIRST native preparation");
        midi.reserve(128);
    }
    std::unique_ptr<Node> graph(Source source, int preFirstMidi = -1)
    {
        std::vector<std::unique_ptr<Node>> inputs;
        if (source == Source::unmarkedWrapper)
            inputs.push_back(
                std::make_unique<OpaquePass>(std::make_unique<Leaf>(Source::certifiedZero, owner, counts)));
        else
            inputs.push_back(std::make_unique<Pass>(std::make_unique<Leaf>(source, owner, counts)));
        if (preFirstMidi >= 0)
        {
            require(source == Source::certifiedZero && preFirstMidi <= 1,
                    "Closed original pre-first-prepare MIDI fault fixture");
            auto unprepared = std::make_unique<LatencyNode>(std::move(inputs.back()), 256);
            injectedUnpreparedProcessor = unprepared->structuralZeroHistoryRuntimeToken();
            auto *processor = const_cast<LatencyProcessor *>(
                static_cast<const LatencyProcessor *>(injectedUnpreparedProcessor));
            tracktion::MidiMessageArray injected;
            if (preFirstMidi == 0)
                injected.addMidiMessage(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
            else
                injected.isAllNotesOff = true;
            processor->writeMIDI(injected); // Real public native API before ANY prepare.
            require(!unprepared->hasStructuralZeroHistory(),
                    "Unprepared unknown MIDI writer never certifies");
            inputs.back() = std::move(unprepared);
        }
        inputs.push_back(std::make_unique<Anchor>(counts));
        return std::make_unique<SummingNode>(std::move(inputs));
    }
    LatencyNode *find(Node &node)
    {
        LatencyNode *found = nullptr;
        for (auto *candidate : getNodes(node, VertexOrdering::postordering))
            if (auto *delay = dynamic_cast<LatencyNode *>(candidate);
                delay && delay->getNodeProperties().audioCompensationStage == owner.get())
            {
                require(!found, "Exactly one explicit zero-history stage");
                found = delay;
            }
        require(found && found->compensationSamples() == 256, "Actual native256 compensation queue");
        return found;
    }
};
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "New zero-creation attempt");
    juce::String report =
        "Actual native metadata-only creation/reuse experiment; stable PDC gate unchanged.\n";
    std::vector<float> actual, expected;
    actual.reserve(131072);
    expected.reserve(131072);
    const auto retain = [&]
    {
        for (int which = 0; which < 2; ++which)
        {
            const auto &data = which ? expected : actual;
            juce::FileOutputStream out(
                folder.getChildFile(which ? "expected-planar-f32le.raw" : "actual-planar-f32le.raw"));
            require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                        out.write(data.data(), data.size() * sizeof(float)),
                    "Retain all queue PCM");
            out.flush();
            require(out.getStatus().wasOk(), "Flush queue PCM");
        }
        juce::FileOutputStream out(folder.getChildFile("result.txt"));
        require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                    out.writeText(report, false, false, "\n"),
                "Retain queue metadata");
        out.flush();
        require(out.getStatus().wasOk(), "Flush queue metadata");
    };
    try
    {
        for (double rate : {48000., 44100.})
        {
            const auto run = [&](Fixture &f)
            {
                for (int frames : {127, 64, 65, 256, 31, 256})
                {
                    const auto before = f.counts;
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    f.pcm.clear();
                    f.midi.clear();
                    const auto began = juce::Time::getHighResolutionTicks();
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
                        f.player.process(
                            {choc::buffer::FrameCount(frames),
                             {f.reference, f.reference + frames},
                             {f.pcm.getView().getStart(choc::buffer::FrameCount(frames)), f.midi}});
                    }
                    const double elapsed = juce::Time::highResolutionTicksToSeconds(
                        juce::Time::getHighResolutionTicks() - began);
                    bool exact = true;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < frames; ++n)
                        {
                            const auto observed = f.pcm.getView().getSample(choc::buffer::ChannelCount(c),
                                                                            choc::buffer::FrameCount(n));
                            const auto wanted =
                                f.mode == Source::pulse ? pulse(c, f.reference + n - 256) : 0.f;
                            actual.push_back(observed);
                            expected.push_back(wanted);
                            exact = exact && observed == wanted;
                        }
                    report += "callback," + juce::String(rate) + "," + juce::String(f.reference) + "," +
                              juce::String(frames) + "," + juce::String(elapsed, 9) + "\n";
                    require(exact && f.counts.source == before.source + 1 &&
                                f.counts.anchor == before.anchor + 1 && f.midi.isEmpty() &&
                                !f.midi.isAllNotesOff && elapsed <= frames / rate &&
                                callbackAllocations.load() == allocations &&
                                callbackDestructions.load() == destructions,
                            "Exact full PCM/each producer once/MIDI/heap/actual callback deadline");
                    f.reference += frames;
                }
            };
            const auto prepare = [&](Fixture &f, Source next, auto &&inspect)
            {
                const auto before = f.counts;
                auto graph = f.graph(next);
                auto *candidate = graph.get();
                auto ready = f.player.prepareFixedGraph(std::move(graph));
                auto *newTap = f.find(*candidate);
                require(f.counts.source == before.source && f.counts.anchor == before.anchor &&
                            newTap->structuralZeroHistoryRuntimeToken() ==
                                f.tap->structuralZeroHistoryRuntimeToken(),
                        "Prepare no processing and FINAL exact inherited processor, not fresh temporary");
                inspect(*newTap);
                // Intentional CONTROL discard: same native ready API, no post.
            };
            // Reproduces v1's source-derived pre-first-prepare hole with REAL
            // constructed native Nodes/processors, never mocked certificate bits.
            for (int preFirstMidi = 0; preFirstMidi < 2; ++preFirstMidi)
            {
                Fixture f(Source::certifiedZero, rate, preFirstMidi);
                require(!f.tap->hasStructuralZeroHistory() && f.counts.source == 0 && f.counts.anchor == 0,
                        "Prior NoteOn/panic permanently disqualifies FIRST certification without DSP");
                prepare(f, Source::certifiedZero,
                        [&](auto &next)
                        {
                            require(!next.hasStructuralZeroHistory(),
                                    "Pending-MIDI history cannot resurrect through fresh temporary reuse");
                        });
                report += "PASS pre-first-prepare-real-MIDI," + juce::String(preFirstMidi) +
                          ",admission-only/no-callback/no-queue-read\n";
                // Do not clear/drop/read MIDI or claim event-delivery/RT behavior
                // from this deliberately foreign, pre-initialisation writer case.
            }
            {
                Fixture f(Source::certifiedZero, rate);
                require(f.tap->hasStructuralZeroHistory(), "Fresh final zero queue certifies creation");
                run(f);
                prepare(
                    f, Source::certifiedZero, [&](auto &next)
                    { require(next.hasStructuralZeroHistory(), "Certified same-zero reuse keeps proof"); });
                run(f);
                report += "PASS fresh/zero-reuse/cancel-incumbent\n";
            }
            {
                Fixture f(Source::unknownZero, rate);
                require(!f.tap->hasStructuralZeroHistory(),
                        "Unknown is default even when every emitted sample is zero");
                run(f);
                prepare(f, Source::certifiedZero,
                        [&](auto &next)
                        {
                            require(!next.hasStructuralZeroHistory(),
                                    "Fresh temporary MUST NOT certify actual reused unknown queue");
                        });
                run(f);
                report += "PASS final-reuse-creation-hazard/unknown-zero\n";
            }
            {
                Fixture f(Source::certifiedZero, rate);
                run(f);
                prepare(f, Source::unknownZero,
                        [&](auto &next)
                        {
                            require(!next.hasStructuralZeroHistory() && !f.tap->hasStructuralZeroHistory(),
                                    "Unknown candidate invalidates actual shared certificate before post");
                        });
                run(f);
                prepare(f, Source::certifiedZero,
                        [&](auto &next)
                        {
                            require(!next.hasStructuralZeroHistory(),
                                    "Cancelled unknown candidate never permits retroactive recertification");
                        });
                run(f);
                report += "PASS monotonic-unknown/cancel/non-resurrection\n";
            }
            {
                Fixture f(Source::certifiedZero, rate);
                run(f);
                prepare(f, Source::pulse,
                        [&](auto &next)
                        {
                            require(!next.hasStructuralZeroHistory(),
                                    "Nonzero candidate invalidation before any candidate processing");
                        });
                run(f);
                report += "PASS nonzero-candidate-cancel/incumbent-unmodified\n";
            }
            {
                Fixture f(Source::pulse, rate);
                require(!f.tap->hasStructuralZeroHistory(), "Real nonzero producer unqualified");
                run(f);
            }
            {
                Fixture f(Source::unmarkedWrapper, rate);
                require(!f.tap->hasStructuralZeroHistory(),
                        "Unmarked native Node defaults unknown despite certified leaf");
                run(f);
            }
            {
                Fixture f(Source::sharedZero, rate);
                require(!f.tap->hasStructuralZeroHistory(),
                        "Shareable zero storage cannot certify immutable silence");
                run(f);
            }
            {
                Fixture f(Source::midiUnknown, rate);
                require(!f.tap->hasStructuralZeroHistory(),
                        "Possible MIDI is unqualified; no fabricated event qualification");
            }
            // Deliberate CONTROL-only API mutation with callbacks excluded.
            // Token names the pinned ACTUAL processor; no FIFO content is read.
            for (int mutation = 0; mutation < 5; ++mutation)
            {
                Fixture f(Source::certifiedZero, rate);
                require(f.tap->hasStructuralZeroHistory(), "Mutation fixture genuinely certified first");
                auto *processor = const_cast<LatencyProcessor *>(
                    static_cast<const LatencyProcessor *>(f.tap->structuralZeroHistoryRuntimeToken()));
                if (mutation == 0)
                    processor->prepareToPlay(rate, 256, 2);
                else if (mutation == 1)
                    processor->setLatencyNumSamples(256);
                else if (mutation == 2)
                {
                    choc::buffer::ChannelArrayBuffer<float> zeros{choc::buffer::Size::create(2, 31)};
                    zeros.clear();
                    processor->writeAudio(zeros.getView());
                }
                else if (mutation == 3)
                {
                    tracktion::MidiMessageArray events;
                    events.isAllNotesOff = true;
                    processor->writeMIDI(events);
                }
                else
                {
                    tracktion::MidiMessageArray events;
                    events.addMidiMessage(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
                    processor->writeMIDI(events);
                }
                require(!f.tap->hasStructuralZeroHistory(),
                        "Reprepare/config/foreign audio/panic invalidates monotonically");
                report += "PASS CONTROL-mutation," + juce::String(mutation) + "\n";
                // No audio/recovery claim after intentionally corrupting owner protocol.
            }
        }
        report += "PASS metadata only. Joint relocation/app whitelist/MIDI/worker remain unqualified.\n";
        retain();
    }
    catch (...)
    {
        report += "FAILED; preserve attempt.\n";
        retain();
        throw;
    }
}
} // namespace zero_history_creation_acceptance
