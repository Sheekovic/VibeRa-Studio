// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <random>
#include <vector>

namespace vibera::chords
{
// Original chord progression generator. Diatonic chords follow common functional moves
// (a weighted degree-to-degree table); "adventure" blends that table toward free choice
// and raises the odds of seventh chords and borrowed chords from the parallel mode.
// Voicings move as little as possible from chord to chord around the requested octave.
struct Settings
{
    int key = 0;        // 0 = C .. 11 = B.
    int scale = 0;      // Index into scaleNames().
    int count = 4;      // Chords, 2..8.
    int octave = 4;     // Voicing centre octave (C4 = MIDI 60).
    double adventure = .3;
    std::uint32_t seed = 1;
};
struct Chord
{
    int degree = 0;              // 0..6 in the scale (borrowed chords keep their degree).
    bool borrowed = false;       // Taken from the parallel major/minor.
    bool seventh = false;
    int root = 0;                // Pitch class 0..11.
    std::vector<int> pitches;    // Voiced MIDI notes, low to high.
    juce::String name;
};

inline const juce::StringArray &scaleNames()
{
    static const juce::StringArray names{"Major (Ionian)", "Minor (Aeolian)", "Dorian", "Mixolydian", "Harmonic minor"};
    return names;
}
inline std::array<int, 7> scaleSteps(int scale)
{
    switch (scale)
    {
    case 1: return {0, 2, 3, 5, 7, 8, 10};
    case 2: return {0, 2, 3, 5, 7, 9, 10};
    case 3: return {0, 2, 4, 5, 7, 9, 10};
    case 4: return {0, 2, 3, 5, 7, 8, 11};
    default: return {0, 2, 4, 5, 7, 9, 11};
    }
}
inline juce::String noteName(int pitchClass)
{
    static const char *names[]{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return names[((pitchClass % 12) + 12) % 12];
}
// Name from the stacked intervals above the root.
inline juce::String chordName(int root, int third, int fifth, int seventh)
{
    juce::String quality;
    if (third == 4 && fifth == 7)
        quality = seventh < 0 ? "" : seventh == 11 ? "maj7" : "7";
    else if (third == 3 && fifth == 7)
        quality = seventh < 0 ? "m" : seventh == 10 ? "m7" : "m(maj7)";
    else if (third == 3 && fifth == 6)
        quality = seventh < 0 ? "dim" : seventh == 10 ? "m7b5" : "dim7";
    else if (third == 4 && fifth == 8)
        quality = "aug";
    else
        quality = "sus";
    return noteName(root) + quality;
}

class Generator
{
  public:
    explicit Generator(Settings s) : settings(s), random(s.seed) {}

    std::vector<Chord> generate()
    {
        std::vector<Chord> chords;
        const bool minorHome = settings.scale == 1 || settings.scale == 2 || settings.scale == 4;
        int degree = minorHome && chance(.35) ? 5 : 0; // Sometimes open on vi / VI for colour.
        if (chance(.75))
            degree = 0; // Most progressions start home.
        for (int i = 0; i < settings.count; ++i)
        {
            if (i > 0)
                degree = nextDegree(degree, i == settings.count - 1);
            chords.push_back(build(degree));
        }
        voice(chords);
        return chords;
    }
    // A different chord for slot `index`, keeping its neighbours.
    Chord reroll(const std::vector<Chord> &chords, size_t index)
    {
        const int previous = index > 0 ? chords[index - 1].degree : 0;
        Chord next;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            next = build(index == 0 ? pick({.5, .05, .05, .15, .1, .15, 0}) : nextDegree(previous, index + 1 == chords.size()));
            if (next.degree != chords[index].degree || next.seventh != chords[index].seventh)
                break;
        }
        auto copy = chords;
        copy[index] = next;
        voice(copy);
        return copy[index];
    }
    // Re-voice a whole list (after a single reroll) for smooth motion.
    void voice(std::vector<Chord> &chords) const
    {
        const int centre = 12 * (settings.octave + 1) + 4; // Around E of the octave.
        std::vector<int> previous;
        for (auto &chord : chords)
        {
            auto intervals = chordIntervals(chord);
            std::vector<int> best;
            double bestCost = 1e9;
            for (int inversion = 0; inversion < int(intervals.size()); ++inversion)
                for (int shift = -2; shift <= 1; ++shift)
                {
                    std::vector<int> v;
                    for (size_t k = 0; k < intervals.size(); ++k)
                    {
                        int interval = intervals[(size_t(inversion) + k) % intervals.size()];
                        if (k + size_t(inversion) >= intervals.size())
                            interval += 12;
                        v.push_back(12 * (settings.octave + shift + 1) + chord.root + interval);
                    }
                    double average = 0;
                    for (int p : v)
                        average += p;
                    average /= double(v.size());
                    double cost = std::abs(average - centre) * .6;
                    if (!previous.empty())
                        for (size_t k = 0; k < std::min(v.size(), previous.size()); ++k)
                            cost += std::abs(v[k] - previous[k]);
                    if (v.front() < 36 || v.back() > 96)
                        cost += 100;
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        best = v;
                    }
                }
            chord.pitches = best;
            previous = best;
        }
    }

