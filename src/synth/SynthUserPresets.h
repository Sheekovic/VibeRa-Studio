// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SynthBrowser.h"
#include "SynthState.h"
#include <set>
#include <utility>
#include <vector>

namespace vibera::synth
{
struct SynthUserPresetMetadata
{
    juce::String id, name, basePreset, family, genre, source, sampleAsset;
    MacroMapping macros;
    juce::File file;
    juce::var toVar() const
    {
        juce::var value(new juce::DynamicObject);
        auto *object = value.getDynamicObject();
        object->setProperty("id", id);
        object->setProperty("name", name);
        object->setProperty("basePreset", basePreset);
        object->setProperty("family", family);
        object->setProperty("genre", genre);
        object->setProperty("source", source);
        object->setProperty("sampleAsset", sampleAsset);
        object->setProperty("path", file.getFullPathName());
        object->setProperty("factory", false);
        object->setProperty("schemaVersion", 1);
        juce::Array<juce::var> genres, tags{family, source, "user"};
        for (const auto &tag : juce::StringArray::fromTokens(genre, "|,", ""))
            genres.add(tag);
        object->setProperty("genres", genres);
        object->setProperty("tags", tags);

        juce::var mapping(new juce::DynamicObject);
        auto *m = mapping.getDynamicObject();
        m->setProperty("toneMinimumHz", macros.toneMinimumHz);
        m->setProperty("toneMaximumHz", macros.toneMaximumHz);
        m->setProperty("warmthMaximumBoostDb", macros.warmthMaximumBoostDb);
        m->setProperty("spaceMaximumMix", macros.spaceMaximumMix);
        m->setProperty("motionMaximumCents", macros.motionMaximumCents);
        object->setProperty("macros", mapping);
        return value;
    }
};
struct SynthUserPreset
{
    SynthUserPresetMetadata metadata;
    juce::MemoryBlock nativeState;
    Values normalizedValues{};
};
namespace synth_user_detail
{
inline bool validId(const juce::String &id)
{
    return id.length() == 32 && !juce::Uuid(id).isNull() && juce::Uuid(id).toString() == id;
}
inline juce::String sourceName(const Values &values)
{
    const auto range = synthParameterRange(parameterSpec(ParameterId::sourceType));
    const auto source = std::lround(
        range.snapToLegalValue(range.convertFrom0to1(values[parameterIndex(ParameterId::sourceType)])));
    return sourceKindName(static_cast<SourceKind>(source));
}
inline bool macroValue(const juce::XmlElement &node, const char *name, float expected)
{
    const auto token = node.getStringAttribute(name);
    char *end = nullptr;
    const auto number = std::strtod(token.toRawUTF8(), &end);
    return token.isNotEmpty() && end && *end == '\0' && std::isfinite(number) &&
           std::abs(number - double(expected)) <= 1e-6;
}
inline juce::Result decode(const void *data, int bytes, SynthUserPreset &destination)
{
    const auto fail = [](const char *reason)
    { return juce::Result::fail("synth.invalidUserPreset: " + juce::String(reason)); };
    if (!data || bytes < 8 || std::size_t(bytes) > maximumSynthStateBytes)
        return fail("Maximum user preset size is256KB");
    const auto xml = juce::AudioProcessor::getXmlFromBinary(data, bytes);
    if (!xml || !xml->hasTagName("VIBERA_SYNTH_PRESET") || xml->getNumAttributes() != 6 ||
        xml->getStringAttribute("schemaVersion") != "1")
        return fail("Unknown user preset envelope/schema");
    SynthUserPreset next;
    auto &m = next.metadata;
    m.id = xml->getStringAttribute("id");
    m.name = xml->getStringAttribute("name");
    m.basePreset = xml->getStringAttribute("basePreset");
    m.family = xml->getStringAttribute("family");
    m.genre = xml->getStringAttribute("genre");
    if (!validId(m.id) || m.name.trim().isEmpty() || m.name.length() > 128 || m.name != m.name.trim())
        return fail("Invalid stable ID or display name");
    const auto *base = findFactoryPreset(m.basePreset.toStdString());
    if (!base)
        return fail("Unknown base factory preset");
    if (m.family != base->family || m.genre != base->genre)
        return fail("Family/genre disagree with current base reference");
    const juce::XmlElement *native = nullptr, *source = nullptr, *macros = nullptr;
    for (auto *node = xml->getFirstChildElement(); node; node = node->getNextElement())
    {
        if (node->hasTagName("VIBERA_SYNTHESIZER") && !native)
            native = node;
        else if (node->hasTagName("SOURCE") && !source)
            source = node;
        else if (node->hasTagName("MACROS") && !macros)
            macros = node;
        else
            return fail("Unknown or duplicate wrapper child");
    }
    if (!native || !source || !macros || source->getNumAttributes() != 2 ||
        source->getNumChildElements() != 0 || macros->getNumAttributes() != 5 ||
        macros->getNumChildElements() != 0)
        return fail("Incomplete source/macro/native content");
    juce::AudioProcessor::copyXmlToBinary(*native, next.nativeState);
    if (auto result = parseSynthNativeState(next.nativeState.getData(), int(next.nativeState.getSize()),
                                            next.normalizedValues);
        result.failed())
        return result;
    const auto &actualBase = synthStateBasePreset(next.normalizedValues);
    if (m.basePreset != actualBase.id)
        return fail("Wrapper/native base identities disagree");
    m.source = source->getStringAttribute("kind");
    m.sampleAsset = source->getStringAttribute("sampleAsset");
    if (m.source != sourceName(next.normalizedValues) || m.sampleAsset != base->sampleAsset)
        return fail("Source references disagree with native state");
    m.macros = base->macros;
    if (!macroValue(*macros, "toneMinimumHz", m.macros.toneMinimumHz) ||
        !macroValue(*macros, "toneMaximumHz", m.macros.toneMaximumHz) ||
        !macroValue(*macros, "warmthMaximumBoostDb", m.macros.warmthMaximumBoostDb) ||
        !macroValue(*macros, "spaceMaximumMix", m.macros.spaceMaximumMix) ||
        !macroValue(*macros, "motionMaximumCents", m.macros.motionMaximumCents))
        return fail("Unsupported changed macro mapping");
    next.nativeState = makeSynthNativeState(next.normalizedValues);
    destination = std::move(next);
    return juce::Result::ok();
}
} // namespace synth_user_detail
inline juce::Result readSynthUserPreset(const juce::File &file, SynthUserPreset &destination)
{
    if (!file.hasFileExtension("vrsynth") || file.getSize() < 8 ||
        file.getSize() > juce::int64(maximumSynthStateBytes))
        return juce::Result::fail("synth.invalidUserPreset: Expected a bounded .vrsynth file");
    auto stream = file.createInputStream();
    if (!stream)
        return juce::Result::fail("Cannot read user preset");
    juce::MemoryBlock bytes;
    stream->readIntoMemoryBlock(bytes, juce::ssize_t(maximumSynthStateBytes + 1));
    if (bytes.getSize() > maximumSynthStateBytes)
        return juce::Result::fail("synth.invalidUserPreset: Maximum user preset size is256KB");
    SynthUserPreset next;
    if (auto result = synth_user_detail::decode(bytes.getData(), int(bytes.getSize()), next); result.failed())
        return result;
    next.metadata.file = file;
    destination = std::move(next);
    return juce::Result::ok();
}
inline juce::Result writeSynthUserPreset(const juce::File &file, const juce::MemoryBlock &state,
                                         SynthUserPresetMetadata *written = nullptr)
{
    if (!file.isAChildOf(synthUserDirectory()) || !file.hasFileExtension("vrsynth"))
        return juce::Result::fail("synth.userPath: Save a .vrsynth inside the user preset directory");
    Values values{};
    if (auto result = parseSynthNativeState(state.getData(), int(state.getSize()), values); result.failed())
        return result;
    const auto name = file.getFileNameWithoutExtension().trim();
    if (name.isEmpty() || name.length() > 128)
        return juce::Result::fail("synth.userName: Use a display name from1to128characters");
    juce::String id;
    if (file.existsAsFile())
    {
        SynthUserPreset previous;
        if (auto result = readSynthUserPreset(file, previous); result.failed())
            return result;
        id = previous.metadata.id;
    }
    else
        id = juce::Uuid().toString();
    const auto &base = synthStateBasePreset(values);
    juce::XmlElement xml("VIBERA_SYNTH_PRESET");
    xml.setAttribute("schemaVersion", 1);
    xml.setAttribute("id", id);
    xml.setAttribute("name", name);
    xml.setAttribute("basePreset", base.id);
    xml.setAttribute("family", base.family);
    xml.setAttribute("genre", base.genre);
    auto *source = xml.createNewChildElement("SOURCE");
    source->setAttribute("kind", synth_user_detail::sourceName(values));
    source->setAttribute("sampleAsset", base.sampleAsset);
    auto *macros = xml.createNewChildElement("MACROS");
    macros->setAttribute("toneMinimumHz", double(base.macros.toneMinimumHz));
    macros->setAttribute("toneMaximumHz", double(base.macros.toneMaximumHz));
    macros->setAttribute("warmthMaximumBoostDb", double(base.macros.warmthMaximumBoostDb));
    macros->setAttribute("spaceMaximumMix", double(base.macros.spaceMaximumMix));
    macros->setAttribute("motionMaximumCents", double(base.macros.motionMaximumCents));
    const auto nativeBytes = makeSynthNativeState(values);
    auto native = juce::AudioProcessor::getXmlFromBinary(nativeBytes.getData(), int(nativeBytes.getSize()));
    xml.addChildElement(native.release());
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(xml, bytes);
    SynthUserPreset validated;
    if (auto result = synth_user_detail::decode(bytes.getData(), int(bytes.getSize()), validated);
        result.failed())
        return result;
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail("Cannot create user preset directory");
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithData(bytes.getData(), bytes.getSize()) ||
        !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Cannot save user preset; existing file retained");
    validated.metadata.file = file;
    if (written)
        *written = std::move(validated.metadata);
    return juce::Result::ok();
}
inline juce::Result listSynthUserPresets(const juce::File &directory,
                                         std::vector<SynthUserPresetMetadata> &destination,
                                         juce::StringArray &errors)
{
    const auto user = synthUserDirectory();
    if (directory != user && !directory.isAChildOf(user))
        return juce::Result::fail("synth.userPath: List only user preset directories");
    std::vector<SynthUserPresetMetadata> rows;
    juce::StringArray issues;
    if (!directory.exists())
    {
        destination.clear();
        errors.clear();
        return juce::Result::ok();
    }
    std::set<juce::String> ids;
    std::vector<std::pair<juce::File, unsigned>> folders{{directory, 0}};
    std::size_t visited = 0;
    for (std::size_t folder = 0; folder < folders.size(); ++folder)
    {
        for (const auto &entry : juce::RangedDirectoryIterator(folders[folder].first, false, "*",
                                                               juce::File::findFilesAndDirectories))
        {
            if (++visited > 4096)
                return juce::Result::fail("synth.userPresetLimit: Maximum4096directory entries");
            const auto file = entry.getFile();
            if (file.isSymbolicLink())
            {
                issues.add(file.getFileName() + ": Linked user preset paths are not listed");
                continue;
            }
            if (entry.isDirectory())
            {
                if (folders.size() >= 128 || folders[folder].second >= 8)
                    return juce::Result::fail("synth.userPresetLimit: Directory traversal bound exceeded");
                folders.emplace_back(file, folders[folder].second + 1);
                continue;
            }
            if (!file.hasFileExtension("vrsynth"))
                continue;
            if (rows.size() + std::size_t(issues.size()) >= 512)
                return juce::Result::fail("synth.userPresetLimit: Maximum512user preset files");
            SynthUserPreset document;
            const auto result = readSynthUserPreset(file, document);
            if (result.failed())
                issues.add(file.getFileName() + ": " + result.getErrorMessage());
            else if (!ids.insert(document.metadata.id).second)
                issues.add(file.getFileName() + ": Duplicate stable user preset ID");
            else
                rows.push_back(std::move(document.metadata));
        }
    }
    destination = std::move(rows);
    errors = std::move(issues);
    return juce::Result::ok();
}
} // namespace vibera::synth
