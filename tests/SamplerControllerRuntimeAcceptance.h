// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MidiController.h"
#include <stdexcept>

// Original CONTROL dispatcher/producer/file tests. No MIDI device is opened.
// These assertions do not establish app pickup, audible PCM, hardware or callback timing.
namespace vibera::test::sampler_controller_runtime
{
inline void check(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
inline ControllerBinding absolute(int cc, int channel = 0, int port = 1)
{
    ControllerBinding b;
    b.action = "mixer.gain";
    b.mode = 1;
    b.number = cc;
    b.channel = channel;
    b.port = port;
    b.label = "Original global control";
    return b;
}
inline SamplerControllerLink link(Project &p, int cc, int midiChannel = 1, std::uint32_t parameter = 0,
                                  int port = 1)
{
    SamplerControllerLink row;
    row.id = p.allocate();
    row.channel = p.channels.front().id;
    row.cc = cc;
    row.midiChannel = midiChannel;
    row.parameter = parameter;
    row.port = port;
    const auto meta = lookupSamplerAutomationParameter(parameter);
    check(meta.has_value(), "Original known sampler metadata");
    row.minimum = meta->minimum;
    row.maximum = meta->maximum;
    row.label = "Original project control";
    return row;
}
inline juce::var projectRow(const MidiController &c, std::int64_t id)
{
    const auto state = c.projectBindingState();
    for (const auto &row : *state["projectRows"].getArray())
        if (juce::int64(row["linkID"]) == id)
            return row;
    return {};
}
inline void cc(MidiControlInput &input, MidiController &controller, int number, int value, int channel = 1,
               int port = 1)
{
    input.observe(juce::MidiMessage::controllerEvent(channel, number, value), port);
    controller.drain();
}
__declspec(noinline) inline juce::Result verify(const juce::File &folder)
{
    try
    {
        check(folder.createDirectory().wasOk(), "Owned runtime test folder");
        auto input = std::make_unique<MidiControlInput>();
        auto c = std::make_unique<MidiController>(*input, false);
        auto p = std::make_unique<Project>(Project::empty());
        auto global = absolute(21);
        auto button = absolute(60, 1);
        button.kind = 1;
        button.mode = 0;
        button.action = "transport.play";
        c->bindings = {global, button};
        c->publish();
        const auto globalBytes = juce::JSON::toString(c->state());
        std::vector<ControllerInvocation> calls;
        c->invoke = [&](const auto &call) { calls.push_back(call); };
        auto first = link(*p, 21);
        auto second = link(*p, 22, 1, 19);
        p->samplerControllerLinks = {first, second};
        check(c->publishProjectBindings(*p, "actual-project-A").wasOk(), "Prepare actual project projection");
        const auto firstState = projectRow(*c, first.id), secondState = projectRow(*c, second.id);
        const auto generation = c->projectProjectionGeneration();
        const auto ccEpoch = input->controllerGeneration();
        check(juce::JSON::toString(c->state()) == globalBytes && int(firstState["slot"]) >= 512 &&
                  int(secondState["slot"]) >= 512,
              "Project rows preserve global profile1 and use separate CONTROL slots");
        cc(*input, *c, 21, 63);
        check(calls.size() == 1 && calls.back().projectLink == first.id && calls.back().value == 0 &&
                  calls.back().samplerParameter == 0 && calls.back().samplerDestination == "volume" &&
                  calls.back().action == "channel.sampler.modulation.enabled" &&
                  calls.back().samplerAvailable &&
                  c->projectInvocationCurrent(calls.back(), *p, "actual-project-A"),
              "Code0 present, CC63 rounds to flag0 and exact fixed owner/destination witness is current");
        const auto oldCall = calls.back();
        cc(*input, *c, 21, 64);
        check(calls.size() == 2 && calls.back().value == 1, "CC64 independently rounds flag1");
        cc(*input, *c, 21, 127, 2);
        check(calls.size() == 3 && calls.back().projectIdentity.isEmpty() && calls.back().binding == 0 &&
                  calls.back().action == "mixer.gain",
              "Project channel1 shadows wildcard global only on actual channel1");
        cc(*input, *c, 22, 127);
        check(calls.back().projectLink == second.id && calls.back().value == 2,
              "Actual project shape endpoint is physically quantized to index2");
        const auto shadow = c->projectBindingState()["globalRows"][0]["shadowedByProjectLinks"];
        check(shadow.isArray() && shadow.size() == 1 && juce::int64(shadow[0]) == first.id,
              "Readback identifies exact overriding project link instead of dropping global config");
        p->channels.front().gain = .25f;
        check(c->publishProjectBindings(*p, "actual-project-A").wasOk() &&
                  generation == c->projectProjectionGeneration() && ccEpoch == input->controllerGeneration(),
              "Ordinary scalar Project revision does not republish or invalidate CC/pickup state");
        input->observe(juce::MidiMessage::noteOn(1, 60, juce::uint8(90)), 1);
        c->drain();
        const auto afterPress = calls.size();
        auto inserted = link(*p, 23, 1, 17);
        p->samplerControllerLinks.insert(p->samplerControllerLinks.begin(), inserted);
        check(c->publishProjectBindings(*p, "actual-project-A").wasOk(),
              "Insert new row before retained IDs");
        auto nowFirst = projectRow(*c, first.id), nowSecond = projectRow(*c, second.id);
        check(int(nowFirst["slot"]) == int(firstState["slot"]) &&
                  nowFirst["bindingStateGeneration"].toString() ==
                      firstState["bindingStateGeneration"].toString() &&
                  int(nowSecond["slot"]) == int(secondState["slot"]) &&
                  nowSecond["bindingStateGeneration"].toString() ==
                      secondState["bindingStateGeneration"].toString(),
              "New-first insertion cannot steal any retained link slot or state generation");
        input->observe(juce::MidiMessage::noteOn(1, 60, juce::uint8(90)), 1);
        c->drain();
        check(calls.size() == afterPress,
              "Unrelated global held press stays down across project publication");
        input->observe(juce::MidiMessage::noteOff(1, 60), 1);
        c->drain();
        input->observe(juce::MidiMessage::noteOn(1, 60, juce::uint8(90)), 1);
        c->drain();
        check(calls.size() == afterPress + 1,
              "Real release/repress still executes after retained publication");
        cc(*input, *c, 23, 0);
        const auto period = lookupSamplerAutomationParameter(17);
        check(calls.back().projectLink == inserted.id && calls.back().value == period->minimum,
              "Physical minimum preserved exactly without interpolation roundoff");
        cc(*input, *c, 23, 127);
        check(calls.back().value == period->maximum,
              "Physical maximum preserved exactly without interpolation roundoff");
        std::reverse(p->samplerControllerLinks.begin(), p->samplerControllerLinks.end());
        const auto reorderedEpoch = input->controllerGeneration();
        check(c->publishProjectBindings(*p, "actual-project-A").wasOk() &&
                  input->controllerGeneration() == reorderedEpoch,
              "Order-only config retains effective fixed slots and epoch");
        check(!c->projectInvocationCurrent(oldCall, *p, "actual-project-A") &&
                  !c->projectInvocationCurrent(oldCall, *p, "different-project"),
              "Old projection/session witnesses refuse");
        const auto stableGeneration = c->projectProjectionGeneration();
        const auto stableEpoch = input->controllerGeneration();
        const auto duplicate = p->samplerControllerLinks.back();
        p->samplerControllerLinks.push_back(duplicate);
        check(c->publishProjectBindings(*p, "actual-project-A").failed() &&
                  c->projectProjectionGeneration() == stableGeneration &&
                  input->controllerGeneration() == stableEpoch,
              "Duplicate allocated link refuses before publication");
        p->samplerControllerLinks.pop_back();
        p->channels.front().audioClip =
            true; // Projection eligibility seam only, no audio-file/hosting claim.
        check(c->publishProjectBindings(*p, "actual-project-A").wasOk(),
              "Unsupported actual role remains reserved");
        const auto beforeDormant = calls.size();
        cc(*input, *c, 21, 127);
        check(calls.size() == beforeDormant + 1 && calls.back().projectLink == first.id &&
                  !calls.back().samplerAvailable &&
                  c->projectInvocationCurrent(calls.back(), *p, "actual-project-A"),
              "Dormant fixed owner emits unavailable witness once and never global fallback");
        const auto dormant = projectRow(*c, first.id);
        check(bool(dormant["reservesSource"]) && !bool(dormant["available"]),
              "Dormant reservation is explicit");
        p->samplerControllerLinks.clear();
        check(c->publishProjectBindings(*p, "actual-project-B").wasOk(),
              "Project replacement removes old reservations");
        cc(*input, *c, 21, 127);
        check(calls.back().projectIdentity.isEmpty() && juce::JSON::toString(c->state()) == globalBytes,
              "Project close/replacement restores same global row without preference edits");

        // A global modifier's release can become shadowed only AFTER it opens
        // another layer. Its old press ownership must close without global action.
        auto layerInput = std::make_unique<MidiControlInput>();
        auto layered = std::make_unique<MidiController>(*layerInput, false);
        auto modifier = absolute(30, 1);
        modifier.mode = 4;
        modifier.layer = 1;
        modifier.action.clear();
        auto base = absolute(41, 1), shifted = base;
        shifted.action = "mixer.pan";
        shifted.layer = 1;
        layered->bindings = {modifier, base, shifted};
        layered->publish();
        auto layerProject = std::make_unique<Project>(Project::empty());
        auto layerLink = link(*layerProject, 30);
        layerLink.layer = 1;
        layerProject->samplerControllerLinks = {layerLink};
        check(layered->publishProjectBindings(*layerProject, "layer-owner").wasOk(),
              "Prepare layer-scoped override");
        std::vector<ControllerInvocation> layerCalls;
        layered->invoke = [&](const auto &call) { layerCalls.push_back(call); };
        cc(*layerInput, *layered, 30, 127);
        cc(*layerInput, *layered, 30, 0);
        cc(*layerInput, *layered, 41, 127);
        check(layerCalls.size() == 2 && layerCalls[0].projectLink == layerLink.id &&
                  layerCalls[1].projectIdentity.isEmpty() && layerCalls[1].action == "mixer.gain",
              "Shadowed global modifier release clears its held layer without duplicate dispatch");

        // Fixed CONTROL capacity does not shrink the legacy512 global prefix.
        auto capacityInput = std::make_unique<MidiControlInput>();
        auto capacity = std::make_unique<MidiController>(*capacityInput, false);
        for (int i = 0; i < 512; ++i)
            capacity->bindings.push_back(absolute(i % 128, 1 + i / 128));
        capacity->publish();
        auto full = std::make_unique<Project>(Project::empty());
        for (int i = 0; i < 512; ++i)
            full->samplerControllerLinks.push_back(link(*full, i % 128, 5 + i / 128));
        const auto capacityPrefs = juce::JSON::toString(capacity->state());
        check(capacity->publishProjectBindings(*full, "full-1024").wasOk(),
              "All512 globals plus512 project rows fit");
        auto capacityState = capacity->projectBindingState();
        check(capacityState["globalRows"].size() == 512 && capacityState["projectRows"].size() == 512 &&
                  int(capacityState["capacity"]) == 1024,
              "No capacity-blocking of unrelated global controls");
        for (const auto &row : *capacityState["globalRows"].getArray())
            check(!bool(row["capacityBlocked"]), "Every global prefix row retains capacity");
        std::vector<ControllerInvocation> capacityCalls;
        capacity->invoke = [&](const auto &call) { capacityCalls.push_back(call); };
        cc(*capacityInput, *capacity, 127, 127, 4);
        cc(*capacityInput, *capacity, 127, 127, 8);
        check(capacityCalls.size() == 2 && capacityCalls[0].binding == 511 &&
                  capacityCalls[1].binding == 1023 &&
                  capacityCalls[1].projectLink == full->samplerControllerLinks.back().id,
              "Real last-global and last-project slots both dispatch their exact owners");
        const auto fullGeneration = capacity->projectProjectionGeneration();
        const auto fullEpoch = capacityInput->controllerGeneration();
        full->samplerControllerLinks.push_back(link(*full, 0, 9));
        check(capacity->publishProjectBindings(*full, "full-1024").failed() &&
                  capacity->projectProjectionGeneration() == fullGeneration &&
                  capacityInput->controllerGeneration() == fullEpoch &&
                  juce::JSON::toString(capacity->state()) == capacityPrefs,
              "Over-capacity refuses before old projection/epoch/global state changes");

        // Project CC-only Learn: fence pre-arm CC, capture once, preserve musical ownership.
        auto learnInput = std::make_unique<MidiControlInput>();
        auto learn = std::make_unique<MidiController>(*learnInput, false);
        learn->bindings = {absolute(21, 0, 0), absolute(64, 0, 0), absolute(120, 0, 0), absolute(123, 0, 0)};
        learn->publish();
        std::vector<ControllerInvocation> learnedCalls;
        learn->invoke = [&](const auto &call) { learnedCalls.push_back(call); };
        check(!learnInput->observe(juce::MidiMessage::noteOn(1, 67, juce::uint8(90)), 0),
              "Pre-arm musical note forwarded");
        // Sustain initially unmapped; install its mapping only after original ownership is recorded.
        auto heldInput = std::make_unique<MidiControlInput>();
        check(!heldInput->observe(juce::MidiMessage::controllerEvent(1, 64, 127), 0),
              "Original held sustain owner");
        MidiControlInput::PreparedMappings heldMasks;
        heldMasks.controllers[1] = 1;
        heldInput->replaceMappings(heldMasks);
        heldInput->beginControllerLearn();
        check(!heldInput->observe(juce::MidiMessage::controllerEvent(1, 64, 0), 0),
              "CC-only publication/Learn does not steal original held pedal release");
        heldInput->endControllerLearn();
        learnInput->observe(juce::MidiMessage::controllerEvent(1, 21, 40), 0);
        const auto noteGenerations = learnInput->mappingGenerations();
        int captures = 0;
        ControlMessage captured;
        check(learn
                  ->armProjectLearn(77,
                                    [&](auto token, const auto &event)
                                    {
                                        check(token == 77, "Exact owned Learn token");
                                        ++captures;
                                        captured = event;
                                    })
                  .wasOk(),
              "Arm project-only CC capture");
        learn->drain();
        check(captures == 0 && learnedCalls.empty(),
              "Pre-arm queued CC cannot capture or apply after new epoch");
        check(!learnInput->observe(juce::MidiMessage::noteOn(1, 68, juce::uint8(90)), 0),
              "New ignored musical note plays while project CC Learn waits");
        check(learnInput->observe(juce::MidiMessage::controllerEvent(1, 21, 80), 0),
              "First eligible CC captured not applied");
        learn->drain();
        check(captures == 1 && captured.number == 21 && captured.value == 80 && learnedCalls.empty(),
              "One candidate, no old/global action");
        learnInput->observe(juce::MidiMessage::controllerEvent(2, 22, 15), 0);
        learn->drain();
        check(captures == 1 && int(learn->projectLearnState()["cc"]) == 21 && learnedCalls.empty(),
              "Later different eligible source is suppressed without retargeting candidate");
        for (const auto number : {64, 120, 123})
            cc(*learnInput, *learn, number, 100, 1, 0);
        check(captures == 1 && learnedCalls.size() == 3 && learnedCalls[0].projectIdentity.isEmpty(),
              "Ineligible performance sustain/all-off CC retains configured global semantics during Learn");
        learn->armLearn(0);
        check(learn->learnIndex == -1 && bool(learn->projectLearnState()["active"]) &&
                  learn->lastMessage.startsWith("controller.learnBusy:"),
              "Global Learn cannot steal project owner");
        check(!learn->cancelProjectLearn(78) && learn->cancelProjectLearn(77) &&
                  !bool(learn->projectLearnState()["active"]),
              "Only exact token cancels actual capture lease");
        check(learnInput->mappingGenerations() == noteGenerations &&
                  !learnInput->observe(juce::MidiMessage::noteOff(1, 67), 0) &&
                  !learnInput->observe(juce::MidiMessage::noteOff(1, 68), 0),
              "Learn/cancel changes only CC epoch; old and new musical note releases stay forwarded");
        cc(*learnInput, *learn, 21, 100, 1, 0);
        check(learnedCalls.size() == 4,
              "After explicit cancel, next genuine CC may invoke old mapping; no replay");
        check(learn
                  ->armProjectLearn(79,
                                    [&](auto, const auto &event)
                                    {
                                        ++captures;
                                        captured = event;
                                    })
                  .wasOk(),
              "New independent control-port capture");
        cc(*learnInput, *learn, 64, 33, 3, 1);
        check(captures == 2 && captured.port == 1 && captured.number == 64 && learnedCalls.size() == 4 &&
                  learn->cancelProjectLearn(79),
              "Control-port genuine CC64 is eligible, distinct from performance pedal");

        // Global Learn delegates to the actual save-before-publish owned-file seam.
        const auto file = folder.getChildFile("global-learn-profile.json");
        const auto blocker = folder.getChildFile("global-learn-blocking-parent");
        check(!file.exists() && !blocker.exists(), "Never overwrite earlier failure evidence");
        check(learn->save(file).wasOk() && blocker.replaceWithText("original owned blocker"),
              "Real owned-file baseline");
        const auto disk = file.loadFileAsString(), prefs = juce::JSON::toString(learn->state());
        const auto impossible = blocker.getChildFile("controller.json");
        const auto beforeFailureGeneration = learn->projectProjectionGeneration();
        check(learn->armLearn(0, impossible).wasOk(), "Arm exact owned profile without changing mapping");
        cc(*learnInput, *learn, 25, 101, 1, 1);
        check(learn->learnIndex == 0 && learnInput->learning.load() &&
                  learn->lastMessage.startsWith("controller.profileSaveFailed:") &&
                  juce::JSON::toString(learn->state()) == prefs && file.loadFileAsString() == disk &&
                  blocker.loadFileAsString() == "original owned blocker" &&
                  learn->projectProjectionGeneration() == beforeFailureGeneration && learnedCalls.size() == 4,
              "Actual failed Learn save retains old mappings/file/runtime and pending Learn, never executes "
              "capture");
        learn->cancelLearn();
        check(learn->armLearn(0, file).wasOk(), "Arm exact durable original profile");
        cc(*learnInput, *learn, 26, 102, 1, 1);
        check(learn->learnIndex == -1 && !learnInput->learning.load() && learn->bindings[0].number == 26 &&
                  learn->bindings[0].port == 1 && file.loadFileAsString() != disk &&
                  juce::JSON::toString(juce::JSON::parse(file)) == juce::JSON::toString(learn->state()) &&
                  learnedCalls.size() == 4,
              "Successful Learn publishes only after durable file replacement; captured CC not executed");
        cc(*learnInput, *learn, 26, 127);
        check(learnedCalls.size() == 5 && learnedCalls.back().value == 1,
              "A subsequent actual CC uses committed learned mapping");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::test::sampler_controller_runtime
