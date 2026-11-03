// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "../src/Commands.h"
#include "../src/AutomationState.h"
#include "../src/MixerInsertLifecycle.h"
#include "../src/MixerOrganisationReferences.h"
#include <limits>
#include <stdexcept>

// Original MODEL/codec/history proof only. No graph/provider/DSP/UI claim.
namespace vibera::acceptance::mixerSendModel
{
inline void check(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
inline Project seed()
{
    auto p = Project::empty();
    p.mixer[1].index = 500;
    p.mixer[2].index = 900;
    for (auto &channel : p.channels)
    {
        if (channel.mixer == 1)
            channel.mixer = 500;
        if (channel.mixer == 2)
            channel.mixer = 900;
    }
    p.mixer[1].routes = {{0, .25f}, {900, .75f}};
    p.mixer[3].routes = {{0, 1.f}, {900, .5f}};
    check(p.validate().wasOk(), "Sparse source/destination IDs and unequal sends validate");
    return p;
}
inline CommandArgs target(int from, int to)
{
    CommandArgs a;
    a.mixer = from;
    a.automationControl = MixerAutomationControl::send;
    a.automationSendTarget = to;
    return a;
}
__declspec(noinline) inline void commands()
{
    static_assert(
        unsigned(AutomationParameter::mixerGain) == 0 && unsigned(AutomationParameter::mixerPan) == 1 &&
        unsigned(AutomationParameter::channelGain) == 2 && unsigned(AutomationParameter::channelPan) == 3 &&
        unsigned(AutomationParameter::pluginNormalized) == 4 &&
        unsigned(AutomationParameter::mixerStereoSeparation) == 5 &&
        unsigned(AutomationParameter::mixerSend) == 6);
    auto c = std::make_unique<Controller>();
    c->replace(seed());
    const auto baseline = c->project.state();
    auto a = target(500, 900);
    a.length = 4;
    a.beat = 2;
    a.lane = 1;
    check(c->execute("automation.create", a).wasOk(), "Canonical create exact send source");
    const auto source = c->project.automation.sources.front().id;
    const AutomationKey edge{AutomationParameter::mixerSend, 500, 900};
    const auto &created = c->project.automation.sources.front();
    check(created.links.size() == 1 && created.links.front().key == edge &&
              created.links.front().minimum == 0 && created.links.front().maximum == 2 &&
              created.points.size() == 2 && created.points[0].value == .375f &&
              created.points[1].value == .375f && c->project.automation.initialValues.front().key == edge &&
              c->project.automation.initialValues.front().value == .75f &&
              c->undo.getUndoDescriptions().size() == 1,
          "Literal stable pair, normalized manual ordinate and one real Undo");
    const auto changed = c->project.state();
    check(c->undoEditChecked().wasOk() && c->project.state().isEquivalentTo(baseline) &&
              c->redoEditChecked().wasOk() && c->project.state().isEquivalentTo(changed),
          "Actual canonical Undo/Redo restores complete source/placement/initialization");
    MixerAutomationTarget resolved;
    check(
        resolveMixerAutomationTarget(c->project, 500, MixerAutomationControl::send, resolved, 900).wasOk() &&
            resolved.key == edge && resolved.manual == .75f && resolved.minimum == 0 &&
            resolved.maximum == 2 && resolved.units == "linearGain",
        "Shared target resolver uses route, not insert fader or ordinal");
    const auto state = mixerAutomationTargetState(resolved);
    check(state["kind"].toString() == "send" && int(state["sourceInsert"]) == 500 &&
              int(state["destinationInsert"]) == 900 && !bool(state["dspSettlingAcknowledged"]),
          "Typed pair readback is canonical, not DSP completion");
    a.target = source;
    const auto revision = c->revision;
    const auto history = c->undo.getUndoDescription();
    const auto refuse = [&](const juce::String &id, const CommandArgs &bad, const char *code)
    {
        const auto result = c->execute(id, bad);
        check(result.failed() && result.getErrorMessage().startsWith(code) && c->revision == revision &&
                  c->undo.getUndoDescription() == history && c->project.state().isEquivalentTo(changed),
              "Invalid send metadata refuses without model/history/revision changes");
    };
    auto bad = a;
    bad.automationSendTarget.reset();
    refuse("automation.link", bad, "automation.invalidArguments:");
    for (int to : {-1, 500, 777})
    {
        bad = a;
        bad.automationSendTarget = to;
        refuse("automation.link", bad, "automation.invalidSendTarget:");
    }
    bad = a;
    bad.mixer = 0;
    refuse("automation.link", bad, "automation.invalidSendTarget:");
    bad = a;
    bad.automationSendTarget = 4;
    refuse("automation.link", bad, "automation.missingRoute:"); // Existing ID, absent edge.
    bad = a;
    bad.automationControl = MixerAutomationControl::gain;
    refuse("automation.link", bad, "automation.invalidArguments:");
    bad = a;
    bad.automationControl.reset();
    refuse("project.tempo", bad, "automation.invalidArguments:");
    a.value = 1.25;
    check(c->execute("automation.init", a).wasOk() &&
              c->project.automation.initialValues.front().value == 1.25f && c->undoEditChecked().wasOk() &&
              c->project.state().isEquivalentTo(changed),
          "Explicit initialization uses send units and one real Undo");
    a.beat = 2;
    a.length = .5;
    check(c->execute("automation.range", a).wasOk() &&
              c->project.automation.sources.front().links.front().minimum == 2 &&
              c->project.automation.sources.front().links.front().maximum == .5f &&
              c->undoEditChecked().wasOk() && c->project.state().isEquivalentTo(changed),
          "Committed inverted send range is one exact Undo");
    check(c->beginGesture("original-send"), "Own real source edit gesture");
    a.beat = 2;
    a.length = .5;
    check(c->execute("automation.range", a, "original-send").wasOk() &&
              c->cancelGestureChecked("original-send").wasOk() && c->project.state().isEquivalentTo(changed),
          "Inverted range preview cancels full source state without new history");
    check(c->undo.getUndoDescription() == history, "Cancelled gesture retains real history");
    a = target(500, 0);
    a.length = 4;
    check(c->execute("automation.create", a).wasOk() &&
              c->project.automation.sources.back().links.front().key ==
                  AutomationKey{AutomationParameter::mixerSend, 500, 0} &&
              c->project.automation.sources.back().points[0].value == .125f,
          "Master destination0 is a distinct actual send, not Mixer gain");
    auto &points = c->project.automation.sources.front().points;
    points.front().value = 0;
    points.back().value = 1; // Independent literal curve.
    AutomationProgram song, pattern;
    std::string error;
    check(AutomationProgram::compile(c->project.automation, true, song, error,
                                     c->project.automationDefaults()) &&
              song.valueAt(edge, 1) == .75f && song.valueAt(edge, 2) == 0.f && song.valueAt(edge, 4) == 1.f &&
              song.valueAt(edge, 6) == 2.f && song.valueAt(edge, 10) == 2.f &&
              AutomationProgram::compile(c->project.automation, false, pattern, error,
                                         c->project.automationDefaults()) &&
              !pattern.find(edge),
          "Independent normalized0..1 maps to literal0..2, initial/final hold; Pattern has no lane");
    check(c->project.validate().wasOk(), "Actual send target validates after source curve edits");
}
__declspec(noinline) inline void references()
{
    auto c = std::make_unique<Controller>();
    c->replace(seed());
    auto a = target(500, 900);
    a.length = 4;
    check(c->execute("automation.create", a).wasOk(), "Create referenced edge");
    const auto source = c->project.automation.sources.front().id;
    a.target = source;
    auto master = target(500, 0);
    master.target = source;
    auto incoming = target(3, 900);
    incoming.target = source;
    check(c->execute("automation.link", master).wasOk() && c->execute("automation.link", incoming).wasOk(),
          "One source links distinct source/destination pairs");
    auto baseline = c->project;
    auto candidate = baseline;
    check(setMixerRoute(candidate, 500, 900, false)
                  .getErrorMessage()
                  .startsWith("automation.routeReferenced:") &&
              candidate.state().isEquivalentTo(baseline.state()),
          "Referenced route disable is atomic refusal");
    check(setMixerRoute(candidate, 500, 4, true, true).failed() &&
              candidate.state().isEquivalentTo(baseline.state()),
          "RouteOnly refuses all affected links before clearing");
    check(c->execute("automation.unlink", a).wasOk(), "Explicit unlink exact pair");
    candidate = c->project;
    check(setMixerRoute(candidate, 500, 900, false).failed(), "Initialization alone retains edge reference");
    check(c->execute("automation.init.remove", a).wasOk(), "Explicit initialization removal");
    candidate = c->project;
    check(setMixerRoute(candidate, 500, 900, false).wasOk() && candidate.validate().wasOk() &&
              mixerRoute(candidate, 500, 0)->gain == .25f &&
              automationState(candidate.automation).isEquivalentTo(automationState(c->project.automation)),
          "Disable after explicit cleanup retains other pair and source points");
    c->replace(baseline);
    const auto observed = mixerDeletionReferences(c->project, 900);
    check(observed["automationSources"].size() == 1 && int(observed["initializedTargets"]) == 2,
          "Destination deletion readback counts both incoming send keys");
    CommandArgs remove;
    remove.mixer = 900;
    remove.mixerDelete = MixerInsertDeletePolicy{4, false, false, false};
    std::unique_ptr<PreparedControllerChange> ticket;
    check(c->prepareMixerInsertCommand("mixer.insert.delete", remove, c->project, ticket).failed() && !ticket,
          "Incoming automation cannot silently follow replacement insert");
    remove.mixerDelete->removeAutomation = true;
    check(c->prepareMixerInsertCommand("mixer.insert.delete", remove, c->project, ticket).wasOk(),
          "Explicit removeAutomation prepares one actual delete action");
    const auto *next = ticket->destinationBeforeCommit();
    check(!mixerInsert(*next, 900) && mixerRoute(*next, 500, 4)->gain == .75f &&
              mixerRoute(*next, 3, 4)->gain == .5f && next->automation.sources.front().links.size() == 1 &&
              next->automation.sources.front().links.front().key ==
                  AutomationKey{AutomationParameter::mixerSend, 500, 0} &&
              next->automation.initialValues.size() == 1 &&
              next->automation.sources.front().points.size() == 2 && next->automation.placements.size() == 1,
          "Literal deletion removes only affected keys, never retargets automation or deletes music");
    check(c->validatePreparedChange(*ticket).wasOk() && c->commitPreparedChange(*ticket),
          "Commit validated prepared delete");
    check(c->prepareMixerInsertHistory(HistoryDirection::undo, c->project, ticket).wasOk() &&
              c->validatePreparedChange(*ticket).wasOk() && c->commitPreparedChange(*ticket) &&
              c->project.state().isEquivalentTo(baseline.state()),
          "Real history Undo restores exact routes and send references");
    candidate = baseline;
    check(deleteMixerInsert(candidate, 500, MixerInsertDeletePolicy{4, false, false, false}).failed(),
          "Outgoing send source deletion also requires explicit reference removal");
    check(deleteMixerInsert(candidate, 500, MixerInsertDeletePolicy{4, false, true, false}).wasOk() &&
              candidate.validate().wasOk() && candidate.automation.sources.front().links.size() == 1 &&
              candidate.automation.sources.front().links.front().key ==
                  AutomationKey{AutomationParameter::mixerSend, 3, 900},
          "Source deletion retains unrelated incoming curve target");
    auto detector = seed();
    juce::PluginDescription descriptor;
    descriptor.name = "Original absent detector model";
    descriptor.pluginFormatName = "VST3";
    descriptor.fileOrIdentifier = "original-send-model-absent.vst3";
    descriptor.numInputChannels = descriptor.numOutputChannels = 2;
    const auto id = detector.allocate();
    auto *row = const_cast<MixerInsert *>(mixerInsert(detector, 900));
    row->effects.push_back({id, descriptor.name, descriptor.createXml()->toString(), {}, false, false});
    row->effects.back().detector = PluginDetectorAssignment{500, 31, 2};
    check(detector.validate().wasOk(), "Actual persisted original detector dependency validates");
    const auto before = detector.state();
    const auto refs = mixerDeletionReferences(detector, 500);
    check(!bool(refs["available"]) && refs["errorCode"].toString() == "mixer.detectorReferenced" &&
              refs["detectors"].size() == 1 &&
              deleteMixerInsert(detector, 500, MixerInsertDeletePolicy{4, true, true, true})
                  .getErrorMessage()
                  .startsWith("mixer.detectorReferenced:") &&
              detector.state().isEquivalentTo(before),
          "Surviving detector source refuses even explicit other cleanup; never redirect to replacement");
}
__declspec(noinline) inline void codec()
{
    auto c = std::make_unique<Controller>();
    c->replace(seed());
    auto a = target(500, 900);
    a.length = 4;
    check(c->execute("automation.create", a).wasOk(), "Codec actual canonical source");
    const auto exact = c->project.state();
    Project parsed;
    check(Project::parse(exact, parsed).wasOk() && parsed.state().isEquivalentTo(exact),
          "Current schema preserves exact source/destination/manual/ranges/initial/points");
    for (const int version : {22, 23, 24, 25})
    {
        auto old = seed().state();
        old.setProperty("version", version, nullptr);
        stripPatternExtentForLegacy(old);
        auto expectedOld = seed();
        for (auto &pattern : expectedOld.patterns)
            pattern.autoLength = false;
        Project migrated;
        check(Project::parse(old, migrated).wasOk() && migrated.version == Project::currentVersion &&
                  migrated.automation.sources.empty() && migrated.state().isEquivalentTo(expectedOld.state()),
              "Genuine prior schema without send keys preserves complete model without invented automation");
        if (version < 25) // Send key kind6 was INTRODUCED in genuine schema25.
        {
            auto disguised = exact.createCopy();
            disguised.setProperty("version", version, nullptr);
            check(Project::parse(disguised, parsed).failed() && parsed.state().isEquivalentTo(exact),
                  "Kind6 forbidden in genuine pre25 formats without mutating destination Project");
        }
    }
    {
        // Main I/O schema26 must not erase or reinterpret genuine25 send data.
        auto priorSend = exact.createCopy();
        priorSend.setProperty("version", 25, nullptr);
        stripPatternExtentForLegacy(priorSend);
        auto expectedPriorSend = c->project;
        for (auto &pattern : expectedPriorSend.patterns)
            pattern.autoLength = false;
        Project migrated;
        check(Project::parse(priorSend, migrated).wasOk() && migrated.version == Project::currentVersion &&
                  migrated.state().isEquivalentTo(expectedPriorSend.state()),
              "Genuine schema25 send keys migrate with exact source/destination/manual/initial/points");
    }
    for (auto malformed : {std::int64_t(-1), std::int64_t(500), std::int64_t(777),
                           std::int64_t(std::numeric_limits<std::uint32_t>::max())})
    {
        auto tree = exact.createCopy();
        tree.getChildWithName("AUTOMATION")
            .getChildWithName("SOURCES")
            .getChild(0)
            .getChildWithName("LINKS")
            .getChild(0)
            .setProperty("parameter", juce::int64(malformed), nullptr);
        check(Project::parse(tree, parsed).failed() && parsed.state().isEquivalentTo(exact),
              "Malformed/self/missing/overflow send destination refuses atomically");
    }
    auto detached = c->project;
    std::erase_if(detached.mixer[1].routes, [](const auto &route) { return route.target == 900; });
    check(detached.validate().failed(), "Persistent key cannot refer to a removed route");
}
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        commands();
        references();
        codec();
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        return juce::Result::fail(e.what());
    }
}
} // namespace vibera::acceptance::mixerSendModel
