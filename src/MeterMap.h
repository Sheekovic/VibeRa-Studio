// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <vector>

// Song time signatures for real-time readers (metronome): the base meter plus changes at
// bar lines, built on the control thread and immutable once published. Lookups do not allocate.
namespace vibera
{
struct MeterMap
{
    int numerator = 4, denominator = 4;
    std::vector<std::array<double, 3>> changes; // beat, numerator, denominator; rising beats.
    struct Position
    {
        int bar = 0;
        double beatInBar = 0, barLength = 4;
        int numerator = 4, denominator = 4;
    };
    Position at(double beat) const noexcept
    {
        Position p;
        int n = numerator, d = denominator, bars = 0;
        double from = 0;
        for (const auto &change : changes)
        {
            if (change[0] > beat + 1.e-9)
                break;
            bars += int(std::round((change[0] - from) / (n * 4. / d)));
            from = change[0];
            n = int(change[1]);
            d = int(change[2]);
        }
        p.barLength = n * 4. / d;
        const int whole = int(std::floor((beat - from) / p.barLength + 1.e-9));
        p.bar = bars + whole;
        p.beatInBar = beat - from - whole * p.barLength;
        p.numerator = n;
        p.denominator = d;
        return p;
    }
};
} // namespace vibera
