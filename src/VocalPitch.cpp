// SPDX-License-Identifier: AGPL-3.0-or-later
#include "VocalPitch.h"
#include "AudioStretch.h"
#include <algorithm>
#include <cmath>
#include <signalsmith-stretch.h>

namespace vibera::vocal
{
juce::Result renderPitch(const juce::File &, double, double, const std::vector<Shift> &, const juce::File &, bool);
namespace
{
constexpr double minimumHz = 65, maximumHz = 1050;
double median(std::vector<double> values)
{
    if (values.empty())
        return 0;
    const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}
juce::Result writeWav(const juce::File &target, const juce::AudioBuffer<float> &pcm, double rate)
{
    const auto part = target.getSiblingFile(target.getFileName() + ".part");
    part.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
            part.createOutputStream().release(), rate, unsigned(pcm.getNumChannels()), 24, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()))
        {
            part.deleteFile();
            return juce::Result::fail("Writing " + target.getFileName() + " failed");
        }
    }
    target.deleteFile();
    return part.moveFileTo(target) ? juce::Result::ok() : juce::Result::fail("Cannot finish " + target.getFileName());
}
bool readWindow(const juce::File &source, double offsetSeconds, double durationSeconds, juce::AudioBuffer<float> &pcm,
                double &rate, juce::String &error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (!reader || reader->sampleRate <= 0)
    {
        error = "Cannot read " + source.getFileName();
        return false;
    }
    rate = reader->sampleRate;
    const int channels = int(juce::jlimit(1u, 2u, reader->numChannels));
    const auto start = juce::int64(std::max(0., offsetSeconds) * rate);
    const auto frames = std::min<juce::int64>(juce::int64(durationSeconds * rate), reader->lengthInSamples - start);
    if (frames < 4096 || frames > juce::int64(rate * 600))
    {
        error = frames < 4096 ? "The clip is too short (under 0.1 s)" : "Vocal pitch editing is limited to ten minutes";
        return false;
    }
    pcm.setSize(channels, int(frames));
    reader->read(&pcm, 0, int(frames), start, true, channels > 1);
    return true;
}
} // namespace

juce::String noteName(double midi)
{
    static const char *const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int nearest = int(std::lround(midi));
    return juce::String(names[((nearest % 12) + 12) % 12]) + juce::String(nearest / 12); // C5 = 60, as in the Piano roll.
}

