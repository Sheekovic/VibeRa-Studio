// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <functional>
namespace vibera
{
// Message-thread ownership shared by typing and preview gestures.
class PerformanceInput
{
    std::array<std::array<bool, 128>, 2> held{};

  public:
    std::function<void(int, bool, int)> emit;
    void set(int source, int pitch, bool on, int velocity = 100)
    {
        if (source < 0 || source > 1 || pitch < 0 || pitch > 127 || held[size_t(source)][size_t(pitch)] == on)
            return;
        bool before = held[0][size_t(pitch)] || held[1][size_t(pitch)];
        held[size_t(source)][size_t(pitch)] = on;
        bool after = held[0][size_t(pitch)] || held[1][size_t(pitch)];
        if (before != after && emit)
            emit(pitch, after, velocity);
    }
    void release(int source)
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            set(source, pitch, false, 0);
    }
};
} // namespace vibera
