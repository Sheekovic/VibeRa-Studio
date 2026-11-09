// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <memory>
namespace vibera
{
struct WorkerLimits
{
    size_t memoryMegabytes = 0; // Zero preserves the existing plugin policy.
    unsigned cpuPercent = 0;
    bool belowNormalPriority = false, containDescendants = false;
};
// Retain a Windows process handle or a waitable POSIX child identity. Never use
// executable-name lookup. Constrained Job-style providers fail closed on POSIX
// until an equivalent resource/descendant-containment backend is available.
class WorkerProcess
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    WorkerProcess();
    ~WorkerProcess();
    bool start(const juce::StringArray &, int unusedStreamFlags = 0, WorkerLimits = {});
    bool isRunning() const;
    bool waitForProcessToFinish(int milliseconds) const;
    juce::uint32 getExitCode() const;
    juce::uint32 processId() const;
    void requestGracefulShutdown();
    bool kill(); // requires the explicitly approved owned-worker timeout exception
};
// Real-time audio processes opt out of Windows EcoQoS execution-speed throttling, which Windows
// may apply to windowless or background processes and which makes callbacks miss deadlines.
// No effect elsewhere.
void disableProcessPowerThrottling() noexcept;
} // namespace vibera
