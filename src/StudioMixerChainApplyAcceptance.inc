// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual application/worker acceptance, included in the owned synthetic
// Studio routing scenario. Structural gaps are allowed; completed must mean actual
// native activation and playable recovery. No hardware/listening/native-input claim.
stage = "actual-live-chain-paste-load-apply-history-recovery";
juce::Array<juce::var> chainReceipts;
const auto chainCall = [&](const juce::var &args) { return invoke("vibera_mixerchain", args); };
const auto chainRequest = [&](const char *action, int insert)
{
    auto args = object();
    put(args, "action", action);
    put(args, "mixer", insert);
    put(args, "projectIdentity", controller.projectIdentity);
    put(args, "revision", juce::int64(controller.revision));
    return args;
};
const auto finishChain = [&](juce::var current, const char *expected)
{
    const auto operation = current["operation"].toString();
    check(operation.isNotEmpty(), "Chain admission owns a real session operation");
    const auto started = juce::Time::getMillisecondCounter();
    juce::Array<juce::var> phases;
    juce::uint32 callbackMs = 0, serviceMs = 0, dispatchMs = 0, readbackMs = 0;
    const auto observePhase = [&]
    {
        const auto phase = current["phase"].toString();
        if (phases.isEmpty() || phases.getLast()["phase"].toString() != phase)
        {
            auto value = object();
            put(value, "phase", phase);
            put(value, "milliseconds", int(juce::Time::getMillisecondCounter() - started));
            phases.add(value);
        }
    };
    observePhase();
    while (current["completionState"].toString() == "pending" &&
           juce::Time::getMillisecondCounter() - started < 15000)
    {
        auto began = juce::Time::getMillisecondCounter();
        tick();
        callbackMs += juce::Time::getMillisecondCounter() - began;
        began = juce::Time::getMillisecondCounter();
        timerCallback();
        serviceMs += juce::Time::getMillisecondCounter() - began;
        began = juce::Time::getMillisecondCounter();
        dispatchMessages(5);
        dispatchMs += juce::Time::getMillisecondCounter() - began;
        auto args = object();
        put(args, "action", "job");
        put(args, "operation", operation);
        began = juce::Time::getMillisecondCounter();
        current = chainCall(args);
        readbackMs += juce::Time::getMillisecondCounter() - began;
        observePhase();
    }
    auto retained = current;
    put(retained, "observedMilliseconds", int(juce::Time::getMillisecondCounter() - started));
    put(retained, "observedPhases", juce::var(phases));
    put(retained, "callbackAndTraceMs", int(callbackMs));
    put(retained, "controlServiceMs", int(serviceMs));
    put(retained, "dispatchMs", int(dispatchMs));
    put(retained, "jobReadbackMs", int(readbackMs));
    chainReceipts.add(retained);
    check(folder.getChildFile("chain-apply-transactions.json")
              .replaceWithText(juce::JSON::toString(juce::var(chainReceipts))),
          "Retain actual chain outcomes including failures");
    check(current["completionState"].toString() == expected,
          "Chain reaches expected actual terminal state within bounded test observation");
    if (bool(current["documentChanged"]) && juce::String(expected) == "completed")
        check(bool(current["undoable"]) && !bool(current["nativeRecoveryRequired"]) &&
                  current["audioState"].toString() == "active" &&
                  audio.projectionDiagnostics().pluginSourceState == "active" && !needsRebuild &&
                  engineRevision == controller.revision,
              "Changed chain completion waits for actual native source activation and synchronized model");
    return current;
};
bool structuralResetExercised = false;
const auto finishChainHistory = [&](bool undo)
{
    if (undo)
        undoButton.onClick();
    else
        history("edit.redo");
    const auto operation = lastMixerChainHistoryOperation;
    if (!structuralResetExercised)
    {
        check(mixerChains->active && mixerChains->active->fullCapture,
              "Reset regression targets only the actual full structural history owner");
        const auto host = audio.parameterHost(fixtureId);
        check(host && host->alive(), "Reset regression uses the actual original SDK worker");
        const auto beforeReset = host->midiResetGeneration();
        host->resetNotes();
        const auto started = juce::Time::getMillisecondCounter();
        while (host->midiResetGeneration() == beforeReset &&
               juce::Time::getMillisecondCounter() - started < 15000)
        {
            tick();
            dispatchMessages(5);
        }
        auto evidence = object();
        put(evidence, "before", juce::int64(beforeReset));
        put(evidence, "after", juce::int64(host->midiResetGeneration()));
        put(evidence, "instance", host->runtimeIdentity());
        check(folder.getChildFile("structural-reset-history.json")
                  .replaceWithText(juce::JSON::toString(evidence)),
              "Retain actual native reset acknowledgement without fabricated counters");
        check(host->midiResetGeneration() != beforeReset,
              "Native DSP acknowledged reset between structural admission and capture");
        structuralResetExercised = true;
    }
    auto job = object();
    put(job, "action", "job");
    put(job, "operation", operation);
    return finishChain(chainCall(job), "completed");
};
const auto verifyRecoveredSound = [&]
{
    check(audio.playing(), "Structural completion automatically resumes the previously playing transport");
    audio.setMidiTarget(selectedChannel);
    audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 64, juce::uint8(110)));
    float recoveredPeak = 0;
    for (int n = 0; n < 16; ++n)
    {
        tick();
        dispatchMessages(2);
        recoveredPeak = std::max(recoveredPeak, block.getMagnitude(0, 256));
    }
    audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 64));
    tick();
    check(std::isfinite(recoveredPeak) && recoveredPeak > .0001f,
          "Actual completed chain produces finite non-silent native PCM after the accepted structural gap");
};
const int destinationInsert = controller.project.channel(selectedChannel)->mixer;
const int emptyInsert = 4;
check(mixerInsert(controller.project, 1)->effects.size() == 1 &&
          mixerInsert(controller.project, emptyInsert)->effects.empty() && destinationInsert != 1 &&
          destinationInsert != emptyInsert,
      "Genuine unequal-state source, occupied destination and empty target are distinct");
