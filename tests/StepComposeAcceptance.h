// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepCompose.h"
#include <limits>
#include <set>
#include <type_traits>

static void stepComposeTests()
{
    static_assert(std::is_same_v<decltype(StepComposeRecipe{}.seed), std::uint32_t>);
    StepComposeRecipe recipe;
    recipe.sequenceSteps = 8;
    recipe.every = 3;
    std::vector<std::uint8_t> mask;
    require(stepCompositionMask(recipe, mask).wasOk() &&
                mask == std::vector<std::uint8_t>{1, 0, 0, 1, 0, 0, 1, 0},
            "Every-N uses source origin zero and includes the partial final interval");
    recipe.rotation = 2;
    require(stepCompositionMask(recipe, mask).wasOk() &&
                mask == std::vector<std::uint8_t>{1, 0, 1, 0, 0, 1, 0, 0},
            "Positive rotation shifts hits toward later cells and wraps");
    recipe.rotation = -6;
    std::vector<std::uint8_t> equivalent;
    require(stepCompositionMask(recipe, equivalent).wasOk() && equivalent == mask,
            "Negative rotations normalize to the same cyclic mask");
    recipe.mode = StepComposeMode::euclidean;
    recipe.rotation = 0;
    recipe.pulses = 3;
    require(stepCompositionMask(recipe, mask).wasOk() &&
                mask == std::vector<std::uint8_t>{1, 0, 1, 0, 0, 1, 0, 0},
            "Original Euclidean phase has literal three-of-eight golden mask");
    for (int length = 1; length <= 32; ++length)
        for (int hits = 0; hits <= length; ++hits)
        {
            recipe.sequenceSteps = length;
            recipe.pulses = hits;
            require(stepCompositionMask(recipe, mask).wasOk() &&
                        std::count(mask.begin(), mask.end(), std::uint8_t(1)) == hits,
                    "Euclidean count exact for empty/full and short cycles");
            if (hits > 0)
            {
                std::vector<int> cells;
                for (int cell = 0; cell < length; ++cell)
                    if (mask[size_t(cell)])
                        cells.push_back(cell);
                int minimum = length, maximum = 0;
                for (size_t hit = 0; hit < cells.size(); ++hit)
                {
                    const int next = hit + 1 == cells.size() ? cells.front() + length : cells[hit + 1];
                    minimum = std::min(minimum, next - cells[hit]);
                    maximum = std::max(maximum, next - cells[hit]);
                }
                require(maximum - minimum <= 1, "Euclidean cyclic gaps differ by at most one cell");
            }
        }
    recipe.sequenceSteps = 17;
    recipe.pulses = 5;
    recipe.dice = true;
    recipe.seed = 123456789;
    require(stepCompositionMask(recipe, mask).wasOk() && stepCompositionMask(recipe, equivalent).wasOk() &&
                equivalent == mask && std::count(mask.begin(), mask.end(), std::uint8_t(1)) == 5,
            "Seeded Dice is reproducible and preserves exact generated hit count");
    std::set<std::vector<std::uint8_t>> variations;
    for (std::uint32_t seed = 0; seed < 8; ++seed)
    {
        recipe.seed = seed;
        require(stepCompositionMask(recipe, equivalent).wasOk(), "Bounded Dice seed accepted");
        variations.insert(equivalent);
    }
    require(variations.size() > 1, "Explicit Dice seed has an actual musical effect");
    recipe.seed = std::numeric_limits<std::uint32_t>::max();
    require(stepCompositionMask(recipe, mask).wasOk() && stepCompositionMask(recipe, equivalent).wasOk() &&
                mask == equivalent && std::count(mask.begin(), mask.end(), std::uint8_t(1)) == 5,
            "Maximum UI/MCP seed is accepted with deterministic64-bit generator state");
    for (int hits : {0, 17})
    {
        recipe.pulses = hits;
        recipe.rotation = std::numeric_limits<int>::min();
        require(stepCompositionMask(recipe, mask).wasOk() &&
                    std::count(mask.begin(), mask.end(), std::uint8_t(1)) == hits,
                "Empty/full Dice and extreme signed rotation stay bounded");
    }
    recipe = {};
    recipe.mode = StepComposeMode::manual;
    recipe.sequenceSteps = 4;
    recipe.manual = {0, 1, 0, 1};
    recipe.rotation = 1;
    require(stepCompositionMask(recipe, mask).wasOk() && mask == std::vector<std::uint8_t>{1, 0, 1, 0},
            "Manual mask and rotation are exact");
    const auto validMask = mask;
    recipe.manual[1] = 2;
    require(stepCompositionMask(recipe, mask).failed() && mask == validMask,
            "Invalid manual bit refuses without replacing output");
    recipe.manual = {1};
    require(stepCompositionMask(recipe, mask).failed() && mask == validMask,
            "Wrong manual mask size refuses without replacing output");
    recipe.mode = StepComposeMode(99);
    require(stepCompositionMask(recipe, mask).failed(), "Unknown mode refuses");
    for (int length : {0, 4097})
    {
        recipe.sequenceSteps = length;
        require(stepCompositionMask(recipe, mask).failed(), "Sequence bounds are strict");
    }
    recipe = {};
    recipe.mode = StepComposeMode::euclidean;
    recipe.sequenceSteps = 4096;
    recipe.pulses = 4096;
    require(stepCompositionMask(recipe, mask).wasOk() && mask.size() == 4096 &&
                std::count(mask.begin(), mask.end(), std::uint8_t(1)) == 4096,
            "Maximum source cycle and full pulse count remain bounded");
    for (int hits : {-1, 4097})
    {
        recipe.pulses = hits;
        require(stepCompositionMask(recipe, mask).failed(), "Invalid Euclidean count refuses");
    }

    auto project = Project::empty();
    auto &pattern = project.patterns.front();
    pattern.autoLength = false;
    pattern.length = 8;
    const Id patternId = pattern.id, channel = project.channels[0].id, other = project.channels[1].id;
    const Id kept = project.allocate(), chord = project.allocate(), muted = project.allocate(),
             removed = project.allocate(), dormant = project.allocate(), unrelated = project.allocate();
    pattern.notes = {
        {kept, channel, 0, .75, 64, .37f, true},        {chord, channel, 0, .25, 72, .63f, true},
        {muted, channel, .5, .5, 68, 0.f, true, true},  {removed, channel, .25, .25, 61, .2f, true},
        {dormant, channel, 1, 2, 51, .4f, false, true}, {unrelated, other, .125, 2, 55, .8f, false}};
    const auto before = project.state();
    recipe = {};
    recipe.channel = channel;
    recipe.sequenceSteps = 4;
    recipe.every = 2;
    const Id allocator = project.nextId;
    require(applyStepComposition(project, patternId, {recipe}).wasOk(), "Compose source cycle");
    const auto *composed = project.pattern(patternId);
    const auto lookup = [](const Pattern &p, Id id) -> const Note *
    {
        const auto found =
            std::find_if(p.notes.begin(), p.notes.end(), [&](const auto &note) { return note.id == id; });
        return found == p.notes.end() ? nullptr : &*found;
    };
    const auto *keptNote = lookup(*composed, kept);
    const auto *mutedNote = lookup(*composed, muted);
    require(composed->notes.size() == 5 && keptNote && keptNote->length == .75 && keptNote->pitch == 64 &&
                keptNote->velocity == .37f && lookup(*composed, chord) && mutedNote && mutedNote->muted &&
                mutedNote->velocity == 0 && !lookup(*composed, removed) &&
                lookup(*composed, dormant)->beat == 1 && lookup(*composed, dormant)->length == 2 &&
                !lookup(*composed, dormant)->step && lookup(*composed, unrelated)->beat == .125 &&
                project.nextId == allocator,
            "Surviving step IDs/duration/chords/mute/velocity and outside-cycle/other parts stay exact");
    require(composed->length == 8 && !composed->autoLength && channelLoop(*composed, channel) &&
                channelLoopLength(project, *composed, channel) == 1 &&
                rawPatternOccurrences(project, *composed).size() == 25,
            "Short source cycle repeats non-destructively; dormant source is not baked");
    const auto once = project.state();
    require(!once.isEquivalentTo(before), "Composition made the intended source/loop edit");
    require(applyStepComposition(project, patternId, {recipe}).wasOk() &&
                project.state().isEquivalentTo(once),
            "Identical composition is exact and allocates no identities");
    auto malformed = recipe;
    malformed.channel = other;
    malformed.every = 0;
    require(applyStepComposition(project, patternId, {recipe, malformed}).failed() &&
                project.state().isEquivalentTo(once),
            "Invalid later recipe cannot partially commit an earlier Channel");
    require(applyStepComposition(project, patternId, {recipe, recipe}).failed() &&
                project.state().isEquivalentTo(once),
            "Duplicate target refuses atomically");
    malformed = recipe;
    malformed.channel = 999999;
    require(applyStepComposition(project, patternId, {malformed}).failed() &&
                applyStepComposition(project, 999999, {recipe}).failed() &&
                applyStepComposition(project, patternId, {}).failed() && project.state().isEquivalentTo(once),
            "Missing targets and empty batch preserve document/allocator");
    recipe.loop = false;
    require(applyStepComposition(project, patternId, {recipe}).wasOk() &&
                !channelLoop(*project.pattern(patternId), channel) &&
                lookup(*project.pattern(patternId), dormant),
            "Loop off removes only target metadata and retains outside-cycle source data");

    auto melodic = Project::empty();
    const Id melodicPattern = melodic.patterns[0].id, melodicChannel = melodic.channels[0].id;
    const Id melodicId = melodic.allocate(), offGridId = melodic.allocate();
    melodic.patterns[0].notes = {{melodicId, melodicChannel, .125, 2, 67, .4f, false, true},
                                 {offGridId, melodicChannel, .625, 1, 72, .7f, true}};
    const auto melodicBefore = melodic.state();
    recipe = {};
    recipe.channel = melodicChannel;
    recipe.sequenceSteps = 4;
    recipe.every = 2;
    require(applyStepComposition(melodic, melodicPattern, {recipe}).failed() &&
                melodic.state().isEquivalentTo(melodicBefore),
            "Melodic/off-grid conversion requires explicit consent and is atomic");
    recipe.convertMelodic = true;
    require(applyStepComposition(melodic, melodicPattern, {recipe}).wasOk(),
            "Explicit conversion admits in-cycle melody");
    const auto *converted = melodic.pattern(melodicPattern);
    require(lookup(*converted, melodicId)->beat == 0 && lookup(*converted, melodicId)->length == .25 &&
                lookup(*converted, melodicId)->step && lookup(*converted, melodicId)->muted &&
                lookup(*converted, melodicId)->pitch == 67 && lookup(*converted, offGridId)->beat == .5 &&
                lookup(*converted, offGridId)->step,
            "Conversion floor-aligns and normalizes lengths while preserving identity/music/mute");

    auto fresh = Project::empty();
    const Id freshPattern = fresh.patterns[0].id;
    StepComposeRecipe first, second;
    first.channel = fresh.channels[0].id;
    first.sequenceSteps = 3;
    first.every = 2;
    second = first;
    second.channel = fresh.channels[1].id;
    second.mode = StepComposeMode::euclidean;
    second.pulses = 0;
    const Id firstId = fresh.nextId;
    require(applyStepComposition(fresh, freshPattern, {first, second}).wasOk() &&
                fresh.patterns[0].notes.size() == 2 && fresh.nextId == firstId + 2 &&
                fresh.patterns[0].notes[0].id == firstId && fresh.patterns[0].notes[1].id == firstId + 1 &&
                fresh.patterns[0].notes[1].beat == .5 && fresh.patterns[0].notes[0].pitch == 60 &&
                fresh.patterns[0].notes[0].velocity == .8f && fresh.patterns[0].channelLoops.size() == 2,
            "Multi-Channel Accept creates source hits/defaults only; empty loop is valid");
    require(fresh.patterns[0].autoLength && fresh.patterns[0].length == 4,
            "Auto extent reuses current musical bar policy instead of feeding repeated hits back");

    auto clipped = Project::empty();
    Channel audio;
    audio.id = clipped.allocate();
    audio.name = "Unavailable source clip";
    audio.source = "E:/VibeRa-owned-missing-test.wav";
    audio.audioClip = true;
    audio.audioDuration = 1;
    clipped.channels.push_back(audio);
    first.channel = clipped.channels[0].id;
    second.channel = audio.id;
    const auto clippedBefore = clipped.state();
    require(applyStepComposition(clipped, clipped.patterns[0].id, {first, second}).failed() &&
                clipped.state().isEquivalentTo(clippedBefore),
            "Mixed instrument/Audio Clip batch refuses all targets without mutation");

    auto expanded = Project::empty();
    expanded.patterns[0].autoLength = false;
    expanded.patterns[0].length = 1000000;
    recipe = {};
    recipe.channel = expanded.channels[0].id;
    recipe.sequenceSteps = 1;
    recipe.every = 1;
    const auto expandedBefore = expanded.state();
    require(applyStepComposition(expanded, expanded.patterns[0].id, {recipe}).failed() &&
                expanded.state().isEquivalentTo(expandedBefore),
            "Expanded occurrence budget refuses before allocating source identities");
    expanded.patterns[0].length = 25000;
    const auto expansionAllocator = expanded.nextId;
    require(applyStepComposition(expanded, expanded.patterns[0].id, {recipe}).wasOk() &&
                expanded.patterns[0].notes.size() == 1 && expanded.nextId == expansionAllocator + 1 &&
                channelLoopLength(expanded, expanded.patterns[0], recipe.channel) == .25,
            "Exactly100000 expanded occurrences admit one fresh source note with no baked copies");

    auto capacity = Project::empty();
    auto &crowded = capacity.patterns[0];
    crowded.autoLength = false;
    crowded.length = 4;
    for (int index = 0; index < 100000; ++index)
        crowded.notes.push_back({capacity.allocate(), capacity.channels[1].id, 0, .25, 60, .8f, true});
    recipe.channel = capacity.channels[0].id;
    recipe.loop = false;
    const auto capacityBefore = capacity.state();
    require(applyStepComposition(capacity, crowded.id, {recipe}).failed() &&
                capacity.state().isEquivalentTo(capacityBefore),
            "Source budget includes untouched Channels and refuses atomically");
    recipe.mode = StepComposeMode::euclidean;
    recipe.pulses = 0;
    require(applyStepComposition(capacity, capacity.patterns[0].id, {recipe}).wasOk() &&
                capacity.state().isEquivalentTo(capacityBefore),
            "Exactly100000 unchanged source notes are admitted when the recipe adds nothing");

    auto exhausted = Project::empty();
    exhausted.nextId = std::numeric_limits<Id>::max();
    recipe.mode = StepComposeMode::everyN;
    recipe.channel = exhausted.channels[0].id;
    const auto exhaustedBefore = exhausted.state();
    require(applyStepComposition(exhausted, exhausted.patterns[0].id, {recipe}).failed() &&
                exhausted.state().isEquivalentTo(exhaustedBefore),
            "Exhausted stable-ID allocator refuses without signed overflow");
    auto exhaustedNoop = Project::empty();
    exhaustedNoop.patterns[0].autoLength = false;
    recipe.channel = exhaustedNoop.channels[0].id;
    exhaustedNoop.patterns[0].notes = {{exhaustedNoop.allocate(), recipe.channel, 0, .25, 60, .8f, true}};
    exhaustedNoop.nextId = std::numeric_limits<Id>::max();
    const auto noopBefore = exhaustedNoop.state();
    require(applyStepComposition(exhaustedNoop, exhaustedNoop.patterns[0].id, {recipe}).wasOk() &&
                exhaustedNoop.state().isEquivalentTo(noopBefore),
            "No-op needs no fresh identity even when the allocator is exhausted");
}
