// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual-provider GUI acceptance support. No global/native input.
#pragma once
#include "PluginHost.h"
#include "NativeGestureFixture.h"
#include <algorithm>
#include <cmath>
#include "../tests/SyntheticDevice.h"
#include <array>
#include <chrono>
#include <thread>
#include <vector>

namespace vibera::test::fx_opening
{
// Exactly one synthetic-device DSP caller. Main only handles actual JUCE UI and
// native-editor asynchronous receipts. PCM/statistics are read after stop/join.
class Capture
{
    static constexpr int capacity = 4096, frames = 256;
    SyntheticDevice &device;
    std::shared_ptr<PluginHost> host;
    juce::AudioBuffer<float> buffer{2, frames};
    HANDLE pacingTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                TIMER_MODIFY_STATE | SYNCHRONIZE);
    std::jthread thread;
    struct Frame
    {
        double callbackMs = 0, latenessMs = 0;
        std::uint64_t submitted = 0, processed = 0, missed = 0, dropped = 0;
        int phase = 0;
    };
    std::vector<Frame> records{capacity};
    std::array<std::vector<float>, 2> pcm{std::vector<float>(capacity * frames),
                                          std::vector<float>(capacity *frames)};

  public:
    std::atomic<int> complete{0}, phase{0};
    std::atomic<bool> exhausted{false}, threw{false};
    explicit Capture(SyntheticDevice &d, std::shared_ptr<PluginHost> h) : device(d), host(std::move(h)) {}
    ~Capture()
    {
        stop();
        if (pacingTimer)
            CloseHandle(pacingTimer);
    }
    void start()
    {
        if (!pacingTimer)
            throw std::runtime_error("Private high-resolution test driver timer unavailable");
        thread = std::jthread(
            [this](std::stop_token stop)
            {
                using Clock = std::chrono::steady_clock;
                const auto period = std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(double(frames) / 48000.));
                auto due = Clock::now();
                for (int i = 0; !stop.stop_requested(); ++i)
                {
                    if (i == capacity)
                    {
                        exhausted = true;
                        break;
                    }
                    // Private process-owned driver wait, outside actual DSP.
                    // std::sleep_until can round to a Windows scheduling tick;
                    // do not change system timer settings or relax deadlines.
                    if (const auto remaining = due - Clock::now(); remaining > Clock::duration::zero())
                    {
                        LARGE_INTEGER delay;
                        delay.QuadPart = -std::max<LONGLONG>(
                            1, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
                        if (!SetWaitableTimer(pacingTimer, &delay, 0, nullptr, nullptr, FALSE) ||
                            WaitForSingleObject(pacingTimer, 2000) != WAIT_OBJECT_0)
                        {
                            threw = true;
                            break;
                        }
                    }
                    auto &record = records[std::size_t(i)];
                    record.phase = phase.load(std::memory_order_relaxed);
                    record.latenessMs = std::chrono::duration<double, std::milli>(Clock::now() - due).count();
                    buffer.clear();
                    const auto began = juce::Time::getHighResolutionTicks();
                    try
                    {
                        device.process(buffer);
                    }
                    catch (...)
                    {
                        threw = true;
                        break;
                    }
                    record.callbackMs = 1000. * juce::Time::highResolutionTicksToSeconds(
                                                    juce::Time::getHighResolutionTicks() - began);
                    const auto stats = host->statistics();
                    record.submitted = stats.submitted;
                    record.processed = stats.processed;
                    record.missed = stats.missed;
                    record.dropped = stats.dropped;
                    for (int c = 0; c < 2; ++c)
                        std::copy_n(buffer.getReadPointer(c), frames,
                                    pcm[std::size_t(c)].data() + i * frames);
                    complete.store(i + 1, std::memory_order_release);
                    due += period;
                    if (Clock::now() > due)
                        due = Clock::now() + period; // No catch-up burst.
                }
            });
    }
    void stop()
    {
        if (thread.joinable())
        {
            thread.request_stop();
            thread.join();
        }
    }
    // Called only AFTER stop. Full raw bits/CSV survive any following assertion.
    bool evidence(const juce::File &folder, bool exactFixture, std::uint64_t missed, std::uint64_t dropped,
                  juce::String &summary)
    {
        const int count = complete.load(std::memory_order_acquire);
        auto raw = folder.getChildFile("actual-pcm-planar-float32le.raw").createOutputStream();
        if (!raw)
            return false;
        raw->setPosition(0);
        raw->truncate();
        for (int c = 0; c < 2; ++c)
            if (!raw->write(pcm[std::size_t(c)].data(), std::size_t(count * frames) * sizeof(float)))
                return false;
        raw.reset();
        juce::String csv = "callback,phase,callbackMs,latenessMs,submitted,processed,missed,dropped\n";
        double error = 0, peak = 0, maximumMs = 0, maximumLatenessMs = 0;
        int overruns = 0, pacingMisses = 0;
        bool finite = true, intact = count >= 8 && !exhausted && !threw;
        for (int i = 0; i < count; ++i)
        {
            const auto &r = records[std::size_t(i)];
            maximumMs = std::max(maximumMs, r.callbackMs);
            maximumLatenessMs = std::max(maximumLatenessMs, r.latenessMs);
            overruns += r.callbackMs > 256000. / 48000.;
            pacingMisses += r.latenessMs > 256000. / 48000.;
            intact = intact && r.missed == missed && r.dropped == dropped &&
                     r.callbackMs <= 256000. / 48000. && r.latenessMs <= 256000. / 48000.;
            csv += juce::String(i) + "," + juce::String(r.phase) + "," + juce::String(r.callbackMs, 6) + "," +
                   juce::String(r.latenessMs, 6) + "," + juce::String(juce::int64(r.submitted)) + "," +
                   juce::String(juce::int64(r.processed)) + "," + juce::String(juce::int64(r.missed)) + "," +
                   juce::String(juce::int64(r.dropped)) + "\n";
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < frames; ++n)
                {
                    const float value = pcm[std::size_t(c)][std::size_t(i * frames + n)];
                    finite = finite && std::isfinite(value);
                    if (std::isfinite(value))
                    {
                        peak = std::max(peak, std::abs(double(value)));
                        // Original ParameterFixture default DSP: input*.5 +0.
                        // Original unity-envelope source is .125L/.25R, velocity1.
                        error = std::max(error, std::abs(double(value) - (c == 0 ? .0625 : .125)));
                    }
                }
        }
        summary = "callbacks=" + juce::String(count) + ";peak=" + juce::String(peak, 9) +
                  ";maxError=" + juce::String(error, 12) + ";maxCallbackMs=" + juce::String(maximumMs, 6) +
                  ";maxReleaseLatenessMs=" + juce::String(maximumLatenessMs, 6) +
                  ";callbackOverruns=" + juce::String(overruns) +
                  ";driverPacingMisses=" + juce::String(pacingMisses) +
                  ";finite=" + juce::String(int(finite)) +
                  ";originalExactOracle=" + juce::String(int(exactFixture)) + "\n";
        if (!folder.getChildFile("callbacks.csv").replaceWithText(csv) ||
            !folder.getChildFile("audio-summary.txt").replaceWithText(summary))
            return false;
        return intact && finite && (exactFixture ? error == 0 : peak > 1.0e-7);
    }
};

