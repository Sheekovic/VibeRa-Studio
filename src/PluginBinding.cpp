// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginBinding.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>

namespace vibera
{
namespace
{
bool integer(const juce::var &value, std::int64_t &result)
{
    if (value.isInt() || value.isInt64())
    {
        result = juce::int64(value);
        return true;
    }
    if (!value.isString())
        return false;
    const auto text = value.toString().toStdString();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
bool scalar(const juce::var &value, float &result)
{
    double number = 0;
    if (value.isInt() || value.isInt64() || value.isDouble())
        number = double(value);
    else if (value.isString())
    {
        const auto text = value.toString().toStdString();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return false;
    }
    else
        return false;
    if (!std::isfinite(number) || number < 0 || number > 1)
        return false;
    result = float(number);
    return true;
}
} // namespace
bool validPluginBindings(std::span<const PluginBinding> bindings)
{
    if (bindings.size() > 4096)
        return false;
    std::set<std::uint32_t> ids;
    for (const auto &binding : bindings)
        if (!ids.insert(binding.id).second || !std::isfinite(binding.manual) || binding.manual < 0 ||
            binding.manual > 1 || binding.steps < 0 || (binding.flags & 1u) == 0 ||
            (binding.flags & ((1u << 1) | (1u << 4) | (1u << 15))) != 0 || binding.name.length() > 1024 ||
            binding.label.length() > 128)
            return false;
    return true;
}
juce::ValueTree pluginBindingsState(std::span<const PluginBinding> bindings)
{
    juce::ValueTree root("AUTOMATION_BINDINGS");
    for (const auto &binding : bindings)
    {
        juce::ValueTree item("BINDING");
        item.setProperty("id", juce::int64(binding.id), nullptr);
        item.setProperty("flags", juce::int64(binding.flags), nullptr);
        item.setProperty("steps", binding.steps, nullptr);
        item.setProperty("manual", double(binding.manual), nullptr);
        item.setProperty("name", binding.name, nullptr);
        item.setProperty("label", binding.label, nullptr);
        root.appendChild(item, nullptr);
    }
    return root;
}
juce::Result parsePluginBindings(const juce::ValueTree &root, std::vector<PluginBinding> &output)
{
    const auto fail = [] { return juce::Result::fail("Malformed or unsupported plugin automation binding"); };
    if (!root.hasType("AUTOMATION_BINDINGS") || root.getNumProperties() != 0 || root.getNumChildren() > 4096)
        return fail();
    std::vector<PluginBinding> parsed;
    for (const auto &item : root)
    {
        if (!item.hasType("BINDING") || item.getNumProperties() != 6 || item.getNumChildren() != 0)
            return fail();
        for (const auto *name : {"id", "flags", "steps", "manual", "name", "label"})
            if (!item.hasProperty(name))
                return fail();
        std::int64_t id = 0, flags = 0, steps = 0;
        PluginBinding binding;
        if (!integer(item["id"], id) || id < 0 || id > std::numeric_limits<std::uint32_t>::max() ||
            !integer(item["flags"], flags) || flags < 0 ||
            flags > std::numeric_limits<std::uint32_t>::max() || !integer(item["steps"], steps) ||
            steps < 0 || steps > std::numeric_limits<int>::max() || !scalar(item["manual"], binding.manual) ||
            !item["name"].isString() || !item["label"].isString())
            return fail();
        binding.id = std::uint32_t(id);
        binding.flags = std::uint32_t(flags);
        binding.steps = int(steps);
        binding.name = item["name"].toString();
        binding.label = item["label"].toString();
        parsed.push_back(std::move(binding));
    }
    if (!validPluginBindings(parsed))
        return fail();
    output = std::move(parsed);
    return juce::Result::ok();
}
PluginBindingAdmission admitPluginBindings(std::span<const PluginBinding> bindings,
                                           const PluginParameterCatalogue &catalogue,
                                           unsigned currentGeneration)
{
    if (!validPluginBindings(bindings))
        return {PluginBindingStatus::invalidDeclaration};
    if (catalogue.error != 0 || catalogue.generation == 0 || catalogue.reportedCount < 0 ||
        catalogue.parameters.size() > ipc::parameters ||
        std::size_t(catalogue.reportedCount) != catalogue.parameters.size())
        return {PluginBindingStatus::catalogueUnavailable};
    if (currentGeneration == 0 || currentGeneration != catalogue.generation)
        return {PluginBindingStatus::staleCatalogue};
    std::unordered_map<std::uint32_t, const PluginParameter *> native;
    native.reserve(catalogue.parameters.size());
    for (const auto &p : catalogue.parameters)
        if (p.steps < 0 || (p.availability & ~3u) != 0 || !std::isfinite(p.value) || p.value < 0 ||
            p.value > 1 || !std::isfinite(p.defaultValue) || p.defaultValue < 0 || p.defaultValue > 1 ||
            !native.emplace(p.id, &p).second)
            return {PluginBindingStatus::catalogueUnavailable};
    for (const auto &binding : bindings)
    {
        const auto found = native.find(binding.id);
        if (found == native.end())
            return {PluginBindingStatus::missingParameter, 0, binding.id};
        const auto &p = *found->second;
        if ((p.availability & 1u) == 0)
            return {PluginBindingStatus::unavailableParameter, 0, binding.id};
        if (!p.automatable() || (p.flags & (1u << 15)) != 0)
            return {PluginBindingStatus::notAutomatable, 0, binding.id};
        if (p.flags != binding.flags || p.steps != binding.steps)
            return {PluginBindingStatus::schemaChanged, 0, binding.id};
    }
    return {PluginBindingStatus::ready, currentGeneration};
}
} // namespace vibera
