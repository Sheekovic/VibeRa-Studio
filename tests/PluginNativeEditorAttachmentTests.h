// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include "NativeGestureFixture.h"

// Include after require and parameterSpec. These are genuine original SDK
// provider cases; they have not been compiled or run by the draft author.
static void pluginNativeEditorAttachmentTests(const juce::File &folder, const juce::File &binary)
{
    using namespace vibera;
    auto spec = parameterSpec(binary); // Scan before arming any original-fixture environment.
    require(spec.name == "VibeRa Parameter Fixture", "attachment test targets the genuine original fixture");
    struct ScopedEditorMode
    {
        const wchar_t *names[5]{L"VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS",
                                L"VIBERA_FIXTURE_EDITOR_ATTACH_FAIL", L"VIBERA_FIXTURE_EDITOR_ATTACH_CRASH",
                                L"VIBERA_FIXTURE_EDITOR_UNSUPPORTED",
                                L"VIBERA_FIXTURE_EDITOR_NULL_AFTER_PROBES"};
        juce::String previous[5];
        explicit ScopedEditorMode(int mode)
        {
            for (int i = 0; i < 5; ++i)
            {
                previous[i] = juce::SystemStats::getEnvironmentVariable(juce::String(names[i]), {});
                require(_wputenv_s(names[i], L"") == 0, "clear only scoped original editor fixture mode");
            }
            if (mode == 0)
                require(_wputenv_s(names[1], L"1") == 0, "arm genuine SDK attached failure");
            if (mode == 1)
                require(_wputenv_s(names[3], L"1") == 0, "arm genuine unsupported native editor");
            if (mode == 2)
                require(_wputenv_s(names[4], L"2") == 0, "arm null editor after pinned JUCE probes");
            if (mode == 4)
                require(_wputenv_s(names[2], L"1") == 0, "arm actual native attachment worker crash");
        }
        ~ScopedEditorMode()
        {
            for (int i = 0; i < 5; ++i)
                _wputenv_s(names[i], previous[i].toWideCharPointer());
        }
    };
    juce::String report;
    for (int mode = 0; mode < 5; ++mode)
    {
        ScopedEditorMode scope(mode);
        PluginHost host;
        require(host.start(spec, 48000, 256, false).wasOk(), "original native editor fixture worker ready");
        const auto instance = host.runtimeIdentity();
        const auto request = host.requestEditor(instance, PluginEditorAction::open);
        require(request.serial && request.code == PluginEditorAdmissionCode::accepted,
                "genuine editor outcome begins as an accepted pending request");
        PluginEditorResult result;
        const auto until = juce::Time::getMillisecondCounter();
        while (!host.readEditorResult(instance, request.serial, result) &&
               juce::Time::getMillisecondCounter() - until <= pluginEditorTimeoutMs + 1000)
            juce::Thread::sleep(2);
        const auto expected = mode == 4   ? PluginEditorOutcome::workerUnavailable
                              : mode == 1 ? PluginEditorOutcome::unsupported
                              : mode < 3  ? PluginEditorOutcome::creationFailed
                                          : PluginEditorOutcome::opened;
        report += "mode=" + juce::String(mode) + " expected=" + pluginEditorOutcomeName(expected) +
                  " actual=" + pluginEditorOutcomeName(result.outcome) +
                  " serial=" + juce::String(result.serial) +
                  " completionKnown=" + juce::String(int(result.completionKnown)) +
                  " availability=" + juce::String(unsigned(result.window.availability)) +
                  " exists=" + juce::String(int(result.window.exists)) + " hostError=" + host.error() + "\n";
        require(folder.createDirectory().wasOk() &&
                    folder.getChildFile("native-editor-attachment.txt").replaceWithText(report),
                "Retain every actual SDK attachment outcome before asserting");
        if (mode == 4)
        {
            require(result.serial == request.serial && result.outcome == expected && result.dispatched &&
                        !result.completionKnown && !result.window.known && !host.alive() &&
                        !test::gestureFixtureWindow(host.processId()),
                    "Actual attachment crash returns this owner's dispatched/unknown failure, never native "
                    "success");
            continue;
        }
        require(result.serial == request.serial && result.outcome == expected && result.completionKnown &&
                    result.nativeOutcome == expected && host.alive(),
                "actual SDK attach failure/null/unsupported cannot masquerade as successful blank wrapper");
        if (mode < 3)
            require(result.window.known && !result.window.exists && !result.window.visible &&
                        !test::gestureFixtureWindow(host.processId()),
                    "failed native attachment leaves no host peer or original native child behind");
        else
        {
            require(result.window.exists && result.window.visible &&
                        test::gestureFixtureWindow(host.processId()) != nullptr,
                    "successful attachment has an actual original HWND child in the owned worker");
            const auto child = test::gestureFixtureWindow(host.processId());
            const auto nativeWindow = GetAncestor(child, GA_ROOT);
            DWORD owner = 0;
            GetWindowThreadProcessId(nativeWindow, &owner);
            require(nativeWindow && owner == host.processId() && host.alive(),
                    "Native minimise/manual-close target is an actual window in this retained owned worker");
            auto waitWindow = [&](auto predicate)
            {
                const auto started = juce::Time::getMillisecondCounter();
                while (!predicate(host.editorWindowState()) && host.alive() &&
                       juce::Time::getMillisecondCounter() - started <= 1500)
                    juce::Thread::sleep(2);
                return host.alive() && predicate(host.editorWindowState());
            };
            require(
                ShowWindowAsync(nativeWindow, SW_MINIMIZE) &&
                    waitWindow([](auto state)
                               { return state.known && state.exists && state.minimised && !state.visible; }),
                "Actual owned native minimise is reflected in worker readback");
            const auto reopen = [&](const char *message)
            {
                const auto open = host.requestEditor(instance, PluginEditorAction::open);
                require(open.serial, "Native restore/reuse request admitted");
                const auto started = juce::Time::getMillisecondCounter();
                while (!host.readEditorResult(instance, open.serial, result) &&
                       juce::Time::getMillisecondCounter() - started <= pluginEditorTimeoutMs + 1000)
                    juce::Thread::sleep(2);
                require(
                    result.serial == open.serial && result.outcome == PluginEditorOutcome::broughtToFront &&
                        result.window.exists && result.window.visible && !result.window.minimised &&
                        GetAncestor(test::gestureFixtureWindow(host.processId()), GA_ROOT) == nativeWindow,
                    message);
            };
            reopen("Acknowledged open restores the exact existing minimised native peer");
            require(PostMessageW(nativeWindow, WM_CLOSE, 0, 0) &&
                        waitWindow([](auto state) { return state.known && state.exists && !state.visible; }),
                    "Actual owned WM_CLOSE hides its retained editor without claiming destruction");
            reopen("Acknowledged open reuses the exact manually hidden native peer");
            report += "actual native minimise/restore/manual-close/reopen retained same HWND/worker; no "
                      "pointer input\n";
            const auto close = host.requestEditor(instance, PluginEditorAction::close);
            require(close.serial, "attached native editor close admitted");
            const auto began = juce::Time::getMillisecondCounter();
            while (!host.readEditorResult(instance, close.serial, result) &&
                   juce::Time::getMillisecondCounter() - began <= pluginEditorTimeoutMs + 1000)
                juce::Thread::sleep(2);
            require(result.serial == close.serial && result.outcome == PluginEditorOutcome::closed &&
                        !result.window.exists && !test::gestureFixtureWindow(host.processId()),
                    "native attached child and owned wrapper are removed on acknowledged close");
        }
        report += "mode=" + juce::String(mode) + " outcome=" + pluginEditorOutcomeName(expected) +
                  "; actual original SDK worker/window path; no audio/listening evidence.\n";
    }
    require(folder.getChildFile("native-editor-attachment.txt").replaceWithText(report),
            "actual attachment failure/success evidence retained");
}
