// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <utility>
#include <vector>

namespace vibera
{
// Where an audio recording lands: `spans` are the Song beat ranges of the take's passes,
// stacked on `tracks` input tracks each (spans.size() * tracks consecutive lanes). Audio tracks
// (PlaylistTrack::audio) are tried first, in lane order; otherwise the first lanes that are
// free across every span. Returns 0..127 (the last block start if nothing fits).
inline int chooseTakeLane(const Project &project, const std::vector<std::pair<double, double>> &spans, int tracks)
{
    const int count = std::max(1, int(spans.size()));
    const int lanes = count * std::max(1, tracks);
    auto free = [&](int lane, double from, double to)
    {
        return std::none_of(project.placements.begin(), project.placements.end(),
                            [&](const auto &p) { return p.lane == lane && p.beat < to && p.beat + p.length > from; }) &&
               std::none_of(project.audioPlacements.begin(), project.audioPlacements.end(),
                            [&](const auto &p)
                            { return p.lane == lane && p.beat < to && project.audioEndBeat(p.beat, p.duration) > from; }) &&
               !(project.track(lane) && project.track(lane)->locked);
    };
    auto fits = [&](int lane)
    {
        if (lane + lanes > 128)
            return false;
        for (int i = 0; i < lanes; ++i)
        {
            const auto span = spans.empty() ? std::pair{0., 0.} : spans[size_t(i % count)];
            if (!free(lane + i, span.first, span.second))
                return false;
        }
        return true;
    };
    for (const auto &track : project.tracks)
        if (track.audio && fits(track.lane))
            return track.lane;
    int lane = 0;
    while (lane + lanes < 128 && !fits(lane))
        ++lane;
    return lane;
}
} // namespace vibera
