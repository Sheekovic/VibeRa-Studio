// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"

// Parent must supply a genuine original VST3 fixture with an actual native
// editor, configured before startup for the selected constructor/work delay.
// This test does not invent a provider implementation or claim it was run.
enum class PluginEditorDeadlineCase
{
    timelyLatePolling,
    genuinelyLate,
    timeoutThenLateReply
};
static void pluginEditorHostDeadlineAcceptance(const vibera::PluginSpec &spec,
                                               PluginEditorDeadlineCase scenario, const juce::File &folder)
{
    using namespace vibera;
    PluginHost host;
    require(host.start(spec, 48000, 256, false).wasOk(), "original native-editor worker starts");
    const auto instance = host.runtimeIdentity();
    PluginParameterCatalogue initial;
    require(host.readInitialParameterCatalogue(initial) && !initial.error,
            "native fixture starts with a real independent parameter catalogue");
    juce::String cachedState;
    unsigned cachedRevision = 0;
    require(host.reuseCapturedState(cachedState, cachedRevision) && cachedState.isNotEmpty(),
            "Unopened native fixture has an actual acknowledged reusable startup capture");
    const auto admission = host.requestEditor(instance, PluginEditorAction::open);
    require(admission.code == PluginEditorAdmissionCode::accepted && admission.serial,
            "actual native editor request accepted, not yet reported completed");
    require(!host.reuseCapturedState(cachedState, cachedRevision),
            "Actual accepted native editor invalidates the prior opaque cache");
    // For these cases the fixture must retain pending/running native work long
    // enough for this bounded observation. A missing interval is a fixture defect.
    require(host.editorBusy(), "fixture supplies an observable pending/native editor interval");
    require(host.requestState() == 0 && host.requestParameterCatalogue() == 0,
            "busy native editor explicitly refuses state and catalogue work without queueing it");
    if (!initial.parameters.empty())
    {
        const auto &parameter = initial.parameters.front();
        require(host.setParameter(parameter.id, parameter.value, initial.generation) == 0,
                "busy native editor refuses reverse parameter admission before its existing2s watchdog");
    }
    require(host.requestEditor("foreign-instance", PluginEditorAction::close).code ==
                    PluginEditorAdmissionCode::instanceChanged &&
                host.cancelEditorRequest("foreign-instance", admission.serial) ==
                    PluginEditorCancellation::instanceChanged,
            "another worker identity cannot issue or cancel this owner's token");
    PluginEditorResult sentinel{999}, result = sentinel;
    require(!host.readEditorResult("foreign-instance", admission.serial, result) && result == sentinel,
            "another worker identity cannot consume the accepted request's result");

    const auto started = juce::Time::getMillisecondCounter();
    // Keep actual native completion independent of host polling. The timely
    // fixture must finish below5s; late cases must finish in(5s,6s]. No deadlines
    // are relaxed: the only deliberately delayed operation is host observation.
    const auto waitUntil = [&](unsigned elapsed)
    {
        while (juce::Time::getMillisecondCounter() - started < elapsed)
            juce::Thread::sleep(2);
    };
    if (scenario == PluginEditorDeadlineCase::timeoutThenLateReply)
    {
        waitUntil(pluginEditorTimeoutMs + 50);
        require(host.readEditorResult(instance, admission.serial, result) &&
                    result.outcome == PluginEditorOutcome::timeout && result.dispatched &&
                    !result.completionKnown && !result.window.known,
                "running editor timeout preserves uncertainty before the fixture's late reply");
        const auto failure = result;
        waitUntil(6100);
        require(!host.readEditorResult(instance, admission.serial, result) && result == failure &&
                    !host.editorResultReady(instance, admission.serial),
                "late real-worker acknowledgement cannot resurrect a consumed terminal timeout");
    }
    else
    {
        waitUntil(6100);
        require(host.readEditorResult(instance, admission.serial, result) && result.completionKnown &&
                    result.dispatched && result.window.known &&
                    result.nativeOutcome == PluginEditorOutcome::opened,
                "actual native completion has an independently stamped outcome and observed window");
        const auto elapsed = result.completedAt - result.admittedAt;
        if (scenario == PluginEditorDeadlineCase::timelyLatePolling)
        {
            require(result.succeeded() && elapsed <= pluginEditorTimeoutMs && host.alive(),
                    "timely real native completion succeeds when its host polls after the deadline");
            // Fresh Save-style capture remains supported while the native editor
            // is open, but those bytes cannot authorize later structural reuse.
            const auto openStateSerial = host.requestState();
            juce::String openState;
            const auto openStateStarted = juce::Time::getMillisecondCounter();
            while (!host.readState(openStateSerial, openState) &&
                   juce::Time::getMillisecondCounter() - openStateStarted < 10000)
                juce::Thread::sleep(2);
            require(openStateSerial && openState.isNotEmpty() &&
                        !host.reuseCapturedState(cachedState, cachedRevision),
                    "Fresh open-editor state succeeds without revalidating its opaque cache");
            // This completed owner is already consumed here. A second request
            // is held unread to prove ownership counts are not physical activity.
            const auto close = host.requestEditor(instance, PluginEditorAction::close);
            require(close.serial, "actual native editor close accepted");
            const auto closeStarted = juce::Time::getMillisecondCounter();
            while (!host.editorResultReady(instance, close.serial) &&
                   juce::Time::getMillisecondCounter() - closeStarted <= pluginEditorTimeoutMs + 50)
                juce::Thread::sleep(2);
            require(
                host.editorResultReady(instance, close.serial) && !host.editorBusy() &&
                    host.pendingEditorRequests() == 1,
                "completed unread editor receipt is retained without blocking unrelated control admission");
            require(host.readState(openStateSerial, openState) &&
                        !host.reuseCapturedState(cachedState, cachedRevision),
                    "Closing native editor and rereading an older receipt cannot resurrect stale cache");
            const auto stateSerial = host.requestState();
            require(stateSerial != 0, "completed unread editor result does not refuse state capture");
            juce::String state;
            const auto stateStarted = juce::Time::getMillisecondCounter();
            while (!host.readState(stateSerial, state) &&
                   juce::Time::getMillisecondCounter() - stateStarted <= 10000)
                juce::Thread::sleep(2);
            const bool stateRead = host.readState(stateSerial, state);
            const bool closeRead = host.readEditorResult(instance, close.serial, result);
            require(folder.createDirectory().wasOk() && folder.getChildFile("native-editor-state-debug.txt").replaceWithText(
                        "stateSerial=" + juce::String(stateSerial) + " stateRead=" + juce::String(int(stateRead)) +
                        " closeRead=" + juce::String(int(closeRead)) + " outcome=" + pluginEditorOutcomeName(result.outcome) +
                        " exists=" + juce::String(int(result.window.exists)) + " alive=" + juce::String(int(host.alive())) +
                        " phase=" + juce::String(ipc::dspPhaseName(host.statistics().workerPhase)) +
                        " error=" + host.error() + "\n"), "Retain actual state/close outcome before assertion");
            require(stateRead && closeRead &&
                        result.outcome == PluginEditorOutcome::closed && !result.window.exists,
                    "actual captured state and unrelated retained native-close receipt both survive");
            require(host.reuseCapturedState(cachedState, cachedRevision) && cachedState == state &&
                        cachedRevision == host.parameterRevision(),
                    "New acknowledged closed-editor capture restores exact reuse authority");
        }
        else
            require(result.outcome == PluginEditorOutcome::timeout && !result.succeeded() &&
                        elapsed > pluginEditorTimeoutMs && result.window.visible,
                    "genuinely late native completion fails while retaining actual visible-window evidence");
    }
    // Service only this owned host's existing grace/retirement path. No unrelated
    // process cleanup or audio timing/continuity claim is made by this test.
    const auto retirementStarted = juce::Time::getMillisecondCounter();
    while (!host.alive() && juce::Time::getMillisecondCounter() - retirementStarted < 2200)
    {
        host.service();
        juce::Thread::sleep(5);
    }
}
