// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginBuses.h"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

namespace vibera
{
enum class PluginWrapperView : int
{
    plugin = 0,
    settings = 1,
    parameters = 2
};
enum class PluginWrapperAction : int
{
    bypass = 1,
    mix = 2,
    parameter = 3,
    automationCreate = 4,
    initialize = 5,
    removeInitialization = 6,
    refresh = 7,
    notifications = 8,
    audioIo = 9,
    chainCopy = 10,
    chainSave = 11,
    chainPaste = 12,
    chainLoad = 13,
    chainDirectory = 14,
    sidechain = 15,
    // Instrument Channel strip in the wrapper (familiar ON / PAN / VOL / TRACK header).
    channelOn = 16,    // value 1 on, 0 muted
    channelGain = 17,  // value 0..1 = volume 0..125%
    channelPan = 18,   // value 0..1 = pan -1..1
    channelTrack = 19  // parameter = Mixer insert index, value 0
};
// The instrument Channel that owns the plugin, as published by the application.
struct PluginWrapperChannel
{
    bool present = false, on = true;
    float gain = .78f, pan = 0;
    int track = 0, maxTrack = 0;
    bool operator==(const PluginWrapperChannel &) const = default;
};
struct PluginWrapperIntent
{
    PluginWrapperAction action = PluginWrapperAction::bypass;
    std::uint32_t parameter = 0, generation = 0;
    float value = 0;
    bool gestureOpen = false;
    PluginMainIo io{};
};
struct PluginWrapperState
{
    bool ready = false, instrument = false, bypassed = false, pending = false;
    float mix = 1;
    PluginWrapperView view = PluginWrapperView::plugin;
    bool notifications = true;
    std::uint64_t lastTweakedSelection = 0;
    PluginWrapperChannel channel{};
    bool operator==(const PluginWrapperState &) const = default;
};
inline bool pluginWrapperParameterAction(PluginWrapperAction action) noexcept
{
    return action == PluginWrapperAction::parameter || action == PluginWrapperAction::automationCreate ||
           action == PluginWrapperAction::initialize || action == PluginWrapperAction::removeInitialization;
}
inline bool pluginWrapperIntentValid(const PluginWrapperIntent &intent) noexcept
{
    const auto action = int(intent.action);
    return action >= int(PluginWrapperAction::bypass) && action <= int(PluginWrapperAction::channelTrack) &&
           std::isfinite(intent.value) && intent.value >= 0 && intent.value <= 1 &&
           ((intent.action != PluginWrapperAction::bypass &&
             intent.action != PluginWrapperAction::notifications && intent.action != PluginWrapperAction::channelOn) ||
            intent.value == 0 || intent.value == 1) &&
           (!pluginWrapperParameterAction(intent.action) || intent.generation != 0) &&
           (!intent.gestureOpen || intent.action == PluginWrapperAction::parameter) &&
           (intent.action < PluginWrapperAction::chainCopy || intent.action > PluginWrapperAction::sidechain ||
            (intent.value == 0 && intent.parameter == 0 && intent.generation == 0)) &&
           (intent.action != PluginWrapperAction::channelTrack ||
            (intent.value == 0 && intent.generation == 0 && intent.parameter <= 100000)) &&
           (intent.action != PluginWrapperAction::audioIo || intent.io.valid());
}
struct PluginWrapperRequest
{
    unsigned serial = 0;
    PluginWrapperIntent intent;
};
enum class PluginWrapperPhase : unsigned
{
    idle,
    writing,
    submitted,
    accepted,
    terminal
};
enum class PluginWrapperOutcome : unsigned
{
    completed,
    failed,
    cancelled
};

// One worker-GUI producer and one application-CONTROL consumer. No DSP caller.
// Request identity/payload is immutable from submitted until terminal is consumed.
// Only desired is mutable during an owned parameter gesture; its packed atomic
// keeps final value and end-of-gesture together. No retry loop, wait or allocation.
struct PluginWrapperMailbox
{
    std::atomic<PluginWrapperPhase> phase{PluginWrapperPhase::idle};
    unsigned serial = 0;
    PluginWrapperAction action = PluginWrapperAction::bypass;
    std::uint32_t parameter = 0, generation = 0;
    PluginMainIo io{}; // Immutable CONTROL request, published with submitted phase.
    std::atomic<std::uint64_t> desired{0};
    PluginWrapperOutcome outcome = PluginWrapperOutcome::failed;
    char operation[48]{}, code[80]{}, message[256]{};
    std::atomic<bool> ready{false}, bypassed{false};
    std::atomic<float> mix{1};
    std::atomic<PluginWrapperView> view{PluginWrapperView::plugin};
    std::atomic<std::uint64_t> selection{0}; // Actual displayed generation:32/native-ID:32.
    // CONTROL-published completed native gesture; never another consumer of the DSP gesture queue.
    std::atomic<std::uint64_t> lastTweakedSelection{0};
    // CONTROL-published instrument Channel strip (read by the worker GUI).
    std::atomic<bool> channelPresent{false}, channelOn{true};
    std::atomic<float> channelGain{.78f}, channelPan{0};
    std::atomic<int> channelTrack{0}, channelMaxTrack{0};

    static std::uint64_t pack(float value, bool gestureOpen) noexcept
    {
        return std::uint64_t(std::bit_cast<std::uint32_t>(value)) | (std::uint64_t(gestureOpen) << 32);
    }
    static float value(std::uint64_t packed) noexcept
    {
        return std::bit_cast<float>(std::uint32_t(packed));
    }
    static bool gestureOpen(std::uint64_t packed) noexcept
    {
        return (packed >> 32) != 0;
    }
    bool submit(const PluginWrapperIntent &intent) noexcept
    {
        if (!pluginWrapperIntentValid(intent))
            return false;
        const auto current = phase.load(std::memory_order_acquire);
        if ((current == PluginWrapperPhase::submitted || current == PluginWrapperPhase::accepted) &&
            action == PluginWrapperAction::parameter && intent.action == action &&
            parameter == intent.parameter && generation == intent.generation)
        {
            // A closed gesture cannot reopen while its completion is pending.
            if (!gestureOpen(desired.load(std::memory_order_acquire)))
                return false;
            desired.store(pack(intent.value, intent.gestureOpen), std::memory_order_release);
            return true;
        }
        auto expected = PluginWrapperPhase::idle;
        if (!phase.compare_exchange_strong(expected, PluginWrapperPhase::writing, std::memory_order_acq_rel))
            return false;
        ++serial;
        if (!serial)
            ++serial;
        action = intent.action;
        parameter = intent.parameter;
        generation = intent.generation;
        io = intent.io;
        desired.store(pack(intent.value, intent.gestureOpen), std::memory_order_relaxed);
        phase.store(PluginWrapperPhase::submitted, std::memory_order_release);
        return true;
    }
};
static_assert(std::atomic<PluginWrapperPhase>::is_always_lock_free &&
              std::atomic<PluginWrapperView>::is_always_lock_free &&
              std::atomic<float>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace vibera
