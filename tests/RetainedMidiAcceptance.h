// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual-player MIDI oracles; prior adapter failures remain retained.
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace vibera::draft::retained_midi
{
enum class Kind
{
    on,
    off,
    cc123,
    allOffFlag
};
struct Event
{
    std::int64_t sample = 0;
    Kind kind = Kind::on;
    int key = 60, channel = 1;
    tracktion::MPESourceID source = 101;
};
constexpr std::uint64_t producer = 0x76696272614d5352ULL, consumer = 0x76696272614d4453ULL;

// Original immutable script. It does not use a MIDI device, project sequencer,
// plugin mock, worker, musical playhead or mutable global injection hook.
class Script final : public tracktion::graph::Node
{
    std::shared_ptr<const std::vector<Event>> events;
    double rate;

  public:
    Script(std::shared_ptr<const std::vector<Event>> script, double sampleRate)
        : events(std::move(script)), rate(sampleRate)
    {
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return {true, true, 2, 0, producer};
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &pc) override
    {
        pc.buffers.midi.clear();
        for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
        {
            pc.buffers.audio.getSample(0, n) = .125f;
            pc.buffers.audio.getSample(1, n) = -.25f;
        }
        for (const auto &event : *events)
        {
            if (!pc.referenceSampleRange.contains(event.sample))
                continue;
            if (event.kind == Kind::allOffFlag)
            {
                pc.buffers.midi.isAllNotesOff = true;
                continue;
            }
            auto message = event.kind == Kind::on
                               ? juce::MidiMessage::noteOn(event.channel, event.key, juce::uint8(100))
                           : event.kind == Kind::off ? juce::MidiMessage::noteOff(event.channel, event.key)
                                                     : juce::MidiMessage::allNotesOff(event.channel);
            pc.buffers.midi.addMidiMessage(std::move(message),
                                           double(event.sample - pc.referenceSampleRange.getStart()) / rate,
                                           event.source);
        }
    }
};

class Output final : public tracktion::graph::Node
{
    std::unique_ptr<tracktion::graph::LatencyNode> tap;

  public:
    Output(std::shared_ptr<const std::vector<Event>> events, double rate, int delay)
    {
        tracktion::graph::LatencyNode::RetainedAudioEdge edge{{producer, consumer, 0}, 1024, this, true};
        std::unique_ptr<tracktion::graph::Node> source = std::make_unique<Script>(std::move(events), rate);
        tap = std::make_unique<tracktion::graph::LatencyNode>(std::move(source), delay, edge);
        setAudioCompensationHistory(1024, 16384);
        setMidiCompensationHistory({256, 16, 256, 1, 1024 * 1024});
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto props = tap->getNodeProperties();
        props.nodeID = consumer;
        return props;
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {tap.get()};
    }
    bool isReadyToProcess() override
    {
        return tap->hasProcessed();
    }
    void process(ProcessContext &pc) override
    {
        const auto source = tap->getProcessedOutput();
        setAudioOutput(tap.get(), source.audio);
        pc.buffers.midi.copyFrom(source.midi);
    }
    auto diagnostics() const noexcept
    {
        return tap->historyDiagnostics();
    }
};

struct Observation
{
    Event event;
    std::int64_t callbackStart = 0;
};
struct Scenario
{
    const char *name;
    int before, after;
    bool replace;
    std::vector<Event> script;
    // Pending events keep their enqueue-time due sample; new events use the new delay.
    std::vector<Event> expected;
    bool expectFlag = false;
};

inline bool sameMessage(const Event &a, const Event &b)
{
    return a.sample == b.sample && a.kind == b.kind && a.key == b.key && a.channel == b.channel &&
           a.source == b.source;
}

// Independent event/gate oracle. Capture is retained BEFORE the final assertion;
// missing queued note-off/on/CC123 or panic flag is a failure, never an expected pass.
// These event oracles alone provide no real-time allocation/deadline qualification.
// The successor explicitly opts into bounded storage; closed callback checks are separate.
inline void retainedMidiDraftTests(const juce::File &folder)
{
    if (folder.createDirectory().failed())
        throw std::runtime_error("Owned retained MIDI evidence folder");
    const std::vector<Scenario> scenarios{
        {"same-delay-retains-pending-off",
         128,
         128,
         true,
         {{16, Kind::on}, {208, Kind::off}, {528, Kind::on}, {592, Kind::off}},
         {{144, Kind::on}, {336, Kind::off}, {656, Kind::on}, {720, Kind::off}}},
        {"increase-retains-pending-off",
         128,
         256,
         true,
         {{16, Kind::on}, {208, Kind::off}, {528, Kind::on}, {592, Kind::off}},
         {{144, Kind::on}, {336, Kind::off}, {784, Kind::on}, {848, Kind::off}}},
        {"decrease-retains-pending-pair",
         256,
         128,
         true,
         {{16, Kind::on}, {80, Kind::off}, {528, Kind::on}, {592, Kind::off}},
         {{272, Kind::on}, {336, Kind::off}, {656, Kind::on}, {720, Kind::off}}},
        {"positive-to-zero-drains-pending-off",
         128,
         0,
         true,
         {{16, Kind::on}, {208, Kind::off}, {528, Kind::on}, {592, Kind::off}},
         {{144, Kind::on}, {336, Kind::off}, {528, Kind::on}, {592, Kind::off}}},
        {"zero-to-positive-new-events",
         0,
         256,
         true,
         {{16, Kind::on}, {208, Kind::off}, {528, Kind::on}, {592, Kind::off}},
         {{16, Kind::on}, {208, Kind::off}, {784, Kind::on}, {848, Kind::off}}},
        {"explicit-panic-event-survives-delay-change",
         128,
         256,
         true,
         {{16, Kind::on}, {208, Kind::cc123}, {528, Kind::on}, {592, Kind::off}},
         {{144, Kind::on}, {336, Kind::cc123}, {784, Kind::on}, {848, Kind::off}}},
        {"positive-steady-panic-flag-not-lost",
         128,
         128,
         false,
         {{16, Kind::on}, {256, Kind::allOffFlag}},
         {{144, Kind::on}},
         true},
        {"positive-replaced-panic-flag-not-lost",
         128,
         256,
         true,
         {{16, Kind::on}, {256, Kind::allOffFlag}},
         {{144, Kind::on}},
         true},
        {"zero-steady-panic-flag-baseline",
         0,
         0,
         false,
         {{16, Kind::on}, {256, Kind::allOffFlag}},
         {{16, Kind::on}},
         true},
        {"source-channel-identities-survive",
         256,
         128,
         true,
         {{16, Kind::on, 60, 1, 101},
          {80, Kind::off, 60, 1, 101},
          {32, Kind::on, 64, 2, 202},
          {96, Kind::off, 64, 2, 202}},
         {{272, Kind::on, 60, 1, 101},
          {336, Kind::off, 60, 1, 101},
          {288, Kind::on, 64, 2, 202},
          {352, Kind::off, 64, 2, 202}}}};
    juce::String report = "Original scripted events through actual retained LatencyNode and actual "
                          "LockFreeMultiThreadedNodePlayer. No device/plugin/listening claim.\n";
    bool allPassed = true;
    std::size_t attempted = 0, completed = 0;
    for (const auto rate : {48000., 44100.})
        for (const auto &scenario : scenarios)
        {
            ++attempted;
            try
            {
                auto script = std::make_shared<const std::vector<Event>>(scenario.script);
                tracktion::graph::LockFreeMultiThreadedNodePlayer player;
                player.setNumThreads(0);
                player.setNode(std::make_unique<Output>(script, rate, scenario.before), rate, 64);
                choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2, 64));
                tracktion::MidiMessageArray midi;
                midi.reserve(256);
                std::vector<Observation> observed;
                observed.reserve(32);
                bool pcmPassed = true, reused = !scenario.replace, queueReset = false;
                for (std::int64_t reference = 0; reference < 1152; reference += 64)
                {
                    if (reference == 256 && scenario.replace)
                    {
                        player.setNode(std::make_unique<Output>(script, rate, scenario.after), rate, 64);
                        const auto d = dynamic_cast<Output *>(player.getNode())->diagnostics();
                        reused = d.reuse == vibera::dsp::CompensationReuse::matched;
                        queueReset = d.midiQueueReset;
                    }
                    pcm.clear();
                    midi.clear();
                    player.process({64, {reference, reference + 64}, {pcm.getView(), midi}});
                    const auto effectiveDelay =
                        reference < 256 || !scenario.replace ? scenario.before : scenario.after;
                    for (int frame = 0; frame < 64; ++frame)
                    {
                        const bool available = reference + frame >= effectiveDelay;
                        pcmPassed = pcmPassed &&
                                    pcm.getView().getSample(0, frame) == (available ? .125f : 0.f) &&
                                    pcm.getView().getSample(1, frame) == (available ? -.25f : 0.f);
                    }
                    if (midi.isAllNotesOff)
                        observed.push_back({{reference, Kind::allOffFlag}, reference});
                    for (const auto &event : midi)
                    {
                        Kind kind;
                        if (event.isNoteOn())
                            kind = Kind::on;
                        else if (event.isNoteOff())
                            kind = Kind::off;
                        else if (event.isAllNotesOff())
                            kind = Kind::cc123;
                        else
                            throw std::runtime_error("Unexpected MIDI fixture output type");
                        const auto absolute = reference + std::llround(event.getTimeStamp() * rate);
                        const auto key = kind == Kind::cc123 ? 60 : event.getNoteNumber();
                        observed.push_back(
                            {{absolute, kind, key, event.getChannel(), event.mpeSourceID}, reference});
                    }
                }
                bool matched = true, panicObserved = !scenario.expectFlag;
                size_t messageCount = 0, flagCount = 0;
                std::array<std::array<bool, 128>, 16> held{};
                for (const auto &value : observed)
                {
                    const auto &e = value.event;
                    if (e.kind == Kind::allOffFlag)
                    {
                        ++flagCount;
                        // Boolean flags have no intra-block timestamp. A safety-immediate
                        // policy OR a bounded delayed policy is acceptable; losing it is not.
                        panicObserved = panicObserved || (e.sample >= 256 && e.sample <= 512);
                        for (auto &channel : held)
                            channel.fill(false);
                        continue;
                    }
                    ++messageCount;
                    matched = matched && std::count_if(scenario.expected.begin(), scenario.expected.end(),
                                                       [&](const Event &expected)
                                                       { return sameMessage(e, expected); }) == 1;
                    if (e.kind == Kind::cc123)
                        held[size_t(e.channel - 1)].fill(false);
                    else
                        held[size_t(e.channel - 1)][size_t(e.key)] = e.kind == Kind::on;
                }
                for (const auto &expected : scenario.expected)
                    matched = matched &&
                              std::count_if(observed.begin(), observed.end(), [&](const Observation &actual)
                                            { return sameMessage(actual.event, expected); }) == 1;
                const bool stuck = std::any_of(
                    held.begin(), held.end(), [](const auto &channel)
                    { return std::any_of(channel.begin(), channel.end(), [](bool on) { return on; }); });
                const bool passed = matched && messageCount == scenario.expected.size() && panicObserved &&
                                    flagCount == (scenario.expectFlag ? 1u : 0u) && !stuck && reused &&
                                    pcmPassed;
                allPassed = allPassed && passed;
                ++completed;
                juce::String failureClass;
                if (!matched || messageCount != scenario.expected.size())
                    failureClass += "event-tuples;";
                if (!panicObserved || flagCount != (scenario.expectFlag ? 1u : 0u))
                    failureClass += "boolean-panic;";
                if (stuck)
                    failureClass += "held-gates;";
                if (!reused)
                    failureClass += "audio-history-reuse;";
                if (!pcmPassed)
                    failureClass += "independent-pcm;";
                report +=
                    juce::String(scenario.name) + " rate=" + juce::String(rate, 0) +
                    " pass=" + juce::String(int(passed)) +
                    " failureClass=" + (passed ? juce::String("none") : failureClass) +
                    " exactMessages=" + juce::String(int(matched)) +
                    " count=" + juce::String(int(messageCount)) + " flags=" + juce::String(int(flagCount)) +
                    " panicFlag=" + juce::String(int(panicObserved)) + " stuck=" + juce::String(int(stuck)) +
                    " PCM=" + juce::String(int(pcmPassed)) + " historyReused=" + juce::String(int(reused)) +
                    " queueReset=" + juce::String(int(queueReset)) + "\n";
                for (const auto &actual : observed)
                    report += "  observed sample=" + juce::String(juce::int64(actual.event.sample)) +
                              " callback=" + juce::String(juce::int64(actual.callbackStart)) +
                              " kind=" + juce::String(int(actual.event.kind)) +
                              " key=" + juce::String(actual.event.key) +
                              " channel=" + juce::String(actual.event.channel) +
                              " source=" + juce::String(int(actual.event.source)) + "\n";
                for (const auto &expected : scenario.expected)
                    report += "  expected sample=" + juce::String(juce::int64(expected.sample)) +
                              " kind=" + juce::String(int(expected.kind)) +
                              " key=" + juce::String(expected.key) +
                              " channel=" + juce::String(expected.channel) +
                              " source=" + juce::String(int(expected.source)) + "\n";
            }
            catch (const std::exception &error)
            {
                allPassed = false;
                report += juce::String(scenario.name) + " rate=" + juce::String(rate, 0) +
                          " pass=0 failureClass=case-exception-unclassified diagnostic=" +
                          juce::String(error.what()) + "\n";
            }
            catch (...)
            {
                allPassed = false;
                report += juce::String(scenario.name) + " rate=" + juce::String(rate, 0) +
                          " pass=0 failureClass=case-exception-unclassified diagnostic=unknown\n";
            }
        }
    report += "attempted=" + juce::String(int(attempted)) + " completed=" + juce::String(int(completed)) +
              " expected=20\n";
    allPassed = allPassed && attempted == 20 && completed == 20;
    if (!folder.getChildFile("retained-midi.txt").replaceWithText(report))
        throw std::runtime_error("Retain MIDI queue evidence before assertion");
    if (!allPassed)
        throw std::runtime_error("Retained MIDI lost/reordered safety state; see retained-midi.txt");
}
} // namespace vibera::draft::retained_midi
