// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <compare>
#include <optional>
#include <string>
#include <vector>
#include <span>

namespace vibera
{
enum class AutomationParameter : unsigned
{
    mixerGain = 0,
    mixerPan = 1,
    channelGain = 2,
    channelPan = 3,
    pluginNormalized = 4,
    mixerStereoSeparation = 5,
    mixerSend = 6,        // owner=source Mixer ID, parameter=destination Mixer ID (0=Master).
    samplerModulation = 7 // owner=stable Channel ID, parameter=frozen sampler target/field code.
};
struct AutomationKey
{
    AutomationParameter kind = AutomationParameter::mixerGain;
    std::int64_t owner = 0;
    std::uint32_t parameter = 0; // Native VST3 parameter ID or send destination, according to kind.
    auto operator<=>(const AutomationKey &) const = default;
};
enum class AutomationShape : unsigned
{
    single,
    hold,
    smooth,
    // Repeating shapes towards the next point; tension sets the count (-1: 1, 0: 4, +1: 16).
    stairs, // Rising (or falling) steps.
    pulse,  // Square alternation between the two values.
    wave,   // Sine-shaped oscillation from this value to the next and back.
    doubleCurve,  // S-shaped transition; tension sets how sharp the middle is.
    smoothStairs  // Stairs whose steps glide smoothly (count from tension like stairs).
};
// Steps or cycles of a repeating shape for a point tension -1..1.
inline int automationShapeCount(float tension) noexcept
{
    const float t = tension < -1 ? -1.f : tension > 1 ? 1.f : tension;
    const int count = int(4.0 * std::pow(2.0, 2.0 * t) + .5);
    return count < 1 ? 1 : count > 16 ? 16 : count;
}
inline const char *automationShapeName(AutomationShape shape) noexcept
{
    switch (shape)
    {
    case AutomationShape::hold: return "hold";
    case AutomationShape::smooth: return "smooth";
    case AutomationShape::stairs: return "stairs";
    case AutomationShape::pulse: return "pulse";
    case AutomationShape::wave: return "wave";
    case AutomationShape::doubleCurve: return "double";
    case AutomationShape::smoothStairs: return "smoothStairs";
    default: return "single";
    }
}
struct AutomationPoint
{
    std::int64_t id = 0;
    double beat = 0;
    float value = 0;
    AutomationShape incoming = AutomationShape::single;
    float tension = 0;
};
struct AutomationLink
{
    AutomationKey key;
    float minimum = 0, maximum = 1;
};
struct AutomationSource
{
    std::int64_t id = 0;
    double length = 4;
    std::vector<AutomationPoint> points;
    std::vector<AutomationLink> links;
    std::string name = "Automation";
    std::uint32_t colour = 0;
};
struct AutomationPlacement
{
    std::int64_t id = 0, source = 0;
    double beat = 0, length = 4, offset = 0;
    int lane = 0;
    bool muted = false;
};
struct AutomationInitialValue
{
    AutomationKey key;
    float value = 0;
};
float automationSourceValueAt(const AutomationSource &, double beat) noexcept;
// Modulator (version 57): a tempo-synced LFO / random source that moves one target around its
// manual (or initialized) value without an Automation Clip. One per target; a target that an
// Automation Clip links cannot also be modulated.
enum class ModulatorShape : unsigned
{
    sine,
    triangle,
    sawUp,
    sawDown,
    square,
    random,       // Sample and hold: a new random value every cycle.
    smoothRandom, // Random values joined by smooth curves.
    follower      // Envelope follower: rises with the live level of a source Mixer insert.
};
inline const char *modulatorShapeName(ModulatorShape shape) noexcept
{
    switch (shape)
    {
    case ModulatorShape::triangle: return "triangle";
    case ModulatorShape::sawUp: return "sawUp";
    case ModulatorShape::sawDown: return "sawDown";
    case ModulatorShape::square: return "square";
    case ModulatorShape::random: return "random";
    case ModulatorShape::smoothRandom: return "smoothRandom";
    case ModulatorShape::follower: return "follower";
    default: return "sine";
    }
}
inline std::optional<ModulatorShape> modulatorShapeFromName(const std::string &name) noexcept
{
    for (unsigned i = 0; i <= unsigned(ModulatorShape::follower); ++i)
        if (name == modulatorShapeName(ModulatorShape(i)))
            return ModulatorShape(i);
    return {};
}
// Envelope follower levels: each Mixer insert's gain stage publishes its smoothed output level
// (0..1) here every block; follower modulators read it. Open-addressed, lock-free, fixed size.
class InsertLevels
{
  public:
    static InsertLevels &instance() noexcept
    {
        static InsertLevels table;
        return table;
    }
    void publish(std::int64_t insert, float level) noexcept
    {
        for (std::size_t probe = 0; probe < size; ++probe)
        {
            auto &slot = slots[(std::size_t(insert) + probe) % size];
            auto key = slot.key.load(std::memory_order_acquire);
            if (key == insert + 1 ||
                (key == 0 && slot.key.compare_exchange_strong(key, insert + 1, std::memory_order_acq_rel)) ||
                key == insert + 1)
            {
                slot.level.store(level, std::memory_order_relaxed);
                return;
            }
        }
    }
    float level(std::int64_t insert) const noexcept
    {
        for (std::size_t probe = 0; probe < size; ++probe)
        {
            const auto &slot = slots[(std::size_t(insert) + probe) % size];
            const auto key = slot.key.load(std::memory_order_acquire);
            if (key == insert + 1)
                return slot.level.load(std::memory_order_relaxed);
            if (key == 0)
                return 0;
        }
        return 0;
    }

