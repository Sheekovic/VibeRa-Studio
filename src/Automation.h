// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
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
    wave    // Sine-shaped oscillation from this value to the next and back.
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
// Original source/placement/initialization model; Project rejects unsupported targets.
struct AutomationDocument
{
    std::vector<AutomationSource> sources;
    std::vector<AutomationPlacement> placements;
    std::vector<AutomationInitialValue> initialValues;
};
class AutomationProgram
{
  public:
    struct Span
    {
        double beat = 0, origin = 0, inverseLength = 0;
        double coefficients[4]{};
        unsigned shape = 0; // 0 polynomial; else AutomationShape stairs/pulse/wave.
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
