// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include <map>
#include <atomic>

namespace vibera
{
class AudioEngine;
// Original private startup receipt. Worker handles cannot be borrowed/injected
// from another graph. No UI/native editor controls expose this unpublished set.
// Prepare on a caller-owned background/control job, NEVER audio. Its final
// destruction and all cancellation/draining must also occur away from DSP.
class StoppedEffectWorkers final
{
  public:
    using Ptr = std::shared_ptr<const StoppedEffectWorkers>;
    // Success = independent workers restored, NOT graph ready/posted/selected.
    // Failure preserves output; all started workers remain owned until their
    // ordinary background/control destruction. Cancellation uses existing Host
    // startup cancellation and verified worker ownership; no name-wide cleanup.
    // Legacy standalone compatibility only. Production must use the plan overload
    // and reject this null-plan receipt before any graph claim/publication.
    static juce::Result prepare(std::vector<PluginSpec>, double sampleRate, int blockSize,
                                const std::function<bool()> &cancelled, Ptr &output);
    // Ordinary live preparation MUST receive the actual AudioEngine projection's
    // owned plan, copied on CONTROL before dispatching this background job.
    // An equivalent newly-created plan is a different source-session owner.
    // This overload never falls back to legacy1Q on null/stale plan or failure.
    static juce::Result prepare(std::vector<PluginSpec>, const PreparedPluginTransport::Ptr &,
                                const std::function<bool()> &cancelled, Ptr &output);
    ~StoppedEffectWorkers();
    StoppedEffectWorkers(const StoppedEffectWorkers &) = delete;
    StoppedEffectWorkers &operator=(const StoppedEffectWorkers &) = delete;

  private:
    friend class AudioEngine;
    StoppedEffectWorkers() = default;
    static juce::Result prepareImpl(std::vector<PluginSpec>, double, int, const std::function<bool()> &,
                                    Ptr &, const PreparedPluginTransport::Ptr &);
    std::vector<PluginSpec> specs;
    std::map<std::int64_t, std::shared_ptr<PluginHost>> hosts;
    PreparedPluginTransport::Ptr transportPlan; // Immutable; pins actual manager reader/device epoch.
    double rate = 0;
    int block = 0;
    mutable std::atomic<bool> claimed{false}; // One graph admission ever; no cross-context DSP reuse.
};
} // namespace vibera