  private:
    static constexpr std::size_t size = 512;
    struct Slot
    {
        std::atomic<std::int64_t> key{0};
        std::atomic<float> level{0};
    };
    Slot slots[size];
};
// Bipolar unit value (-1..1) of a modulator shape at a cycle position; seed varies random shapes.
double modulatorUnit(ModulatorShape, double cycles, std::uint64_t seed) noexcept;
struct AutomationModulator
{
    std::int64_t id = 0;
    AutomationLink link; // Target and its value range (swing and clamping use it).
    ModulatorShape shape = ModulatorShape::sine;
    double period = 4;   // Beats per cycle, 1/64 .. 1024.
    float depth = .25f;  // 0..1: peak-to-peak swing as a fraction of the range.
    float phase = 0;     // 0..1 cycle offset.
    bool enabled = true;
    std::string name = "Modulator";
    std::int64_t source = 0; // follower: the Mixer insert whose level drives it (0 = Master).
};
inline constexpr double minimumModulatorPeriod = 1. / 64, maximumModulatorPeriod = 1024;
// Macro knob (version 57): one 0..1 value driving several targets, each through its own range
// (link.minimum at 0, link.maximum at 1; inverted ranges allowed). A macro target cannot also be
// driven by an Automation Clip or a modulator.
struct AutomationMacro
{
    std::int64_t id = 0;
    std::string name = "Macro";
    float value = 0;
    std::vector<AutomationLink> targets;
};
// Original source/placement/initialization model; Project rejects unsupported targets.
struct AutomationDocument
{
    std::vector<AutomationSource> sources;
    std::vector<AutomationPlacement> placements;
    std::vector<AutomationInitialValue> initialValues;
    std::vector<AutomationModulator> modulators;
    std::vector<AutomationMacro> macros;
};
class AutomationProgram
{
  public:
    struct Span
    {
        double beat = 0, origin = 0, inverseLength = 0;
        double coefficients[4]{};
        unsigned shape = 0; // 0 polynomial; AutomationShape stairs/pulse/wave; modulatorBase+ a modulator.
        static constexpr unsigned modulatorBase = 100;
        double from = 0, delta = 0, count = 1;
        float valueAt(double position) const noexcept;
    };
    struct Lane
    {
        AutomationKey key;
        float initial = 0;
        std::vector<Span> spans;
        float valueAt(double beat) const noexcept;
    };
    const Lane *find(AutomationKey) const noexcept;
    std::span<const Lane> pluginLanes(std::int64_t owner) const noexcept;
    std::span<const Lane> compiledLanes() const noexcept
    {
        return lanes;
    }
    std::optional<float> valueAt(AutomationKey, double beat) const noexcept;
    // Control/compilation thread only. Failure preserves the previous output program.
    static bool compile(const AutomationDocument &, bool songMode, AutomationProgram &, std::string &error,
                        std::span<const AutomationInitialValue> manualDefaults = {});
    // Prepared planar storage; no allocation, I/O, locks, UI or resource destruction.
    // Caller retains an immutable program lease for this entire operation.
    bool applyGain(AutomationKey, float *const *channels, int channelCount, int frames, double beat,
                   double beatsPerFrame) const noexcept;
    std::size_t targetCount() const noexcept
    {
        return lanes.size();
    }
    std::size_t spanCount() const noexcept;

  private:
    std::vector<Lane> lanes;
};
} // namespace vibera
