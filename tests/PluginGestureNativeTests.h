// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "NativeGestureFixture.h"
static void nativeEditorGestureTests(const juce::File &folder, const juce::File &binary)
{
    PluginHost host;
    auto spec = parameterSpec(binary);
    require(host.start(spec, 48000, 256, false).wasOk(), "native gesture fixture starts");
    auto catalogue = parameterCatalogue(host);
    auto manual = parameterResult(host, host.setParameter(0, .6f, catalogue.generation));
    require(manual.result == 0 && !host.lastParameterGesture(),
            "host inspector writes do not impersonate native editor gestures");
    host.editor(true);
    HWND window = nullptr;
    const auto deadline = juce::Time::getMillisecondCounter() + 5000;
    while (!window && juce::Time::getMillisecondCounter() < deadline)
    {
        window = test::gestureFixtureWindow(host.processId());
        juce::Thread::sleep(5);
    }
    require(window != nullptr, "actual SDK IPlugView attached in the specifically owned worker");
    const auto click = [&](int zone)
    {
        require(test::clickGestureFixture(window, host.processId(), zone),
                "targeted original native editor handler");
    };
    click(0);
    auto gesture = host.lastParameterGesture();
    require(gesture && gesture->completedTicks > 0,
            "actual native worker publishes a cross-process completion timestamp");
    require(gesture && gesture->id == 0 && gesture->generation == catalogue.generation &&
                gesture->before == .6f && gesture->after == .2f,
            "actual component handler begin/perform/end crosses worker IPC with native ID/value identity");
    const auto firstSerial = gesture->serial;
    click(1);
    const auto sparseDeadline = juce::Time::getMillisecondCounter() + 1000;
    do
    {
        gesture = host.lastParameterGesture();
        if (gesture && gesture->serial > firstSerial)
            break;
        juce::Thread::sleep(2);
    } while (juce::Time::getMillisecondCounter() < sparseDeadline);
    const auto actual = parameterCatalogue(host, true);
    const auto sparseDiagnostic =
        gesture
            ? " id=" + std::to_string(gesture->id) + " before=" + std::to_string(gesture->before) +
                  " after=" + std::to_string(gesture->after) + " serial=" + std::to_string(gesture->serial)
            : std::string(" no completed gesture");
    require(gesture && gesture->id == 0xfedcba98u && gesture->after == .8f && gesture->serial > firstSerial,
            "native sparse unsigned parameter ID retained through editor callbacks" + sparseDiagnostic +
                " current=" + std::to_string(nativeParameter(actual, 0xfedcba98u).value) +
                " editorZone=" + std::to_string(nativeParameter(actual, 99).value));
    require(parameterResult(host, host.setParameter(42, .25f, catalogue.generation)).result == 0,
            "redundant metadata notification during native editor use");
    juce::Thread::sleep(60);
    const auto unchanged = parameterCatalogue(host, true);
    require(unchanged.generation == catalogue.generation && !host.lastParameterGesture(),
            "redundant metadata preserves route generation but invalidates stale gesture history");
    click(1);
    gesture = host.lastParameterGesture();
    require(gesture && gesture->id == 0xfedcba98u && gesture->generation == catalogue.generation,
            "fresh native editor gesture is available after redundant metadata notification");
    const auto lastSerial = gesture->serial;
    click(2);
    gesture = host.lastParameterGesture();
    require(gesture && gesture->id == 0xfedcba98u && gesture->serial == lastSerial &&
                host.droppedParameterGestures() == 0,
            "1000 unbracketed telemetry notifications neither displace last tweaked nor fill gesture queue");
    host.setParameterNotificationsEnabled(false);
    click(0);
    require(!host.lastParameterGesture(),
            "disabled notifications clear prior target and suppress native gestures");
    // Saved manual overlays remain live even with notifications disabled.
    const auto request = host.requestState();
    juce::String state;
    const auto captureDeadline = juce::Time::getMillisecondCounter() + 10000;
    while (!host.readState(request, state) && juce::Time::getMillisecondCounter() < captureDeadline)
        juce::Thread::sleep(5);
    require(state.isNotEmpty(), "native edit state capture with notifications disabled");
    PluginHost restored;
    spec.state = state;
    require(restored.start(spec, 48000, 256, false).wasOk(),
            "native editor state restored in new owned worker");
    require(nativeParameter(parameterCatalogue(restored), 0).value == .2f,
            "disabled notifications do not disable saved native manual value");
    host.setParameterNotificationsEnabled(true);
    require(!host.lastParameterGesture(), "re-enable starts without resurrecting old target");
    click(0);
    require(host.lastParameterGesture().has_value(), "fresh completed gesture recovers after re-enable");
    click(3);
    require(host.droppedParameterGestures() > 0 && !host.lastParameterGesture(),
            "overflow explicitly invalidates entire incomplete queued prefix");
    click(0);
    require(host.lastParameterGesture().has_value(), "fresh valid native gesture recovers after overflow");
    manual = parameterResult(host, host.setParameter(42, 1, catalogue.generation));
    catalogue = parameterCatalogue(host, true);
    require(manual.result == 0 && catalogue.generation > 1 && !host.lastParameterGesture(),
            "schema change invalidates old native gesture even when parameter IDs are reused");
    host.editor(false);
    require(folder.getChildFile("native-editor-gestures.txt")
                .replaceWithText(
                    "PASS actual original SDK IPlugView -> IComponentHandler begin/perform/end "
                    "-> JUCE listener -> bounded IPC. "
                    "Zero/sparse IDs, host-write exclusion, telemetry flood, explicit "
                    "disable/re-enable, saved manual state, "
                    "overflow refusal/recovery, real schema invalidation and redundant-metadata gesture "
                    "epoch invalidation. Targeted window "
                    "messages, not physical input, "
                    "installed-provider gesture compatibility or listening.\n"),
            "native gesture receipt");
}
