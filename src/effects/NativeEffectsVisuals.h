// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Live editor visuals for every built-in effect (message thread only). Each effect draws
// what its controls change (a response or transfer curve, an LFO, echoes, a step grid, a
// stereo field, a scale) and moves with the actual audio (scope, level dot, playhead).
// Curves are display models of the controls; the DSP itself is in effects/dsp.
#include "dsp/BeatGate.h"
#include "dsp/GrooveEffects.h"
#include "dsp/EffectsCatalogue.h"
#include "dsp/PitchCorrect.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <complex>
#include <functional>

namespace vibera::effects::visuals
{
struct Palette
{
    juce::Colour accent, warm, text, muted, grid, plate;
};
struct Live
{
    const float *mid = nullptr, *side = nullptr;
    int count = 0;
    float inputPeak = 0, outputPeak = 0, reductionDb = 0;
    double beat = 0, clock = 0, rate = 48000, detectedHz = 0, targetHz = 0;
};

namespace detail
{
inline float clamp01(float v)
{
    return juce::jlimit(0.f, 1.f, v);
}
inline float db(float gain)
{
    return juce::Decibels::gainToDecibels(gain, -96.f);
}
// RBJ cookbook biquads (public formulas): 0 LP, 1 HP, 2 BP, 3 peak, 4 low shelf, 5 high shelf.
inline double biquadDb(int type, double f0, double q, double gainDb, double hz, double fs)
{
    f0 = juce::jlimit(1., fs * .49, f0);
    q = std::max(.05, q);
    const double a = std::pow(10., gainDb / 40.), w0 = 2 * juce::MathConstants<double>::pi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0), alpha = sw / (2 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
    case 0: b0 = (1 - cw) / 2, b1 = 1 - cw, b2 = (1 - cw) / 2, a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha; break;
    case 1: b0 = (1 + cw) / 2, b1 = -(1 + cw), b2 = (1 + cw) / 2, a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha; break;
    case 2: b0 = alpha, b1 = 0, b2 = -alpha, a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha; break;
    case 3: b0 = 1 + alpha * a, b1 = -2 * cw, b2 = 1 - alpha * a, a0 = 1 + alpha / a, a1 = -2 * cw, a2 = 1 - alpha / a; break;
    default:
    {
        const double s = 2 * std::sqrt(a) * alpha, sign = type == 4 ? 1. : -1.;
        b0 = a * ((a + 1) - sign * (a - 1) * cw + s);
        b1 = sign * 2 * a * ((a - 1) - sign * (a + 1) * cw);
        b2 = a * ((a + 1) - sign * (a - 1) * cw - s);
        a0 = (a + 1) + sign * (a - 1) * cw + s;
        a1 = -sign * 2 * ((a - 1) + sign * (a + 1) * cw);
        a2 = (a + 1) + sign * (a - 1) * cw - s;
    }
    }
    const std::complex<double> z = std::polar(1., -2 * juce::MathConstants<double>::pi * hz / fs);
    const auto h = (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z);
    return 20 * std::log10(std::max(1e-9, std::abs(h)));
}
inline float xOfHz(juce::Rectangle<float> plot, double hz)
{
    return plot.getX() + float(std::log(hz / 20.) / std::log(1000.)) * plot.getWidth();
}
inline void frequencyGrid(juce::Graphics &g, juce::Rectangle<float> plot, const Palette &p, float range)
{
    g.setFont(juce::FontOptions(10.f));
    for (double hz : {50., 100., 200., 500., 1000., 2000., 5000., 10000.})
    {
        const auto x = xOfHz(plot, hz);
        g.setColour(p.grid.withAlpha(.55f));
        g.drawVerticalLine(int(x), plot.getY(), plot.getBottom());
        g.setColour(p.muted);
        g.drawText(hz >= 1000 ? juce::String(int(hz / 1000)) + "k" : juce::String(int(hz)), int(x) + 3,
                   int(plot.getBottom()) - 13, 32, 12, juce::Justification::centredLeft);
    }
    for (float d : {-range, -range / 2, 0.f, range / 2, range})
    {
        const float y = plot.getCentreY() - d / range * plot.getHeight() * .45f;
        g.setColour(d == 0 ? p.muted.withAlpha(.6f) : p.grid.withAlpha(.5f));
        g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
    }
}
template <class Response> void responseCurve(juce::Graphics &g, juce::Rectangle<float> plot, const Palette &p,
                                             float range, Response &&response)
{
    frequencyGrid(g, plot, p, range);
    const auto yOf = [&](double d) { return plot.getCentreY() - float(d / range) * plot.getHeight() * .45f; };
    juce::Path curve, fill;
    for (int x = 0; x <= int(plot.getWidth()); x += 2)
    {
        const double hz = 20. * std::pow(1000., x / double(plot.getWidth()));
        const float y = juce::jlimit(plot.getY(), plot.getBottom(), yOf(response(hz)));
        if (x == 0)
        {
            curve.startNewSubPath(plot.getX(), y);
            fill.startNewSubPath(plot.getX(), yOf(0));
        }
        curve.lineTo(plot.getX() + float(x), y);
        fill.lineTo(plot.getX() + float(x), y);
    }
    fill.lineTo(plot.getRight(), yOf(0));
    fill.closeSubPath();
    g.setColour(p.accent.withAlpha(.18f));
    g.fillPath(fill);
    g.setColour(p.accent);
    g.strokePath(curve, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));
}
// The live output waveform across a strip (the newest ~40 ms).
inline void scope(juce::Graphics &g, juce::Rectangle<float> strip, const Live &live, const Palette &p,
                  juce::Colour colour)
{
    g.setColour(p.plate.brighter(.06f));
    g.fillRoundedRectangle(strip, 6.f);
    if (!live.mid || live.count < 2)
        return;
    const int span = std::min(live.count, int(live.rate * .04));
    juce::Path wave;
    for (int x = 0; x <= int(strip.getWidth()); ++x)
    {
        const int i = live.count - span + int(double(x) / strip.getWidth() * (span - 1));
        const float v = juce::jlimit(-1.f, 1.f, live.mid[std::max(0, i)]);
        const float y = strip.getCentreY() - v * strip.getHeight() * .46f;
        if (x == 0)
            wave.startNewSubPath(strip.getX(), y);
        else
            wave.lineTo(strip.getX() + float(x), y);
    }
    g.setColour(colour.withAlpha(.9f));
    g.strokePath(wave, juce::PathStrokeType(1.4f));
}
inline void caption(juce::Graphics &g, juce::Rectangle<float> plot, const Palette &p, const juce::String &left,
                    const juce::String &right = {})
{
    g.setFont(juce::FontOptions(11.f, juce::Font::bold));
    g.setColour(p.muted);
    g.drawText(left, plot.withHeight(16.f), juce::Justification::centredLeft);
    if (right.isNotEmpty())
    {
        g.setColour(p.text);
        g.drawText(right, plot.withHeight(16.f), juce::Justification::centredRight);
    }
}
// Input dB -> output dB transfer, with the live input level as a moving dot.
template <class Curve> void transfer(juce::Graphics &g, juce::Rectangle<float> plot, const Palette &p,
                                     const Live &live, float low, Curve &&curve)
{
    const auto side = std::min(plot.getWidth() * .42f, plot.getHeight());
    auto square = plot.withWidth(side).withHeight(side).withCentre({plot.getX() + side * .5f + 4.f, plot.getCentreY()});
    const auto xOf = [&](float d) { return square.getX() + (d - low) / -low * square.getWidth(); };
    const auto yOf = [&](float d) { return square.getBottom() - (juce::jlimit(low, 0.f, d) - low) / -low * square.getHeight(); };
    g.setColour(p.plate.brighter(.06f));
    g.fillRoundedRectangle(square, 6.f);
    g.setColour(p.grid.withAlpha(.6f));
    for (float d = low; d <= 0.f; d += -low / 4)
    {
        g.drawVerticalLine(int(xOf(d)), square.getY(), square.getBottom());
        g.drawHorizontalLine(int(yOf(d)), square.getX(), square.getRight());
    }
    g.drawLine(square.getX(), square.getBottom(), square.getRight(), square.getY(), 1.f);
    juce::Path line;
    for (int i = 0; i <= 96; ++i)
    {
        const float in = low + -low * i / 96.f;
        const auto pt = juce::Point<float>(xOf(in), yOf(curve(in)));
        if (i == 0)
            line.startNewSubPath(pt);
        else
            line.lineTo(pt);
    }
    g.setColour(p.accent);
    g.strokePath(line, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));
    const float in = juce::jlimit(low, 0.f, db(live.inputPeak));
    const auto dot = juce::Point<float>(xOf(in), yOf(curve(in)));
    g.setColour(p.warm);
    g.fillEllipse(dot.x - 5.f, dot.y - 5.f, 10.f, 10.f);
    g.setFont(juce::FontOptions(10.f));
    g.setColour(p.muted);
    g.drawText("IN dB", square.withTrimmedTop(square.getHeight() - 14.f), juce::Justification::centredRight);
    auto right = plot.withTrimmedLeft(side + 18.f);
    caption(g, right, p, "INPUT / OUTPUT",
            juce::String(db(live.inputPeak), 1) + " / " + juce::String(db(live.outputPeak), 1) + " dB");
    scope(g, right.withTrimmedTop(22.f), live, p, p.accent);
}
inline void lfo(juce::Graphics &g, juce::Rectangle<float> plot, const Palette &p, const Live &live, double hz,
                float depth, std::function<float(double)> shape, const juce::String &label)
{
    auto top = plot.withHeight(plot.getHeight() * .62f);
    caption(g, top, p, label, juce::String(hz, hz < 10 ? 2 : 1) + " Hz");
    top = top.withTrimmedTop(18.f);
    g.setColour(p.plate.brighter(.06f));
    g.fillRoundedRectangle(top, 6.f);
    g.setColour(p.grid.withAlpha(.6f));
    g.drawHorizontalLine(int(top.getCentreY()), top.getX(), top.getRight());
    // Two cycles drawn; the phase dot moves at the actual rate (sped down when very fast).
    const double shown = hz > 12 ? hz / std::ceil(hz / 12) : hz;
    const double phase = std::fmod(live.clock * shown, 1.0);
    juce::Path wave;
    for (int x = 0; x <= int(top.getWidth()); ++x)
    {
        const double t = 2. * x / top.getWidth();
        const float y = top.getCentreY() - shape(t) * depth * top.getHeight() * .42f;
        if (x == 0)
            wave.startNewSubPath(top.getX(), y);
        else
            wave.lineTo(top.getX() + float(x), y);
    }
    g.setColour(p.accent);
    g.strokePath(wave, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        const double t = phase + cycle;
        const float x = top.getX() + float(t / 2.) * top.getWidth();
        const float y = top.getCentreY() - shape(t) * depth * top.getHeight() * .42f;
        g.setColour(p.warm);
        g.fillEllipse(x - 5.f, y - 5.f, 10.f, 10.f);
    }
    scope(g, plot.withTrimmedTop(top.getBottom() - plot.getY() + 8.f), live, p, p.accent);
}
inline void stereo(juce::Graphics &g, juce::Rectangle<float> plot, const Palette &p, const Live &live,
                   const juce::String &label, const juce::String &value)
{
    // Same frame as the curve displays: the stereo field on the left, the value and the live
    // waveform on the right.
    const auto side = std::min(plot.getWidth() * .42f, plot.getHeight());
    auto field = plot.withWidth(side).withHeight(side).withCentre({plot.getX() + side * .5f + 4.f, plot.getCentreY()});
    g.setColour(p.plate.brighter(.06f));
    g.fillRoundedRectangle(field, 6.f);
    auto circle = field.reduced(14.f);
    g.setColour(p.grid.withAlpha(.7f));
    g.drawEllipse(circle, 1.f);
    g.drawLine(circle.getCentreX(), circle.getY(), circle.getCentreX(), circle.getBottom(), 1.f);
    g.drawLine(circle.getX(), circle.getCentreY(), circle.getRight(), circle.getCentreY(), 1.f);
    g.setFont(juce::FontOptions(10.f, juce::Font::bold));
    g.setColour(p.muted);
    g.drawText("L", field.withWidth(14.f), juce::Justification::centred);
    g.drawText("R", field.withTrimmedLeft(field.getWidth() - 14.f), juce::Justification::centred);
    // Goniometer: mid up, side across (L at left, R at right).
    if (live.mid && live.count > 0)
    {
        const int n = std::min(live.count, 2048);
        g.setColour(p.accent.withAlpha(.55f));
        for (int i = live.count - n; i < live.count; i += 2)
        {
            const float x = circle.getCentreX() - live.side[i] * circle.getWidth() * .48f;
            const float y = circle.getCentreY() - live.mid[i] * circle.getHeight() * .48f;
            g.fillRect(x, y, 1.6f, 1.6f);
        }
    }
    auto right = plot.withTrimmedLeft(side + 18.f);
    caption(g, right, p, label);
    g.setFont(juce::FontOptions(17.f, juce::Font::bold));
    g.setColour(p.text);
    g.drawFittedText(value, right.withTrimmedTop(20.f).withHeight(48.f).toNearestInt(),
                     juce::Justification::centredLeft, 2, .9f);
    scope(g, right.withTrimmedTop(76.f), live, p, p.accent);
}
} // namespace detail

