// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <cstddef>
#include <vector>

namespace vibera
{
struct GroovePoint
{
    double beat = 0, length = .25, velocity = .8;
};
struct GrooveTemplate
{
    juce::String name;
    double cycle = 4;
    std::vector<GroovePoint> points;
};
juce::Result validateGroove(const GrooveTemplate &groove);
juce::var grooveState(const GrooveTemplate &groove);
bool grooveNumber(const juce::var &value);
bool grooveKeys(const juce::var &value, const juce::StringArray &keys);
juce::Result parseGroove(const juce::var &value, GrooveTemplate &destination);
juce::Result captureGroove(const Project &project, Id patternId, Id channelId, const std::vector<Id> &ids,
                           double cycle, const juce::String &name, GrooveTemplate &destination);
std::vector<GrooveTemplate> factoryGrooves();
std::vector<double> grooveGuidePositions(const GrooveTemplate &groove, double begin, double end,
                                         size_t limit = 2048);
juce::Result loadGroove(const juce::File &file, GrooveTemplate &destination);
juce::Result saveGroove(const GrooveTemplate &groove, const juce::File &file, bool overwrite);
} // namespace vibera
