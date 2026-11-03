// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"

// Actual state admission and project-boundary checks; no device or listening.
static void pluginStateAdmissionTests(const juce::File &folder, const juce::File &binary)
{
    auto p = Project::empty();
    {
        AudioEngine audio(false);
        int reached = 0;
        for (int index = 0; index < 12; ++index)
        {
            bool threw = false;
            preparationAllocationFailureHit = false;
            preparationAllocationFailureAfter = index;
            try
            {
                const auto result = audio.requestPluginState(p);
                preparationAllocationFailureAfter = -1;
                require(result.wasOk(), "Fresh snapshot admission after allocation probe");
            }
            catch (const std::bad_alloc &)
            {
                threw = true;
            }
            preparationAllocationFailureAfter = -1;
            reached += preparationAllocationFailureHit;
            // A failed copy/reservation must not leave a hidden active capture.
            if (threw)
                require(audio.requestPluginState(p).wasOk(),
                        "Failed allocation leaves subsequent capture admissible");
            Project captured;
            auto result = juce::Result::fail("not completed");
            require(audio.pollPluginState(captured, result) && result.wasOk() &&
                        captured.state().isEquivalentTo(p.state()),
                    "Snapshot after allocation probe completes without partial project state");
        }
        require(reached > 0, "Control allocation failure was actually reached, never silently skipped");
    }
    struct FixtureEnvironment
    {
        const juce::String previous =
            juce::SystemStats::getEnvironmentVariable("VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS", {});
        FixtureEnvironment()
        {
            require(_wputenv_s(L"VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS", L"200") == 0,
                    "Arm original native attachment delay");
        }
        ~FixtureEnvironment()
        {
            _wputenv_s(L"VIBERA_FIXTURE_EDITOR_ATTACH_DELAY_MS", previous.toWideCharPointer());
        }
    } environment;
    auto spec = parameterSpec(binary);
    require(spec.name == "VibeRa Parameter Fixture", "State admission uses genuine original SDK provider");
    spec.id = p.allocate();
    p.mixer[1].effects = {spec};
    AudioEngine audio(false);
    require(awaitProjection(audio, p, true, p.patterns.front().id).wasOk(),
            "Prepare actual isolated live state owner");
    const auto host = audio.preparedPluginHost(p, spec.id);
    require(host && host->alive(), "Prepared project binding resolves actual owned worker");
    const auto instance = host->runtimeIdentity();
    juce::String preparationEvidence;
    bool exhaustedSites = false;
    int reachedPreparation = 0;
    // Includes Project validation before private worker preparation. The first
    // run reached all48 sites; enumerate a larger bounded set until the actual
    // first non-hit, without weakening the exhaustion/retention/retry oracle.
    for (int failureIndex = 0; failureIndex < 512; ++failureIndex)
    {
        auto candidate = p;
        auto extra = spec;
        extra.id = candidate.allocate();
        candidate.mixer[2].effects = {extra};
        const auto oldProjection = audio.projectionDiagnostics().edit;
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = failureIndex;
        bool threw = false;
        try
        {
            audio.rebuild(candidate, true, candidate.patterns.front().id);
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        catch (...)
        {
            preparationAllocationFailureAfter = -1;
            throw;
        }
        preparationAllocationFailureAfter = -1;
        const bool reached = preparationAllocationFailureHit;
        reachedPreparation += reached;
        const bool retained = audio.projectionDiagnostics().edit == oldProjection &&
                              audio.preparedPluginHost(p, spec.id) == host && host->alive();
        const auto retry = awaitProjection(audio, candidate, true, candidate.patterns.front().id);
        preparationEvidence +=
            "site=" + juce::String(failureIndex) + " reached=" + juce::String(int(reached)) +
            " threw=" + juce::String(int(threw)) + " retained=" + juce::String(int(retained)) +
            " retry=" + retry.getErrorMessage() + "\n";
        require(folder.createDirectory().wasOk() &&
                    folder.getChildFile("preparation-allocation.txt").replaceWithText(preparationEvidence),
                "Retain actual preparation failure/retry evidence before assertion");
        require(retained && retry.wasOk(),
                "Preparation allocation failure preserves old live owner and allows real retry");
        require(awaitProjection(audio, p, true, p.patterns.front().id).wasOk() &&
                    audio.preparedPluginHost(p, spec.id) == host,
                "Original project/worker remains usable after each structural preparation probe");
        // Match the application's control timer: retire removed fixture workers
        // during the loop rather than keeping hundreds alive until destruction.
        audio.reclaimPluginHosts();
        if (!reached)
        {
            exhaustedSites = true;
            break;
        }
    }
    require(exhaustedSites && reachedPreparation > 0,
            "Actual preparation allocation sites reached and exhausted");
    const auto request = host->requestEditor(instance, PluginEditorAction::open);
    require(request.serial && host->editorBusy(), "Actual editor work has a pending native interval");
    const auto before = p.state();
    require(audio.requestPluginState(p).failed() && p.state().isEquivalentTo(before),
            "Live editor work refuses capture before any state snapshot can be silently omitted");
    PluginEditorResult editor;
    const auto started = juce::Time::getMillisecondCounter();
    while (!host->readEditorResult(instance, request.serial, editor) &&
           juce::Time::getMillisecondCounter() - started <= pluginEditorTimeoutMs + 1000)
        juce::Thread::sleep(2);
    require(editor.serial == request.serial && editor.succeeded(),
            "Actual native request finishes before snapshot retry");
    require(audio.requestPluginState(p).wasOk(), "Completed editor work does not leave state capture busy");
    Project captured;
    auto result = juce::Result::fail("pending");
    const auto captureStarted = juce::Time::getMillisecondCounter();
    bool completed = false;
    while (!(completed = audio.pollPluginState(captured, result)) &&
           juce::Time::getMillisecondCounter() - captureStarted <= 11000)
    {
        audio.reclaimPluginHosts();
        juce::Thread::sleep(2);
    }
    require(completed && result.wasOk() && captured.plugin(spec.id)->state.isNotEmpty(),
            "Retry captures genuine nonempty recoverable native state");
    auto replacement = p;
    auto second = spec;
    second.id = replacement.allocate();
    replacement.mixer[2].effects = {second};
    require(audio.rebuild(replacement, true, replacement.patterns.front().id).getErrorMessage() ==
                "Preparing VST3 workers...",
            "Structural preparation starts while retaining the previous project's live worker");
    audio.invalidateProjection();
    require(audio.parameterHost(spec.id) == host && !audio.preparedPluginHost(p, spec.id),
            "Old retained runtime is not advertised as a new project's prepared binding");
    require(audio.requestPluginState(p).wasOk(),
            "Unprepared new project preserves its own recoverable state");
    Project fresh;
    require(audio.pollPluginState(fresh, result) && result.wasOk() && fresh.plugin(spec.id)->state.isEmpty(),
            "Same-ID same-descriptor fresh project never captures an obsolete worker's nonempty state");
    require(awaitProjection(audio, replacement, true, replacement.patterns.front().id).wasOk(),
            "New project completes after invalidating an unpublished reuse preparation");
    const auto replacementHost = audio.preparedPluginHost(replacement, spec.id);
    require(replacementHost && replacementHost->alive() && replacementHost->runtimeIdentity() != instance &&
                host->alive(),
            "New project cannot inherit mutable old-project workers through a pending preparation");
    require(folder.createDirectory().wasOk() &&
                folder.getChildFile("plugin-state-admission.txt")
                    .replaceWithText(
                        "PASS actual native-editor busy capture refusal/retry; genuine opaque state; stale "
                        "same-ID prepared binding; unpublished reuse job invalidation; reached control "
                        "allocation failures. No audio/native input/listening/hardware claim.\n"),
            "Retain actual snapshot admission evidence");
}