finishChain(chainCall(chainRequest("copy", 1)), "completed");
const auto sourceState = controller.project.plugin(fixtureId)->state;
check(sourceState == expectedNativeState && sourceState.isNotEmpty(),
      "Portable source uses actual previously captured original SDK state, not invented bytes");
const auto presets = folder.getChildFile("apply-presets");
check(presets.createDirectory().wasOk(), "Owned isolated preset directory created");
auto configure = object();
put(configure, "action", "configureDirectory");
put(configure, "directory", presets.getFullPathName());
check(chainCall(configure)["completionState"].toString() == "completed",
      "Canonical preset directory configured");
const auto preset = presets.getChildFile("actual-native-state.vibfx");
auto save = chainRequest("save", 1);
put(save, "path", preset.getFullPathName());
put(save, "overwrite", false);
finishChain(chainCall(save), "completed");
check(preset.existsAsFile(), "Actual captured chain preset committed before Load-and-apply");

// Refuse armed recording before touching the canonical document or native graph.
const auto beforeRefusal = controller.project.state().toXmlString();
const auto beforeRefusalRevision = controller.revision;
{
    const juce::ScopedValueSetter<bool> armed(recordArmed, true);
    const auto refused = chainCall(chainRequest("paste", emptyInsert));
    check(refused["completionState"].toString() == "failed" &&
              refused["code"].toString() == "effectChain.controlBusy" &&
              controller.revision == beforeRefusalRevision &&
              controller.project.state().toXmlString() == beforeRefusal,
          "Incompatible armed recording explicitly refuses structural Apply without data loss");
}
auto cancelAdmission = chainCall(chainRequest("paste", emptyInsert));
auto cancelArgs = object();
put(cancelArgs, "action", "cancel");
put(cancelArgs, "operation", cancelAdmission["operation"]);
finishChain(chainCall(cancelArgs), "cancelled");
check(controller.revision == beforeRefusalRevision &&
          controller.project.state().toXmlString() == beforeRefusal,
      "Actually accepted cancellation drains and preserves exact source and history");

audio.playStop();
tick();
const auto beforeEmptyRevision = controller.revision;
finishChain(chainCall(chainRequest("paste", emptyInsert)), "completed");
const auto *emptyResult = mixerInsert(controller.project, emptyInsert);
check(emptyResult->effects.size() == 1 && emptyResult->effects.front().id != fixtureId &&
          emptyResult->effects.front().state == sourceState && controller.revision == beforeEmptyRevision + 1,
      "Empty-target Paste creates a fresh stable ID and one exact canonical history change");
