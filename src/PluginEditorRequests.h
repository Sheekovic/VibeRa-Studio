// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace vibera
{
// Worker-instance-scoped control-thread requests. This interface controls only
// the native editor or explicit wrapper controls; arbitrary third-party widgets
// are not exposed.
enum class PluginEditorAction : int
{
    open = 1,
    close = 2,
    settings = 3,
    parameters = 4
};
inline constexpr bool pluginEditorOpens(PluginEditorAction action) noexcept
{
    return action == PluginEditorAction::open || action == PluginEditorAction::settings ||
           action == PluginEditorAction::parameters;
}
enum class PluginEditorAvailability : unsigned
{
    unknown,
    available,
    unsupported,
    creationFailed,
    controlsOnly = 4 // Wrapper controls are available, but there is no native UI.
};
enum class PluginEditorOutcome : int
{
    opened,
    broughtToFront,
    closed,
    unsupported,
    creationFailed,
    nativeFailure,
    invalidRequest,
    cancelled,
    workerUnavailable,
    timeout,
    invalidAcknowledgement
};
enum class PluginEditorAdmissionCode
{
    accepted,
    busy,
    capacity,
    serialExhausted,
    unavailable,
    invalidAction,
    instanceChanged
};
enum class PluginEditorCancellation
{
    cancelled,
    tooLate,
    alreadyCompleted,
    unknownRequest,
    instanceChanged
};
enum class PluginEditorReplyDisposition
{
    completed,
    timeout,
    alreadyTerminal,
    invalid
};
inline constexpr unsigned pluginEditorTimeoutMs = 5000;
inline constexpr const char *pluginEditorOutcomeName(PluginEditorOutcome outcome) noexcept
{
    switch (outcome)
    {
    case PluginEditorOutcome::opened:
        return "opened";
    case PluginEditorOutcome::broughtToFront:
        return "brought_to_front";
    case PluginEditorOutcome::closed:
        return "closed";
    case PluginEditorOutcome::unsupported:
        return "unsupported";
    case PluginEditorOutcome::creationFailed:
        return "creation_failed";
    case PluginEditorOutcome::nativeFailure:
        return "native_failure";
    case PluginEditorOutcome::invalidRequest:
        return "invalid_request";
    case PluginEditorOutcome::cancelled:
        return "cancelled";
    case PluginEditorOutcome::workerUnavailable:
        return "worker_unavailable";
    case PluginEditorOutcome::timeout:
        return "timeout";
    case PluginEditorOutcome::invalidAcknowledgement:
        return "invalid_acknowledgement";
    }
    return "unknown";
}

struct PluginEditorWindowState
{
    bool known = false, exists = false, visible = false, minimised = false;
    PluginEditorAvailability availability = PluginEditorAvailability::unknown;
    std::uint32_t revision = 0;
    bool valid() const noexcept
    {
        return unsigned(availability) <= unsigned(PluginEditorAvailability::controlsOnly) &&
               (known ||
                (!exists && !visible && !minimised && availability == PluginEditorAvailability::unknown)) &&
               (!visible || (exists && !minimised)) && (!minimised || exists) &&
               (!(availability == PluginEditorAvailability::unsupported ||
                  availability == PluginEditorAvailability::creationFailed) ||
                !exists);
    }
    std::uint64_t packed() const noexcept
    {
        return (std::uint64_t(revision) << 32) | (known ? 1u : 0u) | (exists ? 2u : 0u) |
               (visible ? 4u : 0u) | (minimised ? 8u : 0u) | (unsigned(availability) << 4);
    }
    static PluginEditorWindowState unpack(std::uint64_t value) noexcept
    {
        return {bool(value & 1),
                bool(value & 2),
                bool(value & 4),
                bool(value & 8),
                PluginEditorAvailability((value >> 4) & 7),
                std::uint32_t(value >> 32)};
    }
    static bool validPacked(std::uint64_t value) noexcept
    {
        return (value & 0x00000000ffffff80ULL) == 0 && unpack(value).valid();
    }
    bool operator==(const PluginEditorWindowState &) const = default;
};
struct PluginEditorResult
{
    unsigned serial = 0;
    PluginEditorAction action = PluginEditorAction::open;
    PluginEditorOutcome outcome = PluginEditorOutcome::workerUnavailable;
    PluginEditorWindowState window;
    // True means native work began, not that it succeeded. In particular a
    // timeout after dispatch cannot establish whether a window was created.
    bool dispatched = false;
    // Windows host/worker ticks share raw QPC-derived milliseconds (IPC12).
    unsigned admittedAt = 0, completedAt = 0;
    bool completionKnown = false;
    PluginEditorOutcome nativeOutcome = PluginEditorOutcome::invalidRequest;
    bool succeeded() const noexcept
    {
        if (!window.known || !window.valid())
            return false;
        if (outcome == PluginEditorOutcome::closed)
            return action == PluginEditorAction::close && !window.exists && !window.visible;
        return (outcome == PluginEditorOutcome::opened || outcome == PluginEditorOutcome::broughtToFront) &&
               pluginEditorOpens(action) && window.exists && window.visible &&
               (window.availability == PluginEditorAvailability::available ||
                (action != PluginEditorAction::open &&
                 window.availability == PluginEditorAvailability::controlsOnly));
    }
    bool operator==(const PluginEditorResult &) const = default;
};
struct PluginEditorAdmission
{
    unsigned serial = 0;
    PluginEditorAdmissionCode code = PluginEditorAdmissionCode::unavailable;
};
enum class PluginEditorPhase : int
{
    idle,
    hostWriting,
    pending,
    nativeRunning,
    reply,
    hostReading
};
inline constexpr bool pluginEditorPhaseBusy(PluginEditorPhase phase) noexcept
{
    // A reply is terminal native work; retaining/reading its receipt must not
    // block unrelated state/parameter work merely because its caller polls late.
    return phase == PluginEditorPhase::hostWriting || phase == PluginEditorPhase::pending ||
           phase == PluginEditorPhase::nativeRunning || phase == PluginEditorPhase::hostReading;
}
// Exactly one physical request. Plain fields may only be accessed by the phase
// owner after an acquire transition; publication uses release. Not used by DSP.
struct PluginEditorMailbox
{
    std::atomic<PluginEditorPhase> phase{PluginEditorPhase::idle};
    unsigned serial = 0, admittedAt = 0, completedAt = 0;
    PluginEditorAction action = PluginEditorAction::open;
    PluginEditorOutcome outcome = PluginEditorOutcome::invalidRequest;
    std::uint64_t window = 0;
    std::uint64_t wrapperSelection = 0; // Optional generation:32/native-ID:32;0 preserves selection.
};

// No allocation, locks or clock calls. All methods have one host control-thread
// owner. Completed receipts remain until that particular caller consumes or
// abandons them; a later caller cannot evict or accidentally drain another.
class PluginEditorRequests
{
    struct Entry
    {
        PluginEditorResult result;
        unsigned started = 0;
        bool used = false, completed = false, abandoned = false;
    };
    std::array<Entry, 64> entries{};
    unsigned serial = 0;
    static bool validAction(PluginEditorAction action) noexcept
    {
        return pluginEditorOpens(action) || action == PluginEditorAction::close;
    }
    Entry *find(unsigned token) noexcept
    {
        for (auto &entry : entries)
            if (entry.used && entry.result.serial == token)
                return &entry;
        return nullptr;
    }
    const Entry *find(unsigned token) const noexcept
    {
        for (const auto &entry : entries)
            if (entry.used && entry.result.serial == token)
                return &entry;
        return nullptr;
    }
    static void finish(Entry &entry, PluginEditorResult result) noexcept
    {
        if (entry.abandoned)
            entry = {};
        else
        {
            entry.result = result;
            entry.completed = true;
        }
    }

  public:
    explicit PluginEditorRequests(unsigned lastSerial = 0) noexcept : serial(lastSerial) {}
    PluginEditorAdmission reserve(PluginEditorAction action, unsigned now) noexcept
    {
        if (!validAction(action))
            return {0, PluginEditorAdmissionCode::invalidAction};
        if (serial == std::numeric_limits<unsigned>::max())
            return {0, PluginEditorAdmissionCode::serialExhausted};
        for (auto &entry : entries)
            if (!entry.used)
            {
                entry = {{++serial, action, PluginEditorOutcome::workerUnavailable, {}, false},
                         now,
                         true,
                         false,
                         false};
                entry.result.admittedAt = now;
                return {serial, PluginEditorAdmissionCode::accepted};
            }
        return {0, PluginEditorAdmissionCode::capacity};
    }
    bool known(unsigned token) const noexcept
    {
        return find(token) != nullptr;
    }
    bool ready(unsigned token) const noexcept
    {
        const auto *entry = find(token);
        return entry && entry->completed;
    }
    void markDispatched(unsigned token) noexcept
    {
        if (auto *entry = find(token); entry && !entry->completed)
            entry->result.dispatched = true;
    }
    PluginEditorReplyDisposition completeNative(PluginEditorResult result, unsigned observedAt) noexcept
    {
        auto *entry = find(result.serial);
        if (!entry || entry->result.action != result.action || !result.dispatched || !result.window.known ||
            !result.window.valid())
            return PluginEditorReplyDisposition::invalid;
        const bool opening = pluginEditorOpens(result.action);
        const bool openSuccess = result.outcome == PluginEditorOutcome::opened ||
                                 result.outcome == PluginEditorOutcome::broughtToFront;
        const bool closeSuccess = result.outcome == PluginEditorOutcome::closed;
        const bool nativeFailure = result.outcome == PluginEditorOutcome::nativeFailure ||
                                   result.outcome == PluginEditorOutcome::invalidRequest;
        const bool openFailure = result.outcome == PluginEditorOutcome::unsupported ||
                                 result.outcome == PluginEditorOutcome::creationFailed;
        if ((openSuccess && (!opening || !result.window.exists || !result.window.visible)) ||
            (closeSuccess && (opening || result.window.exists || result.window.visible)) ||
            (openFailure && !opening) || !(openSuccess || closeSuccess || nativeFailure || openFailure))
            return PluginEditorReplyDisposition::invalid;
        const bool supportedOpen = result.window.availability == PluginEditorAvailability::available ||
                                   (opening && result.action != PluginEditorAction::open &&
                                    result.window.availability == PluginEditorAvailability::controlsOnly);
        // A native Open refusal must not hide an already existing controls-only
        // peer. It is a failure, not native attachment or wrapper success.
        const bool nativeUnavailableWithControls =
            result.action == PluginEditorAction::open &&
            result.window.availability == PluginEditorAvailability::controlsOnly && result.window.exists;
        if ((openSuccess && !supportedOpen) ||
            (result.outcome == PluginEditorOutcome::unsupported &&
             result.window.availability != PluginEditorAvailability::unsupported &&
             !nativeUnavailableWithControls) ||
            (result.outcome == PluginEditorOutcome::creationFailed &&
             result.window.availability != PluginEditorAvailability::creationFailed))
            return PluginEditorReplyDisposition::invalid;
        if (entry->completed)
            return PluginEditorReplyDisposition::alreadyTerminal; // Never resurrect timed-out/failed
                                                                  // ownership.
        const unsigned elapsed = result.completedAt - result.admittedAt;
        const unsigned observedElapsed = observedAt - result.admittedAt;
        if (result.admittedAt != entry->started || observedElapsed >= 0x80000000u ||
            elapsed > observedElapsed)
            return PluginEditorReplyDisposition::invalid; // Wrong clock/identity/future completion.
        result.completionKnown = true;
        result.nativeOutcome = result.outcome;
        const bool late = elapsed > pluginEditorTimeoutMs;
        if (late)
            result.outcome = PluginEditorOutcome::timeout; // Keep the actual observed window/native outcome.
        finish(*entry, result);
        return late ? PluginEditorReplyDisposition::timeout : PluginEditorReplyDisposition::completed;
    }
    bool cancelBeforeDispatch(unsigned token, PluginEditorWindowState window) noexcept
    {
        auto *entry = find(token);
        if (!entry || entry->completed || entry->result.dispatched)
            return false;
        auto result = entry->result;
        result.outcome = PluginEditorOutcome::cancelled;
        result.window = window;
        finish(*entry, result);
        return true;
    }
    bool consume(unsigned token, PluginEditorResult &out) noexcept
    {
        auto *entry = find(token);
        if (!entry || !entry->completed)
            return false;
        out = entry->result;
        *entry = {};
        return true;
    }
    void abandon(unsigned token) noexcept
    {
        if (auto *entry = find(token))
        {
            if (entry->completed)
                *entry = {};
            else
                entry->abandoned = true;
        }
    }
    void failPending(PluginEditorOutcome outcome) noexcept
    {
        if (outcome != PluginEditorOutcome::workerUnavailable && outcome != PluginEditorOutcome::timeout &&
            outcome != PluginEditorOutcome::invalidAcknowledgement)
            return;
        for (auto &entry : entries)
            if (entry.used && !entry.completed)
            {
                auto result = entry.result;
                result.outcome = outcome;
                result.window = {}; // Worker failure cannot establish current native visibility.
                finish(entry, result);
            }
    }
    bool expired(unsigned now, unsigned budget) const noexcept
    {
        for (const auto &entry : entries)
            if (entry.used && !entry.completed && now - entry.started > budget)
                return true;
        return false;
    }
    unsigned outstanding() const noexcept
    {
        unsigned count = 0;
        for (const auto &entry : entries)
            count += entry.used;
        return count;
    }
};
static_assert(std::atomic<PluginEditorPhase>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace vibera