Analysis analyzeBuffer(const float *mono, int frames, double rate)
{
    Analysis result;
    // Work at about 12 kHz: voices fit, and the YIN difference function is 16x cheaper.
    const int factor = std::max(1, int(rate / 12000));
    const double low = rate / factor;
    std::vector<float> x(size_t(frames / factor));
    for (size_t i = 0; i < x.size(); ++i)
    {
        float sum = 0;
        for (int k = 0; k < factor; ++k)
            sum += mono[i * size_t(factor) + size_t(k)];
        x[i] = sum / float(factor);
    }
    const int window = int(std::ceil(low / minimumHz * 1.25)), maxLag = int(low / minimumHz),
              minLag = std::max(2, int(low / maximumHz)), hop = std::max(1, int(low * .01));
    result.hop = hop / low;
    std::vector<double> d(size_t(maxLag + 2));
    float peak = 0;
    for (const float s : x)
        peak = std::max(peak, std::abs(s));
    const double gate = std::max(1.e-4, peak * .03); // RMS below 3 % of the peak is silence.
    for (int at = 0; at + window + maxLag < int(x.size()); at += hop)
    {
        PitchFrame frame;
        frame.seconds = (at + window / 2.) / low;
        double energy = 0;
        for (int i = 0; i < window; ++i)
            energy += double(x[size_t(at + i)]) * x[size_t(at + i)];
        if (std::sqrt(energy / window) >= gate)
        {
            // YIN: difference function, cumulative mean normalization, absolute threshold, parabola.
            double running = 0;
            int best = -1;
            d[0] = 1;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                double sum = 0;
                for (int i = 0; i < window; ++i)
                {
                    const double delta = double(x[size_t(at + i)]) - x[size_t(at + i + tau)];
                    sum += delta * delta;
                }
                running += sum;
                d[size_t(tau)] = running > 0 ? sum * tau / running : 1;
            }
            for (int tau = minLag; tau < maxLag; ++tau)
                if (d[size_t(tau)] < .15)
                {
                    while (tau + 1 < maxLag && d[size_t(tau + 1)] < d[size_t(tau)])
                        ++tau;
                    best = tau;
                    break;
                }
            if (best > 0)
            {
                const double a = d[size_t(best - 1)], b = d[size_t(best)], c = d[size_t(best + 1)];
                const double denominator = a - 2 * b + c;
                const double lag = best + (denominator != 0 ? .5 * (a - c) / denominator : 0.);
                const double hz = low / lag;
                if (hz >= minimumHz && hz <= maximumHz)
                    frame.midi = 69 + 12 * std::log2(hz / 440);
            }
        }
        result.curve.push_back(frame);
    }
    // Notes: runs of voiced frames, split where the pitch moves away from the note for 3 frames.
    auto &curve = result.curve;
    size_t i = 0;
    while (i < curve.size())
    {
        while (i < curve.size() && curve[i].midi == 0)
            ++i;
        size_t first = i;
        std::vector<double> pitches;
        while (i < curve.size() && curve[i].midi != 0)
        {
            const double centre = median(pitches);
            if (pitches.size() >= 5 && std::abs(curve[i].midi - centre) > .6)
            {
                size_t away = 0;
                while (i + away < curve.size() && curve[i + away].midi != 0 &&
                       std::abs(curve[i + away].midi - centre) > .6 && away < 3)
                    ++away;
                if (away >= 3)
                    break; // A new note starts here.
            }
            pitches.push_back(curve[i].midi);
            ++i;
        }
        if (i > first && double(i - first) * result.hop >= .06)
            result.notes.push_back({int(result.notes.size()), curve[first].seconds - result.hop / 2,
                                    curve[i - 1].seconds + result.hop / 2, median(pitches)});
    }
    return result;
}

juce::Result analyze(const juce::File &source, double offsetSeconds, double durationSeconds, Analysis &out)
{
    juce::AudioBuffer<float> pcm;
    double rate = 0;
    juce::String error;
    if (!readWindow(source, offsetSeconds, durationSeconds, pcm, rate, error))
        return juce::Result::fail(error);
    if (pcm.getNumChannels() > 1)
    {
        pcm.addFrom(0, 0, pcm, 1, 0, pcm.getNumSamples());
        pcm.applyGain(0, 0, pcm.getNumSamples(), .5f);
    }
    out = analyzeBuffer(pcm.getReadPointer(0), pcm.getNumSamples(), rate);
    return juce::Result::ok();
}

