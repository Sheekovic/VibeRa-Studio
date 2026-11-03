// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "Guide.h"

// Song time-signature changes: they start on bar lines, bar numbering and bar lines follow
// them, a change cannot strand a later one off its bar line, one Undo each, project v54.
namespace song_meter_acceptance
{
using namespace vibera;

inline void tests(const juce::File &)
{
    Controller controller;
    controller.replace(Project::empty()); // 4/4
    auto run = [&](const char *id, double beat, int numerator, int denominator, Id target = 0)
    {
        CommandArgs a;
        a.beat = beat;
        a.numerator = numerator;
        a.denominator = denominator;
        a.target = target;
        return controller.execute(id, a);
    };
    const auto empty = controller.project.state();
    require(run("meter.change.add", 8, 3, 4).wasOk(), "3/4 from bar 3 (beat 8)");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(empty), "Adding is one exact Undo step");
    controller.redoEdit();
    const auto &p = controller.project;
    require(p.barPosition(8).bar == 2 && p.barPosition(11).bar == 3 && std::abs(p.barPosition(11).beatInBar) < 1.e-9 &&
                p.barPosition(12.5).bar == 3 && std::abs(p.barPosition(12.5).beatInBar - 1.5) < 1.e-9 &&
                p.barPosition(12.5).numerator == 3 && p.barPosition(4).barLength == 4,
            "Bars count 4/4 then 3/4");
    require(run("meter.change.add", 9, 6, 8).failed(), "A change off a bar line is refused");
    require(run("meter.change.add", 14, 6, 8).wasOk(), "6/8 from beat 14 (two 3/4 bars later)");
    require(std::abs(controller.project.nextBarLine(12) - 14) < 1.e-9 &&
                std::abs(controller.project.barPosition(17).beatInBar) < 1.e-9,
            "Next bar line and 6/8 bars (3 beats each)");
    const Id first = controller.project.meterChanges[0].id;
    require(run("meter.change.set", 8, 7, 8, first).failed(),
            "7/8 at beat 8 would leave the 6/8 change off its bar line");
    require(run("meter.change.add", 8, 2, 4).failed(), "Only one change per beat");
    require(run("meter.change.add", 20, 5, 3).failed(), "Denominators are 2, 4, 8 or 16");
    Project saved;
    require(Project::parse(controller.project.state(), saved).wasOk() && saved.meterChanges.size() == 2 &&
                saved.meterChanges[1].numerator == 6 && saved.meterChanges[1].denominator == 8,
            "Time-signature changes survive save/load");
    auto older = controller.project.state();
    older.setProperty("version", 53, nullptr);
    require(Project::parse(older, saved).failed(), "Time-signature changes require project version 54");
    const Id second = controller.project.meterChanges[1].id;
    require(run("meter.change.delete", 0, 4, 4, second).wasOk() && controller.project.meterChanges.size() == 1,
            "Delete a change");
    require(run("meter.change.delete", 0, 4, 4, 999999).failed(), "Unknown changes are refused");

    // Metronome: 4/4 then 3/4 from beat 8 then 6/8 from beat 14 at 120 BPM. Quarter ticks,
    // then eighth ticks in 6/8; accents on every bar's first tick (beats 0, 4, 8, 11, 14, 17).
    {
        MeterMap meters;
        meters.changes = {{8, 3, 4}, {14, 6, 8}};
        GuideControl control;
        control.enabled = true;
        GuideDsp dsp;
        juce::AudioBuffer<float> buffer(2, 256);
        std::vector<std::pair<double, bool>> ticks; // (beat, accent)
        const int total = 48000 * 10; // 20 beats
        for (int frame = 0; frame < total; frame += 256)
        {
            buffer.clear();
            dsp.process(buffer, 0, 256, frame / 48000., 48000, true, 120, 4, 4, control, nullptr, &meters);
            for (int i = 0; i < dsp.hitCount; ++i)
                ticks.push_back({(frame + dsp.lastHits[size_t(i)].frame) / 24000., dsp.lastHits[size_t(i)].accent});
        }
        std::vector<double> accents, beats;
        for (const auto &[beat, accent] : ticks)
        {
            beats.push_back(std::round(beat * 2) / 2);
            if (accent)
                accents.push_back(std::round(beat * 2) / 2);
        }
        require(accents == std::vector<double>{0, 4, 8, 11, 14, 17}, "Accents fall on every bar line across changes");
        require(std::count_if(beats.begin(), beats.end(), [](double b) { return b >= 8 && b < 14; }) == 6 &&
                    std::count_if(beats.begin(), beats.end(), [](double b) { return b >= 14 && b < 20; }) == 12,
                "Quarter ticks in 3/4, eighth ticks in 6/8");
    }
}
} // namespace song_meter_acceptance
