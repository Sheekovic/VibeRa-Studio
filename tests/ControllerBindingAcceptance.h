// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MidiController.h"
#include "../src/McpSchema.h"
#include <stdexcept>

// Original metadata/file/history proof. Hardware is explicitly disabled.
namespace vibera::test::controller_binding
{
inline void check(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
inline ControllerBinding original()
{
    ControllerBinding b;
    b.action = "mixer.send";
    b.label = "Original stable send mapping";
    b.port = 1;
    b.channel = 1;
    b.number = 23;
    b.mode = 1;
    b.mixer = 500;
    b.sendTarget = 900;
    b.minimum = 0;
    b.maximum = 2;
    return b;
}
__declspec(noinline) inline juce::Result verify(const juce::File &output)
{
    try
    {
        check(output.createDirectory().wasOk(), "Owned controller binding output");
        const auto file = output.getChildFile("original-controller-binding.json");
        check(!file.exists(), "Never overwrite earlier binding fixture");
        auto inbox = std::make_unique<MidiControlInput>();
        auto c = std::make_unique<MidiController>(*inbox, false);
        check(c->setPorts("original-no-hardware-input", "original-no-hardware-output").wasOk(),
              "Hardware=false stores identifiers without opening MIDI ports");
        const auto input = c->inputIdentifier(), out = c->outputIdentifier();
        auto binding = original();
        juce::String initial;
        check(c->bindingProfileIdentity(file, initial).wasOk() && initial.length() == 64,
              "Missing saved file has an actual fresh identity");
        check(c->setBinding(0, binding, initial, file).wasOk() && file.existsAsFile() &&
                  c->bindings.size() == 1 && c->bindings[0].mixer == 500 &&
                  c->bindings[0].sendTarget == 900 && c->inputIdentifier() == input &&
                  c->outputIdentifier() == out,
              "Actual atomic save/append publishes exact stable pair without port rebinding");
        const auto saved = c->state();
        const auto savedJson = juce::JSON::toString(saved);
        const auto disk = file.loadFileAsString();
        check(juce::JSON::toString(juce::JSON::parse(disk)) == savedJson,
              "Saved readback contains exact actual complete runtime profile");
        auto reloadInbox = std::make_unique<MidiControlInput>();
        auto loaded = std::make_unique<MidiController>(*reloadInbox, false);
        check(loaded->importState(juce::JSON::parse(disk)).wasOk() &&
                  juce::JSON::toString(loaded->state()) == savedJson && loaded->inputIdentifier() == input,
              "Actual file reload shares binding parser and preserves target, ports and all metadata");
        auto item = saved["bindings"].getArray()->getReference(0);
        validateMcpSchema(MidiController::bindingSchema(), item);
        ControllerBinding parsed;
        check(MidiController::parseBinding(item, parsed).wasOk() && parsed.sendTarget == 900 &&
                  parsed.mixer == 500 && parsed.minimum == 0 && parsed.maximum == 2,
              "Shared typed parser accepts independent complete send fixture");
        for (const auto &field : juce::StringArray{"sendTarget", "mixer", "enabled", "minimum"})
        {
            auto malformed = juce::JSON::parse(juce::JSON::toString(item));
            malformed.getDynamicObject()->setProperty(field, field == "enabled" ? juce::var("true")
                                                                                : juce::var(true));
            parsed.label = "unchanged-output";
            check(MidiController::parseBinding(malformed, parsed).failed() &&
                      parsed.label == "unchanged-output",
                  "Wrong types refuse without changing parser output");
        }
        auto malformed = juce::JSON::parse(juce::JSON::toString(item));
        malformed.getDynamicObject()->setProperty("unknownMappingField", 1);
        check(MidiController::parseBinding(malformed, parsed).failed(),
              "Unknown mapping metadata is never silently dropped");
        juce::String current;
        check(c->bindingProfileIdentity(file, current).wasOk() && current != initial,
              "Saved transition changes exact profile identity");
        const auto fail =
            [&](size_t index, const ControllerBinding &candidate, const juce::String &token, const char *code)
        {
            inbox->observe(juce::MidiMessage::controllerEvent(1, 23, 64), 1);
            const auto result = c->setBinding(index, candidate, token, file);
            ControlMessage pending;
            check(result.failed() && result.getErrorMessage().startsWith(code) &&
                      c->inputIdentifier() == input && c->outputIdentifier() == out &&
                      juce::JSON::toString(c->state()) == savedJson && file.loadFileAsString() == disk &&
                      inbox->pop(1, pending) && inbox->current(pending) && pending.number == 23 &&
                      pending.value == 64,
                  "Refusal retains actual runtime/file/ports and queued control generation");
        };
        fail(0, binding, initial, "controller.staleProfile:");
        fail(2, binding, current, "controller.invalidIndex:");
        auto invalid = binding;
        invalid.sendTarget = -1;
        fail(0, invalid, current, "controller.invalidBinding:");
        invalid = binding;
        invalid.nativeArea = 1;
        fail(0, invalid, current, "controller.invalidBinding:");
        fail(1, binding, current, "controller.bindingConflict:");
        const auto blocker = output.getChildFile("original-blocking-parent");
        check(!blocker.exists() && blocker.replaceWithText("original owned sentinel"),
              "Own real filesystem failure parent");
        const auto impossible = blocker.getChildFile("controller.json");
        juce::String blocked;
        check(c->bindingProfileIdentity(impossible, blocked).wasOk(),
              "Identify missing target before real save failure");
        c->armLearn(0);
        inbox->observe(juce::MidiMessage::controllerEvent(1, 24, 65), 1);
        auto changed = binding;
        changed.label = "Must not publish after failed save";
        const auto failure = c->setBinding(0, changed, blocked, impossible);
        ControlMessage pending;
        check(failure.getErrorMessage().startsWith("controller.profileSaveFailed:") &&
                  juce::JSON::toString(c->state()) == savedJson && c->learnIndex == 0 &&
                  inbox->learning.load() && c->inputIdentifier() == input && c->outputIdentifier() == out &&
                  file.loadFileAsString() == disk &&
                  blocker.loadFileAsString() == "original owned sentinel" && inbox->pop(1, pending) &&
                  inbox->current(pending) && pending.number == 24,
              "Actual save failure preserves old binding, learning, pending input, ports and original file");
        c->cancelLearn();
        juce::String unchanged;
        check(c->bindingProfileIdentity(file, unchanged).wasOk() && unchanged == current,
              "Failure never changes the successful saved/runtime identity");
        check(file.replaceWithText(disk + "\n"), "Owned external-byte change fixture");
        const auto externallyChanged = file.loadFileAsString();
        check(externallyChanged != disk, "Actual external file bytes changed before stale-token proof");
        changed = binding;
        changed.label = "Newer requested metadata";
        check(c->setBinding(0, changed, current, file)
                      .getErrorMessage()
                      .startsWith("controller.staleProfile:") &&
                  juce::JSON::toString(c->state()) == savedJson &&
                  file.loadFileAsString() == externallyChanged,
              "Changed saved bytes invalidate token before file or runtime replacement");
        juce::String sentinel = "do-not-replace";
        check(c->bindingProfileIdentity(output, sentinel).failed() && sentinel == "do-not-replace",
              "Unreadable profile identity preserves caller output and old mappings");
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        return juce::Result::fail(e.what());
    }
}
// Root may instantiate this in its fresh isolated Studio(false,256) GUI branch.
// It refuses production/hardware settings before ANY mutation. No app is launched here.
template <class StudioType> __declspec(noinline) juce::Result verifyPublicMcp(StudioType &studio)
{
    try
    {
        auto request = [](const juce::String &action)
        {
            auto *o = new juce::DynamicObject;
            o->setProperty("action", action);
            return juce::var(o);
        };
        const auto state = studio.mcpCall("vibera_controller", request("state"))["structuredContent"];
        check(state["controllerBindingPersistence"].toString() == "sessionOnly" &&
                  !bool(state["testKeyDiagnosticsAvailable"]) &&
                  bool(state["controllerProfileIdentityKnown"]),
              "Actual public API test requires isolated no-hardware non-diagnostic Studio");
        const auto baseline = studio.controller.project.state();
        const auto revision = studio.controller.revision;
        const auto undo = studio.controller.undo.getUndoDescription(),
                   redo = studio.controller.undo.getRedoDescription();
        auto args = request("setBinding");
        const int index = state["controllerProfile"]["bindings"].size();
        check(index < 512, "Public append fixture requires available row");
        const auto item = juce::JSON::parse(
            R"({"action":"mixer.send","label":"Original public send","port":1,"channel":1,"kind":0,"number":126,"layer":0,"mode":1,"mixer":1,"sendTarget":0,"minimum":0,"maximum":2,"pickup":true,"banked":false,"enabled":false,"nativeArea":0})");
        args.getDynamicObject()->setProperty("index", index);
        args.getDynamicObject()->setProperty("binding", item);
        args.getDynamicObject()->setProperty("expectedProfile", state["controllerProfileIdentity"]);
        const auto applied = studio.mcpCall("vibera_controller", args)["structuredContent"];
        check(
            applied["completionState"].toString() == "completed" &&
                bool(applied["controllerBindingChange"]["configurationCommitted"]) &&
                !bool(applied["controllerBindingChange"]["persisted"]) &&
                !bool(applied["controllerBindingChange"]["projectUndoable"]) &&
                applied["controllerProfile"]["bindings"].size() == index + 1 &&
                baseline.isEquivalentTo(studio.controller.project.state()) &&
                studio.controller.revision == revision &&
                studio.controller.undo.getUndoDescription() == undo &&
                studio.controller.undo.getRedoDescription() == redo,
            "Actual public metadata append completes truthfully session-only, without Project/history edits");
        bool stale = false;
        try
        {
            (void)studio.mcpCall("vibera_controller", args);
        }
        catch (const McpFailure &e)
        {
            stale = e.code == "controller.staleProfile";
        }
        check(stale, "Actual public service rejects stale profile token");
        for (const auto &action : juce::StringArray{"preset", "configure", "learn", "cancel", "inject"})
        {
            bool refused = false;
            try
            {
                (void)studio.mcpCall("vibera_controller", request(action));
            }
            catch (const McpFailure &e)
            {
                refused = e.code == "controller.diagnosticOnly";
            }
            check(refused, "Actual production API refuses diagnostics before MIDI/port/native work");
        }
        const auto final = studio.mcpCall("vibera_controller", request("state"))["structuredContent"];
        check(juce::JSON::toString(final["controllerProfile"]) ==
                      juce::JSON::toString(applied["controllerProfile"]) &&
                  baseline.isEquivalentTo(studio.controller.project.state()) &&
                  studio.controller.revision == revision,
              "All production refusals preserve mapping and musical baseline");
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        return juce::Result::fail(e.what());
    }
}
} // namespace vibera::test::controller_binding