inline void paint(juce::Graphics &g, juce::Rectangle<float> plot, EffectId id, const Values &v, const Live &live,
                  const Palette &p)
{
    using namespace detail;
    const double fs = live.rate > 0 ? live.rate : 48000.;
    switch (id)
    {
    case EffectId::filter:
        return responseCurve(g, plot, p, 24.f, [&](double hz)
                             {
                                 const auto h = biquadDb(int(std::lround(v[0])), v[1], v[2], 0, hz, fs);
                                 const double mix = v[3];
                                 return 20 * std::log10(std::max(1e-6, mix * std::pow(10., h / 20) + (1 - mix)));
                             });
    case EffectId::bassBoost:
        return responseCurve(g, plot, p, 18.f, [&](double hz) { return biquadDb(4, v[0], .707, v[1], hz, fs); });
    case EffectId::dcBlock:
        return responseCurve(g, plot, p, 24.f, [&](double hz)
                             { return 20 * std::log10(hz / std::sqrt(hz * hz + double(v[0]) * v[0])); });
    case EffectId::graphicEq:
        return responseCurve(g, plot, p, 18.f, [&](double hz)
                             {
                                 static constexpr double centres[]{80, 200, 500, 1250, 3150, 8000, 16000};
                                 double total = v[7];
                                 for (int b = 0; b < 7; ++b)
                                     total += biquadDb(3, centres[b], 1.4, v[size_t(b)], hz, fs);
                                 return total;
                             });
    case EffectId::deEsser:
    {
        // The detection band with the live reduction applied to it.
        const float cut = std::min(float(v[2]), -live.reductionDb);
        responseCurve(g, plot, p, 24.f, [&](double hz) { return biquadDb(3, v[0], 1.2, -cut, hz, fs); });
        caption(g, plot, p, "SIBILANCE BAND", juce::String(-cut, 1) + " dB");
        return;
    }
    case EffectId::multiband:
        return responseCurve(g, plot, p, 12.f, [&](double hz)
                             {
                                 const double low = v[0], high = v[1];
                                 const double wl = 1 / (1 + std::pow(hz / low, 4)), wh = 1 / (1 + std::pow(high / hz, 4));
                                 const double wm = std::max(0., 1 - wl - wh);
                                 return wl * v[6] + wm * v[7] + wh * v[8] + v[9];
                             });
    case EffectId::compressor:
        return transfer(g, plot, p, live, -60.f, [&](float in)
                        { return (in < v[0] ? in : v[0] + (in - v[0]) / v[1]) + v[4]; });
    case EffectId::limiter:
        return transfer(g, plot, p, live, -48.f, [&](float in) { return std::min(in + v[2], float(v[0])); });
    case EffectId::maximizer:
        return transfer(g, plot, p, live, -48.f, [&](float in) { return std::min(in + v[0], float(v[1])); });
    case EffectId::gate:
        return transfer(g, plot, p, live, -96.f, [&](float in) { return in < v[0] ? in + v[3] : in; });
    case EffectId::softClip: case EffectId::distortion: case EffectId::saturator: case EffectId::bitCrusher:
    {
        // Waveshaper over -1..1 with the live output waveform beside it.
        const auto side = std::min(plot.getWidth() * .42f, plot.getHeight());
        auto square = plot.withWidth(side).withHeight(side).withCentre({plot.getX() + side * .5f + 4.f, plot.getCentreY()});
        g.setColour(p.plate.brighter(.06f));
        g.fillRoundedRectangle(square, 6.f);
        g.setColour(p.grid.withAlpha(.6f));
        g.drawHorizontalLine(int(square.getCentreY()), square.getX(), square.getRight());
        g.drawVerticalLine(int(square.getCentreX()), square.getY(), square.getBottom());
        const auto shaper = [&](float x) -> float
        {
            if (id == EffectId::softClip)
            {
                const float t = juce::Decibels::decibelsToGain(float(v[0]));
                const float y = std::abs(x) <= t ? x : std::copysign(t + (1 - t) * std::tanh((std::abs(x) - t) / std::max(1e-3f, 1 - t)), x);
                return (y * juce::Decibels::decibelsToGain(float(v[1]))) * v[2] + x * (1 - v[2]);
            }
            if (id == EffectId::distortion)
                return std::tanh(x * juce::Decibels::decibelsToGain(float(v[0]))) *
                           juce::Decibels::decibelsToGain(float(v[1])) * v[2] + x * (1 - v[2]);
            if (id == EffectId::saturator)
            {
                const float d = juce::Decibels::decibelsToGain(float(v[0]));
                const float tape = std::tanh(x * d), tube = std::tanh(x * d + .2f * x * x * d); // Tube adds even harmonics.
                const float y = tape + (tube - tape) * v[1];
                return y * juce::Decibels::decibelsToGain(float(v[4])) * v[3] + x * (1 - v[3]);
            }
            const float steps = std::pow(2.f, float(v[0]) - 1.f);
            return std::round(x * steps) / steps * v[2] + x * (1 - v[2]);
        };
        juce::Path line;
        for (int i = 0; i <= 160; ++i)
        {
            const float x = -1.f + 2.f * i / 160.f;
            const auto pt = juce::Point<float>(square.getCentreX() + x * square.getWidth() * .46f,
                                               square.getCentreY() - juce::jlimit(-1.1f, 1.1f, shaper(x)) * square.getHeight() * .42f);
            if (i == 0)
                line.startNewSubPath(pt);
            else
                line.lineTo(pt);
        }
        g.setColour(p.accent);
        g.strokePath(line, juce::PathStrokeType(2.4f));
        const float in = juce::jlimit(0.f, 1.f, live.inputPeak);
        g.setColour(p.warm);
        g.fillEllipse(square.getCentreX() + in * square.getWidth() * .46f - 5.f,
                      square.getCentreY() - juce::jlimit(-1.1f, 1.1f, shaper(in)) * square.getHeight() * .42f - 5.f, 10.f, 10.f);
        auto right = plot.withTrimmedLeft(side + 18.f);
        caption(g, right, p, "SHAPED OUTPUT", juce::String(db(live.outputPeak), 1) + " dB");
        scope(g, right.withTrimmedTop(22.f), live, p, p.warm);
        return;
    }
    case EffectId::chorus:
        return lfo(g, plot, p, live, v[0], juce::jlimit(.1f, 1.f, v[1] / 12.f),
                   [](double t) { return float(std::sin(t * juce::MathConstants<double>::twoPi)); }, "DELAY SWEEP");
    case EffectId::flanger:
        return lfo(g, plot, p, live, v[0], juce::jlimit(.1f, 1.f, v[1] / 10.f),
                   [](double t) { return float(1 - 4 * std::abs(std::fmod(t, 1.) - .5)); }, "COMB SWEEP");
    case EffectId::phaser:
        return lfo(g, plot, p, live, v[0], std::max(.1f, float(v[1])),
                   [](double t) { return float(std::sin(t * juce::MathConstants<double>::twoPi)); }, "PHASE SWEEP");
    case EffectId::tremolo:
        return lfo(g, plot, p, live, v[0], std::max(.05f, float(v[1])),
                   [shape = float(v[2])](double t)
                   {
                       const float s = float(std::sin(t * juce::MathConstants<double>::twoPi));
                       const float sq = s >= 0 ? 1.f : -1.f;
                       return s + (sq - s) * shape;
                   }, "VOLUME");
    case EffectId::autoPan:
        return lfo(g, plot, p, live, v[0], std::max(.05f, float(v[1])),
                   [](double t) { return float(std::sin(t * juce::MathConstants<double>::twoPi)); }, "LEFT / RIGHT");
    case EffectId::ringMod:
        return lfo(g, plot, p, live, v[0], 1.f,
                   [](double t) { return float(std::sin(t * juce::MathConstants<double>::twoPi)); }, "CARRIER");
    case EffectId::delay: case EffectId::reverb: case EffectId::studioReverb:
    {
        // Echo taps (delay) or the decay envelope (reverbs) over a time axis, plus the scope.
        auto top = plot.withHeight(plot.getHeight() * .62f);
        const bool echoes = id == EffectId::delay;
        const double predelay = id == EffectId::reverb ? v[3] / 1000. : id == EffectId::studioReverb ? v[3] / 1000. : 0;
        const double decay = id == EffectId::reverb ? v[1] : id == EffectId::studioReverb ? v[2] : 0;
        const double span = echoes ? std::max(.5, double(v[0]) / 1000. * 8.) : std::max(.5, predelay + decay * 1.1);
        caption(g, top, p, echoes ? "ECHOES" : "DECAY",
                echoes ? juce::String(int(v[0])) + " ms" : juce::String(decay, 1) + " s");
        top = top.withTrimmedTop(18.f);
        g.setColour(p.plate.brighter(.06f));
        g.fillRoundedRectangle(top, 6.f);
        const auto xOf = [&](double t) { return top.getX() + 8.f + float(t / span) * (top.getWidth() - 16.f); };
        g.setColour(p.text.withAlpha(.8f));
        g.fillRoundedRectangle(xOf(0) - 2.f, top.getY() + 6.f, 4.f, top.getHeight() - 12.f, 2.f);
        if (echoes)
        {
            double gain = v[2];
            for (int k = 1; k <= 24 && k * v[0] / 1000. <= span; ++k, gain *= v[1])
            {
                const float h = float(gain) * (top.getHeight() - 12.f);
                g.setColour(p.accent.withAlpha(.35f + .65f * float(gain)));
                g.fillRoundedRectangle(xOf(k * v[0] / 1000.) - 3.f, top.getBottom() - 6.f - h, 6.f, h, 3.f);
            }
        }
        else
        {
            juce::Path env;
            env.startNewSubPath(xOf(predelay), top.getBottom() - 6.f);
            for (int i = 0; i <= 120; ++i)
            {
                const double t = predelay + i / 120. * (span - predelay);
                const double level = std::pow(10., -3. * (t - predelay) / std::max(.05, decay)); // -60 dB at decay
                env.lineTo(xOf(t), top.getBottom() - 6.f - float(level) * (top.getHeight() - 12.f) * float(v[id == EffectId::reverb ? 4 : 10] * .7 + .3));
            }
            env.lineTo(xOf(span), top.getBottom() - 6.f);
            env.closeSubPath();
            g.setColour(p.accent.withAlpha(.3f));
            g.fillPath(env);
            g.setColour(p.accent);
            g.strokePath(env, juce::PathStrokeType(2.f));
        }
        scope(g, plot.withTrimmedTop(top.getBottom() - plot.getY() + 8.f), live, p, p.accent);
        return;
    }
    case EffectId::transientShaper:
    {
        auto top = plot.withHeight(plot.getHeight() * .62f);
        caption(g, top, p, "HIT ENVELOPE",
                "Attack " + juce::String(int(v[0])) + "%  Sustain " + juce::String(int(v[1])) + "%");
        top = top.withTrimmedTop(18.f);
        g.setColour(p.plate.brighter(.06f));
        g.fillRoundedRectangle(top, 6.f);
        const auto envelope = [&](double t, float attack, float sustain)
        {
            const double peak = 1 + attack / 100.0 * .8, tail = std::pow(10., (1 + sustain / 100.0 * .8) * -1.2 * t);
            return t < .05 ? t / .05 * peak : std::max(0., peak * std::exp(-(t - .05) * 25.) + .45 * tail);
        };
        for (int pass = 0; pass < 2; ++pass)
        {
            juce::Path line;
            for (int i = 0; i <= 160; ++i)
            {
                const double t = i / 160.;
                const float y = top.getBottom() - 6.f - float(juce::jlimit(0., 1.4, envelope(t, pass ? v[0] : 0.f, pass ? v[1] : 0.f)) / 1.4) * (top.getHeight() - 12.f);
                const float x = top.getX() + 6.f + float(t) * (top.getWidth() - 12.f);
                if (i == 0)
                    line.startNewSubPath(x, y);
                else
                    line.lineTo(x, y);
            }
            g.setColour(pass ? p.accent : p.muted.withAlpha(.6f));
            g.strokePath(line, juce::PathStrokeType(pass ? 2.4f : 1.4f));
        }
        scope(g, plot.withTrimmedTop(top.getBottom() - plot.getY() + 8.f), live, p, p.accent);
        return;
    }
    case EffectId::balance:
        return stereo(g, plot, p, live, "GAIN / PAN",
                      juce::String(v[0], 1) + " dB   " +
                          (std::abs(v[1]) < .005f ? juce::String("C") : juce::String(std::abs(v[1]) * 100, 0) + (v[1] < 0 ? "% L" : "% R")));
    case EffectId::stereoWidth:
        return stereo(g, plot, p, live, "WIDTH", juce::String(v[0] * 100, 0) + "%");
    case EffectId::polarity:
        return stereo(g, plot, p, live, "POLARITY",
                      juce::String(v[0] >= .5f ? "Left inverted" : "Left normal") + "\n" +
                          (v[1] >= .5f ? "Right inverted" : "Right normal"));
    case EffectId::mute:
        return stereo(g, plot, p, live, "OUTPUT", v[0] >= .5f ? "Muted" : "Passing");
    case EffectId::stereoMatrix:
        return stereo(g, plot, p, live, "MATRIX",
                      "L = " + juce::String(v[0], 2) + " L + " + juce::String(v[1], 2) + " R\nR = " +
                          juce::String(v[2], 2) + " L + " + juce::String(v[3], 2) + " R");
    case EffectId::pitchCorrect:
    {
        // One octave of keys from the key; scale degrees lit, live detected/target pitch.
        std::array<int, 12> cents{};
        int tonic = 0;
        const int count = PitchCorrect::scaleDegrees(int(std::lround(v[1])), cents, tonic);
        auto keys = plot.withHeight(plot.getHeight() * .6f);
        const auto note = [](double hz)
        {
            if (!(hz > 0))
                return juce::String("--");
            const double midi = 69 + 12 * std::log2(hz / 440.);
            static const char *names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const int n = int(std::lround(midi));
            const int off = int(std::lround((midi - n) * 100));
            return juce::String(names[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1) + (off ? (off > 0 ? " +" : " ") + juce::String(off) + "c" : juce::String());
        };
        caption(g, keys, p, juce::String(PitchCorrect::scaleName(int(std::lround(v[1])))).toUpperCase(),
                note(live.detectedHz) + "  ->  " + note(live.targetHz));
        keys = keys.withTrimmedTop(18.f);
        const float w = keys.getWidth() / 24.f; // Quarter-tone columns over one octave.
        for (int q = 0; q < 24; ++q)
        {
            const int c = q * 50;
            bool degree = false;
            for (int d = 0; d < count; ++d)
                degree = degree || (cents[size_t(d)] + tonic + 1200) % 1200 == c;
            auto cell = juce::Rectangle<float>(keys.getX() + q * w, keys.getY(), w - 2.f, keys.getHeight());
            g.setColour(degree ? p.accent : q % 2 ? p.plate.brighter(.02f) : p.plate.brighter(.1f));
            g.fillRoundedRectangle(cell, 3.f);
        }
        if (live.detectedHz > 0)
        {
            const double centsAbove = std::fmod(1200 * std::log2(live.detectedHz / 261.6255653) - v[0] * 100 + 12000, 1200.);
            const float x = keys.getX() + float(centsAbove / 1200.) * keys.getWidth();
            g.setColour(p.warm);
            g.fillRoundedRectangle(x - 2.f, keys.getY() - 3.f, 4.f, keys.getHeight() + 6.f, 2.f);
        }
        scope(g, plot.withTrimmedTop(keys.getBottom() - plot.getY() + 8.f), live, p, p.accent);
        return;
    }
    case EffectId::vocoder:
    {
        // 16 band levels of the live output (Goertzel per band) with the carrier note.
        auto bars = plot.withHeight(plot.getHeight() * .62f);
        static const char *names[]{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const int n = int(std::lround(v[0]));
        caption(g, bars, p, "BANDS", "Carrier " + juce::String(names[n % 12]) + juce::String(n / 12 - 1));
        bars = bars.withTrimmedTop(18.f);
        const int span = std::min(live.count, 2048);
        const float w = bars.getWidth() / 16.f;
        for (int b = 0; b < 16; ++b)
        {
            const double hz = 100. * std::pow(80., b / 15.);
            double level = 0;
            if (live.mid && span > 64)
            {
                const double k = 2 * std::cos(juce::MathConstants<double>::twoPi * hz / fs);
                double s1 = 0, s2 = 0;
                for (int i = live.count - span; i < live.count; ++i)
                {
                    const double s = live.mid[i] + k * s1 - s2;
                    s2 = s1;
                    s1 = s;
                }
                level = std::sqrt(std::max(0., s1 * s1 + s2 * s2 - k * s1 * s2)) / span * 2;
            }
            const float h = clamp01((db(float(level)) + 60.f) / 60.f) * (bars.getHeight() - 4.f);
            g.setColour(p.plate.brighter(.06f));
            g.fillRoundedRectangle(bars.getX() + b * w + 1.f, bars.getY(), w - 3.f, bars.getHeight(), 3.f);
            g.setColour(p.accent);
            g.fillRoundedRectangle(bars.getX() + b * w + 1.f, bars.getBottom() - h, w - 3.f, h, 3.f);
        }
        scope(g, plot.withTrimmedTop(bars.getBottom() - plot.getY() + 8.f), live, p, p.accent);
        return;
    }
    case EffectId::beatGate:
    {
        // The 16-step pattern with the live playhead; stutter and half-time segments below.
        const int pattern = int(std::lround(v[0]));
        const double step = BeatGate::stepBeats(int(std::lround(v[1])));
        auto grid = plot.withHeight(plot.getHeight() * .5f);
        const double beat = live.beat;
        caption(g, grid, p, juce::String(BeatGate::patternName(pattern)).toUpperCase(),
                "Bar " + juce::String(int(std::floor(beat / 4)) + 1) + "  Beat " +
                    juce::String(int(std::floor(std::fmod(beat, 4.))) + 1));
        grid = grid.withTrimmedTop(18.f);
        const int current = int(std::floor(beat / step)) & 15;
        const float w = grid.getWidth() / 16.f;
        for (int s = 0; s < 16; ++s)
        {
            const bool open = BeatGate::stepOpen(pattern, s);
            auto cell = juce::Rectangle<float>(grid.getX() + s * w + 2.f, grid.getY(), w - 4.f, grid.getHeight());
            const float closed = 1.f - v[2];
            g.setColour(p.plate.brighter(.08f));
            g.fillRoundedRectangle(cell, 4.f);
            const float level = open ? 1.f : closed;
            auto lit = cell.withTrimmedTop(cell.getHeight() * (1.f - level));
            g.setColour((s == current ? p.warm : p.accent).withAlpha(open ? 1.f : .45f));
            if (level > 0)
                g.fillRoundedRectangle(lit, 4.f);
            if (s % 4 == 0)
            {
                g.setColour(p.text.withAlpha(.5f));
                g.drawHorizontalLine(int(cell.getBottom() + 3.f), cell.getX(), cell.getX() + w * 4.f - 4.f);
            }
        }
        auto lanes = plot.withTrimmedTop(grid.getBottom() - plot.getY() + 10.f);
        const double stutter = BeatGate::stutterBeats(int(std::lround(v[4])));
        const double hold = BeatGate::holdBeats(int(std::lround(v[5])));
        const double cycle = BeatGate::cycleBeats(int(std::lround(v[7])));
        const bool half = v[6] >= .5f;
        auto lane = [&](juce::Rectangle<float> r, const juce::String &name, bool on, double length, double position,
                        double firstPart)
        {
            g.setFont(juce::FontOptions(11.f, juce::Font::bold));
            g.setColour(on ? p.text : p.muted);
            g.drawText(name, r.removeFromLeft(86.f), juce::Justification::centredLeft);
            g.setColour(p.plate.brighter(.06f));
            g.fillRoundedRectangle(r, 5.f);
            if (!on)
            {
                g.setColour(p.muted);
                g.drawText("Off", r, juce::Justification::centred);
                return;
            }
            // Four bars (16 beats) of segments; the live part of each one is solid.
            const double beats = 16;
            for (double start = 0; start < beats; start += length)
            {
                const float x0 = r.getX() + float(start / beats) * r.getWidth();
                const float w0 = float(length / beats) * r.getWidth() - 2.f;
                g.setColour(p.accent.withAlpha(.25f));
                g.fillRoundedRectangle(x0, r.getY() + 3.f, w0, r.getHeight() - 6.f, 3.f);
                g.setColour(p.accent);
                g.fillRoundedRectangle(x0, r.getY() + 3.f, float(firstPart / beats) * r.getWidth(), r.getHeight() - 6.f, 3.f);
            }
            const float x = r.getX() + float(std::fmod(position, beats) / beats) * r.getWidth();
            g.setColour(p.warm);
            g.fillRoundedRectangle(x - 1.5f, r.getY(), 3.f, r.getHeight(), 1.5f);
        };
        const float laneHeight = (lanes.getHeight() - 6.f) / 2.f;
        lane(lanes.removeFromTop(laneHeight), "Stutter", stutter > 0, hold, beat, std::min(stutter, hold));
        lanes.removeFromTop(6.f);
        lane(lanes.removeFromTop(laneHeight), "Half-time", half, cycle, beat, cycle);
        return;
    }
    case EffectId::pump:
    {
        // One cycle of the volume shape with the live playhead, then the live waveform.
        const double cycle = Pump::cycleBeats(int(std::lround(v[0])));
        auto top = plot.withHeight(plot.getHeight() * .62f);
        caption(g, top, p, "VOLUME SHAPE", juce::String(Pump::cycleName(int(std::lround(v[0])))) + "  /  depth " +
                                                juce::String(int(std::lround(v[1] * 100))) + "%");
        top = top.withTrimmedTop(18.f);
        g.setColour(p.plate.brighter(.06f));
        g.fillRoundedRectangle(top, 6.f);
        const auto yOf = [&](float gain) { return top.getBottom() - 6.f - gain * (top.getHeight() - 12.f); };
        juce::Path line, fill;
        for (int x = 0; x <= int(top.getWidth()); ++x)
        {
            const double phase = 2. * x / top.getWidth() - v[5]; // Two cycles.
            const float y = yOf(Pump::shape(phase, v[1], v[2], v[3], v[4]));
            if (x == 0)
            {
                line.startNewSubPath(top.getX(), y);
                fill.startNewSubPath(top.getX(), top.getBottom());
                fill.lineTo(top.getX(), y);
            }
            line.lineTo(top.getX() + float(x), y);
            fill.lineTo(top.getX() + float(x), y);
        }
        fill.lineTo(top.getRight(), top.getBottom());
        fill.closeSubPath();
        g.setColour(p.accent.withAlpha(.2f));
        g.fillPath(fill);
        g.setColour(p.accent);
        g.strokePath(line, juce::PathStrokeType(2.4f));
        const double phase = std::fmod(live.beat / cycle, 2.);
        const float x = top.getX() + float((phase < 0 ? phase + 2 : phase) / 2.) * top.getWidth();
        g.setColour(p.warm);
        g.fillRoundedRectangle(x - 1.5f, top.getY(), 3.f, top.getHeight(), 1.5f);
        scope(g, plot.withTrimmedTop(top.getBottom() - plot.getY() + 8.f), live, p, p.accent);
        return;
    }
    case EffectId::loFi:
    {
        // Tape wobble (wow + flutter) with the live phase, tone and noise readouts, waveform.
        const float wow = v[0], flutter = v[1];
        lfo(g, plot, p, live, LoFi::wowHz, std::max(.08f, std::min(1.f, wow + flutter * .4f)),
            [wow, flutter](double t)
            {
                const double s = wow * std::sin(t * juce::MathConstants<double>::twoPi) +
                                 flutter * .35 * std::sin(t * juce::MathConstants<double>::twoPi * LoFi::flutterHz / LoFi::wowHz);
                return float(s / std::max(.05, double(wow + flutter * .35f)));
            },
            "TAPE WOBBLE  /  TONE " + juce::String(int(v[4] / 100) / 10.f, 1) + " kHz  /  HISS " +
                juce::String(int(std::lround(v[2] * 100))) + "%  /  CRACKLE " + juce::String(int(std::lround(v[3] * 100))) + "%");
        return;
    }
    case EffectId::convolution:
    {
        // The chosen space's impulse envelope (pre-delay, early reflections, decay to -60 dB)
        // over a time axis, then the live waveform.
        const int chosen = juce::jlimit(0, 4, int(std::lround(v[0])));
        const int space = chosen == 4 ? 1 : chosen; // File: drawn with a generic envelope.
        auto top = plot.withHeight(plot.getHeight() * .62f);
        caption(g, top, p, chosen == 4 ? juce::String("FILE  /  LOADED IMPULSE")
                                       : juce::String(ConvolutionReverb::names[space]).toUpperCase() + "  /  RT60 " +
                                             juce::String(ConvolutionReverb::rt60[space], 1) + " s",
                "PRE " + juce::String(int(std::lround(v[1]))) + " ms  /  MIX " + juce::String(int(std::lround(v[5] * 100))) + "%");
        top = top.withTrimmedTop(18.f);
        g.setColour(p.plate.brighter(.06f));
        g.fillRoundedRectangle(top, 6.f);
        const double span = 3.0, pre = v[1] / 1000.;
        const auto xOf = [&](double t) { return top.getX() + 6.f + float(t / span) * (top.getWidth() - 12.f); };
        juce::Path fill;
        fill.startNewSubPath(xOf(0), top.getBottom());
        for (int x = 0; x <= int(top.getWidth()) - 12; x += 2)
        {
            const double t = span * x / (top.getWidth() - 12.);
            const double level = t < pre ? 0. : std::exp(-6.907755 * (t - pre) / ConvolutionReverb::rt60[space]);
            fill.lineTo(xOf(t), top.getBottom() - float(level) * top.getHeight() * .85f);
        }
        fill.lineTo(top.getRight() - 6.f, top.getBottom());
        fill.closeSubPath();
        g.setColour(p.accent.withAlpha(.3f));
        g.fillPath(fill);
        g.setColour(p.accent);
        g.strokePath(fill, juce::PathStrokeType(1.6f));
        if (ConvolutionReverb::earlyMs[space] > 0)
            for (int tap = 0; tap < 6; ++tap)
            {
                const double t = pre + (ConvolutionReverb::earlyMs[space] * (1 + tap * .55)) / 1000.;
                g.setColour(p.warm);
                g.fillRect(xOf(t) - 1.f, top.getBottom() - top.getHeight() * .85f * float(std::pow(.72, tap)), 2.f,
                           top.getHeight() * .85f * float(std::pow(.72, tap)));
            }
        g.setColour(p.muted);
        g.setFont(juce::FontOptions(10.f));
        for (int s = 1; s <= 2; ++s)
            g.drawText(juce::String(s) + " s", int(xOf(s)) - 12, int(top.getBottom()) - 13, 24, 12, juce::Justification::centred);
        scope(g, plot.withTrimmedTop(top.getBottom() - plot.getY() + 8.f), live, p, p.accent);
        return;
    }
    case EffectId::waveshaper:
    {
        // The transfer curve (input -1..1 across, output up) with the live input peak as a dot,
        // and the live waveform on the right.
        const auto side = std::min(plot.getWidth() * .42f, plot.getHeight());
        auto square = plot.withWidth(side).withHeight(side).withCentre({plot.getX() + side * .5f + 4.f, plot.getCentreY()});
        g.setColour(p.plate.brighter(.06f));
        g.fillRoundedRectangle(square, 6.f);
        g.setColour(p.grid.withAlpha(.6f));
        g.drawHorizontalLine(int(square.getCentreY()), square.getX(), square.getRight());
        g.drawVerticalLine(int(square.getCentreX()), square.getY(), square.getBottom());
        const float driveGain = std::pow(10.f, v[1] / 20.f);
        const auto xOf = [&](float x) { return square.getCentreX() + x * square.getWidth() * .46f; };
        const auto yOf = [&](float y) { return square.getCentreY() - juce::jlimit(-1.1f, 1.1f, y) * square.getHeight() * .42f; };
        juce::Path line;
        for (int i = 0; i <= 120; ++i)
        {
            const float x = -1.f + 2.f * float(i) / 120.f;
            const auto pt = juce::Point<float>(xOf(x), yOf(Waveshaper::curve(v[0], x * driveGain + v[2])));
            if (i == 0)
                line.startNewSubPath(pt);
            else
                line.lineTo(pt);
        }
        g.setColour(p.accent);
        g.strokePath(line, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));
        const float in = juce::jlimit(0.f, 1.f, live.inputPeak);
        const auto dot = juce::Point<float>(xOf(in), yOf(Waveshaper::curve(v[0], in * driveGain + v[2])));
        g.setColour(p.warm);
        g.fillEllipse(dot.x - 5.f, dot.y - 5.f, 10.f, 10.f);
        static const char *names[]{"SOFT", "HARD", "FOLD", "TUBE", "RECTIFY"};
        auto right = plot.withTrimmedLeft(side + 18.f);
        caption(g, right, p, "SHAPE", names[juce::jlimit(0, 4, int(std::lround(v[0])))] + juce::String("  /  DRIVE ") +
                                          juce::String(v[1], 1) + " dB");
        scope(g, right.withTrimmedTop(22.f), live, p, p.accent);
        return;
    }
    case EffectId::frequencyShifter:
    {
        // Harmonics of a 220 Hz tone (muted) and where the shift moves them (accent): equal
        // Hz steps, so the shifted partials stop being harmonic. Live waveform underneath.
        auto top = plot.withHeight(plot.getHeight() * .62f);
        const double s = v[0];
        caption(g, top, p, "PARTIALS  220 Hz x 1..8",
                (s >= 0 ? "+" : "") + juce::String(int(std::lround(s))) + " Hz" +
                    (std::abs(v[1]) > .5f ? "  /  R " + juce::String((s + v[1]) >= 0 ? "+" : "") +
                                                juce::String(int(std::lround(s + v[1]))) + " Hz"
                                          : juce::String()));
        top = top.withTrimmedTop(18.f);
        g.setColour(p.plate.brighter(.06f));
        g.fillRoundedRectangle(top, 6.f);
        const auto xOf = [&](double hz) { return top.getX() + 8.f + float(std::log(std::max(20., hz) / 20.) / std::log(200.)) * (top.getWidth() - 16.f); };
        for (int h = 1; h <= 8; ++h)
        {
            const double f = 220. * h, to = f + s;
            const float height = top.getHeight() * (.85f - .07f * float(h));
            g.setColour(p.muted.withAlpha(.6f));
            g.fillRect(xOf(f) - 1.f, top.getBottom() - height, 2.f, height);
            if (to > 20)
            {
                g.setColour(p.accent);
                g.fillRoundedRectangle(xOf(to) - 2.f, top.getBottom() - height * .9f, 4.f, height * .9f, 2.f);
                g.setColour(p.warm.withAlpha(.7f));
                g.drawArrow(juce::Line<float>(xOf(f), top.getBottom() - height - 4.f, xOf(to), top.getBottom() - height - 4.f), 1.2f, 6.f, 6.f);
            }
        }
        scope(g, plot.withTrimmedTop(top.getBottom() - plot.getY() + 8.f), live, p, p.accent);
        return;
    }
    case EffectId::formant:
    {
        // The three formant bands of the current vowel, summed, over the frequency grid.
        double hz[3], level[3];
        Formant::formants(v[0], v[1], hz, level);
        const double out = std::pow(10., v[4] / 20.), mix = v[3];
        responseCurve(g, plot, p, 30.f, [&](double f)
                      {
                          double sum = 0;
                          for (int i = 0; i < 3; ++i)
                              sum += level[i] * std::pow(10., biquadDb(2, hz[i], v[2], 0, f, fs) / 20.);
                          return 20 * std::log10(std::max(1e-6, mix * sum * out + (1 - mix)));
                      });
        static const char *names[]{"A", "E", "I", "O", "U"};
        const int nearest = juce::jlimit(0, 4, int(std::lround(v[0])));
        g.setColour(p.warm);
        g.setFont(juce::FontOptions(26.f, juce::Font::bold));
        g.drawText(names[nearest], plot.withTrimmedLeft(plot.getWidth() - 60.f).withHeight(36.f).toNearestInt(),
                   juce::Justification::centred);
        for (int i = 0; i < 3; ++i)
        {
            const float x = xOfHz(plot, hz[i]);
            g.setColour(p.warm.withAlpha(.7f));
            g.drawVerticalLine(int(x), plot.getY() + 4.f, plot.getBottom() - 16.f);
            g.setFont(juce::FontOptions(10.f));
            g.drawText("F" + juce::String(i + 1), int(x) + 3, int(plot.getY()) + 4, 24, 12, juce::Justification::centredLeft);
        }
        return;
    }
    case EffectId::spectrumAnalyzer: case EffectId::parametricEq: case EffectId::count:
        return; // Drawn by the editor itself.
    }
}
} // namespace vibera::effects::visuals