struct OwnedWindows
{
    std::array<HWND, 16> peers{};
    int count = 0;
    bool overflow = false;
    HWND originalChild = nullptr;
};
inline OwnedWindows ownedWindows(unsigned pid, bool original)
{
    OwnedWindows result;
    struct Scope
    {
        unsigned pid;
        OwnedWindows &result;
    } scope{pid, result};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL
        {
            auto &scope = *reinterpret_cast<Scope *>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == scope.pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER))
            {
                if (scope.result.count == int(scope.result.peers.size()))
                {
                    scope.result.overflow = true;
                    return FALSE;
                }
                scope.result.peers[std::size_t(scope.result.count++)] = window;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&scope));
    if (original)
        result.originalChild = vibera::test::gestureFixtureWindow(pid);
    return result;
}
inline bool pressMenuItem(juce::Component &root, const juce::String &title)
{
    // Pinned JUCE PopupMenu ItemAccessibilityHandler exposes its exact item text
    // and press action. Restrict search to the freshly opened owned modal menu.
    if (auto *handler = root.getAccessibilityHandler(); handler && handler->getTitle() == title)
        return handler->getActions().invoke(juce::AccessibilityActionType::press);
    for (auto *child : root.getChildren())
        if (pressMenuItem(*child, title))
            return true;
    return false;
}
} // namespace vibera::test::fx_opening