const auto emptyId = emptyResult->effects.front().id;
verifyRecoveredSound();
finishChainHistory(true);
check(mixerInsert(controller.project, emptyInsert)->effects.empty(), "Actual UI Undo restores empty chain");
verifyRecoveredSound();
finishChainHistory(false);
check(mixerInsert(controller.project, emptyInsert)->effects.front().id == emptyId &&
          audio.parameterHost(emptyId) && audio.parameterHost(emptyId)->alive(),
      "Actual MCP Redo restores historical canonical ID with a live native worker");
verifyRecoveredSound();

const auto oldDestination = mixerInsert(controller.project, destinationInsert)->effects;
check(oldDestination.size() == 1 && oldDestination.front().state != sourceState,
      "Occupied target has independently different genuine native state before replacement");
auto apply = chainRequest("loadApply", destinationInsert);
// The two states were captured from different real SDK workers by this scenario.
// Encoding an original two-slot preset is fixture setup, not a claim that a UI
// gesture generated it. Order/independent flags are deliberate musical oracles.
const auto *sourceSpec = controller.project.plugin(fixtureId);
effect_chain::Chain unequal{
    "Original unequal native pair",
    false,
    {{"Native A", sourceSpec->descriptor, sourceState, false, .75f},
     {"Native B", oldDestination.front().descriptor, oldDestination.front().state, true, .25f}}};
std::unique_ptr<juce::MemoryBlock> encoded;
check(effect_chain::encode(unequal, encoded).wasOk() && encoded,
      "Encode original ordered two-slot fixture from actual unequal native captures");
const auto multiPreset = presets.getChildFile("actual-unequal-native-pair.vibfx");
check(multiPreset.replaceWithData(encoded->getData(), encoded->getSize()),
      "Owned original two-slot preset written for actual file-load branch");
put(apply, "path", multiPreset.getFullPathName());
finishChain(chainCall(apply), "completed");
const auto replaced = mixerInsert(controller.project, destinationInsert)->effects;
check(replaced.size() == 2 && replaced.front().id != oldDestination.front().id &&
          replaced[0].id != replaced[1].id && replaced[0].state == sourceState &&
          replaced[1].state == oldDestination.front().state && replaced[0].wet == .75f &&
          replaced[1].wet == .25f && !replaced[0].bypass && replaced[1].bypass &&
          !controller.project.plugin(oldDestination.front().id),
      "Occupied Load-and-apply replaces exact unequal states, order/flags and fresh IDs");
verifyRecoveredSound();
finishChainHistory(true);
check(mixerInsert(controller.project, destinationInsert)->effects == oldDestination,
      "Actual UI Undo restores occupied chain including exact original opaque state and identity");
verifyRecoveredSound();
finishChainHistory(false);
check(mixerInsert(controller.project, destinationInsert)->effects == replaced,
      "Actual MCP Redo restores exact loaded chain and stable slot identity");
verifyRecoveredSound();
passed.add(stage);

stage = "actual-single-effect-replace-retains-neighbour-and-history";
const auto oldSlot = replaced[0], neighbour = replaced[1];
auto replacementValues = object(), replacementCommand = object();
put(replacementValues, "target", juce::int64(oldSlot.id));
put(replacementValues, "text", originalEffect->descriptor);
put(replacementCommand, "command", "plugin.effect.replace");
put(replacementCommand, "args", replacementValues);
put(replacementCommand, "revision", juce::int64(controller.revision));
finishChain(invoke("vibera_command", replacementCommand), "completed");
const auto afterReplace = mixerInsert(controller.project, destinationInsert)->effects;
check(afterReplace.size() == 2 && afterReplace[0].id != oldSlot.id && afterReplace[0].wet == oldSlot.wet &&
          afterReplace[0].bypass == oldSlot.bypass && afterReplace[1] == neighbour &&
          !controller.project.plugin(oldSlot.id),
      "Single-slot Replace changes only intended identity/native state; neighbour/order/controls stay exact");
