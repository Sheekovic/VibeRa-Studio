// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace vibera::vocal
{
// Graphical vocal pitch editing (VariAudio / Flex Pitch style, original implementation):
// analyze a monophonic Audio Clip into a pitch curve and note segments, then render a new
// file where each note is transposed by its own amount (length kept, Signalsmith Stretch).
struct PitchFrame
{
    double seconds = 0; // From the clip window start.
    double midi = 0;    // Fractional MIDI pitch; 0 = unvoiced / silent.
};
struct Note
{
    int index = 0;
    double start = 0, end = 0; // Seconds from the clip window start.
    double pitch = 0;          // Median fractional MIDI pitch.
};
struct Analysis
{
    double hop = 0;                 // Seconds between frames.
    std::vector<PitchFrame> curve;
    std::vector<Note> notes;
};
struct Shift
{
    double start = 0, end = 0; // Seconds from the window start.
    double semitones = 0;      // -24..24.
};
// Pitch range 65..1050 Hz (C2..C6), windows up to ten minutes.
juce::Result analyze(const juce::File &source, double offsetSeconds, double durationSeconds, Analysis &out);
// Pure analysis of mono PCM (exposed for tests).
Analysis analyzeBuffer(const float *mono, int frames, double rate);
// Renders the window with every shift applied (20 ms glides at note edges) into a 24-bit WAV.
juce::Result render(const juce::File &source, double offsetSeconds, double durationSeconds,
                    const std::vector<Shift> &shifts, const juce::File &target);
juce::String noteName(double midi);
} // namespace vibera::vocal