juce::Result render(const juce::File &source, double offsetSeconds, double durationSeconds,
                    const std::vector<Shift> &original, const juce::File &target, bool preserveFormants)
{
    // Timing first: a time map moves every shifted note whole; the gaps around it stretch. Then the
    // pitch pass runs on the retimed audio with each note at its new place.
    auto shifts = original;
    std::sort(shifts.begin(), shifts.end(), [](const Shift &x, const Shift &y) { return x.start < y.start; });
    const bool timing = std::any_of(shifts.begin(), shifts.end(), [](const Shift &s) { return s.time != 0; });
    juce::File retimed;
    if (timing)
    {
        std::vector<std::pair<double, double>> map{{0, 0}};
        for (const auto &s : shifts)
        {
            if (!std::isfinite(s.time) || std::abs(s.time) > 1)
                return juce::Result::fail("Note timing moves are -1..1 seconds");
            // Every listed note is an anchor (unmoved ones stay put), so only the gaps next to a
            // moved note stretch.
            map.push_back({s.start, s.start + s.time});
            map.push_back({s.end, s.end + s.time});
        }
        map.push_back({durationSeconds, durationSeconds});
        for (size_t i = 1; i < map.size(); ++i)
            if (!(map[i].first > map[i - 1].first) || !(map[i].second > map[i - 1].second))
                return juce::Result::fail("Moved notes cannot cross each other or the clip edges");
        retimed = target.getSiblingFile(target.getFileNameWithoutExtension() + " timing.wav");
        if (auto warped = warpAudioFile(source, offsetSeconds, durationSeconds, map, retimed); warped.failed())
            return warped;
        for (auto &s : shifts)
            s.start += s.time, s.end += s.time, s.time = 0;
    }
    const auto input = timing ? retimed : source;
    const double from = timing ? 0. : offsetSeconds;
    auto result = renderPitch(input, from, durationSeconds, shifts, target, preserveFormants);
    if (timing)
        retimed.deleteFile();
    return result;
}
juce::Result renderPitch(const juce::File &source, double offsetSeconds, double durationSeconds,
                         const std::vector<Shift> &shifts, const juce::File &target, bool preserveFormants)
{
    for (const auto &s : shifts)
        if (!std::isfinite(s.semitones) || std::abs(s.semitones) > 24 || !(s.end > s.start) ||
            !std::isfinite(s.formant) || std::abs(s.formant) > 12)
            return juce::Result::fail("Note shifts are -24..24 semitones (formant -12..12) over a positive span");
    juce::AudioBuffer<float> input;
    double rate = 0;
    juce::String error;
    if (!readWindow(source, offsetSeconds, durationSeconds, input, rate, error))
        return juce::Result::fail(error);
    const int channels = input.getNumChannels(), frames = input.getNumSamples();
    // Transposition at a time: the covering note's shift, easing in/out over 20 ms at its edges.
    const double glide = .02;
    auto shiftAt = [&](double seconds, double Shift::*field)
    {
        double value = 0;
        for (const auto &s : shifts)
        {
            if (seconds < s.start - glide || seconds > s.end + glide)
                continue;
            const double in = std::clamp((seconds - (s.start - glide)) / glide, 0., 1.),
                         out = std::clamp(((s.end + glide) - seconds) / glide, 0., 1.);
            value += s.*field * std::min(in, out);
        }
        return value;
    };
    auto transposeAt = [&](double seconds) { return shiftAt(seconds, &Shift::semitones); };
    auto formantAt = [&](double seconds) { return shiftAt(seconds, &Shift::formant); };
    juce::AudioBuffer<float> output(channels, frames);
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault(channels, float(rate));
    std::vector<float *> in(static_cast<size_t>(channels)), out(in);
    auto point = [&](std::vector<float *> &ptrs, juce::AudioBuffer<float> &buffer, int frame)
    {
        for (int c = 0; c < channels; ++c)
            ptrs[size_t(c)] = buffer.getWritePointer(c) + frame;
    };
    // Same-length processing: seek so output frame 0 lines up with input frame 0.
    int consumed = std::min(frames, stretch.outputSeekLength(1.f));
    point(in, input, 0);
    stretch.setTransposeSemitones(float(transposeAt(0)));
    stretch.setFormantSemitones(float(formantAt(0)), preserveFormants);
    stretch.outputSeek(in, consumed);
    const int tail = stretch.outputLatency();
    int written = 0;
    constexpr int block = 256;
    while (written < frames - tail)
    {
        const int count = std::min(block, frames - tail - written);
        const double middle = (written + count * .5) / rate;
        stretch.setTransposeSemitones(float(transposeAt(middle)));
        stretch.setFormantSemitones(float(formantAt(middle)), preserveFormants);
        const int want = std::clamp(written + count + stretch.outputSeekLength(1.f), consumed, frames);
        point(in, input, consumed);
        point(out, output, written);
        stretch.process(in, want - consumed, out, count);
        consumed = want;
        written += count;
    }
    point(out, output, written);
    stretch.flush(out, frames - written, 1.f);
    return writeWav(target, output, rate);
}
} // namespace vibera::vocal
