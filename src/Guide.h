// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <cmath>
#include "TempoMap.h"
#include "MeterMap.h"

namespace vibera
{
// Device stream time paired with the Master edit time of the same block, published
// by the Guide plugin from the audio thread. Audio takes map input blocks onto the
// timeline with it; both sides read the Tracktion device stream clock.
struct TimelineStamp
{
    struct Value
    {
        double stream = 0, edit = 0;
        bool playing = false;
    };
    std::atomic<unsigned> serial{0};
    std::atomic<double> stream{0}, edit{0};
    std::atomic<bool> playing{false};
    void publish(double streamSeconds, double editSeconds, bool running) noexcept
    {
        serial.fetch_add(1);
        stream.store(streamSeconds);
        edit.store(editSeconds);
        playing.store(running);
        serial.fetch_add(1);
    }
    bool read(Value &out) const noexcept
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const auto before = serial.load();
            if (before == 0 || (before & 1) != 0)
                continue;
            Value value{stream.load(), edit.load(), playing.load()};
            if (serial.load() == before)
            {
                out = value;
                return true;
            }
        }
        return false;
    }
};
struct GuideControl
{
    TimelineStamp timeline;
    std::atomic<bool> enabled{false}, leadActive{false}, ready{false};
    std::atomic<float> gain{.65f};
    std::atomic<int> sound{0};
    std::atomic<unsigned> epoch{0};
    std::atomic<double> leadStart{0}, leadEnd{0}, remaining{0};
    std::atomic<std::uint64_t> hits{0};
    void begin(double start, double end)
    {
        leadActive = false;
        ready = false;
        leadStart = start;
        leadEnd = end;
        remaining = end - start;
        ++epoch;
        leadActive = true;
    }
    void cancel()
    {
        leadActive = false;
        ready = false;
        remaining = 0;
    }
};
struct GuideGate
{
    unsigned epoch = 0;
    bool complete = false;
    int skip(const GuideControl *control, double time, int frames, double rate) noexcept
    {
        if (!control || !control->leadActive.load())
            return 0;
        if (!control->ready.load())
            return frames;
        auto generation = control->epoch.load();
        if (generation != epoch)
        {
            epoch = generation;
            complete = false;
        }
        if (complete)
            return 0;
        double difference = control->leadEnd.load() - time;
        if (difference <= 0)
        {
            complete = true;
            return 0;
        }
        if (difference >= frames / rate)
            return frames;
        const int before = juce::jlimit(0, frames, int(std::ceil(difference * rate - .00001)));
        if (before < frames)
            complete = true;
        return before;
    }
};
// Original immutable clicks at 48 kHz, prepared on the control thread. No callback allocation.
class GuideDsp
{
    static constexpr int length = 960;
    std::array<std::array<std::array<float, length>, 2>, 3> sounds{};
    double cursor = length, previousEnd = -1.e100;
    double lastTick = -1.e100;
    int voiceSound = 0, voiceAccent = 0;
    float voiceGain = 0;
    GuideGate lead;
    bool leading = false;
    void render(juce::AudioBuffer<float> &buffer, int offset, int frames, double rate) noexcept
    {
        auto &wave = sounds[size_t(voiceSound)][size_t(voiceAccent)];
        for (int i = 0; i < frames && cursor < length - 1; ++i, cursor += 48000. / rate)
        {
            auto index = int(cursor);
            auto fraction = float(cursor - index);
            auto value = (wave[size_t(index)] + fraction * (wave[size_t(index + 1)] - wave[size_t(index)])) *
                         voiceGain;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample(ch, offset + i, value);
        }
    }

