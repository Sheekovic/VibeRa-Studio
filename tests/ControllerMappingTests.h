// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MidiController.h"

static void controllerMappingTests()
{
    vibera::MidiControlInput input;
    require(!input.observe(juce::MidiMessage::noteOn(1, 60, juce::uint8(90)), 0),
            "Unmapped keyboard note forwarded");
    input.consume(1, 1, 60);
    require(!input.observe(juce::MidiMessage::noteOn(1, 60, juce::uint8(90)), 0),
            "Retrigger ownership survives mapping during held note");
    require(!input.observe(juce::MidiMessage::noteOff(1, 60), 0),
            "Existing played note release survives new mapping");
    require(input.observe(juce::MidiMessage::noteOn(1, 60, juce::uint8(90)), 0), "Mapped note consumed");
    input.clearMappings();
    require(input.observe(juce::MidiMessage::noteOff(1, 60), 0),
            "Consumed note release stays consumed after removal");
    input.learning = true;
    require(input.observe(juce::MidiMessage::noteOn(1, 61, juce::uint8(90)), 0),
            "Learning consumes fresh control note");
    input.learning = false;
    require(input.observe(juce::MidiMessage::noteOff(1, 61), 0),
            "Learned note release survives Learn completion");
    require(!input.observe(juce::MidiMessage::controllerEvent(1, 64, 127), 0), "Unmapped sustain forwarded");
    input.consume(1, 0, 64);
    require(!input.observe(juce::MidiMessage::controllerEvent(1, 64, 127), 0),
            "Repeated held sustain retains ownership");
    require(!input.observe(juce::MidiMessage::controllerEvent(1, 64, 0), 0),
            "Held sustain release survives mapping");
    require(input.observe(juce::MidiMessage::controllerEvent(1, 64, 127), 0), "Mapped pedal press consumed");
    require(input.observe(juce::MidiMessage::controllerEvent(1, 64, 0), 0), "Mapped pedal release consumed");
    input.consume(1, 0, 123);
    require(!input.observe(juce::MidiMessage::controllerEvent(1, 123, 0), 0),
            "All notes off never intercepted");
    input.consume(16, 3, 0);
    require(!input.observe(juce::MidiMessage::pitchWheel(1, 10000), 0), "Pitch mapping channel isolated");
    require(input.observe(juce::MidiMessage::pitchWheel(16, 10000), 0), "Mapped channel pitch consumed");
    input.consume(16, 2, 47);
    require(input.observe(juce::MidiMessage::programChange(16, 47), 0), "Mapped program consumed");
    require(!input.observe(juce::MidiMessage::programChange(16, 48), 0), "Other programs remain musical");
    vibera::MidiController controls(input, false);
    controls.launchkeyPreset();
    std::vector<vibera::ControllerInvocation> events;
    controls.invoke = [&](const vibera::ControllerInvocation &event) { events.push_back(event); };
    auto cc = [&](int number, int value)
    {
        input.observe(juce::MidiMessage::controllerEvent(16, number, value), 1);
        controls.drain();
    };
    cc(115, 127);
    cc(115, 127);
    cc(115, 0);
    require(events.size() == 1 && events[0].action == "transport.play", "Button press edge and release");
    cc(53, 127);
    require(events.back().parameter && events.back().value == 1.5 && events.back().mixer == 1,
            "Fader absolute target");
    controls.moveBank(2);
    cc(53, 64);
    int effectiveTarget = -99;
    const auto normalMixer = vibera::Project::empty();
    require(vibera::resolveControllerMixerTarget(normalMixer, events.back().mixer, events.back().mixerBanked,
                                                 events.back().mixerBankOffset, 0, effectiveTarget)
                    .wasOk() &&
                effectiveTarget == 17,
            "Banked fader accesses final insert");
    cc(61, 64);
    require(events.back().mixer == 0, "Master independent of bank");
    cc(9, 1);
    cc(21, 127);
    require(events.back().action == "mixer.gain" && events.back().value == 1.5,
            "Hardware Volume pot mode follows documented report");
    cc(9, 3);
    cc(21, 0);
    require(events.back().action == "mixer.pan" && events.back().value == -1,
            "Hardware Pan pot mode follows documented report");
    const auto beforeUnsupported = events.size();
    cc(9, 2);
    cc(21, 127);
    require(events.size() == beforeUnsupported, "Unimplemented Device mode cannot fake Pan");
    cc(9, 3);
    controls.bindings.clear();
    vibera::ControllerBinding base;
    base.action = "transport.play";
    base.channel = 16;
    base.number = 115;
    auto shifted = base;
    shifted.action = "transport.stop";
    shifted.layer = 1;
    auto modifier = base;
    modifier.action = "";
    modifier.number = 106;
    modifier.layer = 1;
    modifier.mode = 4;
    controls.bindings = {base, shifted, modifier};
    controls.publish();
    events.clear();
    cc(106, 127);
    cc(115, 127);
    cc(115, 0);
    cc(106, 0);
    cc(115, 127);
    cc(115, 0);
    require(events.size() == 2 && events[0].action == "transport.stop" &&
                events[1].action == "transport.play",
            "Modifier layer releases back to base");
    controls.armLearn(0);
    events.clear();
    cc(42, 70);
    require(controls.learnIndex == -1 && controls.bindings[0].number == 42 && events.empty(),
            "Learn captures without invoking");
    auto state = controls.state();
    vibera::MidiControlInput otherInput;
    vibera::MidiController restored(otherInput, false);
    require(restored.importState(state).wasOk() &&
                juce::JSON::toString(restored.state()) == juce::JSON::toString(state),
            "Controller profile exact round trip");
    auto bad = juce::JSON::parse(juce::JSON::toString(state));
    bad["bindings"][0].getDynamicObject()->setProperty("number", 128);
    require(restored.importState(bad).failed() &&
                juce::JSON::toString(restored.state()) == juce::JSON::toString(state),
            "Invalid profile atomic preservation");
    vibera::ControllerBinding relative;
    relative.action = "mixer.gain";
    relative.channel = 16;
    relative.number = 21;
    relative.mode = 2;
    controls.bindings = {relative};
    controls.publish();
    events.clear();
    cc(21, 1);
    cc(21, 127);
    require(events.size() == 2 && events[0].delta > 0 && events[1].delta < 0,
            "Relative two's complement direction");
    controls.bindings[0].mode = 3;
    controls.publish();
    events.clear();
    cc(21, 65);
    cc(21, 63);
    require(events.size() == 2 && events[0].delta > 0 && events[1].delta < 0, "Relative offset direction");
    const auto originalMappings = controls.bindings;
    const int originalBank = controls.mixerBank;
    // Variable-universe target resolution is CONTROL-only, never MIDI-callback
    // arithmetic on stable IDs. All original hardware mode/edge/overflow tests remain.
    auto sparse = vibera::Project::empty();
    sparse.mixer.emplace_back(500000000, "Sparse original", 1, 0, false);
    std::swap(sparse.mixer[1], sparse.mixer.back());
    vibera::ControllerBinding fixed;
    fixed.action = "mixer.gain";
    fixed.channel = 16;
    fixed.number = 21;
    fixed.mode = 1;
    fixed.mixer = 500000000;
    fixed.maximum = 1.5;
    require(vibera::MidiController::valid(fixed), "Arbitrary sparse fixed ID is a valid profile target");
    require(vibera::resolveControllerMixerTarget(sparse, fixed.mixer, false, 0, 0, effectiveTarget).wasOk() &&
                effectiveTarget == 500000000,
            "Fixed target uses stable ID, not bank or display ordinal");
    require(vibera::resolveControllerMixerTarget(sparse, 1, true, 0, 0, effectiveTarget).wasOk() &&
                effectiveTarget == 500000000,
            "Bank slot follows actual reordered non-Master display position");
    require(vibera::resolveControllerMixerTarget(sparse, -1, false, 0, 500000000, effectiveTarget).wasOk() &&
                effectiveTarget == 500000000,
            "Selected target supports sparse current insert identity");
    controls.bindings = {fixed};
    controls.mixerBank = vibera::controllerMixerBankLimit(vibera::MixerNativeIdentity::capacity - 1);
    controls.publish();
    events.clear();
    cc(21, 127);
    require(events.size() == 1 && !events[0].mixerBanked && events[0].mixer == 500000000 &&
                events[0].value == 1.5,
            "Actual mapped input preserves fixed identity at a nonzero bank");
    const auto expandedProfile = controls.state();
    require(restored.importState(expandedProfile).wasOk() &&
                juce::JSON::toString(restored.state()) == juce::JSON::toString(expandedProfile) &&
                int(expandedProfile["version"]) == 1,
            "Expanded stable target/bank retains exact profile1 roundtrip");
    std::erase_if(sparse.mixer, [](const auto &row) { return row.index == 500000000; });
    effectiveTarget = 31415;
    const auto missing =
        vibera::resolveControllerMixerTarget(sparse, fixed.mixer, false, 0, 0, effectiveTarget);
    require(missing.failed() && missing.getErrorMessage().startsWith("controller.missingMixerTarget:") &&
                effectiveTarget == 31415 && controls.bindings.front().mixer == 500000000,
            "Deleted explicit target refuses unchanged; no remap to selected or surviving insert");
    require(vibera::resolveControllerMixerTarget(sparse, 1, true, 0, 0, effectiveTarget).wasOk() &&
                effectiveTarget == 2,
            "Intentional bank display binding follows current order after deletion");
    auto complete = vibera::Project::empty();
    complete.mixer.clear();
    complete.mixer.emplace_back(0, "Master", 1, 0, false);
    for (int id = 1; id < vibera::MixerNativeIdentity::capacity; ++id)
        complete.mixer.emplace_back(1000 + id, "Original capacity", 1, 0, false);
    require(vibera::resolveControllerMixerTarget(complete, 8, true, controls.mixerBank, 0, effectiveTarget)
                    .wasOk() &&
                effectiveTarget == 1128,
            "Final hardware bank addresses actual128th non-Master insert");
    effectiveTarget = 31415;
    require(vibera::resolveControllerMixerTarget(complete, 9, true, controls.mixerBank, 0, effectiveTarget)
                    .failed() &&
                effectiveTarget == 31415 &&
                vibera::resolveControllerMixerTarget(complete, 1, true, 121, 0, effectiveTarget).failed(),
            "Unavailable slot/unaligned bank refuse rather than clamp to last valid insert");
    require(vibera::resolveControllerMixerTarget(complete, 0, true, controls.mixerBank, 999, effectiveTarget)
                    .wasOk() &&
                effectiveTarget == 0,
            "Master remains independent of bank and selected insert");
    auto invalidBank = juce::JSON::parse(juce::JSON::toString(expandedProfile));
    invalidBank.getDynamicObject()->setProperty("mixerBank", 121);
    require(restored.importState(invalidBank).failed() &&
                juce::JSON::toString(restored.state()) == juce::JSON::toString(expandedProfile),
            "Invalid expanded profile preserves previous exact mappings");
    // Explicit send pairs retain their destination in profile1 and real inbox
    // invocations. Neither a selected insert nor a bank substitutes a target.
    auto send = fixed;
    send.action = "mixer.send";
    send.mixer = 500000000;
    send.sendTarget = 900;
    send.minimum = 0;
    send.maximum = 2;
    send.nativeArea = 0;
    controls.bindings = {send};
    controls.mixerBank = 0;
    controls.publish();
    require(vibera::MidiController::valid(send), "Send parameter requires an explicit stable destination");
    events.clear();
    cc(send.number, 64);
    require(events.size() == 1 && events.front().action == "mixer.send" &&
                events.front().mixer == 500000000 && events.front().sendTarget == 900 &&
                events.front().parameter && std::abs(events.front().value - 128. / 127.) < 1.e-6,
            "Actual CC invocation carries both stable send endpoints and linear output");
    const auto sendProfile = controls.state();
    require(restored.importState(sendProfile).wasOk() && restored.bindings.front().sendTarget == 900 &&
                juce::JSON::toString(restored.state()) == juce::JSON::toString(sendProfile),
            "Profile round trip retains exact send destination and unchanged format1");
    auto invalidSend = send;
    invalidSend.sendTarget = -1;
    require(!vibera::MidiController::valid(invalidSend), "Missing send destination refuses mapping");
    invalidSend = send;
    invalidSend.maximum = 3;
    require(!vibera::MidiController::valid(invalidSend), "Send mapping refuses unsupported gain range");
    invalidSend = send;
    invalidSend.action = "mixer.gain";
    require(!vibera::MidiController::valid(invalidSend), "Foreign send metadata refuses ordinary binding");
    invalidSend = send;
    invalidSend.mode = 0;
    require(!vibera::MidiController::valid(invalidSend),
            "Send mapping cannot masquerade as a targetless button");
    auto malformedSend = juce::JSON::parse(juce::JSON::toString(sendProfile));
    malformedSend["bindings"][0].getDynamicObject()->setProperty("sendTarget", "900");
    require(restored.importState(malformedSend).failed() &&
                juce::JSON::toString(restored.state()) == juce::JSON::toString(sendProfile),
            "Malformed send profile retains exact previous mapping");
    // Restore original relative mapping before unchanged overflow oracle.
    controls.bindings = originalMappings;
    controls.mixerBank = originalBank;
    controls.publish();
    for (int i = 0; i < 2048; ++i)
        input.observe(juce::MidiMessage::controllerEvent(16, 21, 127), 1);
    require(input.dropped() > 0, "Bounded inbox reports overflow");
    events.clear();
    controls.drain();
    require(events.empty(), "Overflow discards stale commands and modifier state");
}
