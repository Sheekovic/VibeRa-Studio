// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

namespace vibera
{
struct WorkspaceTiles
{
    std::array<juce::Rectangle<int>, 5> bounds{};
    bool fits = true, focused = false;
};
inline WorkspaceTiles tileWorkspace(juce::Rectangle<int> area, const std::array<bool, 5> &visible,
                                    int primary = -1, bool native = false)
{
    constexpr int gap = 7;
    std::array<int, 5> minimumHeight{160, 180, 200, 240, 160};
    if (native)
        for (auto &height : minimumHeight)
            height = std::max(height, 196);
    std::vector<int> items;
    for (int i = 0; i < 5; ++i)
        if (visible[size_t(i)])
            items.push_back(i);
    WorkspaceTiles result;
    if (items.empty())
        return result;
    if (area.isEmpty())
    {
        result.fits = false;
        return result;
    }
    auto stack = [&](const std::vector<int> &ids, juce::Rectangle<int> region)
    {
        int minimum = gap * (int(ids.size()) - 1);
        for (int id : ids)
            minimum += minimumHeight[size_t(id)];
        if (minimum > region.getHeight())
            return false;
        int remaining = region.getHeight() - minimum;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            int extra = remaining / int(ids.size() - i);
            remaining -= extra;
            result.bounds[size_t(ids[i])] = region.removeFromTop(minimumHeight[size_t(ids[i])] + extra);
            region.removeFromTop(gap);
        }
        return true;
    };
    if (primary >= 0 && primary < 5 && visible[size_t(primary)] && items.size() > 1 &&
        area.getWidth() >= 607 && area.getHeight() >= minimumHeight[size_t(primary)])
    {
        std::vector<int> secondary;
        for (int id : items)
            if (id != primary)
                secondary.push_back(id);
        auto rest = area;
        auto main = rest.removeFromLeft(juce::jlimit(300, area.getWidth() - 307, int(area.getWidth() * .62)));
        rest.removeFromLeft(gap);
        if (stack(secondary, rest))
        {
            result.bounds[size_t(primary)] = main;
            result.focused = true;
            return result;
        }
        if (items.size() == 4)
        {
            const int bottom =
                *std::max_element(secondary.begin(), secondary.end(), [&](int a, int b)
                                  { return minimumHeight[size_t(a)] < minimumHeight[size_t(b)]; });
            const int height = std::max(minimumHeight[size_t(bottom)], int(area.getHeight() * .34));
            secondary.erase(std::find(secondary.begin(), secondary.end(), bottom));
            if (main.getHeight() - height - gap >= minimumHeight[size_t(primary)] && stack(secondary, rest))
            {
                result.bounds[size_t(bottom)] = main.removeFromBottom(height);
                main.removeFromBottom(gap);
                result.bounds[size_t(primary)] = main;
                result.focused = true;
                return result;
            }
        }
        result = {};
    }
    double best = std::numeric_limits<double>::infinity();
    for (int columns = 1; columns <= int(items.size()); ++columns)
    {
        WorkspaceTiles candidate;
        double score = 0;
        const int rows = (int(items.size()) + columns - 1) / columns;
        for (int row = 0, item = 0; row < rows; ++row)
        {
            const int count = std::min(columns, int(items.size()) - item);
            const int top = area.getY() + row * (area.getHeight() + gap) / rows;
            const int bottom = area.getY() + (row + 1) * (area.getHeight() + gap) / rows - gap;
            for (int column = 0; column < count; ++column, ++item)
            {
                const int left = area.getX() + column * (area.getWidth() + gap) / count;
                const int right = area.getX() + (column + 1) * (area.getWidth() + gap) / count - gap;
                const int id = items[size_t(item)], minWidth = id == 4 ? 190 : 300;
                const auto bounds =
                    juce::Rectangle<int>(left, top, std::max(0, right - left), std::max(0, bottom - top));
                candidate.bounds[size_t(id)] = bounds;
                const int deficit = std::max(0, minWidth - bounds.getWidth()) +
                                    std::max(0, minimumHeight[size_t(id)] - bounds.getHeight());
                candidate.fits = candidate.fits && deficit == 0;
                const double aspect =
                    double(std::max(1, bounds.getWidth())) / std::max(1, bounds.getHeight());
                score += deficit * 1000. + std::pow(std::log(aspect / 1.9), 2);
            }
        }
        if (score < best)
        {
            best = score;
            result = candidate;
        }
    }
    return result;
}
} // namespace vibera
