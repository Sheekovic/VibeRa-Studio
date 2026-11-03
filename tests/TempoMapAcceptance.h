// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AudioEngine.h"
#include "Commands.h"
#include "TempoMap.h"

// Song tempo map: exact jump/ramp integration and inversion, canonical commands with
// persistence and Undo, and rendered Song timing through tempo changes.
namespace tempo_map_acceptance
{
using namespace vibera;

inline juce::AudioBuffer<float> render(AudioEngine &engine, const Project &project, const juce::File &file)
{
    require(engine.renderSync(project, file).wasOk(), "Song renders through the tempo map");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    require(reader != nullptr, "Rendered WAV readable");
    juce::AudioBuffer<float> pcm(int(reader->numChannels), int(reader->lengthInSamples));
    reader->read(&pcm, 0, int(reader->lengthInSamples), 0, true, true);
    return pcm;
}
// First frame after `from` where the signal rises above the threshold.
inline int onset(const juce::AudioBuffer<float> &pcm, int from)
{
    for (int i = std::max(0, from); i < pcm.getNumSamples(); ++i)
        if (std::abs(pcm.getSample(0, i)) > .02f)
            return i;
    return -1;
}

inline void tests(const juce::File &folder)
{
    auto same = [](double a, double b, double tolerance = 1.e-9) { return std::abs(a - b) < tolerance; };
    {
        const TempoMap constant(120, {});
        require(constant.constant() && same(constant.secondsAt(8), 4) && same(constant.beatAt(3), 6) &&
                    same(constant.secondsAt(-2), -1),
                "Without changes the map is the base tempo, pre-roll included");
        const TempoMap jump(120, {{4, 60, false}});
        require(same(jump.secondsAt(4), 2) && same(jump.secondsAt(8), 6) && same(jump.beatAt(6), 8) &&
                    same(jump.bpmAt(3.99), 120) && same(jump.bpmAt(4), 60),
                "A jump changes tempo exactly at its beat");
        const TempoMap ramp(120, {{4, 60, true}});
        double numeric = 0;
        for (int i = 0; i < 400000; ++i)
        {
            const double b = (i + .5) * 4. / 400000;
            numeric += 60. / (120. - 15. * b) * (4. / 400000);
        }
        require(same(ramp.secondsAt(4), numeric, 1.e-6) && same(ramp.secondsAt(4), 60. / -15. * std::log(.5), 1.e-12),
                "A ramp integrates exactly (matches numeric integration)");
        require(same(ramp.bpmAt(2), 90) && same(ramp.secondsAt(8), ramp.secondsAt(4) + 4),
                "The ramp reaches its tempo at the point and holds it after");
        for (double b : {0.5, 2.0, 3.9, 4.0, 7.25})
            require(same(ramp.beatAt(ramp.secondsAt(b)), b, 1.e-9), "beatAt inverts secondsAt through ramps");
    }
    {
        Controller controller;
        controller.replace(Project::empty());
        const auto empty = controller.project.state();
        CommandArgs add;
        add.beat = 8;
        add.value = 90;
        require(controller.execute("tempo.change.add", add).wasOk(), "Tempo change added");
        controller.undoEdit();
        require(controller.project.state().isEquivalentTo(empty), "Adding a tempo change is one exact Undo step");
        controller.redoEdit();
        add.beat = 4;
        add.value = 140;
        add.step = true;
        require(controller.execute("tempo.change.add", add).wasOk(), "Ramped tempo change added");
        require(controller.project.tempoChanges.size() == 2 && controller.project.tempoChanges[0].beat == 4 &&
                    controller.project.tempoChanges[0].ramp,
                "Tempo changes stay sorted by beat");
        require(controller.execute("tempo.change.add", add).failed(), "Two changes at one beat are refused");
        add.beat = 0;
        require(controller.execute("tempo.change.add", add).failed(), "Beat 0 belongs to the base tempo");
        add.beat = 12;
        add.value = 2000;
        require(controller.execute("tempo.change.add", add).failed(), "Out-of-range BPM refused");
        CommandArgs set;
        set.target = controller.project.tempoChanges[1].id;
        set.beat = 2;
        set.value = 100;
        require(controller.execute("tempo.change.set", set).wasOk() && controller.project.tempoChanges[0].beat == 2 &&
                    !controller.project.tempoChanges[0].ramp,
                "Editing a change re-sorts it");
        Project reopened;
        require(Project::parse(controller.project.state(), reopened).wasOk() && reopened.tempoChanges.size() == 2 &&
                    reopened.tempoChanges[1].ramp,
                "Tempo changes survive save and reopen");
        auto old = controller.project.state().createCopy();
        old.setProperty("version", 44, nullptr);
        require(Project::parse(old, reopened).failed(), "A version-44 file cannot carry tempo changes");
        require(same(controller.project.audioEndBeat(0, 1), 2) &&
                    same(controller.project.audioEndBeat(2, 1), controller.project.songTempo().beatAt(
                                                                   controller.project.songTempo().secondsAt(2) + 1)),
                "Audio Clip spans follow the map");
    }
    {
        // A kick on beat 0 of a one-bar Pattern placed at beats 0, 4 and 8. At 120 BPM with
        // 60 BPM from beat 4 the hits are at 0 s, 2 s and 6 s; with a ramp into 60 BPM the
        // second hit moves to 60/-15*ln(1/2) s.
        auto project = Project::empty();
        project.tempo = 120;
        const auto pattern = project.patterns[0].id;
        const auto kick = project.channels[0].id;
        project.patterns[0].length = 4;
        project.patterns[0].notes.push_back({project.allocate(), kick, 0, .25, 60, 1.f, true});
        for (double beat : {0., 4., 8.})
            project.placements.push_back({project.allocate(), pattern, 0, beat, 4, 0});
        AudioEngine engine(false);
        const auto flat = render(engine, project, folder.getChildFile("tempo-flat.wav"));
        project.tempoChanges.push_back({project.allocate(), 4, 60, false});
        const auto jumped = render(engine, project, folder.getChildFile("tempo-jump.wav"));
        project.tempoChanges.back().ramp = true;
        const auto ramped = render(engine, project, folder.getChildFile("tempo-ramp.wav"));
        const int base = onset(flat, 0);
        require(base >= 0 && base < 2000, "The first kick starts the Song");
        auto lands = [&](const juce::AudioBuffer<float> &pcm, double seconds)
        {
            const int expected = int(std::round(seconds * 48000)) + base;
            const int found = onset(pcm, expected - 2400);
            return found >= 0 && std::abs(found - expected) <= 2;
        };
        require(lands(flat, 2) && lands(flat, 4), "Constant tempo: hits at 2 s and 4 s");
        require(lands(jumped, 2) && lands(jumped, 6) && jumped.getNumSamples() > flat.getNumSamples(),
                "A jump to 60 BPM at beat 4 moves the third hit to 6 s and lengthens the Song");
        const double rampSeconds = 60. / -15. * std::log(.5);
        require(lands(ramped, rampSeconds) && lands(ramped, rampSeconds + 4),
                "A ramp into 60 BPM places the hits at its exact integral");
    }
    {
        // Shaped curves: 16 ramps from the tempo at the start to the target, one Undo.
        Controller controller;
        controller.project.tempo = 100;
        CommandArgs curve;
        curve.beat = 8;
        curve.length = 16;
        curve.value = 140;
        curve.text = "ease";
        const auto before = controller.project.state();
        require(controller.execute("tempo.curve", curve).wasOk() && controller.project.tempoChanges.size() == 17,
                "tempo.curve writes an anchor and 16 ramps");
        const auto map = controller.project.songTempo();
        require(same(map.bpmAt(8), 100) && same(map.bpmAt(16), 120, 1.e-6) && same(map.bpmAt(24), 140) &&
                    same(map.bpmAt(40), 140) && map.bpmAt(10) < 100 + 40 * .25 * .5,
                "Ease: starts slowly, crosses the middle at half, ends on the target");
        controller.undoEdit();
        require(controller.project.state().isEquivalentTo(before), "A tempo curve is one Undo step");
        curve.text = "zigzag";
        require(controller.execute("tempo.curve", curve).failed(), "Unknown shapes are refused");

        // Freehand: a drawn automation clip becomes the tempo over its span.
        AutomationSource source;
        source.id = controller.project.allocate();
        source.length = 8;
        source.points = {{controller.project.allocate(), 0, 0.f}, {controller.project.allocate(), 8, 1.f}};
        controller.project.automation.sources.push_back(source);
        AutomationPlacement placed;
        placed.id = controller.project.allocate();
        placed.source = source.id;
        placed.beat = 4;
        placed.length = 8;
        controller.project.automation.placements.push_back(placed);
        CommandArgs drawn;
        drawn.target = placed.id;
        drawn.offset = 80;
        drawn.value = 160;
        const auto applied = controller.execute("tempo.fromAutomation", drawn);
        require(applied.wasOk(), "tempo.fromAutomation applies: " + applied.getErrorMessage().toStdString());
        const auto freehand = controller.project.songTempo();
        require(same(freehand.bpmAt(4), 80, 1.e-6) && same(freehand.bpmAt(8), 120, .5) &&
                    same(freehand.bpmAt(12), 160, .5) && controller.project.tempoChanges.size() < 12,
                "The drawn line maps bottom..top to 80..160 BPM with collinear samples merged");
        drawn.offset = 200;
        require(controller.execute("tempo.fromAutomation", drawn).failed(), "An inverted BPM range is refused");
    }
}
} // namespace tempo_map_acceptance
