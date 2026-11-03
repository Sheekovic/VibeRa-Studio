// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <array>
#include "PluginGestures.h"
#include "PluginBuses.h"
#include "PluginLatency.h"
#include "PluginEditorRequests.h"
#include "PluginWrapperProtocol.h"
#include "PluginNativeMeter.h"

namespace vibera::ipc
{
constexpr int frames = 8192, slots = 8, events = 512, stateBytes = 16 * 1024 * 1024;
constexpr int version = 20; // Bounded native patch ACKs require an exact app/worker pair.
constexpr std::uint32_t magic = 0x56525330 + version;
constexpr int parameters = 8192, parameterEvents = 8192;
struct ParameterInfo
{
    std::uint32_t id = 0, flags = 0;
    std::uint32_t availability =
        3; // bit0 current value, bit1 default; unavailable values are zero placeholders.
    int stepCount = 0, unit = 0;
    float value = 0, defaultValue = 0;
    char name[160]{}, label[48]{};
};
struct ParameterEvent
{
    std::uint32_t id = 0;
    int frame = 0;
    float value = 0;
};
struct ParameterCatalogue
{
    // Exclusive handshake: 0 free, 1 worker writing, 2 ready, 3 host reading.
    std::atomic<int> status{0};
    unsigned serial = 0, generation = 1;
    int count = 0, reportedCount = 0, error = 0;
    char diagnostic[256]{};
    std::array<ParameterInfo, parameters> entries;
};
struct ParameterPatchValue
{
    std::uint32_t id = 0;
    float value = 0, previousValue = 0;
};
struct ParameterControl
{
    // 0 free, -1 host writing, 1 pending, 2 completed, 3 host reading.
    std::atomic<int> status{0};
    unsigned serial = 0, generation = 0;
    std::uint32_t id = 0;
    float requested = 0, actual = 0;
    int result = 0; // 0 applied; 1 stale; 2 unavailable/read-only; 3 malformed.
    float previousValue = 0; // Actual native scalar read immediately before this write.
    int beforeKnown = 0; // 0 unknown; 1 successful write with a finite normalized BEFORE.
    unsigned patchCount = 0; // Zero retains the existing single-scalar contract.
    std::array<ParameterPatchValue, 64> patch{}; // Validated completely before any native mutation.
};
enum class DspPhase : int
{
    preparing,
    waiting,
    processing,
    controlPaused,
    stopped
};
constexpr const char *dspPhaseName(DspPhase phase) noexcept
{
    switch (phase)
    {
    case DspPhase::preparing:
        return "preparing";
    case DspPhase::waiting:
        return "waiting";
    case DspPhase::processing:
        return "processing";
    case DspPhase::controlPaused:
        return "control-paused";
    case DspPhase::stopped:
        return "stopped";
    }
    return "unknown";
}
struct MidiEvent
{
    int frame = 0;
    std::uint8_t bytes[3]{};
    std::uint8_t size = 0;
};
struct Block
{
    // 0 = producer-owned/free, 1 = queued, 2 = worker processing, 3 = completed.
    std::atomic<int> status{0};
    std::uint64_t sequence = 0;
    PluginLatencyStamp latency; // Bound to this completed packet before status release.
    int count = 0, midiCount = 0;
    int parameterCount = 0;
    unsigned parameterGeneration = 0; // Whole batch belongs to this validated native schema.
    double seconds = 0, tempo = 120;
    double ppq = -1; // Song beat at `seconds` when the host follows a tempo map; -1 derives it.
    bool playing = false;
    int numerator = 4, denominator = 4;
    std::array<MidiEvent, events> midi;
    std::array<ParameterEvent, parameterEvents> parameters;
    float input[4][frames]{}, output[2][frames]{};
};
struct Shared
{
    std::uint32_t signature = magic;
    std::uint32_t parentPid = 0;
    double rate = 48000;
    int blockSize = 256;
    PluginBusRequest requestedBuses, negotiatedBuses;
    int negotiatedMainInputs = 0;
    PluginBusCatalogue busCatalogue; // Immutable before status=ready release; CONTROL readers only.
    std::atomic<int> status{0}, shutdown{0};
    PluginEditorMailbox editorControl;
    PluginWrapperMailbox wrapperControl;
    PluginNativeMeterTelemetry nativeOutputMeter;
    std::atomic<std::uint64_t> editorWindow{0};
    std::atomic<std::uint64_t> latency{PluginLatencyStamp{0, 1}.packed()};
    std::atomic<int> panic{0};
    std::atomic<DspPhase> dspPhase{DspPhase::preparing};
    std::atomic<std::uint64_t> dspHeartbeat{0};
    std::atomic<std::uint64_t> oldestNeeded{0};
    std::atomic<unsigned> midiResetGeneration{0};
    bool instrument = false;
    std::atomic<unsigned> stateRequest{0}, stateReady{0}, parameterRevision{0};
    unsigned stateParameterRevision = 0;
    std::atomic<std::uint64_t> processed{0};
    int stateSize = 0;
    char error[512]{};
    std::array<Block, slots> blocks;
    std::array<std::uint8_t, stateBytes> state;
    std::atomic<unsigned> catalogueRequest{0}, catalogueGeneration{1};
    std::atomic<std::uint64_t> parameterEventsApplied{0}, parameterEventsRejected{0};
    ParameterCatalogue catalogue;
    ParameterControl parameterControl;
    std::atomic<bool> gestureNotifications{true};
    std::atomic<unsigned> gestureEpoch{1};
    PluginGestureQueue gestures;
};
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free &&
              std::atomic<DspPhase>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace vibera::ipc
