// SPDX-License-Identifier: AGPL-3.0-or-later
// Original CONTROL-only native properties regression. No callback/timing claim.
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <array>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

namespace vibera::test::summing_properties
{
using namespace tracktion::graph;

struct QueryLimit final : std::exception
{
    const char *what() const noexcept override
    {
        return "Original CONTROL leaf property-query cap exceeded";
    }
};
struct Counter
{
    unsigned queries = 0;
    unsigned maximum = 256;
};
struct Evidence
{
    std::array<Counter, 4> leaves{};
    std::array<NodeProperties, 8> observed{};
    unsigned observations = 0;
    void record(NodeProperties value)
    {
        if (observations >= observed.size())
            throw std::logic_error("Original properties receipt overflow");
        observed[observations++] = value;
    }
};
inline void expect(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
class Leaf final : public Node
{
    Counter &counter;

  public:
    NodeProperties properties;
    Leaf(Counter &c, NodeProperties p) : counter(c), properties(p) {}
    NodeProperties getNodeProperties() override
    {
        // This is only called in CONTROL tests, never from an audio callback.
        // The old exponential traversal throws after 257 leaf visits, not 2^128.
        if (++counter.queries > counter.maximum)
            throw QueryLimit{};
        return properties;
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &) override
    {
        throw std::logic_error("Properties-only fixture must never process DSP");
    }
};
inline std::size_t stageID(std::size_t input)
{
    // Existing public native hash contract, not a test of the hash algorithm.
    std::size_t result = 0;
    if (input != 0)
        tracktion::hash_combine(result, input);
    return result;
}
inline void checkShape(const NodeProperties &p, bool audio, bool midi, int channels, int latency,
                       const char *message)
{
    expect(p.hasAudio == audio && p.hasMidi == midi && p.numberOfChannels == channels &&
               p.latencyNumSamples == latency,
           message);
}

inline void unprepared128(Evidence &e)
{
    auto leaf = std::make_unique<Leaf>(e.leaves[0], NodeProperties{true, true, 3, 37, 123});
    auto *mutableLeaf = leaf.get();
    std::unique_ptr<Node> root = std::move(leaf);
    std::size_t expectedIdentity = 123;
    for (unsigned depth = 0; depth < 128; ++depth)
    {
        std::vector<std::unique_ptr<Node>> input;
        input.push_back(std::move(root));
        root = std::make_unique<SummingNode>(std::move(input));
        expectedIdentity = stageID(expectedIdentity);
    }
    const auto first = root->getNodeProperties();
    e.record(first);
    checkShape(first, true, true, 3, 37, "128 actual unprepared sums preserve literal shape/MIDI/latency");
    expect(first.nodeID == expectedIdentity && e.leaves[0].queries == 1,
           "One leaf traversal per unwrapped128 root query with exact stage identity");
    const auto second = root->getNodeProperties();
    e.record(second);
    expect(second.nodeID == first.nodeID && e.leaves[0].queries == 2,
           "Unprepared sums recompute on second query; no premature property cache");
    mutableLeaf->properties.latencyNumSamples = 53; // CONTROL-only unprepared graph change.
    const auto changed = root->getNodeProperties();
    e.record(changed);
    checkShape(changed, true, true, 3, 53, "Unprepared changed leaf latency is visible through128 sums");
    expect(changed.nodeID == first.nodeID && e.leaves[0].queries == 3,
           "Latency change retains logical stage identity and one traversal");
}

inline void heterogeneous(Evidence &e)
{
    SummingNode empty;
    const auto none = empty.getNodeProperties();
    e.record(none);
    checkShape(none, false, false, 0, 0, "Empty actual sum has literal empty properties");
    expect(none.nodeID == 0, "Empty sum identity remains zero");
    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::make_unique<Leaf>(e.leaves[0], NodeProperties{true, false, 2, 17, 101}));
    nodes.push_back(std::make_unique<Leaf>(e.leaves[1], NodeProperties{false, true, 0, 9, 0}));
    nodes.push_back(std::make_unique<Leaf>(e.leaves[2], NodeProperties{true, false, 4, 37, 303}));
    SummingNode sum(std::move(nodes));
    const auto p = sum.getNodeProperties();
    e.record(p);
    checkShape(p, true, true, 4, 37, "Heterogeneous sum uses OR flags/max channels/max latency");
    std::size_t expected = 0;
    tracktion::hash_combine(expected, std::size_t(101));
    tracktion::hash_combine(expected,
                            std::size_t(0)); // Existing nonzero accumulated stage still combines zero.
    tracktion::hash_combine(expected, std::size_t(303));
    expect(p.nodeID == expected, "Ordered source IDs, including interior zero, preserve native identity");
    expect(e.leaves[0].queries == 1 && e.leaves[1].queries == 1 && e.leaves[2].queries == 1,
           "Each direct logical input is queried exactly once");
}

inline void consumerPdc(Evidence &e)
{
    Leaf source(e.leaves[0], {true, true, 2, 37, 701});
    SummingNode owner;
    const LatencyNode::RetainedAudioEdge edge{{701, 702, 0}, 128, &owner, false};
    auto delay = std::make_unique<LatencyNode>(&source, 11, edge);
    auto *tap = delay.get();
    expect(LatencyNode::logicalInputFor(tap, &owner) == &source,
           "Actual retained PDC wrapper exposes source only to exact consumer");
    owner.addInput(std::move(delay));
    const auto p = owner.getNodeProperties();
    e.record(p);
    checkShape(p, true, false, 2, 48, "Owner uses actual wrapper latency and MIDI policy");
    expect(p.nodeID == stageID(701), "Owner stage identity ignores its own PDC wrapper");
    expect(e.leaves[0].queries == 2,
           "Distinct logical source fallback still traverses source twice; bounded small case");
    tap->updateRetainedRequest(29, edge); // No node has been prepared or processed.
    const auto changed = owner.getNodeProperties();
    e.record(changed);
    checkShape(changed, true, false, 2, 66, "Changed unprepared compensation affects latency");
    expect(changed.nodeID == p.nodeID, "Compensation count is absent from owner logical identity");
    SummingNode foreign(std::vector<Node *>{tap}); // Both sources outlive foreign.
    expect(LatencyNode::logicalInputFor(tap, &foreign) == tap,
           "Other consumer must retain foreign wrapper identity");
    const auto tapProperties = tap->getNodeProperties();
    const auto before = e.leaves[0].queries;
    const auto foreignProperties = foreign.getNodeProperties();
    e.record(foreignProperties);
    checkShape(foreignProperties, true, false, 2, 66, "Foreign consumer retains actual PDC properties");
    expect(tapProperties.nodeID != 701 && foreignProperties.nodeID == stageID(tapProperties.nodeID) &&
               foreignProperties.nodeID != p.nodeID,
           "Foreign wrapper identity remains distinct from underlying logical source");
    expect(e.leaves[0].queries == before + 1, "Same-pointer foreign logical input reuses first properties");
}

inline void explicitDelay(Evidence &e)
{
    Leaf source(e.leaves[0], {true, true, 1, 5, 811});
    auto delay = std::make_unique<LatencyNode>(&source, 19);
    auto *tap = delay.get();
    SummingNode owner;
    expect(LatencyNode::logicalInputFor(tap, &owner) == tap,
           "Explicit native delay is not this consumer's compensation wrapper");
    const auto delayed = tap->getNodeProperties();
    owner.addInput(std::move(delay));
    const auto before = e.leaves[0].queries;
    const auto p = owner.getNodeProperties();
    e.record(p);
    checkShape(p, true, true, 1, 24, "Explicit native delay contributes actual latency and MIDI");
    expect(delayed.nodeID != 811 && p.nodeID == stageID(delayed.nodeID) && p.nodeID != stageID(811),
           "Explicit delay identity must not be stripped by optimization");
    expect(e.leaves[0].queries == before + 1, "Unstripped native delay queries underlying source once");
}

inline void nestedOwnership(Evidence &e)
{
    Leaf source(e.leaves[0], {true, true, 2, 7, 901});
    SummingNode otherConsumer;
    const LatencyNode::RetainedAudioEdge innerEdge{{901, 902, 1}, 128, &otherConsumer, true};
    LatencyNode inner(&source, 13, innerEdge);
    SummingNode owner;
    const LatencyNode::RetainedAudioEdge outerEdge{{903, 904, 2}, 128, &owner, false};
    auto outer = std::make_unique<LatencyNode>(&inner, 17, outerEdge);
    expect(LatencyNode::logicalInputFor(outer.get(), &owner) == &inner,
           "Only this consumer's immediate wrapper is stripped");
    const auto innerProperties = inner.getNodeProperties();
    owner.addInput(std::move(outer));
    const auto p = owner.getNodeProperties();
    e.record(p);
    checkShape(p, true, false, 2, 37, "Nested actual PDC wrappers both retain declared latency");
    expect(p.nodeID == stageID(innerProperties.nodeID) && p.nodeID != stageID(901),
           "Foreign inner PDC wrapper is preserved in owner identity");
}

inline void tests(const juce::File &folder)
{
    expect(folder.createDirectory().wasOk(), "Owned summing-properties evidence directory");
    const auto receipt = folder.getChildFile("summing-properties.txt");
    expect(!receipt.exists(), "Summing properties receipt must be fresh");
    juce::String report = "Original actual unprepared SummingNode/LatencyNode properties only. "
                          "No graph preparation/process/provider/timing/hardware/listening claim.\n";
    std::exception_ptr firstFailure;
    const auto run = [&](const char *name, auto function)
    {
        Evidence e;
        juce::String failure;
        try
        {
            function(e);
        }
        catch (const QueryLimit &error)
        {
            failure = juce::String("queryBudgetExceeded: ") + error.what();
        }
        catch (const std::exception &error)
        {
            failure = error.what();
        }
        catch (...)
        {
            failure = "unclassified exception";
        }
        if (failure.isNotEmpty() && !firstFailure)
            firstFailure = std::make_exception_ptr(std::runtime_error(failure.toStdString()));
        report += juce::String(name) + ": " + (failure.isEmpty() ? "passed" : "FAILED " + failure) + "\n";
        for (std::size_t i = 0; i < e.leaves.size(); ++i)
            report += " leaf" + juce::String(int(i)) + "Queries=" + juce::String(e.leaves[i].queries) +
                      " cap=" + juce::String(e.leaves[i].maximum) + "\n";
        for (unsigned i = 0; i < e.observations; ++i)
        {
            const auto &p = e.observed[i];
            report += " observed" + juce::String(i) + " audio=" + juce::String(int(p.hasAudio)) +
                      " midi=" + juce::String(int(p.hasMidi)) +
                      " channels=" + juce::String(p.numberOfChannels) +
                      " latency=" + juce::String(p.latencyNumSamples) +
                      " id=" + juce::String(static_cast<juce::uint64>(p.nodeID)) + "\n";
        }
        expect(receipt.replaceWithText(report),
               "Retain actual property queries/failure before suite assertion");
    };
    run("unprepared128", unprepared128);
    run("heterogeneous", heterogeneous);
    run("consumerPdc", consumerPdc);
    run("explicitDelay", explicitDelay);
    run("nestedOwnership", nestedOwnership);
    if (firstFailure)
        std::rethrow_exception(firstFailure);
}
} // namespace vibera::test::summing_properties
