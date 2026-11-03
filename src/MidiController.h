// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Customization.h"
#include "MidiControlInput.h"
#include "MixerNativeIdentity.h"
#include "SamplerControllerLinks.h"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace vibera
{
struct ControllerBinding
{
    juce::String action, label;
    int port = 1, channel = 0, kind = 0, number = 0, layer = 0;
    int mode = 0; // Press, absolute, relative twos-complement, relative offset, modifier hold.
    // -1 selected,0 Master; positive is a stable ID when unbanked, otherwise
    // a1-based non-Master DISPLAY slot. Profile format1 remains unchanged.
    int mixer = -1;
    double minimum = 0, maximum = 1;
    bool pickup = true;
    bool banked = false;
    bool enabled = true;
    int nativeArea = 0; // 1: MK3 pots, 2: MK3 faders; follow hardware mode reports.
    int sendTarget = -1; // Explicit stable destination for mixer.send only;0 is Master.
    bool operator==(const ControllerBinding &) const = default;
};
struct ControllerInvocation
{
    juce::String action;
    int mixer = -1;
    bool mixerBanked = false;
    int mixerBankOffset = 0; // CONTROL dispatch snapshot; never add to a stable ID.
    bool parameter = false;
    double value = 0, normalized = 0, delta = 0;
    double minimum = 0, maximum = 1;
    bool pickup = false;
    size_t binding = 0;
    int sendTarget = -1;
    unsigned automationSession = 0;
    double automationSeconds = 0;
    bool automationClockValid = false;
    // Project-owned fixed sampler witness; absent projectIdentity means the original global prefix.
    juce::String projectIdentity, samplerDestination;
    std::int64_t projectLink = 0, channel = 0;
    std::uint32_t samplerParameter = 0;
    std::uint64_t projectProjectionGeneration = 0, bindingStateGeneration = 0;
    bool samplerAvailable = false;
};
inline constexpr int controllerMixerBankWidth = 8; // Existing Launchkey hardware strip count.
inline int controllerMixerBankLimit(int nonMasterCount) noexcept
{
    const int count = juce::jlimit(0, MixerNativeIdentity::capacity - 1, nonMasterCount);
    return count > 0 ? ((count - 1) / controllerMixerBankWidth) * controllerMixerBankWidth : 0;
}
inline int controllerMixerCount(const Project &project) noexcept
{
    return int(std::count_if(project.mixer.begin(), project.mixer.end(),
                             [](const auto &row) { return row.index != 0; }));
}
inline juce::Result resolveControllerMixerTarget(const Project &project, int requested, bool banked,
                                                 int bankOffset, int selected, int &output)
{
    int target = requested < 0 ? selected : requested;
    if (requested < -1)
        return juce::Result::fail(
            "controller.invalidMixerTarget: Selected(-1), Master0 or stable insert ID required");
    if (banked && requested > 0)
    {
        if (bankOffset < 0 || bankOffset > controllerMixerBankLimit(MixerNativeIdentity::capacity - 1) ||
            bankOffset % controllerMixerBankWidth != 0 || requested > MixerNativeIdentity::capacity - 1)
            return juce::Result::fail(
                "controller.invalidBank: Bounded bank offset and display slot required");
        const int position = bankOffset + requested;
        int current = 0;
        bool found = false;
        for (const auto &row : project.mixer)
            if (row.index != 0 && ++current == position)
            {
                target = row.index;
                found = true;
                break;
            }
        if (!found)
            return juce::Result::fail(
                "controller.bankSlotUnavailable: No current insert at this bank display position");
    }
    if (target < 0 ||
        std::none_of(
            project.mixer.begin(), project.mixer.end(), [&](const auto &row) { return row.index == target; }))
        return juce::Result::fail(
            "controller.missingMixerTarget: Bound stable Mixer insert is unavailable; mapping retained");
    output = target;
    return juce::Result::ok();
}
inline juce::String controllerMixerBankLabel(const Project &project, int bankOffset)
{
    const int count = controllerMixerCount(project);
    if (bankOffset < 0 || bankOffset > controllerMixerBankLimit(MixerNativeIdentity::capacity - 1) ||
        bankOffset % controllerMixerBankWidth != 0)
        return "Invalid Mixer display bank";
    if (bankOffset >= count)
        return "Mixer display bank unavailable; mapping retained";
    return "Mixer display positions " + juce::String(bankOffset + 1) + "-" +
           juce::String(std::min(count, bankOffset + controllerMixerBankWidth)) + " of " +
           juce::String(count);
}
class MidiController final : private juce::MidiInputCallback
{
    MidiControlInput &inbox;
    bool persistent;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String wantedInput, wantedOutput, error;
    juce::uint32 lastPoll = 0;
    unsigned lastDropped = 0;
    bool dawEnabled = false;
    std::array<int, 3> lastFeedback{-1, -1, -1};
    std::array<bool, 1024> pressed{};
    std::array<int, 1024> previous{};
    struct EffectiveRow
    {
        bool present = false, project = false, available = false;
        ControllerBinding binding;
        SamplerControllerLink link;
        std::optional<SamplerAutomationParameter> parameter;
        std::vector<std::int64_t> shadowedBy;
        std::uint64_t stateGeneration = 0;
    };
    struct EffectiveProjection
    {
        std::array<EffectiveRow, 1024> rows;
        MidiControlInput::PreparedMappings masks;
        juce::String projectIdentity;
        std::uint64_t generation = 0, stateSerial = 0;
        size_t globals = 0, projects = 0;
    };
    // Pinned across reentrant CONTROL invocation; never read/owned by the MIDI/DSP producer.
    std::shared_ptr<EffectiveProjection> effective;
    juce::String learnExpectedProfile;
    std::optional<juce::File> learnFile;
    std::uint64_t projectLearnToken = 0;
    std::optional<ControlMessage> projectLearnCandidate;
    std::function<void(std::uint64_t, const ControlMessage &)> projectLearnCapture;
    juce::Result armLearnImpl(int, const juce::File *);
    juce::Result prepareProjection(const std::vector<ControllerBinding> &, const Project *,
                                   const juce::String &, std::shared_ptr<EffectiveProjection> &) const;
    void publishPrepared(std::shared_ptr<EffectiveProjection>, bool resetGlobals) noexcept;
    std::array<bool, 4> layers{};
    void handleIncomingMidiMessage(juce::MidiInput *, const juce::MidiMessage &message) override
    {
        inbox.observe(message, 1);
    }
    void close();
    juce::var stateForBindings(const std::vector<ControllerBinding> &) const;
    juce::Result bindingIdentity(const juce::File *, juce::String &) const;
    juce::Result setBindingImpl(size_t, const ControllerBinding &, const juce::String &, const juce::File *);
    void publishValidatedBindings(std::shared_ptr<EffectiveProjection>) noexcept;

  public:
    std::vector<ControllerBinding> bindings;
    juce::String profile = "Generic MIDI Learn";
    juce::String lastMessage = "Move a control to see its MIDI message";
    int learnIndex = -1;
    int mixerBank = 0;
    int potMode = 3, faderMode = 1;
    void moveBank(int delta, int nonMasterCount = MixerNativeIdentity::capacity - 1)
    {
        const auto requested = std::int64_t(mixerBank) + std::int64_t(delta) * controllerMixerBankWidth;
        mixerBank = int(juce::jlimit<std::int64_t>(0, controllerMixerBankLimit(nonMasterCount), requested));
        save();
    }
    std::function<void(const ControllerInvocation &)> invoke;
    std::function<bool(const juce::String &)> inputAllowed;
    explicit MidiController(MidiControlInput &, bool hardware);
    ~MidiController() override;
    juce::Result setPorts(const juce::String &inputId, const juce::String &outputId);
    juce::String inputIdentifier() const
    {
        return wantedInput;
    }
    juce::String outputIdentifier() const
    {
        return wantedOutput;
    }
    juce::String status() const;
    void poll();
    void drain();
    void publish();
    void armLearn(int);
    juce::Result armLearn(int, const juce::File &ownedProfile);
    juce::Result publishProjectBindings(const Project &, const juce::String &projectIdentity);
    std::uint64_t projectProjectionGeneration() const noexcept;
    juce::var projectBindingState() const;
    bool projectInvocationCurrent(const ControllerInvocation &, const Project &,
                                  const juce::String &) const noexcept;
    juce::Result armProjectLearn(std::uint64_t token,
                                 std::function<void(std::uint64_t, const ControlMessage &)> capture);
    bool cancelProjectLearn(std::uint64_t token);
    juce::var projectLearnState() const;
    void cancelLearn();
    void launchkeyPreset();
    void feedback(bool playing, bool recording, bool metronome);
    static bool valid(const ControllerBinding &);
    static juce::Result parseBinding(const juce::var &, ControllerBinding &);
    static juce::var bindingSchema();
    // CONTROL-only full-binding edit. Exact append at size is allowed; never gaps.
    // Save candidate BEFORE noexcept mapping publication; no MIDI port reconfiguration.
    juce::Result setBinding(size_t, const ControllerBinding &, const juce::String &expectedProfile);
    juce::Result bindingProfileIdentity(juce::String &) const;
    bool bindingChangesPersisted() const noexcept
    {
        return persistent;
    }
    // Internal owned-file test seam; never exposed as a semantic filepath argument.
    juce::Result setBinding(size_t, const ControllerBinding &, const juce::String &expectedProfile,
                            const juce::File &ownedProfile);
    juce::Result bindingProfileIdentity(const juce::File &ownedProfile, juce::String &) const;
    static const juce::StringArray &parameterActions();
    bool conflicts(const ControllerBinding &, size_t skip) const;
    juce::var state() const;
    juce::Result importState(const juce::var &);
    juce::Result save() const;
    juce::Result save(const juce::File &) const;
};
} // namespace vibera
