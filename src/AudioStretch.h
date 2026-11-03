// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

namespace vibera
{
// Pitch-preserving time stretch of a source window into a new 24-bit WAV, offline on a
// worker thread (Signalsmith Stretch, MIT, vendored with Tracktion). `ratio` is output
// length / input length: 2 doubles the duration at the same pitch.
juce::Result stretchAudioFile(const juce::File &source, double offsetSeconds, double durationSeconds, double ratio,
                              const juce::File &target);
// Offline sample edits of a source window into a new 24-bit WAV: "reverse", "normalize"
// (peak to `value` dBFS), "gain" (`value` dB) or "consolidate" (the window with fades
// `fadeIn`/`fadeOut` seconds baked in, matching the clip's sine-shaped fades).
juce::Result editAudioFile(const juce::File &source, double offsetSeconds, double durationSeconds,
                           const juce::String &operation, double value, double fadeIn, double fadeOut,
                           const juce::File &target);
} // namespace vibera
