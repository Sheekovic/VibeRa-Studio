// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <algorithm>
#include <map>
#include <random>

// Piano Roll composition tools: chordify, arpeggiate, strum, humanize and scale snap.
// Each works on chosen note indices of one Pattern and is applied by its canonical
// command as one Undo step. Randomness is seeded, so a command replays identically.
namespace vibera::score
{
struct Shape
{
    const char *name;
    std::vector<int> intervals; // Semitones above the root (chords) or pitch classes (scales).
};
inline const std::vector<Shape> &chordShapes()
{
    static const std::vector<Shape> shapes{
        {"major", {0, 4, 7}},        {"minor", {0, 3, 7}},          {"dim", {0, 3, 6}},
        {"aug", {0, 4, 8}},          {"sus2", {0, 2, 7}},           {"sus4", {0, 5, 7}},
        {"maj7", {0, 4, 7, 11}},     {"min7", {0, 3, 7, 10}},       {"dom7", {0, 4, 7, 10}},
        {"dim7", {0, 3, 6, 9}},      {"m7b5", {0, 3, 6, 10}},       {"6", {0, 4, 7, 9}},
        {"m6", {0, 3, 7, 9}},        {"add9", {0, 4, 7, 14}},       {"maj9", {0, 4, 7, 11, 14}},
        {"min9", {0, 3, 7, 10, 14}}, {"9", {0, 4, 7, 10, 14}},      {"power", {0, 7, 12}}};
    return shapes;
}
// 12-tone scales. The maqam entries are 12-TET approximations only (no quarter tones).
inline const std::vector<Shape> &scaleShapes()
{
    static const std::vector<Shape> shapes{
        {"major", {0, 2, 4, 5, 7, 9, 11}},          {"minor", {0, 2, 3, 5, 7, 8, 10}},
        {"harmonicMinor", {0, 2, 3, 5, 7, 8, 11}},  {"melodicMinor", {0, 2, 3, 5, 7, 9, 11}},
        {"dorian", {0, 2, 3, 5, 7, 9, 10}},         {"phrygian", {0, 1, 3, 5, 7, 8, 10}},
        {"lydian", {0, 2, 4, 6, 7, 9, 11}},         {"mixolydian", {0, 2, 4, 5, 7, 9, 10}},
        {"locrian", {0, 1, 3, 5, 6, 8, 10}},        {"majorPentatonic", {0, 2, 4, 7, 9}},
        {"minorPentatonic", {0, 3, 5, 7, 10}},      {"blues", {0, 3, 5, 6, 7, 10}},
        {"hijaz", {0, 1, 4, 5, 7, 8, 10}},          {"nahawand", {0, 2, 3, 5, 7, 8, 11}},
        {"kurd", {0, 1, 3, 5, 7, 8, 10}},           {"ajam", {0, 2, 4, 5, 7, 9, 11}}};
    return shapes;
}
inline const Shape *find(const std::vector<Shape> &shapes, const juce::String &name)
{
    for (const auto &shape : shapes)
        if (name == shape.name)
            return &shape;
    return nullptr;
}
inline juce::StringArray names(const std::vector<Shape> &shapes)
{
    juce::StringArray out;
    for (const auto &shape : shapes)
        out.add(shape.name);
    return out;
}

// Notes sharing a Channel and onset form one chord group, in pitch order.
inline std::vector<std::vector<size_t>> onsetGroups(const Pattern &pattern, const std::vector<size_t> &targets)
{
    std::map<std::pair<Id, double>, std::vector<size_t>> groups;
    for (auto index : targets)
        groups[{pattern.notes[index].channel, pattern.notes[index].beat}].push_back(index);
    std::vector<std::vector<size_t>> out;
    for (auto &[key, members] : groups)
    {
        std::sort(members.begin(), members.end(),
                  [&](size_t a, size_t b) { return pattern.notes[a].pitch < pattern.notes[b].pitch; });
        out.push_back(std::move(members));
    }
    return out;
}

// Every chosen note becomes the root of a chord; inversion moves its lowest notes up an
// octave. Added notes copy the root's properties; existing identical notes are kept.
inline juce::Result chordify(Project &project, Pattern &pattern, const std::vector<size_t> &targets,
                             const juce::String &chord, int inversion)
{
    const auto *shape = find(chordShapes(), chord);
    if (!shape)
        return juce::Result::fail("Chord must be one of " + names(chordShapes()).joinIntoString(", "));
    if (inversion < 0 || inversion >= int(shape->intervals.size()))
        return juce::Result::fail("Inversion must be 0.." + juce::String(int(shape->intervals.size()) - 1));
    if (targets.empty())
        return juce::Result::fail("Select notes to turn into chords");
    std::vector<Note> added;
    for (auto index : targets)
    {
        const auto root = pattern.notes[index];
        for (size_t i = 0; i < shape->intervals.size(); ++i)
        {
            const int interval = shape->intervals[i] + (int(i) < inversion ? 12 : 0);
            const int pitch = root.pitch + interval;
            if (pitch > 127)
                continue;
            if (i == 0)
            {
                pattern.notes[index].pitch = pitch; // The root moves up an octave when inverted.
                continue;
            }
            const auto exists = [&](const Note &n)
            { return n.channel == root.channel && n.beat == root.beat && n.pitch == pitch; };
            if (std::any_of(pattern.notes.begin(), pattern.notes.end(), exists) ||
                std::any_of(added.begin(), added.end(), exists))
                continue;
            auto note = root;
            note.id = project.allocate();
            note.pitch = pitch;
            added.push_back(note);
        }
    }
    if (pattern.notes.size() + added.size() > 100000)
        return juce::Result::fail("Too many notes");
    pattern.notes.insert(pattern.notes.end(), added.begin(), added.end());
    return juce::Result::ok();
}

// Each chord (same Channel and onset) becomes a run of single notes across its longest
// length: `step` beats apart, each `gate` of a step long. Modes: up, down, updown, random.
inline juce::Result arpeggiate(Project &project, Pattern &pattern, const std::vector<size_t> &targets,
                               const juce::String &mode, double step, double gate, std::uint32_t seed)
{
    if (!(step >= 1. / 64 && step <= 16))
        return juce::Result::fail("Arpeggio step is 1/64..16 beats");
    if (!(gate > 0 && gate <= 1))
        return juce::Result::fail("Arpeggio gate is a fraction of the step, 0..1");
    if (mode != "up" && mode != "down" && mode != "updown" && mode != "random")
        return juce::Result::fail("Arpeggio mode is up, down, updown or random");
    if (targets.empty())
        return juce::Result::fail("Select chords to arpeggiate");
    std::vector<Note> output;
    std::vector<Id> removed;
    std::mt19937 random(seed);
    for (const auto &group : onsetGroups(pattern, targets))
    {
        std::vector<Note> chord;
        double span = 0;
        for (auto index : group)
        {
            chord.push_back(pattern.notes[index]);
            span = std::max(span, pattern.notes[index].length);
            removed.push_back(pattern.notes[index].id);
        }
        std::vector<size_t> order(chord.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        if (mode == "down")
            std::reverse(order.begin(), order.end());
        if (mode == "updown" && chord.size() > 2)
            for (size_t i = chord.size() - 2; i > 0; --i)
                order.push_back(i);
        const int count = std::max(1, int(std::floor(span / step + 1.e-9)));
        if (output.size() + size_t(count) > 4096)
            return juce::Result::fail("Arpeggio would add more than 4096 notes");
        for (int k = 0; k < count; ++k)
        {
            if (mode == "random" && k % int(order.size()) == 0)
                std::shuffle(order.begin(), order.end(), random);
            auto note = chord[order[size_t(k) % order.size()]];
            // The chord's own IDs carry its first notes; later ones are new.
            note.id = k < int(chord.size()) ? chord[size_t(k)].id : project.allocate();
            note.beat = chord.front().beat + k * step;
            note.length = std::min(step * gate, chord.front().beat + span - note.beat);
            note.step = false;
            output.push_back(note);
        }
    }
    pattern.notes.erase(std::remove_if(pattern.notes.begin(), pattern.notes.end(),
                                       [&](const Note &n)
                                       { return std::find(removed.begin(), removed.end(), n.id) != removed.end(); }),
                        pattern.notes.end());
    pattern.notes.insert(pattern.notes.end(), output.begin(), output.end());
    return juce::Result::ok();
}

// Each chord's notes start `spread` beats after one another (low to high for "up", high
// to low for "down"), keeping their ends; `taper` 0..1 softens each later note.
inline juce::Result strum(Pattern &pattern, const std::vector<size_t> &targets, const juce::String &direction,
                          double spread, double taper)
{
    if (direction != "up" && direction != "down")
        return juce::Result::fail("Strum direction is up or down");
    if (!(spread > 0 && spread <= 1))
        return juce::Result::fail("Strum spread is 0..1 beat per note");
    if (!(taper >= 0 && taper <= 1))
        return juce::Result::fail("Strum taper is 0..1");
    bool chords = false;
    for (auto group : onsetGroups(pattern, targets))
    {
        if (group.size() < 2)
            continue;
        chords = true;
        if (direction == "down")
            std::reverse(group.begin(), group.end());
        for (size_t k = 1; k < group.size(); ++k)
        {
            auto &note = pattern.notes[group[k]];
            const double end = note.beat + note.length;
            note.beat += double(k) * spread;
            note.length = std::max(1. / 64, end - note.beat);
            note.velocity = juce::jlimit(.01f, 1.f, float(note.velocity * (1. - taper * double(k) / (group.size() - 1) * .5)));
            note.step = false;
        }
    }
    return chords ? juce::Result::ok() : juce::Result::fail("Strum needs chords (2+ notes with the same start)");
}

// Random, seeded variation: onset +/- timing beats, velocity +/- velocity, length
// +/- lengthFraction of itself. Onsets never go below zero.
inline juce::Result humanize(Pattern &pattern, const std::vector<size_t> &targets, double timing,
                             double velocity, double lengthFraction, std::uint32_t seed)
{
    if (!(timing >= 0 && timing <= 1) || !(velocity >= 0 && velocity <= 1) ||
        !(lengthFraction >= 0 && lengthFraction <= .9))
        return juce::Result::fail("Humanize ranges: timing 0..1 beat, velocity 0..1, length 0..0.9");
    if (targets.empty())
        return juce::Result::fail("Select notes to humanize");
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> unit(-1., 1.);
    for (auto index : targets)
    {
        auto &note = pattern.notes[index];
        const double t = unit(random), v = unit(random), l = unit(random);
        note.beat = std::max(0., note.beat + t * timing);
        note.velocity = juce::jlimit(.01f, 1.f, float(note.velocity + v * velocity));
        note.length = std::max(1. / 64, note.length * (1 + l * lengthFraction));
        note.step = false;
    }
    return juce::Result::ok();
}

// Maqamat with quarter tones, in cents above the tonic (24-tone equal temperament). The
// tonic is the named pitch shifted by `tonicCents` (Sikah/Huzam start a half flat lower).
struct Maqam
{
    const char *name;
    int tonicCents;
    std::vector<int> cents;
};
inline const std::vector<Maqam> &maqamat()
{
    static const std::vector<Maqam> list{
        {"rast", 0, {0, 200, 350, 500, 700, 900, 1050}},
        {"bayati", 0, {0, 150, 300, 500, 700, 800, 1000}},
        {"saba", 0, {0, 150, 300, 400, 700, 800, 1000}},
        {"sikah", -50, {0, 150, 350, 550, 700, 850, 1050}},
        {"huzam", -50, {0, 150, 450, 550, 700, 850, 1050}},
        {"hijaz", 0, {0, 100, 400, 500, 700, 800, 1000}},
        {"nahawand", 0, {0, 200, 300, 500, 700, 800, 1100}},
        {"kurd", 0, {0, 100, 300, 500, 700, 800, 1000}},
        {"ajam", 0, {0, 200, 400, 500, 700, 900, 1100}},
        {"nawaAthar", 0, {0, 200, 300, 600, 700, 800, 1100}},
        {"jiharkah", 0, {0, 200, 400, 500, 700, 900, 1050}}};
    return list;
}
// Piano Roll guide: per pitch class 0 = outside, 1 = degree, 2 = tonic, and +4 when the degree
// is a quarter tone below that key (a half flat: maqamSnap writes it as that pitch -50 cents).
inline std::array<int, 12> guideMarks(const juce::String &name, int root, bool maqam)
{
    std::array<int, 12> marks{};
    auto mark = [&](int cents, bool tonic)
    {
        const int up = (cents % 100 + 100) % 100 != 0; // Quarter tone: on the key above, flagged.
        const int key = ((cents + (up ? 50 : 0)) / 100 % 12 + 12) % 12;
        marks[size_t(key)] = std::max(marks[size_t(key)] & 3, tonic ? 2 : 1) | (up ? 4 : 0);
    };
    if (maqam)
    {
        for (const auto &m : maqamat())
            if (name == m.name)
                for (size_t i = 0; i < m.cents.size(); ++i)
                    mark(root * 100 + m.tonicCents + m.cents[i], i == 0);
    }
    else if (const auto *shape = find(scaleShapes(), name))
        for (size_t i = 0; i < shape->intervals.size(); ++i)
            mark((root + shape->intervals[i]) * 100, i == 0);
    return marks;
}
// Moves each note (pitch + fine cents) to the nearest maqam degree and writes it back as
// a MIDI pitch plus fine cents in [-50, 50): quarter tones become -50 cents on the note
// above. Played by the local sampler's per-note tuning; MIDI 1 plugins ignore fine cents.
inline juce::Result maqamSnap(Pattern &pattern, const std::vector<size_t> &targets, const juce::String &name,
                              int tonic)
{
    const auto found = std::find_if(maqamat().begin(), maqamat().end(),
                                    [&](const Maqam &m) { return name == m.name; });
    if (found == maqamat().end())
    {
        juce::StringArray all;
        for (const auto &m : maqamat())
            all.add(m.name);
        return juce::Result::fail("Maqam must be one of " + all.joinIntoString(", "));
    }
    if (tonic < 0 || tonic > 11)
        return juce::Result::fail("Maqam tonic is a pitch class 0..11 (0 = C)");
    if (targets.empty())
        return juce::Result::fail("Select notes to snap to the maqam");
    const double base = tonic * 100. + found->tonicCents;
    for (auto index : targets)
    {
        auto &note = pattern.notes[index];
        const double at = note.pitch * 100. + note.fineCents;
        double best = at, distance = 1.e9;
        const int octave = int(std::floor((at - base) / 1200.));
        for (int o = octave - 1; o <= octave + 1; ++o)
            for (auto degree : found->cents)
            {
                const double candidate = base + o * 1200. + degree;
                const double d = std::abs(candidate - at);
                if (d < distance - 1.e-9 || (std::abs(d - distance) <= 1.e-9 && candidate < best))
                {
                    best = candidate;
                    distance = d;
                }
            }
        const int pitch = int(std::floor((best + 50.) / 100.));
        if (pitch < 0 || pitch > 127)
            continue;
        note.pitch = pitch;
        note.fineCents = best - pitch * 100.;
    }
    return juce::Result::ok();
}

// Moves each note to the nearest pitch of the scale on `root` (0 = C); ties go down.
inline juce::Result scaleSnap(Pattern &pattern, const std::vector<size_t> &targets, const juce::String &scale,
                              int root)
{
    const auto *shape = find(scaleShapes(), scale);
    if (!shape)
        return juce::Result::fail("Scale must be one of " + names(scaleShapes()).joinIntoString(", "));
    if (root < 0 || root > 11)
        return juce::Result::fail("Scale root is 0..11 (0 = C)");
    if (targets.empty())
        return juce::Result::fail("Select notes to snap to the scale");
    auto inScale = [&](int pitch)
    {
        const int pc = ((pitch - root) % 12 + 12) % 12;
        return std::find(shape->intervals.begin(), shape->intervals.end(), pc) != shape->intervals.end();
    };
    for (auto index : targets)
    {
        auto &note = pattern.notes[index];
        for (int distance = 0; distance < 12; ++distance)
        {
            if (note.pitch - distance >= 0 && inScale(note.pitch - distance))
            {
                note.pitch -= distance;
                break;
            }
            if (note.pitch + distance <= 127 && inScale(note.pitch + distance))
            {
                note.pitch += distance;
                break;
            }
        }
    }
    return juce::Result::ok();
}
} // namespace vibera::score