const auto freshSlot = afterReplace[0].id;
const auto verifyDefaultNativeState = [&]
{
    auto host = audio.parameterHost(freshSlot);
    check(host && host->alive(), "Replacement owns an actual healthy native worker");
    const auto serial = host->requestParameterCatalogue();
    PluginParameterCatalogue meta;
    bool acknowledged = false;
    const auto began = juce::Time::getMillisecondCounter();
    while (!(acknowledged = host->readParameterCatalogue(meta) && meta.serial == serial) &&
           juce::Time::getMillisecondCounter() - began < 5000)
    {
        host->service();
        tick();
        dispatchMessages(2);
    }
    const auto gain =
        std::find_if(meta.parameters.begin(), meta.parameters.end(), [](const auto &p) { return p.id == 0; });
    check(serial && acknowledged && !meta.error && gain != meta.parameters.end() && gain->value == .5f,
          "Actual replacement restores original SDK default gain, not old slot's .625 state");
};
verifyDefaultNativeState();
verifyRecoveredSound();
finishChainHistory(true);
check(mixerInsert(controller.project, destinationInsert)->effects == replaced,
      "Actual single-slot Undo restores prior identity/state and every neighbour exactly");
verifyRecoveredSound();
finishChainHistory(false);
check(mixerInsert(controller.project, destinationInsert)->effects[0].id == freshSlot &&
          mixerInsert(controller.project, destinationInsert)->effects[1] == neighbour,
      "Actual single-slot Redo retains replacement ID and neighbour state");
verifyDefaultNativeState();
verifyRecoveredSound();
audio.stop();
tick();
passed.add(stage);

#include "StudioMixerInsertLifecycleAcceptance.inc"
#include "StudioMixerDetectorAcceptance.inc"

// Deliberately withhold ALL synthetic device callbacks after publication. This
// exercises a genuine missing-callback path, not a forged status or fake clock.
stage = "published-chain-activation-stall-is-bounded";
audio.playStop();
tick();
const auto stalledAdmission = chainCall(chainRequest("paste", 5));
const auto stalledToken = stalledAdmission["operation"].toString();
check(stalledToken.isNotEmpty(), "Missing-callback scenario owns an actual structural job");
auto stalledArgs = object();
put(stalledArgs, "action", "job");
put(stalledArgs, "operation", stalledToken);
auto stalled = stalledAdmission;
const auto stallStarted = juce::Time::getHighResolutionTicks();
bool publicationObserved = false;
juce::String committedState;
std::uint64_t committedRevision = 0;
while (stalled["completionState"].toString() == "pending" &&
       juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - stallStarted) < 15.)
{
    timerCallback();
    dispatchMessages(5);
    stalled = chainCall(stalledArgs);
    if (bool(stalled["documentChanged"]) && !publicationObserved)
    {
        publicationObserved = true;
        committedState = controller.project.state().toXmlString();
        committedRevision = controller.revision;
    }
}
chainReceipts.add(stalled);
check(folder.getChildFile("chain-apply-transactions.json")
          .replaceWithText(juce::JSON::toString(juce::var(chainReceipts))),
      "Retain actual missing-callback failure before asserting");
check(publicationObserved &&stalled["completionState"].toString() == "failed" &&
          stalled["code"].toString() == "effectChain.activationTimeout" && bool(stalled["documentChanged"]) &&
          bool(stalled["undoable"]) && bool(stalled["nativeRecoveryRequired"]) &&
          stalled["audioState"].toString() == "fault" && !mixerChainBusy() &&
          controller.revision == committedRevision &&
          controller.project.state().toXmlString() == committedState && controller.undo.canUndo(),
      "Actual publication without callbacks fails boundedly and releases its job, retaining exact "
      "model/history");
const auto stalledId = mixerInsert(controller.project, 5)->effects.front().id;
check(audio.parameterHost(stalledId) && audio.parameterHost(stalledId)->alive(),
      "Timeout does not destroy or force-terminate the published owned worker");
// Returning device callbacks activate the retained source automatically. The old
// failed receipt remains truthful historical evidence, not rewritten success.
for (int n = 0; n < 16; ++n)
{
    tick();
    dispatchMessages(2);
}
verifyRecoveredSound();
check(chainCall(stalledArgs)["completionState"].toString() == "failed" &&
          audio.projectionDiagnostics().pluginSourceState == "active",
      "Actual callbacks recover sound automatically; original timeout outcome remains unchanged");
audio.stop();
tick();
passed.add(stage);
