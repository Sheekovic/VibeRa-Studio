// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../../src/drums/DrumSynth.h"
#include "dsp/EffectsEngine.h"
#include "AllocationGuard.h"
#include <functional>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace vibera::effects;
namespace
{
unsigned checks = 0;
void require(bool pass, const std::string &message)
{
    ++checks;
    if (!pass)
        throw std::runtime_error(message);
}
struct Stereo
{
    std::vector<float> left, right;
    explicit Stereo(int size) : left(std::size_t(size)), right(std::size_t(size)) {}
};
Stereo render(EffectId effect, const Values &parameters, double rate, int count, int partition = 256,
              bool impulse = false)
{
    Stereo audio(count);
    for (int i = 0; i < count; ++i)
    {
        audio.left[std::size_t(i)] =
            impulse ? (i == 0 ? 1.0f : 0) : .2f * float(std::sin(i * 2 * std::numbers::pi * 317 / rate));
        audio.right[std::size_t(i)] =
            impulse ? 0 : .13f * float(std::sin(i * 2 * std::numbers::pi * 719 / rate));
    }
    Engine engine;
    engine.prepare(rate);
    require(engine.setParameters(effect, parameters), "Default/native values refused");
    for (int position = 0; position < count;)
    {
        const auto frames = std::min(partition, count - position);
        require(engine.setParameters(effect, parameters), "Repeated unchanged parameters refused");
        engine.process(audio.left.data() + position, audio.right.data() + position, frames);
        position += frames;
    }
    return audio;
}
double energy(const std::vector<float> &samples, int begin, int end)
{
    double result = 0;
    for (int i = begin; i < end; ++i)
        result += double(samples[std::size_t(i)]) * samples[std::size_t(i)];
    return result / std::max(1, end - begin);
}
void catalogueAndPartitions()
{
    std::set<std::string> ids;
    for (const auto &spec : catalogueEntries())
    {
        require(spec.parameterCount > 0 && spec.parameterCount <= maximumParameters, "Bad parameter count");
        for (std::size_t i = 0; i < spec.parameterCount; ++i)
        {
            const auto &p = spec.parameters[i];
            require(ids.insert(std::string(spec.slug) + "." + p.id).second, "Duplicate stable ID");
            require(p.minimum <= p.initial && p.initial <= p.maximum, "Invalid default");
        }
        for (double rate : {8000.0, 44100.0, 48000.0, 96000.0})
        {
            const auto a = render(spec.id, defaultValues(spec.id), rate, 4096, 256);
            const auto b = render(spec.id, defaultValues(spec.id), rate, 4096, 73);
            for (std::size_t i = 0; i < a.left.size(); ++i)
                require(std::isfinite(a.left[i]) && std::isfinite(a.right[i]) &&
                            std::abs(a.left[i] - b.left[i]) < 1.0e-6f &&
                            std::abs(a.right[i] - b.right[i]) < 1.0e-6f,
                        std::string(spec.name) + " changed with block partition/sample rate");
        }
    }
}
void utilityAndFilterOracles()
{
    auto values = defaultValues(EffectId::balance);
    values[0] = -6;
    values[1] = 1;
    const auto pan = render(EffectId::balance, values, 48000, 64, 64, true);
    require(pan.left[0] == 0 && pan.right[0] == 0, "Hard-right balance failed");
    values[1] = 0;
    const auto trim = render(EffectId::balance, values, 48000, 64, 64, true);
    require(std::abs(trim.left[0] - std::pow(10.0f, -.3f)) < 1.0e-6f, "Trim dB gain wrong");
    values = defaultValues(EffectId::stereoWidth);
    values[0] = 0;
    const auto mono = render(EffectId::stereoWidth, values, 48000, 256, 64, true);
    require(mono.left[0] == .5f && mono.right[0] == .5f, "Mid/side mono fold incorrect");
    values = defaultValues(EffectId::stereoMatrix);
    values[0] = values[3] = 0;
    values[1] = values[2] = 1;
    const auto swap = render(EffectId::stereoMatrix, values, 48000, 64, 64, true);
    require(swap.left[0] == 0 && swap.right[0] == 1, "Stereo matrix does not swap channels");
    Engine dc;
    dc.prepare(48000);
    require(dc.setParameters(EffectId::dcBlock, defaultValues(EffectId::dcBlock)), "DC block setup");
    float l = 0, r = 0;
    for (int i = 0; i < 48000; ++i)
    {
        l = .5f;
        r = -.25f;
        dc.process(&l, &r, 1);
    }
    require(std::abs(l) < 1.0e-5f && std::abs(r) < 1.0e-5f, "DC offset did not decay");
    const auto response = [](EffectId id, Values p, double frequency)
    {
        Engine engine;
        engine.prepare(48000);
        require(engine.setParameters(id, p), "Filter setup refused");
        double output = 0, input = 0;
        for (int i = 0; i < 12000; ++i)
        {
            float l = .1f * float(std::sin(2 * std::numbers::pi * frequency * i / 48000));
            float r = l;
            const auto original = l;
            engine.process(&l, &r, 1);
            if (i >= 6000)
            {
                input += original * original;
                output += l * l;
            }
        }
        return std::sqrt(output / input);
    };
    values = defaultValues(EffectId::filter);
    require(response(EffectId::filter, values, 100) > .95 && response(EffectId::filter, values, 10000) < .02,
            "Low-pass frequency response");
    values[0] = 1;
    require(response(EffectId::filter, values, 100) < .02 && response(EffectId::filter, values, 10000) > .95,
            "High-pass frequency response");
    values = defaultValues(EffectId::graphicEq);
    require(std::abs(response(EffectId::graphicEq, values, 1000) - 1) < 1.0e-5, "Flat EQ not unity");
    values[0] = 6;
    require(response(EffectId::graphicEq, values, 80) > 1.9, "80 Hz band does not boost");
    values = defaultValues(EffectId::bassBoost);
    values[1] = 12;
    require(response(EffectId::bassBoost, values, 30) > 3 &&
                response(EffectId::bassBoost, values, 5000) < 1.01,
            "Bass shelf response incorrect");
}
void dynamicsOracles()
{
    Engine engine;
    engine.prepare(48000);
    require(engine.setParameters(EffectId::compressor, defaultValues(EffectId::compressor)), "Comp setup");
    float l = 0, r = 0;
    for (int i = 0; i < 48000; ++i)
    {
        l = 1;
        r = .25f;
        engine.process(&l, &r, 1);
    }
    require(std::abs(l - .2113489f) < .0001f && std::abs(r / l - .25f) < 1.0e-6f,
            "Linked compression static curve");
    require(engine.setParameters(EffectId::limiter, defaultValues(EffectId::limiter)), "Limiter setup");
    for (int i = 0; i < 2000; ++i)
    {
        l = i % 2 ? -2.0f : 2.0f;
        r = l * .25f;
        engine.process(&l, &r, 1);
        require(std::abs(l) <= .891252f && std::abs(r / l - .25f) < 1.0e-6f,
                "Limiter sample ceiling/stereo link");
    }
    auto values = defaultValues(EffectId::bitCrusher);
    values[0] = 2;
    values[1] = 4;
    require(engine.setParameters(EffectId::bitCrusher, values), "Crush setup");
    for (int i = 0; i < 8; ++i)
    {
        l = i == 0 ? -.95f : .45f;
        r = .95f;
        engine.process(&l, &r, 1);
        require(l == (i < 4 ? -1.0f : .5f) && r == .5f, "Crush quantization/hold mismatch");
    }
    values = defaultValues(EffectId::gate);
    require(engine.setParameters(EffectId::gate, values), "Gate setup");
    for (int i = 0; i < 48000; ++i)
    {
        l = .001f;
        r = -.001f;
        engine.process(&l, &r, 1);
    }
    require(std::abs(l) < 1.0e-6f, "Gate did not attenuate below threshold");
}
void exactBinaryEndpoints()
{
    for (const auto effect : {EffectId::mute, EffectId::polarity})
    {
        Engine engine;
        engine.prepare(48000);
        auto p = defaultValues(effect);
        require(engine.setParameters(effect, p), "Binary utility setup");
        float l = 1, r = .25f;
        engine.process(&l, &r, 1);
        p[0] = 1;
        require(engine.setParameters(effect, p), "Binary utility live edit");
        float previous = l;
        for (int i = 0; i < 242; ++i)
        {
            l = 1;
            r = .25f;
            engine.process(&l, &r, 1);
            require(std::abs(l - previous) < .009f, "Binary utility edit did not fade smoothly");
            previous = l;
        }
        require(l == (effect == EffectId::mute ? 0.0f : -1.0f),
                "Live mute/polarity retained a residual instead of its exact endpoint");
    }
}
void timeOracles()
{
    auto p = defaultValues(EffectId::delay);
    p[0] = 10;
    p[1] = 0;
    p[2] = 1;
    const auto impulse = render(EffectId::delay, p, 48000, 2048, 73, true);
    for (int i = 0; i < 2048; ++i)
        require(std::abs(impulse.left[std::size_t(i)] - (i == 480 ? 1.0f : 0)) < 1.0e-5f &&
                    impulse.right[std::size_t(i)] == 0,
                "Delay impulse position or stereo isolation");
    p[1] = .5f;
    p[3] = 1;
    p[4] = 20000;
    const auto ping = render(EffectId::delay, p, 48000, 2048, 256, true);
    require(ping.left[480] > .99f && ping.right[960] > .3f && std::abs(ping.left[960]) < 1.0e-5f,
            "Delay ping-pong feedback routing");
    p = defaultValues(EffectId::reverb);
    p[3] = 0;
    p[4] = 1;
    const auto room = render(EffectId::reverb, p, 48000, 48000 * 5, 257, true);
    require(room.left[0] == 0 && energy(room.left, 4800, 24000) > 1.0e-9 &&
                energy(room.left, 48000 * 4, 48000 * 5) < energy(room.left, 48000, 48000 * 2),
            "Reverb causality, tail energy or decay");
}
void modulatedFilterRegression()
{
    // The original DF2T prototype overflowed at 6.65 s under these legal edits.
    // Test actual state evolution, not only instantaneous pole stability.
    for (int mode = 0; mode < 3; ++mode)
    {
        Engine engine;
        engine.prepare(48000);
        auto p = defaultValues(EffectId::filter);
        p[0] = float(mode);
        p[1] = 50;
        p[2] = 12;
        float maximum = 0, finalSecond = 0;
        std::array<float, 64> left{}, right{};
        for (int position = 0; position < 48000 * 8; position += 64)
        {
            p[1] = 50 + 30 * float(std::sin(2 * std::numbers::pi * 100 * position / 48000));
            require(engine.setParameters(EffectId::filter, p), "Legal modulated filter control refused");
            left.fill(0);
            right.fill(0);
            if (position == 0)
                left[0] = .1f;
            engine.process(left.data(), right.data(), 64);
            for (float sample : left)
            {
                require(std::isfinite(sample) && std::abs(sample) < 1,
                        "Rapid legal filter automation grew unstable");
                maximum = std::max(maximum, std::abs(sample));
                if (position >= 48000 * 7)
                    finalSecond = std::max(finalSecond, std::abs(sample));
            }
        }
        require(maximum > 0 && finalSecond < .00001f, "Modulated filter failed to decay");
    }
}
void safetyAndSwitching()
{
    Engine engine;
    engine.prepare(48000);
    std::array<float, 256> left{}, right{};
    for (const auto &spec : catalogueEntries())
    {
        auto p = defaultValues(spec.id);
        require(engine.setParameters(spec.id, p), "Safety setup");
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            if (scenario < 2)
                for (std::size_t j = 0; j < spec.parameterCount; ++j)
                    p[j] = scenario == 0 ? spec.parameters[j].minimum : spec.parameters[j].maximum;
            require(engine.setParameters(spec.id, p), "Extreme legal parameters refused");
            for (int block = 0; block < 8; ++block)
            {
                for (std::size_t i = 0; i < left.size(); ++i)
                {
                    left[i] = i % 2 ? .8f : -.8f;
                    right[i] = i % 3 ? -.7f : .7f;
                }
                left[0] = std::numeric_limits<float>::infinity();
                right[0] = std::numeric_limits<float>::quiet_NaN();
                {
                    effects_test::CallbackScope scope;
                    engine.setParameters(spec.id, p);
                    engine.process(left.data(), right.data(), int(left.size()));
                }
                for (std::size_t i = 0; i < left.size(); ++i)
                    require(std::isfinite(left[i]) && std::isfinite(right[i]),
                            std::string(spec.name) + " non-finite output");
            }
        }
        auto bad = p;
        bad[0] = std::numeric_limits<float>::quiet_NaN();
        require(!engine.setParameters(spec.id, bad), "NaN parameter admitted");
        bad[0] = spec.parameters[0].maximum + 1;
        require(!engine.setParameters(spec.id, bad), "Out-of-range parameter admitted");
        {
            effects_test::CallbackScope scope;
            engine.reset();
        }
        left.fill(0);
        right.fill(0);
        engine.process(left.data(), right.data(), int(left.size()));
        require(std::all_of(left.begin(), left.end(), [](float x) { return x == 0; }) &&
                    std::all_of(right.begin(), right.end(), [](float x) { return x == 0; }),
                std::string(spec.name) + " reset retained stale audio");
    }
    require(effects_test::allocations == 0 && effects_test::destructions == 0,
            "DSP parameter/process/reset allocated or destroyed heap objects");
    require(!engine.setParameters(EffectId::count, {}), "Invalid effect admitted");
    bool refused = false;
    try
    {
        engine.prepare(std::numeric_limits<double>::quiet_NaN());
    }
    catch (const std::invalid_argument &)
    {
        refused = true;
    }
    require(refused, "Invalid sample rate admitted");
}
// VibeRa Tune: a sine between two scale degrees comes out on the nearest one.
double measuredHz(const std::vector<float> &signal, int from, double rate)
{
    double first = -1, last = -1;
    int crossings = 0;
    for (std::size_t i = std::size_t(from) + 1; i < signal.size(); ++i)
        if (signal[i - 1] < 0 && signal[i] >= 0)
        {
            const double t = double(i - 1) + signal[i - 1] / double(signal[i - 1] - signal[i]);
            if (first < 0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}
double tuned(double inputHz, int key, int scale, double rate = 48000, float amount = 1, float transpose = 0,
             float flex = 0)
{
    Engine engine;
    engine.prepare(rate);
    Values values = defaultValues(EffectId::pitchCorrect);
    values[0] = float(key);
    values[1] = float(scale);
    values[2] = 0; // Instant retune.
    values[3] = amount;
    values[8] = transpose;
    values[6] = flex;
    require(engine.setParameters(EffectId::pitchCorrect, values), "Tune parameters accepted");
    const int count = int(rate * 1.5);
    auto left = std::vector<float>(static_cast<std::size_t>(count)), right = left;
    for (int i = 0; i < count; ++i)
        left[std::size_t(i)] = right[std::size_t(i)] = .3f * float(std::sin(i * 2 * std::numbers::pi * inputHz / rate));
    const auto before = effects_test::allocations;
    for (int position = 0; position < count; position += 256)
    {
        effects_test::CallbackScope scope;
        engine.process(left.data() + position, right.data() + position, std::min(256, count - position));
    }
    require(effects_test::allocations == before, "Tune processing allocates nothing");
    return measuredHz(left, int(rate * .5), rate);
}
void tuneOracles()
{
    // Shifter accuracy and the added controls: transparent at amount 0, octave transpose,
    // Flex leaves a note far from every degree alone, and the delay is reported.
    require(std::abs(tuned(450, 0, 0, 48000, 0) - 450) < .5, "Tune amount 0 passes the pitch through");
    require(std::abs(tuned(450, 0, 0, 48000, 0, 12) - 900) < 1, "Tune transpose +12 doubles the pitch");
    require(std::abs(tuned(450, 0, 0, 48000, 0, -1) - 424.74) < .5, "Tune transpose -1 lowers a semitone");
    require(std::abs(tuned(452, 0, 1, 48000, 1, 0, 20) - 452) < 1.5,
            "Flex 20 cents leaves a note about 47 cents from C major alone");
    require(std::abs(tuned(442, 0, 1, 48000, 1, 0, 20) - 440) < 1.5, "Flex still corrects a note near a degree");
    {
        Engine engine;
        engine.prepare(48000);
        require(engine.latencySamples(EffectId::pitchCorrect) > 128 && engine.latencySamples(EffectId::reverb) == 0,
                "Tune reports its fixed delay; other effects report none");
    }
    const double a = tuned(450, 0, 0);
    require(std::abs(a - 440) < 1.5, "Chromatic: 450 Hz is corrected to A4 440 Hz (got " + std::to_string(a) + ")");
    const double halfFlat = 329.6276 * std::pow(2., -50. / 1200);
    const double e = tuned(330, 0, 5);
    require(std::abs(e - halfFlat) < 1.5,
            "Rast on C: E4 is corrected to E half-flat (" + std::to_string(halfFlat) + ", got " + std::to_string(e) + ")");
    const double kept = tuned(440, 9, 2);
    require(std::abs(kept - 440) < 1.5, "A note already in the scale is kept (A minor, A4)");
    Engine silent;
    silent.prepare(48000);
    require(silent.setParameters(EffectId::pitchCorrect, defaultValues(EffectId::pitchCorrect)), "Defaults accepted");
    std::vector<float> l(4096, 0.f), r(4096, 0.f);
    silent.process(l.data(), r.data(), 4096);
    require(std::all_of(l.begin(), l.end(), [](float v) { return v == 0; }), "Silence stays silent");
}
void vocoderOracles()
{
    Engine engine;
    engine.prepare(48000);
    Values values = defaultValues(EffectId::vocoder);
    values[0] = 57; // Carrier A3 = 220 Hz.
    values[2] = 0;  // No noise: a pure, pitched carrier.
    require(engine.setParameters(EffectId::vocoder, values), "Vocoder parameters accepted");
    const int count = 48000;
    auto left = std::vector<float>(static_cast<std::size_t>(count)), right = left;
    // Modulator: a 130 Hz harmonic-rich buzz (pitch unrelated to the carrier).
    for (int i = 0; i < count; ++i)
        left[std::size_t(i)] = right[std::size_t(i)] =
            .3f * float(std::fmod(i * 130. / 48000, 1.) * 2 - 1);
    const auto before = effects_test::allocations;
    for (int position = 0; position < count; position += 256)
    {
        effects_test::CallbackScope scope;
        engine.process(left.data() + position, right.data() + position, std::min(256, count - position));
    }
    require(effects_test::allocations == before, "Vocoder allocates nothing");
    double energy = 0;
    for (int i = count / 2; i < count; ++i)
        energy += double(left[std::size_t(i)]) * left[std::size_t(i)];
    require(energy / (count / 2) > 1.e-5, "A voiced modulator opens the bands");
    const double pitch = measuredHz(left, count / 2, 48000);
    require(std::abs(pitch - 220) < 4 || std::abs(pitch - 440) < 6,
            "The output follows the carrier pitch, not the modulator (got " + std::to_string(pitch) + ")");
    Engine silent;
    silent.prepare(48000);
    require(silent.setParameters(EffectId::vocoder, values), "Vocoder defaults accepted");
    std::vector<float> l(9600, 0.f), r(9600, 0.f);
    silent.process(l.data(), r.data(), 9600);
    double rest = 0;
    for (float v : l)
        rest = std::max(rest, double(std::abs(v)));
    require(rest < 1.e-4, "Without a modulator the carrier stays closed");
}
} // namespace
// Studio processors: each one measured on what it is for.
struct Rendered
{
    std::vector<float> left, right;
};
Rendered renderEffect(EffectId effect, Values values, const std::function<float(int)> &source, int count,
                      double rate = 48000)
{
    Engine engine;
    engine.prepare(rate, effect);
    require(engine.setParameters(effect, values), "Studio effect parameters accepted");
    Rendered out{std::vector<float>(size_t(count)), std::vector<float>(size_t(count))};
    for (int i = 0; i < count; ++i)
        out.left[size_t(i)] = out.right[size_t(i)] = source(i);
    const auto before = effects_test::allocations;
    for (int position = 0; position < count; position += 256)
    {
        effects_test::CallbackScope scope;
        engine.process(out.left.data() + position, out.right.data() + position, std::min(256, count - position));
    }
    require(effects_test::allocations == before, "Studio effect processing allocates nothing");
    return out;
}
double rmsOf(const std::vector<float> &x, int from, int to)
{
    double sum = 0;
    for (int i = from; i < to; ++i)
        sum += double(x[size_t(i)]) * x[size_t(i)];
    return std::sqrt(sum / std::max(1, to - from));
}
std::function<float(int)> sine(double hz, double amplitude = .25)
{
    return [=](int i) { return float(amplitude * std::sin(2 * std::numbers::pi * hz * i / 48000)); };
}
void studioOracles()
{
    const int n = 48000;
    // Parametric EQ: a +12 dB bell at 1 kHz lifts 1 kHz by 12 dB, leaves 100 Hz alone,
    // and the editor response function agrees with the measurement.
    auto eq = defaultValues(EffectId::parametricEq);
    eq[3] = 1000;
    eq[4] = 12;
    eq[5] = 1;
    const double lifted = 20 * std::log10(rmsOf(renderEffect(EffectId::parametricEq, eq, sine(1000), n).left, 24000, n) /
                                          rmsOf(renderEffect(EffectId::balance, defaultValues(EffectId::balance), sine(1000), n).left, 24000, n));
    require(std::abs(lifted - 12) < .3, "Parametric EQ bell lifts its centre by its gain");
    const double low = 20 * std::log10(rmsOf(renderEffect(EffectId::parametricEq, eq, sine(100), n).left, 24000, n) /
                                       (.25 / std::sqrt(2.)));
    require(std::abs(low) < .5, "Parametric EQ bell leaves distant frequencies alone");
    require(std::abs(StudioEffects::eqResponseDb(eq, 1000, 48000) - 12) < .05 &&
                std::abs(StudioEffects::eqResponseDb(eq, 100, 48000) - low) < .3,
            "Parametric EQ response curve matches the measured gain");
    eq = defaultValues(EffectId::parametricEq);
    eq[0] = 400;
    const double cut = 20 * std::log10(rmsOf(renderEffect(EffectId::parametricEq, eq, sine(50), n).left, 24000, n) /
                                       (.25 / std::sqrt(2.)));
    require(cut < -30, "Parametric EQ low cut removes rumble");
    // De-esser: loud 8 kHz is turned down, 300 Hz is untouched.
    auto ds = defaultValues(EffectId::deEsser);
    ds[0] = 7000;
    ds[1] = -40;
    ds[2] = 12;
    const double hiss = rmsOf(renderEffect(EffectId::deEsser, ds, sine(8000, .5), n).left, 24000, n) / (.5 / std::sqrt(2.));
    const double body = rmsOf(renderEffect(EffectId::deEsser, ds, sine(300, .5), n).left, 24000, n) / (.5 / std::sqrt(2.));
    require(20 * std::log10(hiss) < -4 && std::abs(20 * std::log10(body)) < .5,
            "De-esser reduces sibilance and leaves the body of the voice");
    // Transient shaper: +100% attack makes a struck note's first 5 ms louder than its tail.
    const auto hit = [](int i)
    {
        const int t = i % 12000;
        return float(.3 * std::exp(-t / 2400.) * std::sin(2 * std::numbers::pi * 220 * t / 48000));
    };
    auto ts = defaultValues(EffectId::transientShaper);
    ts[0] = 100;
    const auto punchy = renderEffect(EffectId::transientShaper, ts, hit, n);
    const auto flat = renderEffect(EffectId::balance, defaultValues(EffectId::balance), hit, n);
    require(rmsOf(punchy.left, 24000, 24240) / rmsOf(flat.left, 24000, 24240) >
                1.15 * rmsOf(punchy.left, 30000, 34000) / rmsOf(flat.left, 30000, 34000),
            "Transient shaper attack emphasises the hit over the tail");
    // Multiband at zero depth: the three bands sum back flat across the spectrum.
    auto mb = defaultValues(EffectId::multiband);
    mb[2] = 0;
    for (double hz : {60., 120., 700., 2500., 9000.})
    {
        const double g = rmsOf(renderEffect(EffectId::multiband, mb, sine(hz), n).left, 24000, n) / (.25 / std::sqrt(2.));
        require(std::abs(20 * std::log10(g)) < .5, "Multiband crossovers sum flat at zero depth");
    }
    mb[2] = 1;
    const double squashed =
        rmsOf(renderEffect(EffectId::multiband, mb, sine(700, .9), n).left, 24000, n) / (.9 / std::sqrt(2.));
    require(20 * std::log10(squashed) < -3, "Multiband downward compression turns loud bands down");
    // Saturator: adds harmonics; mix 0 is the dry signal.
    auto sat = defaultValues(EffectId::saturator);
    sat[0] = 18;
    const auto driven = renderEffect(EffectId::saturator, sat, sine(200, .5), n);
    double third = 0;
    for (int i = 24000; i < n; ++i)
        third += driven.left[size_t(i)] * std::sin(2 * std::numbers::pi * 600 * i / 48000);
    require(std::abs(third) / 12000 > .01, "Saturator adds harmonics");
    sat[3] = 0;
    const auto dry = renderEffect(EffectId::saturator, sat, sine(200, .5), n);
    require(std::abs(rmsOf(dry.left, 24000, n) / (.5 / std::sqrt(2.)) - 1) < .02, "Saturator mix 0 is dry");
    // Maximizer: +18 dB never passes the ceiling, and the lookahead is reported.
    auto mx = defaultValues(EffectId::maximizer);
    mx[0] = 18;
    mx[1] = -1;
    const auto loud = renderEffect(EffectId::maximizer, mx, sine(100, .5), n);
    float top = 0;
    for (float v : loud.left)
        top = std::max(top, std::abs(v));
    require(top <= std::pow(10.f, -1.f / 20) + 1e-6f && rmsOf(loud.left, 24000, n) > .5,
            "Maximizer holds the ceiling while raising loudness");
    Engine engine;
    engine.prepare(48000);
    require(engine.latencySamples(EffectId::maximizer) == 72, "Maximizer reports its 1.5 ms lookahead");
    // Studio Reverb: the measured decay follows the Decay setting, the tail is stereo,
    // and Mix 0 is the dry signal.
    for (float decay : {1.f, 3.f})
    {
        auto rv = defaultValues(EffectId::studioReverb);
        rv[2] = decay;
        rv[3] = 0;
        rv[7] = 20;
        rv[8] = 20000;
        rv[10] = 1;
        const int length = int(48000 * (decay * 1.6 + .5));
        const auto tail = renderEffect(EffectId::studioReverb, rv, [](int i) { return i == 0 ? 1.f : 0.f; }, length);
        // Schroeder backward integration of the energy, then the -5..-25 dB slope.
        std::vector<double> energy(static_cast<std::size_t>(length), 0.0);
        double total = 0;
        for (int i = length - 1; i >= 0; --i)
        {
            total += double(tail.left[size_t(i)]) * tail.left[size_t(i)] + double(tail.right[size_t(i)]) * tail.right[size_t(i)];
            energy[size_t(i)] = total;
        }
        int t5 = -1, t25 = -1;
        for (int i = 0; i < length; ++i)
        {
            const double db = 10 * std::log10(energy[size_t(i)] / energy[0] + 1e-30);
            if (t5 < 0 && db <= -5)
                t5 = i;
            if (t25 < 0 && db <= -25)
                t25 = i;
        }
        require(t5 > 0 && t25 > t5, "Studio Reverb tail decays");
        const double rt60 = (t25 - t5) / 48000. * 3.;
        require(std::abs(rt60 - decay) < decay * .3,
                "Studio Reverb decay matches its setting (" + std::to_string(rt60) + " s for " + std::to_string(decay) + " s)");
        double difference = 0;
        for (int i = 2000; i < 20000; ++i)
            difference += std::abs(tail.left[size_t(i)] - tail.right[size_t(i)]);
        require(difference > 1, "Studio Reverb tail is stereo");
    }
    auto dryVerb = defaultValues(EffectId::studioReverb);
    dryVerb[10] = 0;
    require(std::abs(rmsOf(renderEffect(EffectId::studioReverb, dryVerb, sine(500), n).left, 24000, n) / (.25 / std::sqrt(2.)) - 1) < .01,
            "Studio Reverb mix 0 is dry");
    // Beat Gate at the free-running 120 BPM clock (no host time): 1 beat = 24000 samples.
    {
        constexpr int beat = 24000;
        auto gate = defaultValues(EffectId::beatGate); // Pulse, 1/16 steps (6000 samples), depth 1.
        const auto pulse = renderEffect(EffectId::beatGate, gate, sine(500), 2 * beat);
        const double open = rmsOf(pulse.left, 1000, 5000), closed = rmsOf(pulse.left, 7000, 11000);
        require(open > .17 && closed < .002, "Beat Gate Pulse opens on even 1/16 steps and closes on odd ones (" +
                                                 std::to_string(open) + ", " + std::to_string(closed) + ")");
        require(rmsOf(pulse.left, 13000, 17000) > .17, "Beat Gate reopens on the next step");
        auto halfDepth = gate;
        halfDepth[2] = .5f;
        const double partial = rmsOf(renderEffect(EffectId::beatGate, halfDepth, sine(500), beat).left, 7000, 11000);
        require(std::abs(partial / open - .5) < .03, "Gate depth 0.5 halves the closed steps");
        auto dry = gate;
        dry[8] = 0;
        require(std::abs(rmsOf(renderEffect(EffectId::beatGate, dry, sine(500), beat).left, 7000, 11000) /
                             (.25 / std::sqrt(2.)) - 1) < .01,
                "Beat Gate mix 0 is dry");

        // Stutter 1/8 (12000 samples) in 1-beat segments: the second half repeats the first.
        auto stutter = defaultValues(EffectId::beatGate);
        stutter[0] = 0; // Open gate.
        stutter[4] = 2;
        const auto ramp = [](int i) { return float((i % 24000) * 1.e-5); };
        const auto repeated = renderEffect(EffectId::beatGate, stutter, ramp, 2 * beat);
        double error = 0;
        for (int k = 500; k < 11500; ++k)
            error = std::max(error, double(std::abs(repeated.left[size_t(12000 + k)] - ramp(k))));
        require(error < 1.e-6, "Stutter replays the segment's first slice exactly");
        require(std::abs(repeated.left[size_t(beat + 600)] - ramp(beat + 600)) < 1.e-6,
                "The next segment plays live again");

        // Half-time over 2-beat cycles: output sample c reads input c/2 of the same cycle.
        auto half = defaultValues(EffectId::beatGate);
        half[0] = 0;
        half[6] = 1;
        const auto slow = renderEffect(EffectId::beatGate, half, ramp, 2 * beat);
        double drift = 0;
        for (int c = 1000; c < 40000; c += 2)
            drift = std::max(drift, double(std::abs(slow.left[size_t(c)] - ramp(c / 2))));
        require(drift < 1.e-6, "Half-time plays the cycle at half speed");

        // A playing host re-anchors the beat: at beat 0.25 (an odd 1/16 step) Pulse is closed.
        Engine hosted;
        hosted.prepare(48000);
        require(hosted.setParameters(EffectId::beatGate, gate), "Beat Gate parameters accepted");
        std::vector<float> l(4000), r(4000);
        for (int i = 0; i < 4000; ++i)
            l[size_t(i)] = r[size_t(i)] = sine(500)(i);
        hosted.setTransport(true, true, .25, 120);
        hosted.process(l.data(), r.data(), 4000);
        require(rmsOf(l, 1000, 4000) < .002, "Beat Gate follows the host beat position");
        std::vector<float> l2(4000), r2(4000);
        for (int i = 0; i < 4000; ++i)
            l2[size_t(i)] = r2[size_t(i)] = sine(500)(i);
        hosted.setTransport(true, true, 0., 120);
        hosted.process(l2.data(), r2.data(), 4000);
        require(rmsOf(l2, 1000, 4000) > .17, "A seek back to beat 0 reopens the gate");
    }
    // Pump at the free-running 120 BPM clock: one cycle per beat (24000 samples), depth 0.8.
    {
        constexpr int beat = 24000;
        const auto pumpValues = defaultValues(EffectId::pump); // 1/4, depth .8, release .6, hold .05.
        const auto pumped = renderEffect(EffectId::pump, pumpValues, sine(500), 2 * beat);
        const double full = .25 / std::sqrt(2.);
        const double trough = rmsOf(pumped.left, 200, 1000) / full;   // Inside the hold.
        const double middle = rmsOf(pumped.left, 9000, 10000) / full; // Recovering.
        const double recovered = rmsOf(pumped.left, 19000, 23000) / full;
        require(std::abs(trough - .2) < .03, "Pump drops to 1 - depth on the beat (" + std::to_string(trough) + ")");
        require(middle > trough + .1 && middle < recovered - .05, "Pump recovers through the release");
        require(std::abs(recovered - 1) < .02, "Pump is fully open after the release");
        require(std::abs(rmsOf(pumped.left, beat + 200, beat + 1000) / full - .2) < .03, "Pump repeats every beat");
        auto dryPump = pumpValues;
        dryPump[7] = 0;
        require(std::abs(rmsOf(renderEffect(EffectId::pump, dryPump, sine(500), beat).left, 200, 1000) / full - 1) < .01,
                "Pump mix 0 is dry");
        Engine hosted;
        hosted.prepare(48000);
        require(hosted.setParameters(EffectId::pump, pumpValues), "Pump parameters accepted");
        std::vector<float> l(4000), r(4000);
        for (int i = 0; i < 4000; ++i)
            l[size_t(i)] = r[size_t(i)] = sine(500)(i);
        hosted.setTransport(true, true, .9, 120); // Late in the cycle: open.
        hosted.process(l.data(), r.data(), 4000);
        require(rmsOf(l, 500, 2000) / full > .95, "Pump follows the host beat position");
    }

    // Lo-fi: mix 0 is the input delayed by the reported latency; the tone darkens highs;
    // hiss and crackle add a bounded floor to silence; the wobble moves the pitch.
    {
        auto clean = defaultValues(EffectId::loFi);
        clean[7] = 0;
        Engine probe;
        probe.prepare(48000);
        const int latency = probe.latencySamples(EffectId::loFi);
        require(latency > 0 && latency < 400, "Lo-fi reports its base delay as latency");
        const auto dry = renderEffect(EffectId::loFi, clean, sine(500), 24000);
        double aligned = 0;
        for (int i = 2000; i < 20000; ++i)
            aligned = std::max(aligned, double(std::abs(dry.left[size_t(i)] - sine(500)(i - latency))));
        require(aligned < 1e-5, "Lo-fi mix 0 is the dry signal delayed by exactly the latency");

        auto dark = defaultValues(EffectId::loFi);
        dark[0] = dark[1] = dark[2] = dark[3] = dark[5] = 0;
        dark[4] = 1000;
        dark[6] = 1;
        const double high = rmsOf(renderEffect(EffectId::loFi, dark, sine(8000), 24000).left, 4000, 24000) / (.25 / std::sqrt(2.));
        const double low = rmsOf(renderEffect(EffectId::loFi, dark, sine(200), 24000).left, 4000, 24000) / (.25 / std::sqrt(2.));
        require(high < .1 && low > .9, "Lo-fi tone at 1 kHz keeps lows and removes highs (" + std::to_string(low) + ", " +
                                           std::to_string(high) + ")");

        // Hiss and crackle ride on the audio: compare the same quiet input with and without them.
        auto quiet = dark;
        quiet[4] = 20000;
        auto noisy = quiet;
        noisy[2] = 1;
        noisy[3] = 1;
        const auto plain = renderEffect(EffectId::loFi, quiet, sine(300, .2), 48000);
        const auto withNoise = renderEffect(EffectId::loFi, noisy, sine(300, .2), 48000);
        double added = 0, addedPeak = 0;
        for (int i = 0; i < 48000; ++i)
        {
            const double d = withNoise.left[size_t(i)] - plain.left[size_t(i)];
            added += d * d;
            addedPeak = std::max(addedPeak, std::abs(d));
        }
        added = std::sqrt(added / 48000);
        require(added > 1e-4 && addedPeak < .5, "Hiss and crackle add a bounded noise layer under the audio");
        const auto silent = renderEffect(EffectId::loFi, noisy, [](int) { return 0.f; }, 24000);
        require(std::all_of(silent.left.begin(), silent.left.end(), [](float x) { return x == 0; }),
                "A silent channel stays silent (noise follows the input)");

        auto wobble = dark;
        wobble[0] = 1;
        wobble[4] = 20000;
        const auto bent = renderEffect(EffectId::loFi, wobble, sine(1000), 96000);
        const auto window = [&](int from, int to) { return std::vector<float>(bent.left.begin() + from, bent.left.begin() + to); };
        const double early = measuredHz(window(2000, 14000), 0, 48000), late = measuredHz(window(44000, 56000), 0, 48000);
        require(std::abs(early - late) > 1 && std::abs(early - 1000) < 12 && std::abs(late - 1000) < 12,
                "Wow bends the pitch by a few cents and back (" + std::to_string(early) + ", " + std::to_string(late) + ")");
    }

    // Formant filter: each vowel passes its own formant region (A around 800 Hz, I around
    // 2300 Hz), the shift moves the formants, mix 0 is the dry input and the output is finite.
    {
        const auto level = [&](float vowel, float shift, double hz)
        {
            auto v = defaultValues(EffectId::formant);
            v[0] = vowel;
            v[1] = shift;
            return rmsOf(renderEffect(EffectId::formant, v, sine(hz), 24000).left, 4000, 24000);
        };
        const double a800 = level(0, 0, 800), a2300 = level(0, 0, 2300), i800 = level(2, 0, 800), i2300 = level(2, 0, 2300);
        require(a800 > 2 * a2300 && i2300 > 2 * i800,
                "Formant vowels A and I favour their own regions (" + std::to_string(a800) + ", " + std::to_string(a2300) +
                    ", " + std::to_string(i800) + ", " + std::to_string(i2300) + ")");
        require(level(0, 12, 1600) > 2 * level(0, 0, 1600) * .5 && level(0, 12, 1600) > level(0, 12, 400),
                "An octave formant shift moves A's first formant towards 1.6 kHz");
        auto dryValues = defaultValues(EffectId::formant);
        dryValues[3] = 0;
        const auto dry = renderEffect(EffectId::formant, dryValues, sine(500), 4000);
        double worst = 0;
        for (int i = 0; i < 4000; ++i)
            worst = std::max(worst, double(std::abs(dry.left[size_t(i)] - sine(500)(i))));
        require(worst < 1e-6, "Formant mix 0 is the dry input");
        auto loud = defaultValues(EffectId::formant);
        loud[2] = 20;
        loud[4] = 18;
        const auto hot = renderEffect(EffectId::formant, loud, sine(800, 1), 24000);
        require(std::all_of(hot.left.begin(), hot.left.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 64; }),
                "Formant output stays finite at maximum resonance");
    }

    // Frequency shifter: a 1 kHz tone comes out at 1 kHz + shift (single sideband: the
    // mirror image at 1 kHz - shift stays small), the right offset moves only the right
    // channel, mix 0 is dry and feedback stays bounded.
    {
        auto v = defaultValues(EffectId::frequencyShifter);
        v[0] = 150;
        const auto up = renderEffect(EffectId::frequencyShifter, v, sine(1000), 48000);
        const auto part = [](const std::vector<float> &x) { return std::vector<float>(x.begin() + 8000, x.begin() + 40000); };
        const double hz = measuredHz(part(up.left), 0, 48000);
        require(std::abs(hz - 1150) < 3, "Frequency shifter moves 1 kHz to 1150 Hz (" + std::to_string(hz) + ")");
        // Sideband suppression: correlate with the image frequency.
        double image = 0, wanted = 0;
        for (int i = 8000; i < 40000; ++i)
        {
            const double t = i / 48000.;
            image += up.left[size_t(i)] * std::sin(2 * std::numbers::pi * 850 * t);
            wanted += up.left[size_t(i)] * std::sin(2 * std::numbers::pi * 1150 * t);
        }
        const double imageLevel = std::abs(image) / 16000, rms = rmsOf(up.left, 8000, 40000);
        require(imageLevel < .02 * rms * std::sqrt(2.) + 1e-3 && rms > .1,
                "The mirror sideband is suppressed (" + std::to_string(imageLevel) + " vs " + std::to_string(rms) + ")");
        v[1] = -200; // Right: 150 - 200 = -50 Hz.
        const auto split = renderEffect(EffectId::frequencyShifter, v, sine(1000), 48000);
        const double rightHz = measuredHz(part(split.right), 0, 48000), leftHz = measuredHz(part(split.left), 0, 48000);
        require(std::abs(rightHz - 950) < 3 && std::abs(leftHz - 1150) < 3,
                "The right offset shifts only the right channel (" + std::to_string(leftHz) + ", " + std::to_string(rightHz) + ")");
        auto dryValues = defaultValues(EffectId::frequencyShifter);
        dryValues[3] = 0;
        const auto dry = renderEffect(EffectId::frequencyShifter, dryValues, sine(500), 4000);
        double worst = 0;
        for (int i = 0; i < 4000; ++i)
            worst = std::max(worst, double(std::abs(dry.left[size_t(i)] - sine(500)(i))));
        require(worst < 1e-6, "Frequency shifter mix 0 is the dry input");
        auto loop = defaultValues(EffectId::frequencyShifter);
        loop[2] = .9f;
        const auto fed = renderEffect(EffectId::frequencyShifter, loop, sine(300, 1), 96000);
        require(std::all_of(fed.left.begin(), fed.left.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 32; }),
                "Frequency shifter feedback stays bounded");
    }

    // Waveshaper: more drive adds harmonics, hard clip bounds the level, rectify doubles the
    // frequency, the DC blocker removes the bias offset and mix 0 is dry.
    {
        const auto harmonics = [&](float shape, float driveDb)
        {
            auto v = defaultValues(EffectId::waveshaper);
            v[0] = shape;
            v[1] = driveDb;
            v[3] = 0;
            const auto out = renderEffect(EffectId::waveshaper, v, sine(200, .5), 48000);
            double third = 0, fundamental = 0;
            for (int i = 8000; i < 40000; ++i)
            {
                const double t = i / 48000.;
                fundamental += out.left[size_t(i)] * std::sin(2 * std::numbers::pi * 200 * t);
                third += out.left[size_t(i)] * std::sin(2 * std::numbers::pi * 600 * t);
            }
            return std::abs(third) / std::max(1e-9, std::abs(fundamental));
        };
        require(harmonics(0, 24) > 4 * harmonics(0, 0) && harmonics(0, 24) > .05, "Waveshaper drive adds harmonics");
        auto hard = defaultValues(EffectId::waveshaper);
        hard[0] = 1;
        hard[1] = 36;
        hard[3] = 0;
        const auto clipped = renderEffect(EffectId::waveshaper, hard, sine(200, 1), 24000);
        double peak = 0;
        for (int i = 4000; i < 24000; ++i)
            peak = std::max(peak, double(std::abs(clipped.left[size_t(i)])));
        require(peak < 1.6 && peak > .8, "Hard clip bounds the level (" + std::to_string(peak) + ")");
        auto rect = defaultValues(EffectId::waveshaper);
        rect[0] = 4;
        rect[1] = 0;
        const auto doubled = renderEffect(EffectId::waveshaper, rect, sine(200, .5), 48000);
        const double hz = measuredHz(std::vector<float>(doubled.left.begin() + 8000, doubled.left.begin() + 40000), 0, 48000);
        require(std::abs(hz - 400) < 4, "Rectify doubles the frequency (" + std::to_string(hz) + ")");
        auto biased = defaultValues(EffectId::waveshaper);
        biased[2] = .8f;
        const auto offset = renderEffect(EffectId::waveshaper, biased, sine(300, .3), 96000);
        double mean = 0;
        for (int i = 48000; i < 96000; ++i)
            mean += offset.left[size_t(i)];
        mean /= 48000;
        require(std::abs(mean) < .01, "The DC blocker removes the bias offset (" + std::to_string(mean) + ")");
        auto dryValues = defaultValues(EffectId::waveshaper);
        dryValues[4] = 0;
        const auto dry = renderEffect(EffectId::waveshaper, dryValues, sine(500), 4000);
        double worst = 0;
        for (int i = 0; i < 4000; ++i)
            worst = std::max(worst, double(std::abs(dry.left[size_t(i)] - sine(500)(i))));
        require(worst < 1e-6, "Waveshaper mix 0 is the dry input");
    }

    // Convolution reverb: 512 samples of reported latency (mix 0 is the input delayed by
    // exactly that), an impulse rings out with a decaying tail (Hall longer than Room),
    // pre-delay moves the onset, the output is linear in the input and finite.
    {
        Engine probe;
        probe.prepare(48000, EffectId::convolution);
        require(probe.latencySamples(EffectId::convolution) == 512, "Convolution reports its block latency");
        Engine other;
        other.prepare(48000, EffectId::gate);
        require(other.latencySamples(EffectId::convolution) == 0, "Other effects do not build convolution IRs");
        auto dryValues = defaultValues(EffectId::convolution);
        dryValues[5] = 0;
        const auto dry = renderEffect(EffectId::convolution, dryValues, sine(500), 8000);
        double aligned = 0;
        for (int i = 600; i < 8000; ++i)
            aligned = std::max(aligned, double(std::abs(dry.left[size_t(i)] - sine(500)(i - 512))));
        require(aligned < 1e-6, "Convolution mix 0 is the input delayed by the latency");
        const auto ring = [&](int space, float predelayMs)
        {
            auto v = defaultValues(EffectId::convolution);
            v[0] = float(space);
            v[1] = predelayMs;
            v[2] = 20;
            v[3] = 20000;
            v[5] = 1;
            return renderEffect(EffectId::convolution, v, [](int i) { return i == 0 ? 1.f : 0.f; }, 48000 * 3);
        };
        const auto energy = [](const std::vector<float> &x, double from, double to)
        {
            double e = 0;
            for (int i = int(from * 48000); i < int(to * 48000) && i < int(x.size()); ++i)
                e += double(x[size_t(i)]) * x[size_t(i)];
            return e;
        };
        const auto hall = ring(2, 0), room = ring(0, 0);
        require(energy(hall.left, .05, .3) > 0 && energy(hall.left, .05, .3) > 30 * energy(hall.left, 1.6, 1.9),
                "The Hall tail decays");
        require(energy(hall.left, 1.0, 1.3) > 100 * energy(room.left, 1.0, 1.3) + 1e-12, "Hall rings longer than Room");
        require(energy(room.left, 0, 512 / 48000.) < 1e-12, "Nothing comes out before the latency");
        const auto onset = [](const std::vector<float> &x)
        {
            for (size_t i = 0; i < x.size(); ++i)
                if (std::abs(x[i]) > 1e-4)
                    return int(i);
            return -1;
        };
        const int plain = onset(room.left), late = onset(ring(0, 100).left);
        require(plain >= 512 && late - plain > 4700 && late - plain < 4900,
                "Pre-delay moves the onset by 100 ms (" + std::to_string(plain) + ", " + std::to_string(late) + ")");
        auto v = defaultValues(EffectId::convolution);
        v[5] = 1;
        const auto once = renderEffect(EffectId::convolution, v, sine(300, .25), 24000);
        const auto twice = renderEffect(EffectId::convolution, v, sine(300, .5), 24000);
        double linear = 0, peak = 0;
        for (size_t i = 0; i < once.left.size(); ++i)
        {
            linear = std::max(linear, double(std::abs(twice.left[i] - 2 * once.left[i])));
            peak = std::max(peak, double(std::abs(twice.left[i])));
        }
        require(peak > .01 && peak < 8 && linear < 1e-4 * std::max(1., peak), "Convolution is linear and bounded");

        // A loaded impulse (File space): two taps come back at their times and sign.
        Engine loaded;
        loaded.prepare(48000, EffectId::convolution);
        std::array<std::vector<float>, 2> taps{std::vector<float>(2000), std::vector<float>(2000)};
        for (auto &channel : taps)
        {
            channel[0] = 1;
            channel[1000] = -.5f;
        }
        require(loaded.setUserImpulse(taps) && loaded.hasUserImpulse(), "A user impulse is accepted");
        auto file = defaultValues(EffectId::convolution);
        file[0] = 4;
        file[1] = 0;
        file[2] = 20;
        file[3] = 20000;
        file[5] = 1;
        require(loaded.setParameters(EffectId::convolution, file), "File space parameters accepted");
        std::vector<float> l(4096), r(4096);
        l[0] = r[0] = 1;
        const auto before = effects_test::allocations;
        for (int at = 0; at < 4096; at += 256)
        {
            effects_test::CallbackScope scope;
            loaded.process(l.data() + at, r.data() + at, 256);
        }
        require(effects_test::allocations == before, "File space processing allocates nothing");
        const float first = l[512], second = l[1512];
        require(first > .3f && second < 0 && std::abs(second / first + .5f) < .15f,
                "The loaded taps return at 512 and 1512 samples (" + std::to_string(first) + ", " + std::to_string(second) + ")");
    }

    // Drum Synth DSP: every drum sounds and decays, Tune raises the kick's body, Decay
    // lengthens it, the master level scales it, and silence follows the decay.
    {
        using namespace vibera::drums;
        const auto &specs = parameterSpecs();
        std::array<float, parameterCount> values{};
        for (int i = 0; i < parameterCount; ++i)
            values[size_t(i)] = specs[size_t(i)].initial;
        const auto hit = [&](int drum, const std::array<float, parameterCount> &v, float master, int frames)
        {
            vibera::drums::Engine engine;
            engine.prepare(48000);
            engine.noteOn(padNote(drum), 1.f, v);
            std::vector<float> l(size_t(frames), 0.f), r(size_t(frames), 0.f);
            const auto before = effects_test::allocations;
            {
                effects_test::CallbackScope scope;
                engine.render(l.data(), r.data(), frames, master);
            }
            require(effects_test::allocations == before, "Drum Synth rendering allocates nothing");
            return l;
        };
        for (int drum = 0; drum < drumCount; ++drum)
        {
            const auto out = hit(drum, values, 0, 96000);
            const double early = rmsOf(out, 0, 2400), late = rmsOf(out, 90000, 96000);
            require(early > 1e-3 && late < 1e-6, std::string("Drum ") + drumName(drum) + " sounds then ends");
        }
        auto tuned = values;
        tuned[0] = 12;
        const auto low = hit(kick, values, 0, 24000), high = hit(kick, tuned, 0, 24000);
        const double lowHz = measuredHz(std::vector<float>(low.begin() + 7200, low.begin() + 16800), 0, 48000);
        const double highHz = measuredHz(std::vector<float>(high.begin() + 7200, high.begin() + 16800), 0, 48000);
        require(std::abs(highHz / lowHz - 2) < .12, "Kick Tune +12 doubles its body pitch (" + std::to_string(lowHz) + ", " +
                                                        std::to_string(highHz) + ")");
        auto longer = values;
        longer[1] = 1.5f;
        require(rmsOf(hit(kick, longer, 0, 48000), 24000, 48000) > 4 * rmsOf(hit(kick, values, 0, 48000), 24000, 48000),
                "Kick Decay lengthens the tail");
        const double loud = rmsOf(hit(snare, values, -6, 9600), 0, 9600), quiet = rmsOf(hit(snare, values, -18, 9600), 0, 9600);
        require(std::abs(quiet / loud - .251) < .02, "Master level -12 dB scales the output");
    }

}
int main()
{
    const auto start = std::chrono::steady_clock::now();
    try
    {
        catalogueAndPartitions();
        utilityAndFilterOracles();
        dynamicsOracles();
        exactBinaryEndpoints();
        timeOracles();
        modulatedFilterRegression();
        safetyAndSwitching();
        tuneOracles();
        studioOracles();
        vocoderOracles();
        std::cout << "PASS " << catalogueEntries().size() << " original effects; " << checks
                  << " checks; callback allocations=" << effects_test::allocations
                  << ", destructions=" << effects_test::destructions << "; seconds="
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
