// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <optional>
#include "Project.h"
#include <cstddef>
#include <memory>
#include <span>

namespace vibera::effect_chain
{
// CONTROL-only portable data. No worker handles, project IDs, automation links
// or parameter declarations. Native opaque state is authoritative for this copy.
struct Slot
{
    juce::String name, descriptor, state;
    bool bypass = false;
    float wet = 1;
    int mainInputBus = 0, mainOutputBus = 0, mainInputChannels = 2, mainInputMonoSource = 0;
    bool operator==(const Slot &) const = default;
};
// Mixer track state (familiar "Save mixer track state"): the insert's own controls,
// stored after the slots when present (header flag 2).
struct TrackControls
{
    juce::String name;
    juce::uint32 colour = 0;
    float gain = .8f, pan = 0, stereoSeparation = 0;
    bool phaseInverted = false, stereoSwapped = false;
    std::array<float, 6> eq{0, 120, 0, 1000, 0, 8000}; // gain dB / frequency Hz for low, mid, high.
    float delayMs = 0;
    int dock = 0;
    bool operator==(const TrackControls &) const = default;
};
struct Chain
{
    juce::String name;
    bool effectsBypassed = false;
    std::vector<Slot> effects;
    std::optional<TrackControls> track;
    bool operator==(const Chain &) const = default;
};
inline constexpr std::size_t maximumBytes = 64 * 1024 * 1024;
inline constexpr int maximumSlots = 16;

// Format1: VibeRaFX magic; LE u32 version, flags; UTF-8 name; LE u32 count;
// slots: LE u32 flags, IEEE754 binary32 wet, UTF-8 name/descriptor/opaque state.
// Format2 adds four LE u32 main input/output bus, channels, mono source after
// each wet field. Writer retains Format1 bytes when every slot uses defaults;
// reader preserves genuine Format1 stereo defaults.
// Header flag 2: a track-controls block follows the slots (UTF-8 name, LE u32 colour, binary32
// gain/pan/separation, LE u32 phase|swap<<1, six binary32 EQ values, binary32 delay ms,
// LE u32 dock+1). Strings use LE u32 byte lengths. Unknown flags/version/trailing bytes refused.
// No disk I/O. Failure leaves the caller's owning output unchanged.
juce::Result validate(const Chain &);
juce::Result encode(const Chain &, std::unique_ptr<juce::MemoryBlock> &);
juce::Result decode(std::span<const std::byte>, std::unique_ptr<Chain> &);

enum class StateProvenance
{
    nativeAcknowledgement,
    retainedProjectBytes,
    retainedRecoveryBytes
};
struct CaptureIntent
{
    Project source;
    juce::String projectIdentity;
    std::uint64_t revision = 0;
    int insert = 0;
    std::vector<PluginRuntimeState> owners;
};
struct CaptureWitness
{
    PluginRuntimeState current;
    std::optional<unsigned> capturedRevision;
    unsigned pendingParameters = 0;
    bool editorBusy = false;
};
struct StateReceipt
{
    Id originalPlugin = 0;
    juce::String originalInstance;
    std::optional<unsigned> revision;
    StateProvenance provenance = StateProvenance::retainedProjectBytes;
};
struct Clipboard
{
    Chain chain;
    juce::String sourceProjectIdentity;
    std::uint64_t sourceRevision = 0;
    int sourceInsert = 0;
    std::vector<StateReceipt> receipts;
};
// Caller gathers owners from the actual AudioEngine projection/Host getters.
// An unavailable nonempty runtime identity is NOT a missing-plugin placeholder.
juce::Result beginCapture(const Project &, const juce::String &projectIdentity, std::uint64_t revision,
                          int insert, std::span<const PluginRuntimeState>, std::unique_ptr<CaptureIntent> &);
// captured must come from a SUCCESSFUL TARGET-ONLY requestPluginState/poll cycle,
// filtered by exactly intent.owners[].plugin. Generic all-plugin capture can
// refresh unrelated opaque states and is deliberately refused by this contract.
// capturedRevision comes from capturedPluginRevision(id, exactHost). A caller
// cannot use this data validator as evidence that those operations took place.
// Provider revision visibility does not cover unsignalled native widget changes.
// No-host bytes refreshed by that exact AudioEngine capture may be known matching
// recovery state: preserve them with retainedRecoveryBytes provenance, never call
// them an acknowledgement or force older Project bytes over newer recovery data.
juce::Result finishCapture(const CaptureIntent &, const Project &current, const juce::String &projectIdentity,
                           std::uint64_t revision, const Project &captured, std::span<const CaptureWitness>,
                           std::unique_ptr<Clipboard> &);

struct ReplacementPlan
{
    Project destination;
    std::vector<Id> removedIds, newIds;
};
// Pure model proposal only, not a native graph/history publication or ID lease.
// Caller must first fresh-capture target state, reserve firstReservedId through
// a shared nonrecycling CONTROL allocator, then privately prepare native graph
// and prepared history. No model/history mutation before native admission.
// Target automation references are refused rather than silently unlinked.
juce::Result prepareReplacement(const Project &capturedTarget, int insert, const Chain &, Id firstReservedId,
                                std::unique_ptr<ReplacementPlan> &);
} // namespace vibera::effect_chain
