// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <array>

namespace vibera
{
// MTS-ESP master: broadcasts one Channel's tuning (12-tone table or keyboard-mapped scale)
// to every MTS-ESP client plugin through the user-installed LIBMTS.dll. Control thread only.
class MtsEspMaster
{
  public:
    ~MtsEspMaster();
    // Registers, updates or deregisters for the Channel marked mtsEsp in `project`.
    void sync(const Project &project);
    bool registered() const noexcept { return active; }
    int clients() const;

  private:
    bool active = false;
    std::array<double, 128> sent{};
};
// Frequencies (Hz) of MIDI keys 0..127 under a Channel's tuning (A4 = 440 Hz).
std::array<double, 128> channelFrequencies(const Channel &channel);
} // namespace vibera
