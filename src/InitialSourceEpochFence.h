// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "FixedSourcePcmEpochGuard.h"
#include <memory>
#include <stdexcept>
#include <vector>

namespace vibera::initial_source_epoch
{
// No payload, reset or ownership mutation. The same selected source-processing
// owner publishes, the final root reads once after its children. CONTROL may
// concurrently observe it; detailed faults remain in pinned per-source guards.
class Health final
{
    std::atomic<bool> failed{false};
    static_assert(std::atomic<bool>::is_always_lock_free);

  public:
    void refuse() noexcept
    {
        failed.store(true, std::memory_order_release);
    }
    bool healthy() const noexcept
    {
        return !failed.load(std::memory_order_acquire);
    }
};

// Construct exactly once on CONTROL for a genuine fresh initial Host/Runtime.
// One selected processing owner mutates capture/guard. All owners retire after
// callback exclusion on CONTROL. No Host owning pin is needed inside this
// object: the actual Runtime and membership separately retain that same Host.
class Fence final
{
  public:
    using Ptr = std::shared_ptr<Fence>;
    enum class Reason : std::uint32_t
    {
        none,
        invalidSpan,
        guardRefused,
        streamRefused,
        ownerChanged
    };
    struct Failure
    {
        Reason reason = Reason::none;
        std::int64_t plugin = 0;
        const void *runtime = nullptr;
        PluginHost::AutomationStatus stream = PluginHost::AutomationStatus::ready;
        int actualFrames = 0;
        std::optional<FixedSourcePcmEpochGuard::Failure> guard;
    };
    Fence(std::int64_t id, const void *owner, PluginHost &host, const PreparedPluginTransport::Ptr &plan,
          std::shared_ptr<Health> shared)
        : plugin(id), runtime(owner), hostOwner(&host), transport(plan.get()), health(std::move(shared)),
          guard({host.pcmIdentity(), host.declaredLatency(), maximum(plan)}),
          segments(capacity(guard.expectation())), capture(segments, guard.expectation().identity)
    {
        if (plugin <= 0 || !runtime || !health || !guard.healthy() || host.offline() ||
            host.pcmIdentity().preparedTransport != reinterpret_cast<std::uintptr_t>(transport) ||
            guard.expectation().identity.quantum != plan->budget().quantum ||
            guard.expectation().identity.transportSamples != plan->budget().transportSamples || !host.alive())
            throw std::invalid_argument(
                "sourceEpoch.freshOwner: Actual fresh prepared Host/Runtime/plan required");
    }
    Fence(const Fence &) = delete;
    Fence &operator=(const Fence &) = delete;
    Fence(Fence &&) = delete;
    Fence &operator=(Fence &&) = delete;
    const void *sourceRuntime() const noexcept
    {
        return runtime;
    }
    std::int64_t pluginId() const noexcept
    {
        return plugin;
    }
    const FixedSourcePcmEpochGuard::Expected &expectation() const noexcept
    {
        return guard.expectation();
    }
    bool healthy() const noexcept
    {
        return published.load(std::memory_order_acquire) == 0;
    }
    bool matchesControl(const PluginHost &host, const PreparedPluginTransport::Ptr &plan,
                        const std::shared_ptr<Health> &owner) const noexcept
    {
        return &host == hostOwner && plan && plan.get() == transport && owner == health && healthy() &&
               guard.expectation().identity == host.pcmIdentity() &&
               guard.expectation().native == host.declaredLatency() &&
               guard.expectation().maximumSpan == plan->budget().sourceFrames;
    }
    // BEFORE any Host begin/input/clock work. Only the sole processing owner
    // calls this; coherent Host declaration is one existing packed atomic load.
    bool declaration(PluginHost &host) noexcept
    {
        if (!healthy())
            return false;
        if (&host != hostOwner)
            return refuse(Reason::ownerChanged);
        if (!guard.validateDeclaration(host.declaredLatency()))
            return refuse(Reason::guardRefused);
        return true;
    }
    bool beforeInput(PluginHost *host, int frames) noexcept
    {
        if (!healthy())
            return false;
        if (frames < 0 || frames > guard.expectation().maximumSpan)
            return refuse(Reason::invalidSpan, frames);
        if (!host)
            return refuse(Reason::ownerChanged, frames);
        return declaration(*host);
    }
    PluginPcmCapture *captureForProcessing() noexcept
    {
        return &capture;
    }
    // ALL actual segments are checked before the sampler returns to PluginNode.
    // False requires the caller to clear the entire output span. Source/worker
    // processing already performed is not rolled back or reset.
    bool emitted(int frames, PluginHost::AutomationStatus stream) noexcept
    {
        if (!healthy())
            return false;
        if (!guard.validate(capture, frames))
            return refuse(Reason::guardRefused, frames, stream);
        if (stream != PluginHost::AutomationStatus::ready)
            return refuse(Reason::streamRefused, frames, stream);
        return true;
    }
    std::optional<Failure> failure() const noexcept
    {
        const auto reason = Reason(published.load(std::memory_order_acquire));
        if (reason == Reason::none)
            return {};
        return Failure{reason, plugin, runtime, firstStream, firstFrames, guard.failure()};
    }

  private:
    const std::int64_t plugin;
    const void *const runtime;
    const PluginHost *const hostOwner;
    const PreparedPluginTransport *const transport;
    const std::shared_ptr<Health> health;
    FixedSourcePcmEpochGuard guard;
    std::vector<PluginPcmSegment> segments; // Sized CONTROL, never resized/copied in DSP.
    PluginPcmCapture capture;
    PluginHost::AutomationStatus firstStream = PluginHost::AutomationStatus::ready;
    int firstFrames = 0;
    std::atomic<std::uint32_t> published{0};
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static int maximum(const PreparedPluginTransport::Ptr &plan)
    {
        if (!plan || plan->budget().sourceFrames <= 0)
            throw std::invalid_argument("sourceEpoch.plan: Actual bounded initial source plan required");
        return plan->budget().sourceFrames;
    }
    static std::size_t capacity(const FixedSourcePcmEpochGuard::Expected &expected)
    {
        if (!FixedSourcePcmEpochGuard::validExpected(expected))
            throw std::invalid_argument("sourceEpoch.preparation: Invalid immutable source epoch");
        // Worst case starts at Q-1. Promote before arithmetic; no guessed two
        // segments when the actual admitted source span can cross more packets.
        return std::size_t(1 + (std::int64_t(expected.identity.quantum) - 1 + expected.maximumSpan - 1) /
                                   expected.identity.quantum);
    }
    bool refuse(Reason reason, int frames = 0,
                PluginHost::AutomationStatus stream = PluginHost::AutomationStatus::ready) noexcept
    {
        if (published.load(std::memory_order_relaxed) == 0)
        {
            firstFrames = frames;
            firstStream = stream;
            published.store(std::uint32_t(reason), std::memory_order_release);
            health->refuse();
        }
        return false;
    }
};
} // namespace vibera::initial_source_epoch