  private:
    Settings settings;
    std::mt19937 random;

    bool chance(double p) { return std::uniform_real_distribution<double>(0, 1)(random) < p; }
    int pick(std::array<double, 7> weights)
    {
        double total = 0;
        for (double w : weights)
            total += w;
        double r = std::uniform_real_distribution<double>(0, total)(random);
        for (int i = 0; i < 7; ++i)
            if ((r -= weights[size_t(i)]) <= 0)
                return i;
        return 0;
    }
    int nextDegree(int from, bool last)
    {
        // Conventional functional moves (rows: from I..vii, columns: to I..vii).
        static constexpr double table[7][7]{
            {0, .1, .05, .3, .3, .25, 0},    {.15, 0, 0, .15, .6, 0, .1},   {0, .2, 0, .3, 0, .5, 0},
            {.3, .15, .1, 0, .35, .1, 0},     {.55, 0, 0, .15, 0, .3, 0},   {0, .3, .15, .35, .2, 0, 0},
            {.7, 0, .3, 0, 0, 0, 0}};
        std::array<double, 7> weights{};
        const double a = settings.adventure;
        for (int i = 0; i < 7; ++i)
            weights[size_t(i)] = (1 - a) * table[from][i] + a * (i == from ? 0. : 1. / 6.);
        if (last && a < .7) // Conventional endings land on V or I.
        {
            weights[0] += 1.2 * (1 - a);
            weights[4] += .9 * (1 - a);
        }
        return pick(weights);
    }
    std::vector<int> chordIntervals(const Chord &chord) const
    {
        const auto steps = steps7(chord);
        std::vector<int> intervals;
        for (int k : {0, 2, 4})
            intervals.push_back((steps[size_t((chord.degree + k) % 7)] - steps[size_t(chord.degree)] + 12) % 12);
        if (chord.seventh)
            intervals.push_back((steps[size_t((chord.degree + 6) % 7)] - steps[size_t(chord.degree)] + 12) % 12);
        return intervals;
    }
    std::array<int, 7> steps7(const Chord &chord) const
    {
        if (!chord.borrowed)
            return scaleSteps(settings.scale);
        const bool minorHome = settings.scale == 1 || settings.scale == 2 || settings.scale == 4;
        return scaleSteps(minorHome ? 0 : 1); // Parallel major/minor.
    }
    Chord build(int degree)
    {
        Chord chord;
        chord.degree = degree;
        const double a = settings.adventure;
        // Borrowed colour chords (iv, bVI, bVII in major; IV, V in minor), more when adventurous.
        chord.borrowed = (degree == 3 || degree == 4 || degree == 5 || degree == 6) && chance(.28 * a);
        chord.seventh = chance(.08 + .6 * a) || (degree == 4 && chance(.3));
        const auto steps = steps7(chord);
        chord.root = (settings.key + steps[size_t(degree)]) % 12;
        const auto intervals = chordIntervals(chord);
        chord.name = chordName(chord.root, intervals[1], intervals[2], chord.seventh ? intervals[3] : -1);
        return chord;
    }
};
} // namespace vibera::chords
