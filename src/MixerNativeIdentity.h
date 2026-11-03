// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <algorithm>
#include <array>
#include <optional>
#include <type_traits>

namespace vibera
{
// CONTROL-only fixed-universe mapping. Display vector order never determines
// native track/bus identity. Not an allocator for create/delete: a changed ID
// set needs a separate stable-slot owner and acknowledged old-slot retirement.
class MixerNativeIdentity final
{
  public:
    static constexpr int capacity = 129, firstBus = 10000;
    enum class Error
    {
        none,
        count,
        negativeId,
        duplicateId,
        missingMaster
    };
    static Error prepare(const Project &project, MixerNativeIdentity &output) noexcept
    {
        MixerNativeIdentity next;
        if (project.mixer.empty() || project.mixer.size() > capacity)
            return Error::count;
        next.count_ = int(project.mixer.size());
        for (int i = 0; i < next.count_; ++i)
        {
            const int id = project.mixer[size_t(i)].index;
            if (id < 0)
                return Error::negativeId;
            next.ids_[size_t(i)] = id;
        }
        std::sort(next.ids_.begin(), next.ids_.begin() + next.count_);
        for (int i = 1; i < next.count_; ++i)
            if (next.ids_[size_t(i)] == next.ids_[size_t(i - 1)])
                return Error::duplicateId;
        if (next.ids_[0] != 0)
            return Error::missingMaster;
        output = next; // Strong refusal: no partial table publication.
        return Error::none;
    }
    int size() const noexcept
    {
        return count_;
    }
    // Slot0 reserves Master bus10000; only slots1..128 have native insert tracks.
    std::optional<int> idAtSlot(int slot) const noexcept
    {
        if (slot < 0 || slot >= count_)
            return {};
        return ids_[size_t(slot)];
    }
    std::optional<int> slot(int id) const noexcept
    {
        const auto end = ids_.begin() + count_;
        const auto found = std::lower_bound(ids_.begin(), end, id);
        if (found == end || *found != id)
            return {};
        return int(found - ids_.begin());
    }
    std::optional<int> bus(int id) const noexcept
    {
        if (const auto position = slot(id))
            return firstBus + *position;
        return {};
    }
    bool sameUniverse(const MixerNativeIdentity &other) const noexcept
    {
        return count_ == other.count_ && std::equal(ids_.begin(), ids_.begin() + count_, other.ids_.begin());
    }
    // Remove ONLY presentation row order from an already-copied comparison
    // document. All complete row values remain; changed controls/routes/FX are
    // still visible to the authoritative Project comparison. Refusal is atomic.
    static bool alignDisplayOrder(Project &candidate, const Project &reference) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<MixerInsert>);
        MixerNativeIdentity a, b;
        if (prepare(candidate, a) != Error::none || prepare(reference, b) != Error::none ||
            !a.sameUniverse(b))
            return false;
        for (size_t i = 0; i < reference.mixer.size(); ++i)
            if (candidate.mixer[i].index != reference.mixer[i].index)
                for (size_t j = i + 1; j < candidate.mixer.size(); ++j)
                    if (candidate.mixer[j].index == reference.mixer[i].index)
                    {
                        std::swap(candidate.mixer[i], candidate.mixer[j]);
                        break;
                    }
        return true;
    }

  private:
    std::array<int, capacity> ids_{};
    int count_ = 0;
};
} // namespace vibera
