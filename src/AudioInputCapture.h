// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Guide.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace vibera
{
// Audio-input takes and input monitoring on the device callback.
//
// The callback only copies input into a ring that CONTROL preallocated; it never
// allocates, locks, writes files or destroys objects. A per-take writer thread maps
// the first captured block onto the timeline (TimelineStamp), drops the frames that
// precede the requested start (pre-roll, count-in and round-trip latency) and streams
// the rest to a 24-bit WAV. A full ring is never hidden: dropped frames are counted.
// One pass of a take recorded through a looping transport: where it plays (edit
// seconds) and which window of the recorded file it is.
struct TakePass
{
    double edit = 0, offset = 0, duration = 0;
};
// Splits `seconds` of audio that starts at `startEdit` at every wrap of the loop
// [loopStart, loopEnd). Without a loop (or starting after it) the take is one pass.
// Passes after the first shorter than `minimum` seconds are dropped (a Stop just
// after a wrap).
std::vector<TakePass> loopPasses(double startEdit, double seconds, double loopStart, double loopEnd,
                                 double minimum = .1);

class AudioInputCapture final : public juce::AudioIODeviceCallback
{
public:
    // Device stream time (seconds) at the start of the current device block.
    using StreamClock = std::function<double()>;
    // Latest Master-block stamp, or false when none was published yet.
    using StampSource = std::function<bool(TimelineStamp::Value &)>;

    struct Device
    {
        double rate = 0;
        int inputs = 0, outputs = 0, inputLatency = 0, outputLatency = 0, block = 0;
        bool running = false;
    };
    struct TakeRequest
    {
        juce::File file;
        int channels = 2;          // 1..16 consecutive inputs (several tracks are split after the take)
        int firstInput = 0;        // Index into the device's active input channels.
        double startEdit = -1.e9;  // Earliest kept timeline second; earlier frames are dropped.
        bool compensateLatency = true;
    };
    enum class Outcome
    {
        completed,   // File written; startEdit/frames describe the clip.
        empty,       // Nothing remained after the start trim; no file kept.
        unmapped,    // The transport never played during the take; no file kept.
        failed       // Writer/device failure; see error. Partial file removed.
    };
    struct TakeResult
    {
        Outcome outcome = Outcome::failed;
        juce::File file;
        juce::int64 frames = 0, captured = 0, dropped = 0, trimmed = 0;
        double rate = 0, startEdit = 0, latencySeconds = 0;
        int channels = 0;
        bool interrupted = false;
        juce::String error;
    };

    AudioInputCapture(StreamClock clock, StampSource stamps);
    ~AudioInputCapture() override;

    // CONTROL. Opens the file and writer thread before capture is published.
    juce::Result begin(const TakeRequest &);
    // CONTROL. Unpublishes the take, waits for the callback to leave it, flushes the
    // writer and closes the file. Safe when nothing is capturing.
    TakeResult finish();
    // CONTROL. Like finish() but always deletes the partial file.
    void cancel();
    bool capturing() const noexcept { return session.load() != nullptr; }
    // Frames handed to the ring by the callback for the current take.
    juce::int64 capturedFrames() const noexcept;
    juce::int64 droppedFrames() const noexcept;
    // Edit seconds of the first kept frame once the take is mapped onto the timeline.
    std::optional<double> mappedStartEdit() const noexcept;

    Device device() const noexcept;
    // Latest input peak per channel (linear), cleared by the read.
    std::array<float, 2> takePeaks() noexcept;

    std::atomic<bool> monitor{false};
    std::atomic<float> monitorGain{1.f};
    std::atomic<int> monitorChannels{2}, monitorFirstInput{0};

    void audioDeviceIOCallbackWithContext(const float *const *input, int numInputs, float *const *output,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext &) override;
    void audioDeviceAboutToStart(juce::AudioIODevice *) override;
    void audioDeviceStopped() override;

private:
    struct Session;
    static void write(Session &);
    StreamClock clock;
    StampSource stamps;
    std::atomic<Session *> session{nullptr};
    std::unique_ptr<Session> owned;
    std::atomic<bool> inCallback{false};
    std::atomic<double> rate{0};
    std::atomic<int> inputs{0}, outputs{0}, inputLatency{0}, outputLatency{0}, block{0};
    std::atomic<bool> running{false};
    std::array<std::atomic<float>, 2> peaks{};
    TakeResult stop(bool keep);
};
} // namespace vibera