  public:
    struct Hit
    {
        int frame = 0;
        bool accent = false;
    };
    std::array<Hit, 64> lastHits{};
    int hitCount = 0;
    GuideDsp()
    {
        for (int style = 0; style < 3; ++style)
            for (int accent = 0; accent < 2; ++accent)
                for (int i = 0; i < length; ++i)
                {
                    double t = i / 48000.;
                    const double frequency = (accent ? 1700. : 1150.) * (style == 1 ? .65 : 1.);
                    double wave = std::sin(juce::MathConstants<double>::twoPi * frequency * t);
                    if (style == 0)
                        wave += .3 * std::sin(juce::MathConstants<double>::twoPi * frequency * 2.3 * t);
                    if (style == 1)
                        wave += .45 * std::sin(juce::MathConstants<double>::twoPi * frequency * 1.7 * t);
                    const double envelope = std::min(1., i / 12.) * std::exp(-t * (style == 2 ? 170 : 280));
                    sounds[size_t(style)][size_t(accent)][size_t(i)] =
                        float((accent ? .3 : .2) * wave * envelope);
                }
    }
    void reset() noexcept
    {
        cursor = length;
        previousEnd = -1.e100;
        lastTick = -1.e100;
    }
    void process(juce::AudioBuffer<float> &buffer, int offset, int frames, double time, double rate,
                 bool playing, double tempo, int numerator, int denominator, GuideControl &control,
                 const TempoMap *map = nullptr, const MeterMap *meters = nullptr) noexcept
    {
        hitCount = 0;
        if (!playing)
        {
            reset();
            if (!control.leadActive.load())
                control.remaining = 0;
            return;
        }
        if (control.leadActive.load() && !control.ready.load())
        {
            reset();
            return;
        }
        if (std::abs(time - previousEnd) > 2. / rate)
        {
            cursor = length;
            lastTick = -1.e100;
        }
        previousEnd = time + frames / rate;
        const int before = lead.skip(&control, time, frames, rate);
        leading = before > 0;
        control.remaining = leading ? std::max(0., control.leadEnd.load() - previousEnd) : 0;
        if (!control.enabled.load() && !leading)
        {
            cursor = length;
            return;
        }
        double interval = 60. / tempo * 4. / denominator;
        double origin = leading ? control.leadStart.load() : 0;
        // With tempo changes the Song grid is in beats; each tick's time comes from the map.
        const bool mapped = map && !map->constant() && !leading;
        const double stepBeats = 4. / denominator;
        auto tick = mapped ? std::int64_t(std::ceil(map->beatAt(time) / stepBeats -
                                                    1.1 / rate * map->bpmAt(map->beatAt(time)) / 60. / stepBeats))
                           : std::int64_t(std::ceil((time - origin) / interval - 1.1 / rate / interval));
        // Song time-signature changes: tick on each meter's beat unit, accent each bar's first.
        const bool metered = meters && !meters->changes.empty() && !leading;
        auto beatOf = [&](double seconds) { return map ? map->beatAt(seconds) : seconds * tempo / 60.; };
        auto secondsOf = [&](double beat) { return map ? map->secondsAt(beat) : beat * 60. / tempo; };
        double meterCursor = metered ? std::max(0., beatOf(time - 1.1 / rate)) : 0., meterTick = 0;
        bool meterAccent = false;
        auto meterNext = [&]
        {
            const auto at = meters->at(meterCursor);
            const double step = 4. / at.denominator, barStart = meterCursor - at.beatInBar;
            double inBar = std::ceil(at.beatInBar / step - 1.e-9) * step;
            if (inBar > at.barLength - 1.e-9)
                inBar = at.barLength; // The next bar's first beat.
            meterTick = barStart + inBar;
            meterAccent = inBar < 1.e-9 || inBar > at.barLength - 1.e-9;
            return secondsOf(meterTick);
        };
        auto advance = [&]
        {
            if (metered)
                meterCursor = meterTick + 1.e-6;
            else
                ++tick;
        };
        int cursorFrame = 0;
        while (true)
        {
            double point = metered ? meterNext()
                           : mapped ? map->secondsAt(double(tick) * stepBeats)
                                    : origin + tick * interval;
            int frame = juce::roundToInt((point - time) * rate);
            if (frame >= frames)
                break;
            if (frame >= -1 && frame < 0)
                frame = 0;
            if (std::abs(point - lastTick) < .5 / rate)
            {
                advance();
                continue;
            }
            if (frame >= 0 && (control.enabled.load() || !leading || point < control.leadEnd.load()))
            {
                render(buffer, offset + cursorFrame, frame - cursorFrame, rate);
                cursorFrame = frame;
                if (control.enabled.load() || (leading && point < control.leadEnd.load()))
                {
                    voiceSound = juce::jlimit(0, 2, control.sound.load());
                    voiceAccent = metered ? meterAccent : tick % numerator == 0;
                    voiceGain = control.gain.load();
                    lastTick = point;
                    cursor = 0;
                    if (hitCount < int(lastHits.size()))
                        lastHits[size_t(hitCount++)] = {frame, bool(voiceAccent)};
                    ++control.hits;
                }
            }
            advance();
        }
        render(buffer, offset + cursorFrame, frames - cursorFrame, rate);
    }
};
} // namespace vibera
