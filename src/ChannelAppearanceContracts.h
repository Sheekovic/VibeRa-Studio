// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <cmath>

namespace vibera
{
inline const char *channelSortName(ChannelSortRequest::By by)
{
    switch (by)
    {
    case ChannelSortRequest::By::name:
        return "name";
    case ChannelSortRequest::By::colour:
        return "colour";
    case ChannelSortRequest::By::mixer:
        return "mixer";
    case ChannelSortRequest::By::type:
        return "type";
    }
    return "invalid";
}
inline juce::Result decodeChannelAppearance(const juce::var &values, CommandArgs &args)
{
    const auto fail = []
    {
        return juce::Result::fail("channelAppearance.invalidArguments: Invalid typed appearance/sort fields");
    };
    if (values.hasProperty("appearance"))
    {
        const auto value = values["appearance"];
        const auto *object = value.getDynamicObject();
        if (!object || object->getProperties().size() == 0)
            return fail();
        ChannelAppearancePatch patch;
        for (const auto &property : object->getProperties())
        {
            if (property.name == juce::Identifier("name"))
            {
                if (!property.value.isString())
                    return fail();
                patch.name = property.value.toString();
            }
            else if (property.name == juce::Identifier("icon"))
            {
                if (!property.value.isString() || !validChannelIcon(property.value.toString()))
                    return fail();
                patch.icon = property.value.toString();
            }
            else if (property.name == juce::Identifier("colour"))
            {
                if (!property.value.isInt() && !property.value.isInt64() && !property.value.isDouble())
                    return fail();
                const auto colour = double(property.value);
                if (!std::isfinite(colour) || std::floor(colour) != colour || colour < 0 ||
                    colour > 4294967295.0 || !validChannelColour(juce::uint32(colour)))
                    return fail();
                patch.colour = juce::uint32(colour);
            }
            else
                return fail();
        }
        args.appearance = std::move(patch);
    }
    if (values.hasProperty("channelSort"))
    {
        const auto value = values["channelSort"];
        const auto *object = value.getDynamicObject();
        if (!object || !value["by"].isString())
            return fail();
        for (const auto &property : object->getProperties())
            if (property.name != juce::Identifier("by") && property.name != juce::Identifier("descending"))
                return fail();
        ChannelSortRequest request;
        const auto by = value["by"].toString();
        if (by == "name")
            request.by = ChannelSortRequest::By::name;
        else if (by == "colour")
            request.by = ChannelSortRequest::By::colour;
        else if (by == "mixer")
            request.by = ChannelSortRequest::By::mixer;
        else if (by == "type")
            request.by = ChannelSortRequest::By::type;
        else
            return fail();
        if (value.hasProperty("descending") && !value["descending"].isBool())
            return fail();
        request.descending = bool(value.getProperty("descending", false));
        args.channelSort = request;
    }
    return juce::Result::ok();
}
inline void encodeChannelAppearance(juce::DynamicObject &object, const CommandArgs &args)
{
    if (args.appearance)
    {
        auto *patch = new juce::DynamicObject;
        if (args.appearance->name)
            patch->setProperty("name", *args.appearance->name);
        if (args.appearance->colour)
            patch->setProperty("colour", juce::int64(*args.appearance->colour));
        if (args.appearance->icon)
            patch->setProperty("icon", *args.appearance->icon);
        object.setProperty("appearance", juce::var(patch));
    }
    if (args.channelSort)
    {
        auto *sort = new juce::DynamicObject;
        sort->setProperty("by", channelSortName(args.channelSort->by));
        sort->setProperty("descending", args.channelSort->descending);
        object.setProperty("channelSort", juce::var(sort));
    }
}
inline juce::var channelAppearanceSchema(const juce::String &id)
{
    auto schema = juce::JSON::parse(R"({"type":"object","additionalProperties":false,"properties":{
      "channel":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Existing stable Channel ID. Explicit target; never implicit current selection."},
      "ids":{"type":"array","minItems":1,"maxItems":128,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000},"description":"Explicit existing Channels. Sorting rearranges only their occupied slots, preserving unselected rows and equal-key order."},
      "appearance":{"type":"object","additionalProperties":false,"minProperties":1,"properties":{
        "name":{"type":"string","minLength":1,"maxLength":256,"description":"Single Channel only, trimmed one-line name. Sample replacement retains existing auto-naming behavior."},
        "colour":{"type":"integer","minimum":0,"maximum":4294967295,"description":"Opaque 0xFFRRGGBB ARGB integer;0 follows theme. Omitted leaves unchanged."},
        "icon":{"type":"string","enum":["none","drum","hat","keys","bass","synth","mic","sample","audio"],"description":"Original vector icon;none clears it. Omitted leaves unchanged."}}},
      "channelSort":{"type":"object","additionalProperties":false,"required":["by"],"properties":{
        "by":{"type":"string","enum":["name","colour","mixer","type"],"description":"Case-insensitive name; assigned-colour hue (theme first, greys last); stable Mixer ID; instrument/sampler/Audio Clip type. Theme-following colour ties retain order."},
        "descending":{"type":"boolean","default":false}}}}})");
    auto *properties = schema["properties"].getDynamicObject();
    juce::Array<juce::var> required;
    const bool single = id == "channel.properties", sort = id == "rack.sort";
    required.add(single ? "channel" : "ids");
    required.add(sort ? "channelSort" : "appearance");
    properties->removeProperty(single ? "ids" : "channel");
    properties->removeProperty(sort ? "appearance" : "channelSort");
    if (id == "rack.properties")
        properties->getProperty("appearance")["properties"].getDynamicObject()->removeProperty("name");
    schema.getDynamicObject()->setProperty("required", required);
    return schema;
}
} // namespace vibera
