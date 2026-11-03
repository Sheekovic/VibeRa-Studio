// SPDX-License-Identifier: AGPL-3.0-or-later
// Original unbuilt fixture. Include after require(); root owns integration.
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <stdexcept>

namespace vibera::draft
{
// Factory must call the ACTUAL adapter createDetachedGraph, not a fake base plugin.
// Run once for normal registered RemoteEffect, MixerSidechainSend and FixedSampler.
// FixedSampler is private to AudioEngine.cpp: call there through a narrow original
// acceptance seam; do not expose private sampler construction as a product command.
template <class Factory, class PrepareCandidateGraph>
void detachedPluginConstructionTests(tracktion::Edit &edit, tracktion::Plugin::Ptr original,
                                     Factory createCandidate, PrepareCandidateGraph prepareCandidateGraph)
{
    require(original && !original->isDetachedGraph() && &original->edit == &edit,
            "Genuine registered live app adapter is required");
    const auto cache = edit.getPluginCache().getPlugins(); // Pin baseline cache members.
    require(cache.contains(original.get()) &&
                edit.automatableEditItemCache.findItem(original->itemID) == original.get(),
            "Original is present in both native caches before experiment");
    const auto originalState = original->state.createCopy();
    const auto editState = edit.state.createCopy();
    auto &history = edit.getUndoManager();
    const auto units = history.getNumberOfUnitsTakenUpByStoredCommands();
    const auto actions = history.getNumActionsInCurrentTransaction();
    const bool undo = history.canUndo(), redo = history.canRedo();
    auto preserved = [&]
    {
        const auto after = edit.getPluginCache().getPlugins();
        require(after.size() == cache.size(), "Detached candidate does not enter plugin cache");
        for (int index = 0; index < cache.size(); ++index)
            require(after[index] == cache[index], "Exact original cache ownership and order retained");
        require(edit.automatableEditItemCache.findItem(original->itemID) == original.get(),
                "Same-ID candidate never replaces or erases incumbent item registration");
        require(original->state.isEquivalentTo(originalState) && edit.state.isEquivalentTo(editState),
                "Candidate configuration/destruction leaves exact live ValueTrees unchanged");
        require(history.getNumberOfUnitsTakenUpByStoredCommands() == units &&
                    history.getNumActionsInCurrentTransaction() == actions &&
                    history.canUndo() == undo && history.canRedo() == redo,
                "Private defaults and connections do not consume live Undo/Redo storage");
    };
    for (const bool injectedFailure : {false, true})
    {
        bool sawFailure = false;
        try
        {
            auto candidate = createCandidate(original->state.createCopy());
            require(candidate && candidate.get() != original.get() && candidate->isDetachedGraph() &&
                        candidate->itemID == original->itemID && candidate->getUndoManager() == nullptr &&
                        candidate->getNumAutomatableParameters() == 0,
                    "Actual detached adapter retains stable ID without native parameter registration");
            candidate->state.setProperty("privateFixtureValue", 37, candidate->getUndoManager());
            candidate->remapOnTempoChange = true;
            candidate->makeConnection(0, 0, nullptr);
            candidate->changed();
            preserved();
            // Driver must prepare the actual same-Edit candidate through the
            // native factory/player preparation seam WITHOUT publishing or
            // processing it. This required callback cannot be an empty mock.
            auto graph = prepareCandidateGraph(candidate);
            require(bool(graph), "Actual candidate graph preparation returns owning ready graph");
            preserved();
            if (injectedFailure)
                throw std::runtime_error("Original private preparation failure before graph publication");
        }
        catch (const std::runtime_error &error)
        {
            if (!injectedFailure || juce::String(error.what()) !=
                                        "Original private preparation failure before graph publication")
                throw;
            sawFailure = true;
        }
        require(sawFailure == injectedFailure, "Failure fixture actually reaches candidate destruction");
        preserved();
    }
    for (const auto metadata : {tracktion::IDs::MACROPARAMETERS, tracktion::IDs::MODIFIERASSIGNMENTS})
    {
        auto state = original->state.createCopy();
        auto container = state.getOrCreateChildWithName(metadata, nullptr);
        container.addChild(juce::ValueTree("unsupportedFixtureAssignment"), -1, nullptr);
        bool refused = false;
        try { auto candidate = createCandidate(state); }
        catch (const std::invalid_argument &) { refused = true; }
        require(refused, "Unsupported native macro/modifier state refused before construction side effects");
        preserved();
    }
    auto missingId = original->state.createCopy();
    missingId.removeProperty(tracktion::IDs::id, nullptr);
    bool refused = false;
    try { auto candidate = createCandidate(missingId); }
    catch (const std::invalid_argument &) { refused = true; }
    require(refused, "Missing reserved ID refused instead of allocating an ID from live Edit");
    preserved();
}
} // namespace vibera::draft
