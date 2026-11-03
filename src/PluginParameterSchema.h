// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginProtocol.h"
#include <cstring>
#include <string>
#include <vector>

namespace vibera
{
// Owned control-thread metadata. Current values travel with a publication but
// do not change the native-ID/capability authority of an existing route.
struct PreparedParameterSchema
{
    struct Entry
    {
        ipc::ParameterInfo info;
        int category = 0;
        bool sameSchema(const Entry &other) const noexcept
        {
            const auto &a = info;
            const auto &b = other.info;
            return category == other.category && a.id == b.id && a.flags == b.flags &&
                   a.availability == b.availability && a.stepCount == b.stepCount && a.unit == b.unit &&
                   a.defaultValue == b.defaultValue && std::strncmp(a.name, b.name, sizeof(a.name)) == 0 &&
                   std::strncmp(a.label, b.label, sizeof(a.label)) == 0;
        }
    };
    int reportedCount = 0, error = 0;
    std::string diagnostic;
    std::vector<Entry> parameters;
    bool sameSchema(const PreparedParameterSchema &other) const noexcept
    {
        if (reportedCount != other.reportedCount || error != other.error || diagnostic != other.diagnostic ||
            parameters.size() != other.parameters.size())
            return false;
        for (size_t i = 0; i < parameters.size(); ++i)
            if (!parameters[i].sameSchema(other.parameters[i]))
                return false;
        return true;
    }
};
} // namespace vibera
