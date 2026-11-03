// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "ChannelAppearance.h"
#include "ChannelManagement.h"
#include <array>

static void channelAppearanceTests(const juce::File &folder)
{
    Controller c;
    const auto kick = c.project.channels[0].id, snare = c.project.channels[1].id,
               hat = c.project.channels[2].id;
    c.project.channel(kick)->mixer = 2;
    c.project.channel(kick)->sampler.cents = 17;
    for (const auto &channel : c.project.channels)
        c.project.patterns.front().notes.push_back({c.project.allocate(), channel.id, .5, 1, 60, .7f, false});
    c.project.placements.push_back({c.project.allocate(), c.project.patterns.front().id, 0, 0, 4, 0});
    require(c.project.validate().wasOk(), "Original appearance fixture has real score/routing/source data");
    const auto before = c.project;
    const auto unchangedMusic = [&](const Project &original)
    {
        auto normalized = c.project;
        for (auto &channel : normalized.channels)
        {
            const auto *old = original.channel(channel.id);
            require(old != nullptr, "Appearance never replaces a stable Channel identity");
            channel.name = old->name;
            channel.colour = old->colour;
            channel.icon = old->icon;
        }
        require(normalized.state().isEquivalentTo(original.state()),
                "Appearance changes only explicit metadata, preserving order/notes/routes/source/allocator");
    };
    const auto refused = [&](const char *command, const CommandArgs &args)
    {
        const auto state = c.project.state();
        const auto revision = c.revision;
        const auto undo = c.undo.getUndoDescriptions(), redo = c.undo.getRedoDescriptions();
        require(c.execute(command, args).failed() && c.project.state().isEquivalentTo(state) &&
                    c.revision == revision && c.undo.getUndoDescriptions() == undo &&
                    c.undo.getRedoDescriptions() == redo,
                "Invalid appearance/sort is atomic including revision and both history directions");
    };
    const auto noOp = [&](const char *command, const CommandArgs &args)
    {
        const auto state = c.project.state();
        const auto revision = c.revision;
        const auto undo = c.undo.getUndoDescriptions(), redo = c.undo.getRedoDescriptions();
        require(c.execute(command, args).wasOk() && c.project.state().isEquivalentTo(state) &&
                    c.revision == revision && c.undo.getUndoDescriptions() == undo &&
                    c.undo.getRedoDescriptions() == redo,
                "Equivalent appearance/sort adds no hidden Undo or Redo edge");
    };

    CommandArgs a;
    a.channel = kick;
    ChannelAppearancePatch patch;
    patch.name = "  Renamed kick  ";
    patch.colour = 0xffe04b39u;
    patch.icon = "drum";
    a.appearance = patch;
    const auto revision = c.revision;
    require(c.execute("channel.properties", a).wasOk() && c.revision == revision + 1 &&
                c.project.channel(kick)->name == "Renamed kick" &&
                c.project.channel(kick)->colour == 0xffe04b39u && c.project.channel(kick)->icon == "drum" &&
                c.project.channel(snare)->name == before.channel(snare)->name,
            "Explicit single Channel patch trims name and atomically applies colour/icon to that ID only");
    unchangedMusic(before);
    const auto appearance = c.project.state();
    noOp("channel.properties", a);
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before.state()), "Appearance is one exact musical Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(appearance),
            "Appearance Redo restores exact metadata/identities");
    patch = {};
    patch.colour = 0;
    a.appearance = patch;
    require(c.execute("channel.properties", a).wasOk() && c.project.channel(kick)->colour == 0 &&
                c.project.channel(kick)->name == "Renamed kick" && c.project.channel(kick)->icon == "drum",
            "Explicit theme-colour reset preserves omitted name/icon");
    c.undoEdit();
    patch = {};
    patch.icon = "none";
    a.appearance = patch;
    require(c.execute("channel.properties", a).wasOk() && c.project.channel(kick)->icon == "none" &&
                c.project.channel(kick)->colour == 0xffe04b39u &&
                c.project.channel(kick)->name == "Renamed kick",
            "Explicit icon reset preserves omitted colour/name");
    c.undoEdit();
    patch = {};
    patch.name = "Name only";
    a.appearance = patch;
    require(c.execute("channel.properties", a).wasOk() && c.project.channel(kick)->name == "Name only" &&
                c.project.channel(kick)->colour == 0xffe04b39u && c.project.channel(kick)->icon == "drum",
            "Name-only partial patch preserves other metadata");
    c.undoEdit();

    for (const auto *icon : {"hat", "keys", "bass", "synth", "mic", "sample", "audio", "none"})
    {
        patch = {};
        patch.icon = icon;
        a.appearance = patch;
        require(c.execute("channel.properties", a).wasOk() && c.project.channel(kick)->icon == icon,
                "Every original explicit icon ID is valid canonical metadata");
        c.undoEdit();
        require(c.project.state().isEquivalentTo(appearance),
                "Icon-only Undo preserves original metadata/music");
    }

    CommandArgs bulk;
    bulk.ids = {hat, kick};
    ChannelAppearancePatch bulkPatch;
    bulkPatch.colour = 0xff739abdu;
    bulkPatch.icon = "hat";
    bulk.appearance = bulkPatch;
    const auto bulkBefore = c.project;
    require(c.execute("rack.properties", bulk).wasOk() && c.project.channel(kick)->colour == 0xff739abdu &&
                c.project.channel(hat)->colour == 0xff739abdu && c.project.channel(kick)->icon == "hat" &&
                c.project.channel(hat)->icon == "hat" && c.project.channel(snare)->colour == 0 &&
                c.project.channel(kick)->name == bulkBefore.channel(kick)->name,
            "Bulk colour/icon patches exact unsorted IDs without renaming or touching another row");
    unchangedMusic(bulkBefore);
    const auto bulkState = c.project.state();
    noOp("rack.properties", bulk);
    c.undoEdit();
    require(c.project.state().isEquivalentTo(bulkBefore.state()), "Bulk metadata is one exact Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(bulkState), "Bulk metadata exact Redo");
    auto resetBulk = bulkPatch;
    resetBulk.colour = 0;
    resetBulk.icon = "none";
    bulk.appearance = resetBulk;
    require(c.execute("rack.properties", bulk).wasOk() && c.project.channel(kick)->colour == 0 &&
                c.project.channel(hat)->colour == 0 && c.project.channel(kick)->icon == "none" &&
                c.project.channel(hat)->icon == "none" && c.project.channel(kick)->name == "Renamed kick",
            "Bulk explicit theme/icon reset affects only selected metadata and retains names");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(bulkState),
            "Bulk reset Undo restores both original selected values");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(appearance), "Original single appearance baseline restored");

    CommandArgs invalid;
    invalid.channel = kick;
    refused("channel.properties", invalid);
    invalid.appearance = ChannelAppearancePatch{};
    refused("channel.properties", invalid);
    for (const auto &name : {juce::String("  "), juce::String("line\nbreak"), juce::String("line\rbreak"),
                             juce::String::repeatedString("x", 257)})
    {
        auto bad = ChannelAppearancePatch{};
        bad.name = name;
        bad.colour = 0xff112233u; // Valid neighbour must not be partially published.
        invalid.appearance = bad;
        refused("channel.properties", invalid);
    }
    for (const auto colour : {juce::uint32(0x00112233u), juce::uint32(0x80112233u)})
    {
        auto bad = ChannelAppearancePatch{};
        bad.name = "Must not rename";
        bad.colour = colour;
        invalid.appearance = bad;
        refused("channel.properties", invalid);
    }
    patch = {};
    patch.icon = "invented-icon";
    invalid.appearance = patch;
    refused("channel.properties", invalid);
    patch.icon = "sample";
    invalid.appearance = patch;
    invalid.channel = 0;
    refused("channel.properties", invalid); // No implicit selected Channel.
    invalid.channel = 999999;
    refused("channel.properties", invalid);
    invalid = {};
    invalid.appearance = bulkPatch;
    for (const auto &ids : {std::vector<Id>{}, std::vector<Id>{kick, kick}, std::vector<Id>{kick, 999999}})
    {
        invalid.ids = ids;
        refused("rack.properties", invalid);
    }
    invalid.ids = {kick, hat};
    auto illegalName = bulkPatch;
    illegalName.name = "No ambiguous bulk rename";
    invalid.appearance = illegalName;
    refused("rack.properties", invalid);
    invalid.appearance.reset();
    refused("rack.properties", invalid);

    CommandArgs clone;
    clone.ids = {kick};
    const auto scoreCount = c.project.patterns.front().notes.size();
    require(c.execute("channel.clone", clone).wasOk(), "Clone metadata-bearing sampler Channel");
    const auto &copy = c.project.channels[1];
    require(copy.id != kick && copy.colour == 0xffe04b39u && copy.icon == "drum" &&
                copy.source == c.project.channel(kick)->source &&
                copy.mixer == c.project.channel(kick)->mixer &&
                c.project.patterns.front().notes.size() == scoreCount,
            "Independent adjacent clone inherits appearance/source/routing without duplicating notes");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(appearance), "Clone Undo preserves metadata source exactly");

    const auto file = folder.getChildFile("channel-appearance.vibra");
    require(saveProject(c.project, file).wasOk(), "Current-schema appearance project save");
    Project loaded;
    require(loadProject(file, loaded).wasOk() && loaded.state().isEquivalentTo(appearance) &&
                int(loaded.state()["version"]) == Project::currentVersion,
            "Current-schema disk round trip preserves appearance, score, routing and sample data");
    const auto loadedBefore = loaded.state();
    for (const auto &value : {juce::String("broken"), juce::String("112233"), juce::String("2148606515"),
                              juce::String("4294967296")})
    {
        auto malformed = appearance.createCopy();
        malformed.getChildWithName("CHANNELS").getChild(0).setProperty("colour", value, nullptr);
        require(Project::parse(malformed, loaded).failed() && loaded.state().isEquivalentTo(loadedBefore),
                "Malformed/nonopaque/out-of-range persisted Channel colour preserves destination");
    }
    {
        auto malformed = appearance.createCopy();
        malformed.getChildWithName("CHANNELS").getChild(0).setProperty("icon", "invented-icon", nullptr);
        require(Project::parse(malformed, loaded).failed() && loaded.state().isEquivalentTo(loadedBefore),
                "Unknown persisted icon is rejected without partial load");
    }
    auto legacy = appearance.createCopy();
    legacy.setProperty("version", 26, nullptr);
    stripPatternExtentForLegacy(legacy);
    for (const auto &row : legacy.getChildWithName("CHANNELS"))
    {
        auto mutableRow = row;
        mutableRow.removeProperty("colour", nullptr);
        mutableRow.removeProperty("icon", nullptr);
    }
    auto expectedLegacy = c.project;
    for (auto &pattern : expectedLegacy.patterns)
        pattern.autoLength = false; // Genuine pre30 source windows migrate as Manual.
    for (auto &row : expectedLegacy.channels)
    {
        row.colour = 0;
        row.icon = "none";
    }
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(expectedLegacy.state()),
            "Genuine schema26 shape without Channel appearance migrates to theme colour/no icon only");
    const auto legacyLoaded = loaded.state();
    for (bool icon : {false, true})
    {
        auto masquerading = legacy.createCopy();
        masquerading.getChildWithName("CHANNELS")
            .getChild(0)
            .setProperty(icon ? "icon" : "colour",
                         icon ? juce::var("drum") : juce::var(juce::int64(0xffe04b39u)), nullptr);
        require(Project::parse(masquerading, loaded).failed() && loaded.state().isEquivalentTo(legacyLoaded),
                "Nondefault new appearance cannot masquerade as an older schema and be silently lost");
    }

    // Literal selected-slot orders: anchors1/4 never move, and equal keys retain
    // ORIGINAL project order even when caller IDs are supplied backwards.
    Controller sorting;
    while (sorting.project.channels.size() < 7)
        sorting.project.channels.push_back(
            {sorting.project.allocate(), "Sort", "builtin:hat", 0, .7f, 0, false});
    std::vector<Id> ids;
    for (const auto &row : sorting.project.channels)
        ids.push_back(row.id);
    const std::array<const char *, 7> names{"Zulu",       "ANCHOR one", "alpha", "alpha",
                                            "ANCHOR two", "Beta",       "ALPHA"};
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        auto *row = sorting.project.channel(ids[i]);
        row->name = names[i];
        row->mixer = i == 0 ? 3 : i == 5 ? 2 : 1;
        row->colour = i == 0 ? 0xff0000ffu : i == 3 ? 0xff00ff00u : i == 5 ? 0xff808080u : 0xffff0000u;
        sorting.project.patterns.front().notes.push_back(
            {sorting.project.allocate(), row->id, double(i) / 4, .25, 60, .5f, false});
    }
    const auto ordered = sorting.project;
    const auto order = [&]
    {
        std::vector<Id> result;
        for (const auto &row : sorting.project.channels)
            result.push_back(row.id);
        return result;
    };
    const auto sort = [&](ChannelSortRequest::By by, bool descending, const std::vector<Id> &expected)
    {
        const auto source = sorting.project;
        CommandArgs request;
        request.ids = {ids[6], ids[5], ids[3], ids[2], ids[0]};
        request.channelSort = ChannelSortRequest{by, descending};
        require(sorting.execute("rack.sort", request).wasOk() && order() == expected,
                "Stable selected-slot sort has exact independent literal order and untouched gaps");
        auto normalized = sorting.project;
        normalized.channels.clear();
        for (const auto &row : source.channels)
            normalized.channels.push_back(*sorting.project.channel(row.id));
        require(normalized.state().isEquivalentTo(source.state()),
                "Sort changes only display order, preserving exact per-ID metadata/notes/routes/source");
        const auto sorted = sorting.project.state();
        sorting.undoEdit();
        require(sorting.project.state().isEquivalentTo(source.state()), "Sort is one exact order Undo");
        sorting.redoEdit();
        require(sorting.project.state().isEquivalentTo(sorted), "Sort exact Redo");
        sorting.undoEdit();
    };
    sort(ChannelSortRequest::By::name, false, {ids[2], ids[1], ids[3], ids[6], ids[4], ids[5], ids[0]});
    sort(ChannelSortRequest::By::name, true, {ids[0], ids[1], ids[5], ids[2], ids[4], ids[3], ids[6]});
    sort(ChannelSortRequest::By::mixer, false, {ids[2], ids[1], ids[3], ids[6], ids[4], ids[5], ids[0]});
    sort(ChannelSortRequest::By::colour, false, {ids[2], ids[1], ids[6], ids[3], ids[4], ids[0], ids[5]});
    require(sorting.project.state().isEquivalentTo(ordered.state()),
            "Repeated sort/history never changes source project");
    juce::PluginDescription description;
    description.name = "Original missing sort instrument";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = folder.getChildFile("missing-sort-instrument.vst3").getFullPathName();
    description.isInstrument = true;
    for (const auto index : {2, 6})
        sorting.project.channel(ids[std::size_t(index)])->instrument =
            PluginSpec{sorting.project.allocate(),
                       description.name,
                       description.createXml()->toString(),
                       {},
                       true,
                       false};
    auto *audio = sorting.project.channel(ids[5]);
    audio->audioClip = true;
    audio->audioDuration = 1;
    audio->source = folder.getChildFile("missing-sort-audio.wav").getFullPathName();
    // Audio Clip Channels are arrangement sources, never Piano Roll recipients.
    // Convert this row's fixture data rather than asking validation to accept
    // an impossible hybrid; retain the other six Channels' literal score.
    for (auto &pattern : sorting.project.patterns)
        std::erase_if(pattern.notes, [&](const Note &note) { return note.channel == audio->id; });
    sorting.project.audioPlacements.push_back({sorting.project.allocate(), audio->id, 0, 0, 0, 1});
    require(sorting.project.validate().wasOk(),
            "Model-only mixed instrument/sampler/Audio Clip sort fixture validates");
    sort(ChannelSortRequest::By::type, false, {ids[2], ids[1], ids[6], ids[0], ids[4], ids[3], ids[5]});
    CommandArgs sortedNoop;
    sortedNoop.ids = {ids[0]};
    sortedNoop.channelSort = ChannelSortRequest{};
    const auto sortBaseline = sorting.project.state();
    const auto sortRevision = sorting.revision;
    const auto sortUndo = sorting.undo.getUndoDescriptions(), sortRedo = sorting.undo.getRedoDescriptions();
    require(sorting.execute("rack.sort", sortedNoop).wasOk() && sorting.revision == sortRevision &&
                sorting.undo.getUndoDescriptions() == sortUndo &&
                sorting.undo.getRedoDescriptions() == sortRedo,
            "Single selected slot sort is a true no-op preserving Redo");
    sortedNoop.ids = {ids[6], ids[3], ids[2]};
    require(sorting.execute("rack.sort", sortedNoop).wasOk() &&
                sorting.project.state().isEquivalentTo(sortBaseline) && sorting.revision == sortRevision &&
                sorting.undo.getUndoDescriptions() == sortUndo &&
                sorting.undo.getRedoDescriptions() == sortRedo,
            "All-equal case-insensitive keys retain original selected-slot order and no history despite "
            "reversed request IDs");
    for (const auto &targets :
         {std::vector<Id>{}, std::vector<Id>{ids[0], ids[0]}, std::vector<Id>{ids[0], 999999}})
    {
        sortedNoop.ids = targets;
        const auto state = sorting.project.state();
        require(sorting.execute("rack.sort", sortedNoop).failed() &&
                    sorting.project.state().isEquivalentTo(state) && sorting.revision == sortRevision &&
                    sorting.undo.getUndoDescriptions() == sortUndo &&
                    sorting.undo.getRedoDescriptions() == sortRedo,
                "Sort requires explicit distinct existing targets and cannot use implicit selection");
    }
    sortedNoop.ids = {ids[0], ids[2]};
    sortedNoop.channelSort.reset();
    require(
        sorting.execute("rack.sort", sortedNoop).failed() &&
            sorting.project.state().isEquivalentTo(sortBaseline) && sorting.revision == sortRevision &&
            sorting.undo.getUndoDescriptions() == sortUndo && sorting.undo.getRedoDescriptions() == sortRedo,
        "Sort requires an explicit typed key/direction request without changing either history direction");
}
