// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "LiveTimeline.h"
#include "Tuning.h"

// Channel tuning tables: Scala parsing, maqam presets, the canonical command with
// persistence, and the pitch ratio the local sampler receives per note.
namespace channel_tuning_acceptance
{
using namespace vibera;

inline void tests(const juce::File &)
{
    auto same = [](double a, double b) { return std::abs(a - b) < 1.e-6; };
    {
        tuning::Table table{};
        juce::String equal = "! equal.scl\n12-TET\n 12\n!\n";
        for (int i = 1; i <= 12; ++i)
            equal << juce::String(i * 100.) << ".0\n";
        require(tuning::parseScala(equal, 0, table).wasOk() &&
                    std::all_of(table.begin(), table.end(), [&](double c) { return same(c, 0); }),
                "A 12-TET Scala file is equal temperament");
        const juce::String just = "Just\n12\n16/15\n9/8\n6/5\n5/4\n4/3\n45/32\n3/2\n8/5\n5/3\n9/5\n15/8\n2/1\n";
        require(tuning::parseScala(just, 2, table).wasOk(), "A just-intonation ratio scale parses");
        require(same(table[2], 0) && same(table[6], 1200 * std::log2(5. / 4) - 400) &&
                    same(table[9], 1200 * std::log2(3. / 2) - 700),
                "Ratios map onto keys from the tonic (D: F# is the 5/4, A the 3/2)");
        const juce::String seven = "Seven\n7\n200.\n400.\n500.\n700.\n900.\n1100.\n2/1\n";
        require(tuning::parseScala(seven, 0, table).failed(), "Non-12-note scales are refused");
        require(tuning::maqamTable("rast", 0, table) && same(table[4], -50) && same(table[11], -50) &&
                    same(table[0], 0),
                "Rast on C flattens E and B by 50 cents");
        require(tuning::maqamTable("sikah", 4, table) && same(table[4], -50) && same(table[11], -50),
                "Sikah on E: its tonic E and the fifth degree B are half flat");
        require(!tuning::maqamTable("unknown", 0, table), "Unknown presets are refused");
    }
    auto project = Project::empty();
    const auto channel = project.channels.back().id;
    const auto pattern = project.patterns[0].id;
    project.patterns[0].notes = {{project.allocate(), channel, 0, 1, 64, .8f}, {project.allocate(), channel, 1, 1, 60, .8f}};
    project.patterns[0].notes[0].fineCents = -20;
    Controller controller;
    controller.replace(project);
    CommandArgs set;
    set.channel = channel;
    tuning::Table rast;
    tuning::maqamTable("rast", 0, rast);
    set.tuning = rast;
    const auto before = controller.project.state();
    require(controller.execute("channel.tuning", set).wasOk(), "Channel tuning applies");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "Tuning is one exact Undo step");
    controller.redoEdit();
    auto bad = set;
    bad.tuning->at(3) = 150;
    require(controller.execute("channel.tuning", bad).failed(), "Offsets beyond a semitone are refused");
    {
        Project reopened;
        require(Project::parse(controller.project.state(), reopened).wasOk() &&
                    same(reopened.channel(channel)->tuning[4], -50),
                "Tuning survives save and reopen");
        auto old = controller.project.state().createCopy();
        old.setProperty("version", 46, nullptr);
        require(Project::parse(old, reopened).failed(), "A version-46 file cannot carry tuning");
    }
    auto document = compileLiveDocument(controller.project, true, pattern, 1);
    require(document != nullptr, "Live document compiles");
    const auto *live = document->find(channel);
    require(live && live->notes.size() == 2, "Both notes compiled");
    for (const auto &note : live->notes)
    {
        const double expected = note.pitch == 64 ? std::pow(2., (-20 - 50) / 1200.) : 1.;
        require(same(note.expression.tuning, expected),
                "The sampler receives the note's own cents plus the Channel's pitch-class offset");
    }

    // Keyboard-mapped scales of any size: 24-EDO from key 60, consecutive keys 50 cents apart.
    {
        juce::String text = "! 24-tet.scl\n24-tone equal temperament\n 24\n!\n";
        for (int i = 1; i <= 24; ++i)
            text << juce::String(i * 50.0, 1) << "\n";
        std::vector<double> degrees;
        require(tuning::parseScalaDegrees(text, degrees).wasOk() && degrees.size() == 24 && same(degrees.back(), 1200),
                "A 24-note Scala file parses");
        require(tuning::parseScala(text, 0, rast).failed(), "The 12-tone table loader still refuses it");
        require(same(tuning::scaleDeviation(degrees, 60, 61), -50) && same(tuning::scaleDeviation(degrees, 60, 72), -600) &&
                    same(tuning::scaleDeviation(degrees, 60, 84), -1200) && same(tuning::scaleDeviation(degrees, 60, 59), 50) &&
                    same(tuning::scaleDeviation(degrees, 60, 60), 0),
                "Consecutive keys step one degree; the root plays unchanged");
        const auto before = controller.project.state();
        CommandArgs scale;
        scale.channel = channel;
        scale.scale = degrees;
        scale.pitch = 60;
        require(controller.execute("channel.scale", scale).wasOk(), "Map the scale to the Channel");
        controller.undoEdit();
        require(controller.project.state().isEquivalentTo(before), "Mapping is one exact Undo step");
        controller.redoEdit();
        CommandArgs move;
        move.pattern = pattern;
        Project reopened;
        require(Project::parse(controller.project.state(), reopened).wasOk() && reopened.channel(channel)->scale.size() == 24,
                "The scale survives save and reopen");
        auto old = controller.project.state().createCopy();
        old.setProperty("version", 52, nullptr);
        require(Project::parse(old, reopened).failed(), "A version-52 file cannot carry a mapped scale");
        auto *part = controller.project.pattern(pattern);
        auto mapped = controller.project;
        mapped.pattern(pattern)->notes = {part->notes.front()};
        mapped.pattern(pattern)->notes[0].pitch = 72;
        mapped.pattern(pattern)->notes[0].fineCents = 0;
        auto liveDocument = compileLiveDocument(mapped, true, pattern, 2);
        const auto *liveChannel = liveDocument ? liveDocument->find(channel) : nullptr;
        require(liveChannel && liveChannel->notes.size() == 1 && liveChannel->notes[0].pitch == 66 &&
                    same(liveChannel->notes[0].expression.tuning, 1.),
                "Key 72 plays 6 semitones up (12 quarter tones) with no remainder");
        CommandArgs bad = scale;
        bad.scale = std::vector<double>{100, 50};
        require(controller.execute("channel.scale", bad).failed(), "Falling degrees are refused");
        CommandArgs table;
        table.channel = channel;
        table.tuning = rast;
        require(controller.execute("channel.tuning", table).wasOk() && controller.project.channel(channel)->scale.empty(),
                "A 12-tone table replaces the mapped scale");
    }
}
} // namespace channel_tuning_acceptance
