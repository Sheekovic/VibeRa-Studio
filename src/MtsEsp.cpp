// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MtsEsp.h"
#include "Tuning.h"
#include "third_party/mts-esp/libMTSMaster.h"
#include <cmath>

namespace vibera
{
std::array<double, 128> channelFrequencies(const Channel &channel, double masterCents)
{
    std::array<double, 128> freqs{};
    for (int key = 0; key < 128; ++key)
    {
        const double cents = channel.scale.empty() ? channel.tuning[size_t(key % 12)]
                                                   : tuning::scaleDeviation(channel.scale, channel.scaleRoot, key);
        freqs[size_t(key)] = 440. * std::pow(2., (key - 69 + (cents + masterCents) / 100.) / 12.);
    }
    return freqs;
}
MtsEspMaster::~MtsEspMaster()
{
    if (active)
        MTS_DeregisterMaster();
}
int MtsEspMaster::clients() const
{
    return active ? MTS_GetNumClients() : 0;
}
void MtsEspMaster::sync(const Project &project)
{
    const Channel *source = nullptr;
    for (const auto &channel : project.channels)
        if (channel.mtsEsp)
            source = &channel;
    if (!source)
    {
        if (active)
            MTS_DeregisterMaster();
        active = false;
        return;
    }
    if (!active)
    {
        if (!MTS_CanRegisterMaster()) // No LIBMTS.dll, or another master is running.
            return;
        MTS_RegisterMaster();
        MTS_SetScaleName("VibeRa Studio");
        active = true;
        sent.fill(0);
    }
    if (const auto freqs = channelFrequencies(*source, project.masterPitch); freqs != sent)
    {
        MTS_SetNoteTunings(freqs.data());
        sent = freqs;
    }
}
} // namespace vibera
