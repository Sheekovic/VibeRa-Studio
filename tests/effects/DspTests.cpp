// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/EffectsEngine.h"
#include "AllocationGuard.h"
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
