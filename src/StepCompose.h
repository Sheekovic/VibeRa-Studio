// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PatternChannelLoops.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace vibera
{
// Original composition policy, not a recovered vendor rhythm algorithm.
// This is CONTROL-only document preparation; it owns no UI, DSP or history.
enum class StepComposeMode
{
    manual,
    everyN,
    euclidean
};
struct StepComposeRecipe
{
    Id channel = 0;
    int sequenceSteps = 16; // 1..4096 sixteenth-note cells; source cycle only.
    StepComposeMode mode = StepComposeMode::everyN;
    int every = 4;                    // Every-N interval, 1..sequenceSteps; cell zero is the origin.
    int pulses = 4;                   // Euclidean active count, 0..sequenceSteps.
    std::vector<std::uint8_t> manual; // Manual mode: exactly sequenceSteps values, each zero or one.
    int rotation = 0;                 // Signed cell shift; normalized modulo sequenceSteps.
    bool dice = false;
    std::uint32_t seed = 0; // Shared UI/MCP range0..UINT32_MAX; no process/global random state.
    bool loop = true;
    bool convertMelodic = false;
};

// Failure preserves output. Dice redistributes the base mask's exact active
// count, then rotation is applied. The fixed integer generator and selection
// order make results independent of standard-library random implementations.
inline juce::Result stepCompositionMask(const StepComposeRecipe &recipe, std::vector<std::uint8_t> &output)
{
    const int count = recipe.sequenceSteps;
    if (count < 1 || count > 4096)
        return juce::Result::fail("stepCompose.invalidRecipe: Sequence length must be1..4096 steps");
    std::vector<std::uint8_t> mask(size_t(count), 0);
    switch (recipe.mode)
    {
    case StepComposeMode::manual:
        if (recipe.manual.size() != size_t(count) ||
            std::any_of(recipe.manual.begin(), recipe.manual.end(), [](auto bit) { return bit > 1; }))
            return juce::Result::fail("stepCompose.invalidRecipe: Manual mask needs one0/1 value per step");
        mask = recipe.manual;
        break;
    case StepComposeMode::everyN:
        if (recipe.every < 1 || recipe.every > count)
            return juce::Result::fail("stepCompose.invalidRecipe: Every-N interval must be1..sequenceSteps");
        for (int step = 0; step < count; step += recipe.every)
            mask[size_t(step)] = 1;
        break;
    case StepComposeMode::euclidean:
        if (recipe.pulses < 0 || recipe.pulses > count)
            return juce::Result::fail("stepCompose.invalidRecipe: Euclidean count must be0..sequenceSteps");
        // Place the first hit at zero and distribute subsequent onsets using
        // floor(k*N/K). Cyclic gaps are floor(N/K) or ceil(N/K).
        for (int hit = 0; hit < recipe.pulses; ++hit)
            mask[size_t(hit * count / recipe.pulses)] = 1;
        break;
    default:
        return juce::Result::fail("stepCompose.invalidRecipe: Unknown composition mode");
    }
    if (recipe.dice)
    {
        const int hits = int(std::count(mask.begin(), mask.end(), std::uint8_t(1)));
        std::vector<int> cells(size_t(count), 0);
        for (int cell = 0; cell < count; ++cell)
            cells[size_t(cell)] = cell;
        std::uint64_t state = recipe.seed;
        for (int cell = count - 1; cell > 0; --cell)
        {
            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            const auto selected = size_t((state >> 32) % std::uint64_t(cell + 1));
            std::swap(cells[size_t(cell)], cells[selected]);
        }
        std::fill(mask.begin(), mask.end(), 0);
        for (int hit = 0; hit < hits; ++hit)
            mask[size_t(cells[size_t(hit)])] = 1;
    }
    const int rotation = (recipe.rotation % count + count) % count;
    if (rotation != 0)
        std::rotate(mask.begin(), mask.end() - rotation, mask.end());
    output = std::move(mask);
    return juce::Result::ok();
}

// Atomic prepared-copy API: all target/mask/source and expanded-note budgets
// pass before allocating new note IDs; any failure leaves project/allocator exact.
// Replacement window is [0,sequenceSteps*.25), even for a shorter manual Pattern.
// Notes at/after its end remain exact. Loop=false removes that part's loop and
// may therefore reveal previously dormant data outside the replacement window.
inline juce::Result applyStepComposition(Project &project, Id patternId,
                                         const std::vector<StepComposeRecipe> &recipes,
                                         bool retainSteps = false)
{
    const auto *source = project.pattern(patternId);
    if (!source)
        return juce::Result::fail("stepCompose.missingTarget: Existing Pattern required");
    if (recipes.empty() || recipes.size() > 128)
        return juce::Result::fail("stepCompose.invalidTargets: Choose1..128 distinct score Channels");
    if (auto valid = project.validate(); valid.failed())
        return juce::Result::fail("stepCompose.invalidSource: " + valid.getErrorMessage());
    std::map<Id, size_t> indices;
    std::vector<std::vector<std::uint8_t>> masks(recipes.size()), occupied(recipes.size());
    for (size_t index = 0; index < recipes.size(); ++index)
    {
        const auto &recipe = recipes[index];
        const auto *channel = project.channel(recipe.channel);
        if (!channel || channel->audioClip)
            return juce::Result::fail(
                "stepCompose.missingTarget: Existing playable instrument Channel required");
        if (!indices.emplace(recipe.channel, index).second)
            return juce::Result::fail("stepCompose.duplicateChannel: Each Channel may have only one recipe");
        if (auto result = stepCompositionMask(recipe, masks[index]); result.failed())
            return result;
        occupied[index].assign(size_t(recipe.sequenceSteps), 0);
    }
    std::vector<Note> notes;
    notes.reserve(source->notes.size());
    for (auto note : source->notes)
    {
        const auto target = indices.find(note.channel);
        if (target == indices.end() || note.beat >= recipes[target->second].sequenceSteps * .25)
        {
            notes.push_back(note);
            continue;
        }
        const size_t index = target->second;
        const auto &recipe = recipes[index];
        const bool convert = !note.step || note.beat * 4 != std::floor(note.beat * 4);
        if (convert && !recipe.convertMelodic)
            return juce::Result::fail("stepCompose.melodicConversionRequired: Explicit conversion required "
                                      "for in-cycle melodic/off-grid notes");
        const int cell = int(std::floor(note.beat * 4.));
        if (!masks[index][size_t(cell)] && !retainSteps)
            continue;
        if (convert)
        {
            note.beat = cell * .25;
            note.length = .25;
            note.step = true;
        }
        if (retainSteps)
            note.muted = !masks[index][size_t(cell)];
        // Existing on-grid step notes retain all other properties, including longer
        // duration, chords, mute and zero velocity. Rotation moves the mask;
        // existing properties remain at surviving destination cells.
        occupied[index][size_t(cell)] = masks[index][size_t(cell)];
        notes.push_back(note);
    }
    size_t additional = 0;
    for (size_t index = 0; index < recipes.size(); ++index)
        for (size_t cell = 0; cell < masks[index].size(); ++cell)
            additional += masks[index][cell] && !occupied[index][cell] ? 1 : 0;
    if (notes.size() > 100000 || additional > 100000 - notes.size())
        return juce::Result::fail("stepCompose.capacity: Composition would exceed100000 source notes");
    if (project.nextId < 1 || additional > std::uint64_t(std::numeric_limits<Id>::max() - project.nextId))
        return juce::Result::fail("stepCompose.allocatorExhausted: Not enough stable note identities");
    notes.reserve(notes.size() + additional);
    for (size_t index = 0; index < recipes.size(); ++index)
        for (size_t cell = 0; cell < masks[index].size(); ++cell)
            if (masks[index][cell] && !occupied[index][cell])
                notes.push_back({0, recipes[index].channel, cell * .25, .25, 60, .8f, true});
    Project candidate = project;
    auto *pattern = candidate.pattern(patternId);
    pattern->notes = std::move(notes);
    for (const auto &recipe : recipes)
    {
        auto loop = std::find_if(pattern->channelLoops.begin(), pattern->channelLoops.end(),
                                 [&](const auto &value) { return value.channel == recipe.channel; });
        if (!recipe.loop)
        {
            if (loop != pattern->channelLoops.end())
                pattern->channelLoops.erase(loop);
        }
        else
        {
            const ChannelLoop value{recipe.channel, ChannelLoopMode::fixed, recipe.sequenceSteps * .25};
            if (loop == pattern->channelLoops.end())
                pattern->channelLoops.push_back(value);
            else
                *loop = value;
        }
    }
    refreshAutomaticPatternLength(candidate, *pattern);
    // This helper checks timing/loop owners and expanded occurrence budgets,
    // not note identity uniqueness. Fresh notes intentionally still have id0;
    // full Project identity validation follows allocation below.
    if (auto result = validateChannelLoops(candidate, *pattern); result.failed())
        return juce::Result::fail("stepCompose.capacityOrLoop: " + result.getErrorMessage());
    for (auto &note : pattern->notes)
        if (note.id == 0)
            note.id = candidate.allocate();
    if (auto result = candidate.validate(); result.failed())
        return juce::Result::fail("stepCompose.invalidCandidate: " + result.getErrorMessage());
    project = std::move(candidate);
    return juce::Result::ok();
}
} // namespace vibera
