// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepRepeat.h"
#include "NoteTiming.h"
#include "Customization.h"
#include "PatternChannelLoops.h"
#include <limits>
#include <stdexcept>
#include <tuple>

// Original model/prepared-score/history/profile checks, no provider/DSP qualification.
static void stepRepeatTests(const juce::File &folder)
{
    using namespace vibera;
    const auto close = [](double a, double b) { return std::abs(a - b) < 1.e-10; };
    const double intervals[] = {0,     1,      .75,  2. / 3, .5,      .375,  1. / 3, .25,
                                .1875, 1. / 6, .125, .09375, 1. / 12, .0625, 1. / 24};
    const int counts[] = {1, 1, 2, 2, 2, 3, 3, 4, 6, 6, 8, 11, 12, 16, 24};
    for (int mode = 0; mode <= 14; ++mode)
    {
        const auto *rate = noteRepeatRate(mode);
        NoteRepeatWindow window;
        require(rate && rate->mode == mode && close(rate->intervalBeats, intervals[mode]) &&
                    close(noteRepeatInterval(mode), intervals[mode]) &&
                    noteRepeatVisibleWindow(mode, 0, 1, 0, 1, window) && window.first == 0 &&
                    window.count == unsigned(counts[mode]),
                "All published Repeat indices have literal rates/counts");
    }
    require(!noteRepeatRate(-1) && !noteRepeatRate(15) && juce::String(noteRepeatRate(10)->label) == "1/32" &&
                juce::String(noteRepeatRate(13)->label) == "1/64" &&
                juce::String(noteRepeatRate(14)->label) == "1/64 triplet" &&
                juce::String(stepRepeatInfo()->field) == "repeatMode" && stepRepeatInfo()->minimum == 0 &&
                stepRepeatInfo()->maximum == 14 && stepRepeatInfo()->defaultValue == 0,
            "Exact familiar labels and shared Repeat graph metadata");
    NoteRepeatWindow window{123, 456};
    require(noteRepeatVisibleWindow(10, 0, 1, .375, .875, window) && window.first == 3 && window.count == 4,
            "Half-open visible window retains original repeat ordinals, excludes endpoint onset");
    require(noteRepeatVisibleWindow(10, 0, 1, .4, .9, window) && window.first == 4 && window.count == 4,
            "Nonaligned crop skips hidden triggers without phase compression");
    require(noteRepeatVisibleWindow(14, 0, 1000000, 999999.875, 1000000, window) &&
                window.first == 23999997 && window.count == 3,
            "Late window jumps directly over almost24million hidden triggers");
    require(noteRepeatVisibleWindow(13, 0, .1, 0, 1, window) && window.count == 2 &&
                close(noteRepeatGateDuration(13, .1, 0), .0625) &&
                close(noteRepeatGateDuration(13, .1, 1), .0375),
            "Last Repeat gate ends at the literal source end");
    const auto unchangedWindow = window;
    for (int invalid : {-1, 15})
        require(!noteRepeatVisibleWindow(invalid, 0, 1, 0, 1, window) &&
                    window.first == unchangedWindow.first && window.count == unchangedWindow.count,
                "Invalid rate refuses without partially replacing prepared window");
    require(!noteRepeatVisibleWindow(14, 0, 1.e30, 1.e29, 1.e29 + 1.e20, window) &&
                !noteRepeatVisibleWindow(1, 0, std::numeric_limits<double>::infinity(), 0, 1, window),
            "Unrepresentable ordinal and nonfinite source preparation refuse");

    auto source = Project::empty();
    auto &part = source.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto pattern = part.id, channel = source.channels.front().id, other = source.channels[1].id;
    const int modes[] = {2, 7, 14, 0, 11};
    const double beats[] = {0, .25, .25, .5, 3.75};
    for (size_t i = 0; i < 5; ++i)
        part.notes.push_back({source.allocate(), channel, beats[i], .5, 60 + int(i), i == 2 ? 0.f : .7f, true,
                              i == 1, -.25, 25, 50, modes[i]});
    part.notes.push_back({source.allocate(), channel, .25, 1, 70, .4f, false, false, .5, -25, 100, 4});
    part.notes.push_back({source.allocate(), channel, 4, .5, 71, .5f, true, true, .2, 10, 0, 7});
    part.notes.push_back({source.allocate(), other, 0, .5, 72, .6f, true, false, -.2, -10, 0, 10});
    require(source.validate().wasOk(), "Original repeat chord/muted/zero/dormant fixture validates");
    const Note legacyAggregate{1, channel, 0, .25, 60, .8f, true, false, 0, 0, 0};
    const ScheduledNote legacyScheduled{channel, 0, .25, 60, .8f};
    require(legacyAggregate.repeatMode == 0 && legacyScheduled.repeatOrdinal == 0,
            "Existing source/scheduled aggregate initialization stays neutral");
    const auto before = source.state();
    const auto literal = [&](const Project &project, std::initializer_list<int> values)
    {
        size_t index = 0;
        for (auto value : values)
            require(project.pattern(pattern)->notes[index++].repeatMode == value,
                    "Repeat source indices match independent literals including inactive chord members");
        auto normalized = project;
        for (size_t i = 0; i < 5; ++i)
            normalized.pattern(pattern)->notes[i].repeatMode = modes[i];
        require(normalized.state().isEquivalentTo(before),
                "Repeat graph preserves IDs/raw beat/length/pitch/expression/mute/other Channels/allocator");
    };
    Controller c;
    c.replace(source);
    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.value = 10;
    require(c.execute("steps.repeat", args).wasOk(), "Real canonical Repeat set");
    literal(c.project, {10, 10, 10, 10, 10});
    const auto set = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) && !c.undo.canUndo() &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(set),
            "Repeat set one exact Undo/Redo");
    c.replace(source);
    const juce::String owner("original-repeat-graph");
    require(c.beginGesture(owner), "Own Repeat baseline gesture");
    args.text = "scale";
    args.target = part.notes[0].id;
    args.value = 7;
    require(c.execute("steps.repeat", args, owner).wasOk(),
            "Repeat scale translates rate indices additively");
    literal(c.project, {7, 12, 14, 5, 14});
    require(c.execute("steps.repeat", args, owner).wasOk(), "Repeated Repeat preview does not compound");
    literal(c.project, {7, 12, 14, 5, 14});
    args.target = part.notes[3].id;
    args.value = 2;
    require(c.execute("steps.repeat", args, owner).wasOk(), "Zero index anchor uses same index translation");
    literal(c.project, {4, 9, 14, 2, 13});
    args.text = "ramp";
    args.beat = .5;
    args.length = -.5;
    args.value = 14;
    args.offset = 0;
    require(c.execute("steps.repeat", args, owner).wasOk(), "Reverse Repeat ramp");
    literal(c.project, {0, 7, 7, 14, 11});
    args.beat = 0;
    args.length = .5;
    args.value = 0;
    args.offset = 1;
    require(c.execute("steps.repeat", args, owner).wasOk(),
            "Repeat ramp rounds half indices to nearest integer");
    literal(c.project, {0, 1, 1, 1, 11});
    args.beat = .25;
    args.length = 0;
    args.offset = 9;
    require(c.execute("steps.repeat", args, owner).wasOk(), "Zero Repeat ramp selects endpoint");
    literal(c.project, {2, 9, 9, 0, 11});
    require(c.cancelGestureChecked(owner).wasOk() && c.project.state().isEquivalentTo(before) &&
                !c.undo.canUndo(),
            "Cancel Repeat previews restores actual baseline/history");
    args.text = "reset";
    require(c.beginGesture(owner) && c.execute("steps.repeat", args, owner).wasOk() &&
                c.endGestureChecked("Repeat", owner).wasOk(),
            "Accept Repeat reset gesture");
    literal(c.project, {0, 0, 0, 0, 0});
    const auto reset = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) && !c.undo.canUndo() &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(reset),
            "One exact accepted Repeat gesture");
    const auto refuse = [&](CommandArgs invalid, const Project &baseline, Project candidate)
    {
        const auto state = candidate.state();
        require(applyStepRepeat(candidate, baseline, invalid).failed() &&
                    candidate.state().isEquivalentTo(state),
                "Direct Repeat validation is atomic before any field writes");
    };
    args = {};
    args.pattern = pattern;
    args.channel = channel;
    for (double invalid :
         {-1., 14.1, 7.5, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        auto bad = args;
        bad.value = invalid;
        refuse(bad, source, source);
        bad.value = 0;
        bad.text = "ramp";
        bad.offset = invalid;
        refuse(bad, source, source);
    }
    for (auto ids : {std::vector<Id>{part.notes[0].id, part.notes[0].id},
                     std::vector<Id>{part.notes[0].id, part.notes[5].id},
                     std::vector<Id>{part.notes[0].id, part.notes[6].id},
                     std::vector<Id>{part.notes[0].id, part.notes[7].id}})
    {
        auto bad = args;
        bad.ids = ids;
        refuse(bad, source, source);
    }
    auto bad = args;
    bad.text = "scale";
    bad.target = part.notes[5].id;
    refuse(bad, source, source);
    auto stale = source;
    stale.pattern(pattern)->notes[0].beat = .5;
    bad.target = part.notes[0].id;
    refuse(bad, stale, source);
    bad = args;
    bad.text = "ramp";
    bad.beat = .1;
    refuse(bad, source, source);
    bad.beat = 0;
    bad.length = 4;
    refuse(bad, source, source);
    auto clipSource = source;
    clipSource.channel(channel)->audioClip = true;
    refuse(args, clipSource, clipSource);
    auto alias = source;
    args.text = "scale";
    args.target = part.notes[0].id;
    args.value = 7;
    require(applyStepRepeat(alias, alias, args).wasOk(), "Aliased baseline stores original Repeat anchors");
    literal(alias, {7, 12, 14, 5, 14});

    auto musical = Project::empty();
    auto &score = musical.patterns.front();
    score.autoLength = false;
    score.length = 2;
    const auto scorePattern = score.id, scoreChannel = musical.channels.front().id;
    score.notes = {{musical.allocate(), scoreChannel, 0, .5, 60, .8f, true, false, 0, 0, 0, 10}};
    const auto rawState = musical.state();
    auto scheduled = sequence(musical, true, scorePattern);
    require(scheduled.size() == 4, "Literal half-beat1/32 rate yields four triggers");
    for (size_t i = 0; i < scheduled.size(); ++i)
        require(close(scheduled[i].beat, double(i) * .125) && close(scheduled[i].length, .125) &&
                    scheduled[i].note == score.notes.front().id && scheduled[i].placement == 0 &&
                    scheduled[i].repeatOrdinal == i,
                "Repeat keeps source IDs and distinct stable ordinals");
    musical.placements = {{musical.allocate(), scorePattern, 0, 8, .25, .125}};
    scheduled = sequence(musical, false, scorePattern);
    require(scheduled.size() == 2 && close(scheduled[0].beat, 8) && close(scheduled[1].beat, 8.125) &&
                scheduled[0].repeatOrdinal == 1 && scheduled[1].repeatOrdinal == 2,
            "Song crop preserves source phase and ordinal instead of compressing/rebasing");
    musical.placements.front().offset = .25;
    scheduled = sequence(musical, false, scorePattern);
    require(scheduled.size() == 2 && scheduled[0].repeatOrdinal == 2 && scheduled[1].repeatOrdinal == 3,
            "Changing Clip source window preserves identity of surviving triggers");
    musical.swing = .5;
    musical.channel(scoreChannel)->truncateSwing = true;
    score.notes.front().beat = .25;
    score.notes.front().length = .5;
    score.notes.front().shiftPercent = 50;
    scheduled = sequence(musical, true, scorePattern);
    const double swung[] = {.43625, .56125, .68625, .81125};
    require(scheduled.size() == 4, "Repeat duration uses realized source Swing truncation");
    for (size_t i = 0; i < 4; ++i)
        require(close(scheduled[i].beat, swung[i]) && close(scheduled[i].length, i == 3 ? .0625 : .125),
                "Raw Swing and Shift establish one origin; last gate ends at truncated source duration");
    musical.swing = 0;
    musical.channel(scoreChannel)->truncateSwing = false;
    score.notes.front().beat = 0;
    score.notes.front().length = .375;
    score.notes.front().shiftPercent = 0;
    score.length = 1.25;
    score.channelLoops = {{scoreChannel, ChannelLoopMode::fixed, .5}};
    musical.placements.clear();
    scheduled = sequence(musical, true, scorePattern);
    const double loopBeats[] = {0, .125, .25, .5, .625, .75, 1, 1.125};
    const unsigned ordinals[] = {0, 1, 2, 0, 1, 2, 0, 1};
    const Id loopTags[] = {0, 0, 0, -1, -1, -1, -2, -2};
    require(scheduled.size() == 8,
            "Channel loops and Repeat combine without cycle crop or extra source notes");
    std::set<std::tuple<Id, Id, unsigned>> identities;
    for (size_t i = 0; i < scheduled.size(); ++i)
    {
        require(close(scheduled[i].beat, loopBeats[i]) && scheduled[i].repeatOrdinal == ordinals[i] &&
                    scheduled[i].placement == loopTags[i] && scheduled[i].note == score.notes.front().id,
                "Independent loop/Repeat identities occupy separate dimensions");
        identities.emplace(scheduled[i].note, scheduled[i].placement, scheduled[i].repeatOrdinal);
    }
    require(identities.size() == 8,
            "No duplicate identity across repeated source and Channel-loop occurrences");
    auto legacyClip = musical;
    const Id largeClip = (std::numeric_limits<Id>::max() - 4000000) / 4000001;
    legacyClip.placements = {{largeClip, scorePattern, 0, 0, 1.25, 0}};
    legacyClip.nextId = largeClip + 1;
    require(legacyClip.validate().wasOk(),
            "Existing admitted legacy Clip identity remains valid with Repeat");
    const auto legacyScore = sequence(legacyClip, false, scorePattern);
    require(legacyScore.size() == 8 && legacyScore[0].placement == largeClip &&
                legacyScore[3].placement == -(largeClip * 4000001 + 1) &&
                legacyScore[6].placement == -(largeClip * 4000001 + 2) && legacyScore[4].repeatOrdinal == 1,
            "Repeat ordinal never repacks/collides with existing positive or negative legacy loop tags");
    const auto looped = musical.state();
    c.replace(musical);
    CommandArgs burn;
    burn.pattern = scorePattern;
    burn.channel = scoreChannel;
    require(c.execute("pattern.burnChannelLoop", burn).wasOk(), "Burn actual Channel-loop source");
    const auto burned = sequence(c.project, true, scorePattern);
    require(c.project.pattern(scorePattern)->notes.size() == 3 && burned.size() == 8,
            "Burn materializes raw loop origins retaining Repeat, never double-bakes triggers");
    for (size_t i = 0; i < burned.size(); ++i)
        require(close(burned[i].beat, loopBeats[i]) && close(burned[i].length, scheduled[i].length),
                "Burn preserves independently literal Repeat timing");
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(looped) &&
                c.redoEditChecked().wasOk(),
            "Burn exact Repeat/source/allocator UndoRedo");
    for (auto mode : {ChannelLoopMode::step, ChannelLoopMode::beat, ChannelLoopMode::bar})
    {
        score.channelLoops.front() = {scoreChannel, mode, 1};
        const auto period = channelLoopLength(musical, score, scoreChannel);
        auto neutral = musical;
        neutral.patterns.front().notes.front().repeatMode = 0;
        require(period == channelLoopLength(neutral, neutral.patterns.front(), scoreChannel),
                "Auto loop period stays based on raw source end, never Repeat triggers");
    }
    auto glue = Project::empty();
    auto &glueScore = glue.patterns.front();
    glueScore.autoLength = false;
    glueScore.length = 2;
    const auto gluePattern = glueScore.id, glueChannel = glue.channels.front().id;
    glueScore.notes = {{glue.allocate(), glueChannel, 0, .75, 60, .8f, false, false, 0, 0, 0, 4},
                       {glue.allocate(), glueChannel, .75, .75, 60, .8f, false, false, 0, 0, 0, 7}};
    CommandArgs join;
    join.pattern = gluePattern;
    join.channel = glueChannel;
    join.ids = {glueScore.notes[0].id, glueScore.notes[1].id};
    c.replace(glue);
    const auto unequal = c.project.state();
    require(c.execute("notes.glue", join).wasOk() && c.project.state().isEquivalentTo(unequal) &&
                !c.undo.canUndo(),
            "Unequal Repeat modes cannot silently merge");
    glueScore.notes[1].repeatMode = 4;
    c.replace(glue);
    const auto equal = c.project.state();
    const auto separate = sequence(glue, true, gluePattern);
    require(separate.size() == 4 && close(separate[2].beat, .75) && close(separate[3].beat, 1.25),
            "Separate equal-rate source notes have independently phased repetitions");
    require(c.execute("notes.glue", join).wasOk() && c.project.pattern(gluePattern)->notes.size() == 1,
            "Equal-rate Glue explicitly creates one continuous first-note phase");
    const auto joined = sequence(c.project, true, gluePattern);
    require(joined.size() == 3 && close(joined[0].beat, 0) && close(joined[1].beat, .5) &&
                close(joined[2].beat, 1) && c.undoEditChecked().wasOk() &&
                c.project.state().isEquivalentTo(equal),
            "Glue deliberate phase transform is literal and exactly undoable, not claimed PCM equivalence");

    auto budget = Project::empty();
    auto &longPart = budget.patterns.front();
    longPart.autoLength = false;
    longPart.length = 6250;
    const auto longChannel = budget.channels.front().id, longPattern = longPart.id;
    longPart.notes = {{budget.allocate(), longChannel, 0, 6250, 60, .8f, true, false, 0, 0, 0, 13}};
    require(budget.validate().wasOk(),
            "Exactly100000 expanded Pattern events admit without constructing them");
    longPart.length = 6250.0625;
    longPart.notes.front().length = 6250.0625;
    require(budget.validate().failed(), "100001 expanded Pattern events refuse before allocation");
    longPart.length = 6250;
    longPart.notes.front().length = 6250;
    for (int i = 0; i < 2; ++i)
        budget.placements.push_back({budget.allocate(), longPattern, i, double(i) * 2500, 2500, 0});
    require(budget.validate().wasOk(), "Two40000-event windows admit independent Song budget");
    budget.placements.push_back({budget.allocate(), longPattern, 2, 5000, 2500, 0});
    require(budget.validate().failed(), "Three40000-event Song windows refuse global100000 bound");
    longPart.notes.front().repeatMode = 0;
    require(budget.validate().wasOk(), "All-neutral legacy source keeps original project limits");
    c.replace(budget);
    const auto beforeBudget = c.project.state();
    CommandArgs dense;
    dense.pattern = longPattern;
    dense.channel = longChannel;
    dense.value = 13;
    require(c.execute("steps.repeat", dense).failed() && c.project.state().isEquivalentTo(beforeBudget) &&
                !c.undo.canUndo(),
            "Canonical over-budget Repeat change refuses atomically with exact source/history");
    auto huge = budget;
    huge.placements.clear();
    huge.patterns.front().length = 1000000;
    huge.patterns.front().notes.front().length = 1000000;
    huge.patterns.front().notes.front().repeatMode = 14;
    bool threw = false;
    try
    {
        (void)sequence(huge, true, longPattern);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    require(
        threw,
        "Invalid direct repeated score refuses before24million-event allocation, never silent truncation");

    Project loaded;
    const auto projectFile = folder.getChildFile("step-repeat.vibra");
    require(saveProject(source, projectFile).wasOk() && loadProject(projectFile, loaded).wasOk() &&
                loaded.state().isEquivalentTo(before),
            "Sparse schema33 exact project save/load");
    const auto firstNote = [](juce::ValueTree tree)
    {
        for (auto row : tree.getChildWithName("PATTERNS").getChild(0))
            if (row.hasType("NOTE"))
                return row;
        require(false, "Find actual source NOTE");
        return juce::ValueTree{};
    };
    auto legacy = before.createCopy();
    legacy.setProperty("version", 32, nullptr);
    stripSamplerFilterForLegacy(legacy);
    for (auto pat : legacy.getChildWithName("PATTERNS"))
        for (auto note : pat)
            if (note.hasType("NOTE"))
                note.removeProperty("repeatMode", nullptr);
    auto neutral = source;
    for (auto &note : neutral.patterns.front().notes)
        note.repeatMode = 0;
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(neutral.state()),
            "Genuine schema32 absent Repeat defaults0 without unrelated state changes");
    const auto loadedBefore = loaded.state();
    auto smuggle = legacy.createCopy();
    firstNote(smuggle).setProperty("repeatMode", 0, nullptr);
    require(Project::parse(smuggle, loaded).failed() && loaded.state().isEquivalentTo(loadedBefore),
            "Old schema refuses even neutral explicit Repeat");
    for (const auto invalid : {juce::var(true), juce::var("bad"), juce::var(-1), juce::var(15),
                               juce::var(1.5), juce::var(std::numeric_limits<double>::quiet_NaN())})
    {
        auto state = before.createCopy();
        firstNote(state).setProperty("repeatMode", invalid, nullptr);
        require(Project::parse(state, loaded).failed() && loaded.state().isEquivalentTo(loadedBefore),
                "Invalid Repeat parse is atomic");
    }
    for (auto pat : neutral.state().getChildWithName("PATTERNS"))
        for (auto note : pat)
            if (note.hasType("NOTE"))
                require(!note.hasProperty("repeatMode"), "Neutral Repeat emits no new schema attribute");

    juce::PluginDescription description;
    description.name = "Original absent Repeat score instrument";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "original-repeat-score.vst3";
    description.isInstrument = true;
    description.numOutputChannels = 2;
    auto instrument = source;
    instrument.channel(channel)->instrument =
        PluginSpec{instrument.allocate(), description.name, description.createXml()->toString(), {}, true};
    c.replace(instrument);
    CommandArgs nativeArgs;
    nativeArgs.pattern = pattern;
    nativeArgs.channel = channel;
    nativeArgs.value = 7;
    require(instrument.validate().wasOk() && c.execute("steps.repeat", nativeArgs).wasOk(),
            "Repeat is score scheduling on plugin instruments as well as sampler, not native expression "
            "dispatch");

    Customization settings = Customization::familiarDefaults();
    args = {};
    args.value = 10;
    args.ids = {999999999};
    Macro macro{"macro.original-repeat", "Repeat current Channel", {{"steps.repeat", args, true}}};
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    context.notes = {part.notes[5].id};
    require(settings.addMacro(macro).wasOk(), "Repeat numeric command uses existing macro protocol");
    c.replace(source);
    require(settings.runMacro(macro.id, c, context).wasOk(),
            "Macro resolves current Channel steps not Piano selection");
    literal(c.project, {10, 10, 10, 10, 10});
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before),
            "Repeat macro one exact Undo");
    CommandArgs imported;
    imported.pattern = pattern;
    imported.channel = channel;
    imported.recordedNotes = {part.notes.front(), part.notes.front()};
    imported.recordedNotes[0].repeatMode = 10;
    imported.recordedNotes[1].repeatMode = 7;
    macro = {"macro.original-repeat-import", "Import repeated source", {{"notes.import", imported, false}}};
    settings = Customization::familiarDefaults();
    require(settings.addMacro(macro).wasOk(), "Repeated typed import macro admitted");
    const auto encoded = settings.state();
    const auto same = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    const auto clone = [](const juce::var &v) { return juce::JSON::parse(juce::JSON::toString(v)); };
    const auto rows = [&](juce::var &profile) -> juce::Array<juce::var> *
    {
        for (auto &row : *profile["macros"].getArray())
            if (row["id"].toString() == macro.id)
                return row["steps"].getArray()->getReference(0)["parameters"]["recordedNotes"].getArray();
        require(false, "Locate actual Repeat import macro");
        return nullptr;
    };
    auto inspected = clone(encoded);
    require(int(encoded["version"]) == 14 &&
                rows(inspected)->getReference(0).getDynamicObject()->getProperties().size() == 12,
            "Only nonneutral Repeat import requires profile14 with all12 source fields");
    Customization restored;
    const auto profileFile = folder.getChildFile("repeat-import-profile.json");
    require(settings.save(profileFile).wasOk() && restored.load(profileFile).wasOk() &&
                same(encoded, restored.state()),
            "Actual imported Repeat+Shift profile file round trip is exact");
    c.replace(source);
    const auto allocation = c.project.nextId;
    require(restored.runMacro(macro.id, c, context).wasOk() &&
                c.project.pattern(pattern)->notes[8].id == allocation &&
                c.project.pattern(pattern)->notes[9].id == allocation + 1 &&
                c.project.pattern(pattern)->notes[8].repeatMode == 10 &&
                c.project.pattern(pattern)->notes[9].repeatMode == 7 &&
                !c.project.pattern(pattern)->notes[8].step,
            "Import preserves Repeat through melodic conversion with distinct fresh runtime IDs");
    const auto importedState = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(importedState),
            "Imported Repeat one exact history action");
    for (int version = 1; version <= 13; ++version)
    {
        auto old = clone(encoded);
        old.getDynamicObject()->setProperty("version", version);
        const auto prior = restored.state();
        require(restored.importState(old).failed() && same(prior, restored.state()),
                "Old profile cannot smuggle Repeat");
    }
    for (const auto invalid : {juce::var(true), juce::var("7"), juce::var(-1), juce::var(15), juce::var(7.5)})
    {
        auto badProfile = clone(encoded);
        rows(badProfile)->getReference(0).getDynamicObject()->setProperty("repeatMode", invalid);
        const auto prior = restored.state();
        require(restored.importState(badProfile).failed() && same(prior, restored.state()),
                "Typed Repeat macro parse is atomic");
    }
    auto neutralProfile = clone(encoded);
    neutralProfile.getDynamicObject()->setProperty("version", 13);
    for (auto &row : *rows(neutralProfile))
        row.getDynamicObject()->removeProperty("repeatMode");
    require(restored.importState(neutralProfile).wasOk() && int(restored.state()["version"]) == 13,
            "Neutral Repeat with Shift remains genuine profile13");
    auto explicitZero = clone(neutralProfile);
    rows(explicitZero)->getReference(0).getDynamicObject()->setProperty("repeatMode", 0);
    const auto prior = restored.state();
    require(restored.importState(explicitZero).failed() && same(prior, restored.state()),
            "Even0 Repeat presence requires14");
    for (auto &row : *rows(neutralProfile))
        row.getDynamicObject()->removeProperty("shiftPercent");
    neutralProfile.getDynamicObject()->setProperty("version", 12);
    require(restored.importState(neutralProfile).wasOk() && int(restored.state()["version"]) == 12,
            "Full neutral ten-field source payload remains12");
    auto unsafe = macro;
    unsafe.steps.front().parameters.recordedNotes.front().repeatMode = 15;
    require(restored.addMacro(unsafe).failed(), "Malformed stored Repeat refuses macro admission");
    restored.macros = {unsafe};
    c.replace(source);
    juce::MemoryBlock saved;
    require(profileFile.loadFileAsData(saved) && restored.runMacro(unsafe.id, c, context).failed() &&
                c.project.state().isEquivalentTo(before) && !c.undo.canUndo() &&
                restored.save(profileFile).failed(),
            "Public malformed macro cannot run or overwrite actual profile");
    juce::MemoryBlock afterFile;
    require(profileFile.loadFileAsData(afterFile) && afterFile == saved,
            "Failed Repeat save preserves exact prior bytes");

    auto precise = Project::empty();
    auto &precisePart = precise.patterns.front();
    precisePart.autoLength = false;
    precisePart.length = 2000;
    precisePart.notes = {{precise.allocate(), precise.channels.front().id, 1000,
                          std::nextafter(1., std::numeric_limits<double>::infinity()), 60, .8f, true}};
    Controller preciseController;
    preciseController.replace(precise);
    const auto preciseBefore = preciseController.project.state();
    CommandArgs preciseArgs;
    preciseArgs.pattern = precisePart.id;
    preciseArgs.channel = precise.channels.front().id;
    preciseArgs.value = 14;
    const auto refusedPrecision = preciseController.execute("steps.repeat", preciseArgs);
    require(refusedPrecision.failed() &&
                refusedPrecision.getErrorMessage().contains("noteRepeat.precision") &&
                preciseController.project.state().isEquivalentTo(preciseBefore) &&
                !preciseController.undo.canUndo(),
            "Collapsed positive residual cannot publish an off-before-own-on gate or change history");
}
