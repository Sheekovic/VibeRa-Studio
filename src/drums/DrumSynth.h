// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// VibeRa Drum Synth: an original synthesized drum kit (no samples). Eight voices: kick, rim,
// snare, clap, closed hat, open hat, low tom and high tom, each with tune, decay, tone and
// level; a master level. Notes follow the common drum map (36 kick, 37 rim, 38 snare,
// 39 clap, 42 closed hat, 46 open hat, 45 low tom, 50 high tom) and also C4..G4 (60..67) in
// pad order. A closed hat chokes the open hat. Rendering never allocates or locks.
#include "../effects/dsp/EffectsTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>

namespace vibera::drums
{
inline constexpr int drumCount = 8;
inline constexpr int parametersPerDrum = 4; // tune, decay, tone, level
inline constexpr int parameterCount = drumCount * parametersPerDrum + 1; // + master level
inline constexpr std::uint32_t parameterBaseId = 0x30000;
inline float dbToGain(float db) noexcept { return std::pow(10.f, db / 20.f); }
enum Drum
{
    kick,
    rim,
    snare,
    clap,
    closedHat,
    openHat,
    lowTom,
    highTom
};
inline const char *drumName(int drum) noexcept
{
    static const char *names[drumCount]{"Kick", "Rim", "Snare", "Clap", "Closed hat", "Open hat", "Low tom", "High tom"};
    return names[std::clamp(drum, 0, drumCount - 1)];
}
inline const char *toneName(int drum) noexcept // What Tone changes for each drum.
{
    static const char *names[drumCount]{"Click", "Body", "Snap", "Bright", "Bright", "Bright", "Skin", "Skin"};
    return names[std::clamp(drum, 0, drumCount - 1)];
}
inline int drumForNote(int note) noexcept
{
    switch (note)
    {
    case 35: case 36: return kick;
    case 37: return rim;
    case 38: case 40: return snare;
    case 39: return clap;
    case 42: case 44: return closedHat;
    case 46: return openHat;
    case 41: case 43: case 45: return lowTom;
    case 47: case 48: case 50: return highTom;
    default: return note >= 60 && note < 60 + drumCount ? note - 60 : -1;
    }
}
inline constexpr int padNote(int drum) noexcept
{
    constexpr int notes[drumCount]{36, 37, 38, 39, 42, 46, 45, 50};
    return notes[drum];
}

// Parameter table (ids parameterBaseId + index). Decay is seconds to about -60 dB.
inline const std::array<effects::ParameterSpec, parameterCount> &parameterSpecs()
{
    static const auto specs = []
    {
        std::array<effects::ParameterSpec, parameterCount> result{};
        static constexpr float decays[drumCount]{.5f, .08f, .25f, .3f, .08f, .5f, .45f, .35f};
        static constexpr float tones[drumCount]{.4f, .5f, .55f, .5f, .6f, .6f, .3f, .3f};
        static std::array<std::array<char, 32>, parameterCount> ids{}, names{};
        for (int d = 0; d < drumCount; ++d)
        {
            const char *suffix[parametersPerDrum]{"tune", "decay", "tone", "level"};
            const char *label[parametersPerDrum]{"tune", "decay", toneName(d), "level"};
            for (int p = 0; p < parametersPerDrum; ++p)
            {
                const auto i = std::size_t(d * parametersPerDrum + p);
                std::snprintf(ids[i].data(), ids[i].size(), "d%d-%s", d + 1, suffix[p]);
                std::snprintf(names[i].data(), names[i].size(), "%s %s", drumName(d), label[p]);
                result[i] = p == 0   ? effects::ParameterSpec{ids[i].data(), names[i].data(), -12, 12, 0, "st"}
                            : p == 1 ? effects::ParameterSpec{ids[i].data(), names[i].data(), .03f, 2.f, decays[d], "s", .4f}
                            : p == 2 ? effects::ParameterSpec{ids[i].data(), names[i].data(), 0, 1, tones[d], ""}
                                     : effects::ParameterSpec{ids[i].data(), names[i].data(), -24, 6, 0, "dB"};
            }
        }
        result[parameterCount - 1] = {"master", "Master level", -24, 6, 0, "dB"};
        return result;
    }();
    return specs;
}

// One retriggerable voice per drum.
class Voice
{
  public:
    void prepare(double sampleRate) noexcept
    {
        rate = sampleRate;
        active = false;
    }
    void trigger(int which, float velocity, float tune, float decay, float tone, float level) noexcept
    {
        drum = which;
        gain = velocity * dbToGain(level);
        ratio = std::exp2(tune / 12.f);
        decaySeconds = std::max(.02f, decay);
        toneAmount = std::clamp(tone, 0.f, 1.f);
        t = 0;
        phase1 = phase2 = 0;
        for (auto &p : metal)
            p = 0;
        hp = lp = band = low = 0;
        active = true;
    }
    void choke() noexcept { chokeLevel = 0, choking = active; }
    bool isActive() const noexcept { return active; }
    float envelopeLevel() const noexcept { return active ? envelope(t) : 0.f; }
    float render(std::uint32_t &seed) noexcept
    {
        if (!active)
            return 0;
        const float dt = float(1. / rate);
        float out = 0;
        const float noise = random(seed);
        switch (drum)
        {
        case kick:
        {
            // Pitch sweep from about 4x down to the body frequency, optional click and drive.
            const float body = 50.f * ratio, sweep = body * (1.f + 3.f * std::exp(-t / .028f));
            phase1 += sweep * dt;
            out = std::sin(2.f * std::numbers::pi_v<float> * phase1) * envelope(t);
            out += noise * toneAmount * .6f * std::exp(-t / .0025f);
            out = std::tanh(out * (1.f + 2.f * toneAmount)) / std::tanh(1.f + 2.f * toneAmount);
            break;
        }
        case rim:
        {
            phase1 += 1700.f * ratio * dt;
            phase2 += 2450.f * ratio * dt;
            const float ring = .5f * std::sin(2.f * std::numbers::pi_v<float> * phase1) +
                               .5f * std::sin(2.f * std::numbers::pi_v<float> * phase2);
            out = (ring * (.4f + .6f * toneAmount) + noise * .4f * std::exp(-t / .002f)) * envelope(t);
            break;
        }
        case snare: case lowTom: case highTom:
        {
            // Pitched body plus noise: Snap (snare) or Skin (toms) sets the noise share.
            const float base = drum == snare ? 185.f : drum == lowTom ? 95.f : 160.f;
            const float f = base * ratio * (1.f + .5f * std::exp(-t / .02f));
            phase1 += f * dt;
            const float body = std::sin(2.f * std::numbers::pi_v<float> * phase1) *
                               std::exp(-t / (decaySeconds * (drum == snare ? .25f : .22f)));
            hp = .7f * (hp + noise - lp); // Crude high-pass of the noise.
            lp = noise;
            const float snap = drum == snare ? toneAmount : toneAmount * .35f;
            out = (body * (1.f - snap * .6f) + hp * snap * .8f) * envelope(t);
            break;
        }
        case clap:
        {
            // Three fast bursts then a tail, through a band-pass around 1.2 kHz.
            const float bursts = t < .03f ? std::exp(-std::fmod(t, .01f) / .0025f) : std::exp(-(t - .03f) / (decaySeconds * .2f));
            const float f = (900.f + 900.f * toneAmount) * ratio;
            const float g = std::tan(std::numbers::pi_v<float> * std::min(f, float(rate) * .45f) / float(rate));
            const float k = 1.4f;
            const float hpOut = (noise - (k + g) * band - low) / (1 + k * g + g * g);
            band += g * hpOut;
            low += g * band;
            band += g * hpOut;
            out = band * 2.f * bursts * (t < .03f ? 1.f : envelope(t - .03f) / std::max(1e-4f, envelope(0)));
            break;
        }
        default:
        {
            // Hats: six detuned square partials (metallic) through a high-pass.
            static constexpr float partials[6]{205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
            float metallic = 0;
            for (int i = 0; i < 6; ++i)
            {
                metal[i] += partials[i] * 2.1f * ratio * dt;
                metal[i] -= std::floor(metal[i]);
                metallic += metal[i] < .5f ? 1.f : -1.f;
            }
            metallic = metallic / 6.f * .6f + noise * .4f;
            const float cutoff = (5000.f + 5000.f * toneAmount) / float(rate);
            const float a = std::exp(-2.f * std::numbers::pi_v<float> * cutoff);
            hp = a * (hp + metallic - lp);
            lp = metallic;
            out = hp * envelope(t);
        }
        }
        if (choking)
        {
            chokeLevel += 1.f; // Fast fade over about 2 ms.
            out *= std::max(0.f, 1.f - chokeLevel / float(rate * .002));
            if (chokeLevel > rate * .002)
                active = choking = false;
        }
        t += dt;
        if (t > decaySeconds * 1.6f + .05f)
            active = false;
        return out * gain;
    }

  private:
    float envelope(float time) const noexcept // -60 dB at decaySeconds.
    {
        return std::exp(-6.9078f * time / decaySeconds);
    }
    static float random(std::uint32_t &seed) noexcept
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return float(seed) / 2147483648.f - 1.f;
    }
    double rate = 48000;
    int drum = kick;
    float gain = 1, ratio = 1, decaySeconds = .5f, toneAmount = .5f, t = 0, phase1 = 0, phase2 = 0;
    float metal[6]{}, hp = 0, lp = 0, band = 0, low = 0, chokeLevel = 0;
    bool active = false, choking = false;
};

class Engine
{
  public:
    void prepare(double sampleRate) noexcept
    {
        rate = sampleRate;
        for (auto &v : voices)
            v.prepare(sampleRate);
        reset();
    }
    void reset() noexcept
    {
        for (auto &v : voices)
            v.prepare(rate);
        seed = 0x2545f491u;
    }
    // values: native parameter values in parameterSpecs() order.
    void noteOn(int note, float velocity, const std::array<float, parameterCount> &values) noexcept
    {
        const int drum = drumForNote(note);
        if (drum < 0)
            return;
        if (drum == closedHat)
            voices[openHat].choke();
        const auto base = std::size_t(drum * parametersPerDrum);
        voices[std::size_t(drum)].trigger(drum, std::clamp(velocity, 0.f, 1.f), values[base], values[base + 1],
                                          values[base + 2], values[base + 3]);
    }
    void render(float *left, float *right, int frames, float masterDb) noexcept
    {
        const float master = dbToGain(masterDb);
        for (int i = 0; i < frames; ++i)
        {
            float sum = 0;
            for (auto &v : voices)
                sum += v.render(seed);
            // Gentle safety saturation above 0 dBFS.
            const float y = std::abs(sum * master) <= .9f ? sum * master
                                                           : std::copysign(.9f + .1f * std::tanh((std::abs(sum * master) - .9f) * 10.f), sum);
            left[i] += y;
            right[i] += y;
        }
    }
    float drumLevel(int drum) const noexcept { return voices[std::size_t(drum)].envelopeLevel(); }

  private:
    double rate = 48000;
    std::array<Voice, drumCount> voices{};
    std::uint32_t seed = 0x2545f491u;
};
} // namespace vibera::drums
