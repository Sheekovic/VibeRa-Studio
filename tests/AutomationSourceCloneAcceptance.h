// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AutomationState.h"
#include "BuildAcceptanceOutput.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>

// Model/compiled-program evidence only: the declaration below does not start a native worker.
static void automationSourceCloneTests(const juce::File &folder)
{
    Controller c;
    juce::PluginDescription description;
    description.name = "Original clone binding model fixture";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "model-only-clone-fixture.vst3";
    PluginSpec plugin;
    plugin.id = c.project.allocate();
    plugin.name = description.name;
    plugin.descriptor = description.createXml()->toString();
    plugin.instrument = false;
    plugin.automationBindings = {{0, 1, 0, .3f, "Original parameter zero", "normalized"},
                                 {0xfedcba98u, 1, 7, .6f, "Untouched declaration", "normalized"}};
    c.project.mixer[1].effects.push_back(plugin);
    const AutomationKey gain{AutomationParameter::mixerGain, 1, 0};
    const AutomationKey pan{AutomationParameter::mixerPan, 2, 0};
    const AutomationKey native{AutomationParameter::pluginNormalized, plugin.id, 0};
    const AutomationKey unrelated{AutomationParameter::mixerGain, 3, 0};
    AutomationSource original;
    original.id = c.project.allocate();
    original.name = "Independent curve";
    original.colour = 0xff418ac3u;
    original.length = 4;
    original.points = {{c.project.allocate(), 0, .25f},
                       {c.project.allocate(), 1, .75f, AutomationShape::single, 0},
                       {c.project.allocate(), 2, .125f, AutomationShape::hold, -.25f},
                       {c.project.allocate(), 4, 1, AutomationShape::smooth, .5f}};
    original.links = {{gain, 1.8f, .2f}, {pan, -.75f, .5f}, {native, .9f, .1f}};
    c.project.automation.sources.push_back(original);
    c.project.automation.placements = {{c.project.allocate(), original.id, 2, 4, 0, 5, false},
                                       {c.project.allocate(), original.id, 8, 2, 1, 6, true}};
    c.project.automation.initialValues = {{gain, .6f}, {pan, -.2f}, {native, .3f}, {unrelated, 1.1f}};
    require(c.project.validate().wasOk(),
            "Clone fixture has valid mixed targets and declared native ID zero");
    const auto baseline = c.project.state();
    const auto allocatedBefore = c.project.nextId;
    const auto originalAutomation = automationState(c.project.automation);
    auto compile = [&](const Project &project, bool song)
    {
        AutomationProgram program;
        std::string error;
        require(AutomationProgram::compile(project.automation, song, program, error),
                "Clone fixture compiles actual AutomationProgram");
        return program;
    };
    const auto songBefore = compile(c.project, true);
    const auto patternBefore = compile(c.project, false);
    auto literal = [](const AutomationProgram &program, AutomationKey key, double beat, float expected)
    {
        const auto value = program.valueAt(key, beat);
        require(value.has_value() && std::abs(*value - expected) < 1.e-6f,
                "Literal mixed-target automation value, including inverted range and held boundary");
    };
    literal(songBefore, gain, 1, .6f);
    literal(songBefore, gain, 2, 1.4f);
    literal(songBefore, gain, 2.5, 1.f);
    literal(songBefore, pan, 2.5, -.125f);
    literal(songBefore, native, 2.5, .5f);
    literal(songBefore, gain, 3.5, .6f);
    literal(songBefore, gain, 4, 1.6f);
    auto sameProgram = [](const AutomationProgram &left, const AutomationProgram &right)
    {
        require(left.targetCount() == right.targetCount() && left.spanCount() == right.spanCount(),
                "Unplaced clone creates no additional automation lanes or spans");
        for (const auto &lane : left.compiledLanes())
        {
            const auto *other = right.find(lane.key);
            require(other && other->initial == lane.initial && other->spans.size() == lane.spans.size(),
                    "Unplaced clone preserves every lane key and initial value");
            for (std::size_t i = 0; i < lane.spans.size(); ++i)
            {
                const auto &a = lane.spans[i];
                const auto &b = other->spans[i];
                require(a.beat == b.beat && a.origin == b.origin && a.inverseLength == b.inverseLength &&
                            std::equal(std::begin(a.coefficients), std::end(a.coefficients),
                                       std::begin(b.coefficients)),
                        "Unplaced clone preserves exact compiled polynomial and clock boundaries");
            }
        }
    };
    auto clone = [&](const juce::String &policy)
    {
        CommandArgs args;
        args.target = original.id;
        args.text = policy;
        const auto revision = c.revision;
        require(c.execute("automation.source.clone", args).wasOk() && c.revision == revision + 1,
                "Canonical clone is one synchronous document edit");
        const auto copy = c.project.automation.sources.back();
        require(copy.id >= allocatedBefore && copy.id != original.id &&
                    copy.name == original.name + " - Clone" && copy.length == original.length &&
                    copy.colour == original.colour && copy.points.size() == original.points.size(),
                "Independent clone retains metadata and curve length");
        std::set<Id> ids{copy.id};
        for (std::size_t i = 0; i < copy.points.size(); ++i)
        {
            const auto &point = copy.points[i];
            const auto &old = original.points[i];
            require(point.id >= allocatedBefore && ids.insert(point.id).second &&
                        std::none_of(original.points.begin(), original.points.end(),
                                     [&](const auto &p) { return p.id == point.id; }) &&
                        point.beat == old.beat && point.value == old.value &&
                        point.incoming == old.incoming && point.tension == old.tension,
                    "Every cloned point has independent identity and exact curve data");
        }
        auto remainder = c.project;
        remainder.automation.sources.pop_back();
        remainder.nextId = allocatedBefore;
        require(baseline.isEquivalentTo(remainder.state()),
                "Clone changes no placements, initial values, bindings, source, routing or other model data");
        sameProgram(songBefore, compile(c.project, true));
        sameProgram(patternBefore, compile(c.project, false));
        return copy;
    };

    const auto cleared = clone({});
    require(cleared.links.empty(), "Omitted clone policy clears all destination links");
    const auto clearedState = c.project.state();
    require(c.undoEditChecked().wasOk() && baseline.isEquivalentTo(c.project.state()) && !c.undo.canUndo(),
            "One Undo restores exact pre-clone state with no earlier history");
    require(c.redoEditChecked().wasOk() && clearedState.isEquivalentTo(c.project.state()) &&
                !c.undo.canRedo(),
            "One Redo restores identical clone and point IDs");
    require(c.undoEditChecked().wasOk(), "Return to original for explicit clear policy");
    require(clone("clear").links.empty(), "Explicit clear matches default link policy");
    require(c.undoEditChecked().wasOk() && baseline.isEquivalentTo(c.project.state()),
            "Explicit clear is also one independent history action");
    const auto preserved = clone("preserve");
    require(preserved.links.size() == original.links.size(), "Preserve retains every linked destination");
    for (std::size_t i = 0; i < original.links.size(); ++i)
        require(preserved.links[i].key == original.links[i].key &&
                    preserved.links[i].minimum == original.links[i].minimum &&
                    preserved.links[i].maximum == original.links[i].maximum,
                "Preserve copies stable Mixer/native target and inverted range exactly");

    CommandArgs edit;
    edit.target = preserved.points[1].id;
    edit.beat = 1.25;
    edit.value = .4;
    const auto beforePointEdit = c.project.state();
    require(c.execute("automation.point.move", edit).wasOk(), "Edit clone point through canonical mutator");
    const auto *independent = c.project.automationSource(preserved.id);
    require(independent && independent->points[1].beat == 1.25 && independent->points[1].value == .4f,
            "Point edit resolves fresh clone point identity");
    auto unchangedOriginal = c.project.automation;
    unchangedOriginal.sources.pop_back();
    require(
        automationState(unchangedOriginal).isEquivalentTo(originalAutomation) &&
            c.project.plugin(plugin.id)->automationBindings == plugin.automationBindings,
        "Editing clone cannot change original source, initialized values, placements or native declarations");
    sameProgram(songBefore, compile(c.project, true));
    const auto edited = c.project.state();
    require(c.undoEditChecked().wasOk() && beforePointEdit.isEquivalentTo(c.project.state()) &&
                c.redoEditChecked().wasOk() && edited.isEquivalentTo(c.project.state()),
            "Clone point edit has exact independent Undo and Redo");
    juce::File roundtripOutput;
    const auto outputReady = test::prepareBuildAcceptanceAttempt(
        folder.getChildFile("automation-source-clone-roundtrip").getFullPathName(), roundtripOutput);
    require(outputReady.wasOk(), outputReady.getErrorMessage().toStdString());
    const auto file = roundtripOutput.getChildFile("automation-source-clones.vibra");
    Project reopened;
    require(!file.exists() && saveProject(c.project, file).wasOk() && loadProject(file, reopened).wasOk() &&
                reopened.state().isEquivalentTo(edited),
            "Save/reopen retains unplaced clone, fresh IDs, ranges, curve metadata and native bindings");

    auto refused = [&](Id target, const juce::String &policy, const char *code)
    {
        const auto before = c.project.state();
        const auto revision = c.revision;
        const auto dirty = c.dirty;
        const auto undo = c.undo.getUndoDescription(), redo = c.undo.getRedoDescription();
        const auto units = c.undo.getNumberOfUnitsTakenUpByStoredCommands();
        CommandArgs args;
        args.target = target;
        args.text = policy;
        const auto result = c.execute("automation.source.clone", args);
        require(
            result.failed() && result.getErrorMessage().contains(code) &&
                before.isEquivalentTo(c.project.state()) && c.revision == revision && c.dirty == dirty &&
                c.undo.getUndoDescription() == undo && c.undo.getRedoDescription() == redo &&
                c.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
            "Rejected clone leaves allocator, document, revision, dirty state and actual history unchanged");
    };
    refused(original.id, "keep", "automation.invalidLinkPolicy");
    refused(original.id, "Preserve", "automation.invalidLinkPolicy");
    refused(original.id, " preserve ", "automation.invalidLinkPolicy");
    refused(0, "clear", "automation.sourceMissing");
    refused(original.points.front().id, "preserve", "automation.sourceMissing");

    const auto beforeBatch = c.project.state();
    const auto batchUndo = c.undo.getUndoDescription(), batchRedo = c.undo.getRedoDescription();
    const auto batchDirty = c.dirty;
    const auto batchRevision = c.revision;
    require(c.beginGesture("source-clone-batch"), "Acquire actual clone batch document owner");
    CommandArgs batchClone;
    batchClone.target = original.id;
    batchClone.text = "preserve";
    require(c.execute("automation.source.clone", batchClone, "source-clone-batch").wasOk(),
            "Actual batch preview allocates an independent cloned source");
    batchClone.target = c.project.nextId;
    require(c.execute("automation.source.clone", batchClone, "source-clone-batch").failed() &&
                c.cancelGestureChecked("source-clone-batch").wasOk() &&
                beforeBatch.isEquivalentTo(c.project.state()) && c.dirty == batchDirty &&
                c.undo.getUndoDescription() == batchUndo && c.undo.getRedoDescription() == batchRedo &&
                c.revision > batchRevision && !c.hasGesture(),
            "Failed batch restores exact allocator/project/history/dirty, publishes rollback revision and "
            "releases owner");

    // Exercise the actual source capacity, rather than a deliberately invalid starting document.
    Controller capacity;
    for (int i = 0; i < 1024; ++i)
    {
        AutomationSource source;
        source.id = capacity.project.allocate();
        source.points = {{capacity.project.allocate(), 0, .25f},
                         {capacity.project.allocate(), source.length, .75f}};
        capacity.project.automation.sources.push_back(std::move(source));
    }
    require(capacity.project.validate().wasOk(), "Actual full source-capacity document remains valid");
    const auto capacityBefore = capacity.project.state();
    const auto capacityRevision = capacity.revision;
    const auto capacityDirty = capacity.dirty;
    CommandArgs overflow;
    overflow.target = capacity.project.automation.sources.front().id;
    const auto tooMany = capacity.execute("automation.source.clone", overflow);
    require(tooMany.failed() && tooMany.getErrorMessage().contains("prepared limits") &&
                capacityBefore.isEquivalentTo(capacity.project.state()) &&
                capacityRevision == capacity.revision && capacityDirty == capacity.dirty &&
                !capacity.undo.canUndo() && !capacity.undo.canRedo(),
            "Source capacity refusal leaves allocator, document, clean/dirty and history intact");

    CommandArgs remove;
    remove.target = preserved.id;
    require(c.execute("automation.source.delete", remove).wasOk(), "Delete independent clone source");
    require(automationState(c.project.automation).isEquivalentTo(originalAutomation),
            "Deleting clone retains original placements and every target initialization");
    const auto removed = c.project.state();
    refused(preserved.id, "preserve", "automation.sourceMissing");
    require(c.undoEditChecked().wasOk() && edited.isEquivalentTo(c.project.state()) &&
                c.redoEditChecked().wasOk() && removed.isEquivalentTo(c.project.state()) &&
                c.undoEditChecked().wasOk(),
            "Clone deletion restores exact source/points/links on Undo without stealing init ownership");
    remove.target = original.id;
    const auto beforeOriginalDelete = c.project.state();
    require(c.execute("automation.source.delete", remove).wasOk() &&
                c.project.automationSource(preserved.id) && c.project.automation.placements.empty(),
            "Deleting placed original retains independent unplaced linked clone");
    const auto remaining = automationState(c.project.automation);
    require(remaining.getChildWithName("INITIAL_VALUES")
                    .isEquivalentTo(originalAutomation.getChildWithName("INITIAL_VALUES")) &&
                c.project.plugin(plugin.id)->automationBindings == plugin.automationBindings,
            "Initial values and parameter bindings belong to targets rather than either cloned source");
    const auto fallback = compile(c.project, true);
    literal(fallback, gain, 2.5, .6f);
    literal(fallback, pan, 2.5, -.2f);
    literal(fallback, native, 2.5, .3f);
    refused(original.id, "clear", "automation.sourceMissing");
    require(c.undoEditChecked().wasOk() && beforeOriginalDelete.isEquivalentTo(c.project.state()),
            "Original deletion Undo restores exact placed musical curve and surviving clone");
}
