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
// Windows worker ownership is a retained kernel process object, not an executable-name lookup.
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
} // namespace vibera
