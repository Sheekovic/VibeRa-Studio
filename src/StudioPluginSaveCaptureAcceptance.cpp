// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoInline.h"
#include "Studio.h"
#include "../tests/SyntheticDevice.h"
#include <juce_cryptography/juce_cryptography.h>
#include <stdexcept>

namespace vibera
{
// Original real-SDK acceptance. The supplied callback records close admission;
// it never closes the application, replaces user work, or sends native input.
VIBERA_NOINLINE juce::Result Studio::verifyPluginSaveCapture(const juce::File &folder,
                                                                  const juce::File &binary)
{
    if (!controllerTestMode || !bool(getProperties()["viberaPluginSaveCaptureFixture"]) ||
        getProperties()["viberaPluginSaveCaptureOutput"].toString() != folder.getFullPathName() ||
        !pluginTestSettings || !workflowTestSettings || afterPluginState || pendingExport ||
        audio.rendering() || juce::Component::getCurrentlyModalComponent())
        return juce::Result::fail("Save capture acceptance requires its isolated owned Studio fixture");
    stopTimer(); // Deterministic UI-event-before-next-poll interleaving, not an audio pause or worker stall.
    struct Cleanup final
    {
        Studio &studio;
        ~Cleanup()
        {
            studio.stopTimer();
            studio.afterPluginState = {}; // No callback may retain this test's stack after failure.
            try
            {
                studio.audio.stop();
            }
            catch (...)
            {
            }
        }
    } cleanup{*this};
    auto observations = juce::var(new juce::DynamicObject);
    auto put = [&](const char *key, juce::var value)
    { observations.getDynamicObject()->setProperty(key, value); };
    auto check = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    auto persist = [&](bool passed, const juce::String &failure)
    {
        put("passed", passed);
        put("failure", failure);
        put("scope", "Actual Studio Save/MCP/close admission and original SDK state; no native input, audio, "
                     "hardware or listening");
        return folder.getChildFile("save-capture-verification.json")
            .replaceWithText(juce::JSON::toString(observations, true));
    };
    try
    {
        check(folder.isDirectory(), "Owned attempt directory exists");
        std::vector<juce::PluginDescription> types;
        const auto scan = PluginCatalog::scanOne(binary, types);
        check(scan.wasOk() && types.size() == 1 && types.front().name == "VibeRa Parameter Fixture" &&
                  !types.front().isInstrument,
              "Actual isolated original SDK parameter effect, not fabricated descriptor");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.patterns.front().notes.clear();
        const Id id = project.allocate();
        project.mixer[1].effects.push_back(
            {id, types.front().name, types.front().createXml()->toString(), {}, false, false});
        const Id otherId = project.allocate();
        project.mixer[2].effects.push_back(
            {otherId, types.front().name, types.front().createXml()->toString(), {}, false, false});
        check(project.validate().wasOk(), "Genuine valid original save project");
        controller.replace(std::move(project));
        selectedPattern = controller.project.patterns.front().id;
        selectedChannel = controller.project.channels.front().id;
        auto &manager = audio.engine.getDeviceManager();
        check(!manager.deviceManager.getCurrentAudioDevice(),
              "Refuse an existing device; launcher excludes hardware");
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        check(dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice()) != nullptr,
              "Only the original in-memory provider is active");
        auto ready = juce::Result::fail("Not prepared");
        const auto preparationDeadline = juce::Time::getMillisecondCounter() + 30000;
        do
        {
            ready = audio.rebuild(controller.project, patternMode, selectedPattern);
            if (AudioEngine::preparationPending(ready))
                dispatchMessages(5);
        } while (AudioEngine::preparationPending(ready) &&
                 juce::Time::getMillisecondCounter() < preparationDeadline);
        check(ready.wasOk(),
              ("Actual native fixture projection prepares: " + ready.getErrorMessage()).toRawUTF8());
        needsRebuild = false;
        const auto host = audio.preparedPluginHost(controller.project, id);
        check(host && host->alive(), "Actual projected worker owner");
        juce::String identities;
        for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                                 pluginWorkerExecutable(), binary})
            identities += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
        check(folder.getChildFile("binary-identities.txt").replaceWithText(identities),
              "Retain exact executable/worker/provider bytes");
        auto catalogue = [&](PluginHost &target, bool refresh)
        {
            PluginParameterCatalogue result;
            const auto serial = refresh ? target.requestParameterCatalogue() : 0;
            check(!refresh || serial != 0, "Actual native metadata request admitted");
            const auto deadline = juce::Time::getMillisecondCounter() + 10000;
            while (juce::Time::getMillisecondCounter() < deadline)
            {
                target.service();
                if (target.readParameterCatalogue(result) && result.serial >= serial && result.error == 0)
                    return result;
                juce::Thread::sleep(1); // CONTROL only; no audio callback is driven.
            }
            throw std::runtime_error("Actual native metadata did not complete");
        };
        auto parameter = [&](const PluginParameterCatalogue &metadata)
        {
            const auto found = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
                                            [](const auto &entry) { return entry.id == 0; });
            check(found != metadata.parameters.end() && found->writable(), "Real native gain0 is writable");
            return found->value;
        };
        auto metadata = catalogue(*host, false);
        auto set = [&](float value)
        {
            const auto serial = host->setParameter(0, value, metadata.generation);
            check(serial != 0, "Actual native parameter command admitted");
            PluginParameterResult result;
            const auto deadline = juce::Time::getMillisecondCounter() + 2000;
            while (!host->readParameterResult(serial, result) &&
                   juce::Time::getMillisecondCounter() < deadline)
            {
                host->service();
                juce::Thread::sleep(1);
            }
            check(result.serial == serial && result.result == 0 && result.value == value,
                  "Actual native gain acknowledged; no simulated worker reply");
        };
        set(.25f);
        bool baselineDone = false;
        auto baselineResult = juce::Result::fail("No baseline callback");
        const auto baseline = folder.getChildFile("baseline.vibra");
        check(!baseline.exists(), "No overwrite of baseline output");
        saveTo(baseline,
               [&](juce::Result result)
               {
                   baselineResult = result;
                   baselineDone = true;
               });
        const auto baselineDeadline = juce::Time::getMillisecondCounter() + 12000;
        while (!baselineDone && juce::Time::getMillisecondCounter() < baselineDeadline)
        {
            timerCallback();
            juce::Thread::sleep(1);
        }
        check(baselineDone && baselineResult.wasOk() && !controller.dirty,
              "Actual first Save establishes a clean native.25 baseline");
        const auto musicalRevision = controller.revision;
        const auto projectIdentity = controller.projectIdentity;
        const auto capturedRevision = host->parameterRevision();
        const auto destination = folder.getChildFile("interleaved.vibra");
        check(!destination.exists(), "No overwrite of interleaved output");
        auto saveArgs = juce::var(new juce::DynamicObject);
        saveArgs.getDynamicObject()->setProperty("action", "save");
        saveArgs.getDynamicObject()->setProperty("path", destination.getFullPathName());
        const auto pending = mcpCall("vibera_project", saveArgs)["structuredContent"];
        const auto operation = pending["operation"].toString();
        check(operation.isNotEmpty() && pending["completionState"].toString() == "pending" &&
                  afterPluginState,
              "Actual MCP Save accepted and owns real asynchronous capture");
        // requestEditor(close) is an actual supported readiness witness: Host admission
        // checks stateRequest==stateReady. Close an already closed native window;
        // no mouse/OS input, fake IPC, or unowned state serial inspection.
        PluginEditorAdmission admitted;
        const auto witnessDeadline = juce::Time::getMillisecondCounter() + 10000;
        while (!admitted.serial && juce::Time::getMillisecondCounter() < witnessDeadline)
        {
            admitted = host->requestEditor(host->runtimeIdentity(), PluginEditorAction::close);
            check(admitted.serial || admitted.code == PluginEditorAdmissionCode::busy,
                  "State capture readiness witness refuses only actual busy work");
            if (!admitted.serial)
            {
                host->service();
                juce::Thread::sleep(1);
            }
        }
        check(admitted.serial != 0, "Native opaque capture actually completed before later parameter change");
        PluginEditorResult editor;
        const auto editorDeadline = juce::Time::getMillisecondCounter() + pluginEditorTimeoutMs;
        while (!host->readEditorResult(host->runtimeIdentity(), admitted.serial, editor) &&
               juce::Time::getMillisecondCounter() < editorDeadline)
        {
            host->service();
            juce::Thread::sleep(1);
        }
        check(editor.serial == admitted.serial && editor.succeeded() && editor.completionKnown &&
                  editor.outcome == PluginEditorOutcome::closed && !editor.window.exists,
              "Actual terminal native close proves capture-ready admission without opening a view");
        check(afterPluginState && !controller.dirty && controller.revision == musicalRevision &&
                  host->parameterRevision() == capturedRevision,
              "Paused application polling retains completed opaque capture with clean cached state");
        set(.75f); // Worker really changes AFTER native capture; application timer has not consumed it.
        const auto newerRevision = host->parameterRevision();
        check(newerRevision != capturedRevision && controller.revision == musicalRevision &&
                  !controller.dirty && afterPluginState,
              "Newer native.75 state exists outside Controller revision before Save completion");
        put("capturedNativeRevision", juce::int64(capturedRevision));
        put("newerNativeRevision", juce::int64(newerRevision));
        put("musicalRevision", juce::int64(musicalRevision));
        put("savePendingBeforeClose", bool(afterPluginState));
        put("cachedDirtyBeforeClose", controller.dirty);
        bool proceeded = false;
        requestClose([&] { proceeded = true; }); // RECORD ONLY. Never quit or replace anything.
        put("closeProceededBeforeSaveCompletion", proceeded);
        // If a future fresh-dirty admission presents its normal discard UI, cancel
        // only the newly created fixture modal. No global/native input is sent.
        if (auto *modal = juce::Component::getCurrentlyModalComponent())
            modal->exitModalState(0);
        bool newRefused = false;
        auto newArgs = juce::var(new juce::DynamicObject);
        newArgs.getDynamicObject()->setProperty("action", "new");
        try
        {
            (void)mcpCall("vibera_project", newArgs);
        }
        catch (const std::exception &)
        {
            newRefused = true;
        }
        check(newRefused && controller.projectIdentity == projectIdentity && afterPluginState,
              "MCP rejects replacement during pending Save instead of admitting an orphaned job");
        // Complete the GENUINE Save even after recording the unsafe close. The
        // ordinary same-timer native revision consumer must re-dirty current.75.
        const auto completionDeadline = juce::Time::getMillisecondCounter() + 12000;
        while (afterPluginState && juce::Time::getMillisecondCounter() < completionDeadline)
        {
            timerCallback();
            juce::Thread::sleep(1);
        }
        auto jobArgs = juce::var(new juce::DynamicObject);
        jobArgs.getDynamicObject()->setProperty("action", "job");
        jobArgs.getDynamicObject()->setProperty("operation", operation);
        const auto terminal = mcpCall("vibera_project", jobArgs)["structuredContent"];
        put("saveCompletionState", terminal["completionState"]);
        put("dirtyAfterOrdinaryTimerCompletion", controller.dirty);
        check(!afterPluginState && terminal["completionState"].toString() == "completed" &&
                  controller.dirty && controller.revision == musicalRevision &&
                  controller.projectIdentity == projectIdentity,
              "Ordinary Save terminates its job and SAME timer re-dirties newer native state");
        Project saved;
        check(loadProject(destination, saved).wasOk() && saved.plugin(id),
              "Actual older captured file loads");
        PluginHost restored;
        const auto opened = restored.start(*saved.plugin(id), 48000., 256, true);
        check(opened.wasOk(), "Actual original SDK restores saved opaque state");
        const auto diskGain = parameter(catalogue(restored, false));
        const auto currentGain = parameter(catalogue(*host, true));
        put("savedNativeGain", diskGain);
        put("currentNativeGain", currentGain);
        check(diskGain == .25f && currentGain == .75f,
              "Real provider proves saved.25 differs from current unsaved.75");
        check(persist(false,
                      proceeded ? "Close admitted while real Save and newer native state were pending" : ""),
              "Persist all actual Save/native/close observations before final admission assertion");
        check(!proceeded, "UI close must refuse pending Save/newer native state even when cached Controller "
                          "dirty was false");
        // Independent fresh-dirty admission after a fully completed newer Save.
        bool cleanDone = false;
        auto cleanResult = juce::Result::fail("No clean callback");
        const auto cleanFile = folder.getChildFile("latest-native.vibra");
        check(!cleanFile.exists(), "Fresh subsequent Save output");
        saveTo(cleanFile,
               [&](juce::Result result)
               {
                   cleanResult = result;
                   cleanDone = true;
               });
        const auto cleanDeadline = juce::Time::getMillisecondCounter() + 12000;
        while (!cleanDone && juce::Time::getMillisecondCounter() < cleanDeadline)
        {
            timerCallback();
            juce::Thread::sleep(1);
        }
        check(cleanDone && cleanResult.wasOk() && !controller.dirty, "Real latest native Save returns clean");
        set(.5f); // Actual newer worker value before any application dirty polling.
        check(!controller.dirty, "Fresh native change is not artificially pre-marked dirty by fixture");
        const auto operationsBeforeRefusal = mcpOperations.size();
        bool freshRefused = false;
        try
        {
            (void)mcpCall("vibera_project", newArgs);
        }
        catch (const std::exception &)
        {
            freshRefused = true;
        }
        check(freshRefused && controller.dirty && controller.projectIdentity == projectIdentity &&
                  mcpOperations.size() == operationsBeforeRefusal,
              "Actual MCP fresh-native dirty refuses replacement before accepting a job");
        check(openProjectFile(baseline).failed() && controller.projectIdentity == projectIdentity,
              "Final direct Open observes dirty; file selection alone is not discard permission");
        put("freshNativeMcpReplacementRefused", freshRefused);
        // Exercise the real Controls queue BEFORE its timer can dispatch a write.
        juce::Result selection = juce::Result::fail("Not selected");
        const auto controlsDeadline = juce::Time::getMillisecondCounter() + 5000;
        do
        {
            selection = selectPluginParameter(id, 0, false);
            if (selection.failed())
                dispatchMessages(5);
        } while (selection.failed() && juce::Time::getMillisecondCounter() < controlsDeadline);
        check(selection.wasOk() && parameterWindow, "Actual Controls component selects native gain");
        std::function<juce::Slider *(juce::Component &)> findValue =
            [&](juce::Component &component) -> juce::Slider *
        {
            if (auto *slider = dynamic_cast<juce::Slider *>(&component);
                slider && slider->getName() == "Parameter value")
                return slider;
            for (auto *child : component.getChildren())
                if (auto *value = findValue(*child))
                    return value;
            return nullptr;
        };
        auto *value = findValue(*parameterWindow.getComponent());
        check(value && value->isEnabled(), "Actual writable Controls value target");
        value->setValue(.625, juce::sendNotificationSync); // Calls real queueValue, no pointer simulation.
        check(pluginControlsPending(), "Real Controls edit is queued/unacknowledged");
        auto *queuedWindow = parameterWindow.getComponent();
        queuedWindow->closeButtonPressed();
        check(parameterWindow.getComponent() == queuedWindow && queuedWindow->isVisible() &&
                  pluginControlsPending(),
              "Actual native close button preserves queued Controls owner");
        auto closeArgs = juce::var(new juce::DynamicObject);
        closeArgs.getDynamicObject()->setProperty("action", "closeControls");
        closeArgs.getDynamicObject()->setProperty("plugin", juce::int64(id));
        closeArgs.getDynamicObject()->setProperty("instance", host->runtimeIdentity());
        bool closeRefused = false;
        try
        {
            (void)mcpCall("vibera_plugin", closeArgs);
        }
        catch (const std::exception &)
        {
            closeRefused = true;
        }
        showPluginParameters(otherId);
        check(closeRefused && parameterWindow.getComponent() == queuedWindow &&
                  juce::int64(pluginControlSnapshot()["plugin"]) == id && pluginControlsPending(),
              "Shared semantic close and switching inspector cannot destroy accepted queued value");
        const auto queuedProject = controller.project.state();
        const auto queuedRevision = controller.revision;
        juce::String transitionCode;
        const auto queuedAdmission = admitProjectTransition(true, transitionCode);
        bool queuedProceeded = false;
        requestClose([&] { queuedProceeded = true; });
        auto discardArgs = juce::var(new juce::DynamicObject);
        discardArgs.getDynamicObject()->setProperty("action", "new");
        discardArgs.getDynamicObject()->setProperty("discardChanges", true);
        bool queuedMcpRefused = false;
        try
        {
            (void)mcpCall("vibera_project", discardArgs);
        }
        catch (const std::exception &)
        {
            queuedMcpRefused = true;
        }
        check(queuedAdmission.failed() && transitionCode == "busy" && !queuedProceeded && queuedMcpRefused &&
                  pluginControlsPending() && controller.projectIdentity == projectIdentity &&
                  controller.revision == queuedRevision &&
                  controller.project.state().isEquivalentTo(queuedProject) &&
                  mcpOperations.size() == operationsBeforeRefusal,
              "Shared UI/MCP admission preserves actual queued Controls edit even with explicit discard");
        put("queuedControlUiMcpRefused", queuedMcpRefused && !queuedProceeded);
        const auto drainDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (pluginControlsPending() && juce::Time::getMillisecondCounter() < drainDeadline)
        {
            dispatchMessages(5);
            // Controls now share the canonical Studio ACK/state/history owner.
            // Advance its actual service after the deliberate close-refusal checks.
            timerCallback();
        }
        check(!pluginControlsPending() && host->alive() && parameter(catalogue(*host, true)) == .625f,
              "Actual native catalogue independently proves queued Controls write acknowledged.625");
        const auto closed = mcpCall("vibera_plugin", closeArgs)["structuredContent"];
        check(bool(closed["closed"]) && closed["completionState"].toString() == "completed",
              "Acknowledged Controls can actually close through shared semantic operation");
        put("queuedControlsCloseSwitchRefused", true);
        put("acknowledgedNativeGain", .625f);
        // Accepted audio imports own user work before their metadata becomes a
        // document edit. Both in-flight and completed worker queues count busy.
        const auto wave = folder.getChildFile("original-import.wav");
        {
            auto stream = wave.createOutputStream();
            check(bool(stream), "Fresh original import fixture stream");
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.release(), 48000., 1, 16, {}, 0));
            juce::AudioBuffer<float> original(1, 128);
            juce::FloatVectorOperations::fill(original.getWritePointer(0), .125f, 128);
            check(writer && writer->writeFromAudioSampleBuffer(original, 0, 128),
                  "Genuine valid original PCM import fixture");
        }
        const auto clipsBefore = controller.project.audioPlacements.size();
        importAudioClip(wave, 0, 1);
        check(!audioImports.idle() || audioImportAfterTake, "Actual accepted import remains owned");
        const auto importAdmission = admitProjectTransition(true, transitionCode);
        bool importProceeded = false, importMcpRefused = false;
        requestClose([&] { importProceeded = true; });
        try
        {
            (void)mcpCall("vibera_project", discardArgs);
        }
        catch (const std::exception &)
        {
            importMcpRefused = true;
        }
        const auto importing =
            mcpCall("vibera_project", juce::JSON::parse(R"({"action":"state"})"))["structuredContent"];
        check(importAdmission.failed() && transitionCode == "busy" && !importProceeded && importMcpRefused &&
                  bool(importing["busy"]) && controller.projectIdentity == projectIdentity,
              "UI/MCP cannot replace accepted import before it becomes dirty");
        const auto importDeadline = juce::Time::getMillisecondCounter() + 5000;
        while ((!audioImports.idle() || audioImportAfterTake) &&
               juce::Time::getMillisecondCounter() < importDeadline)
        {
            pollAudioImports();
            juce::Thread::sleep(1);
        }
        check(audioImports.idle() && !audioImportAfterTake &&
                  controller.project.audioPlacements.size() == clipsBefore + 1 && controller.dirty,
              "Original import actually commits once after transition refusal");
        put("acceptedAudioImportPreserved", true);
        auto failedOpen = juce::var(new juce::DynamicObject);
        failedOpen.getDynamicObject()->setProperty("action", "open");
        failedOpen.getDynamicObject()->setProperty("path",
                                                   folder.getChildFile("missing.vibra").getFullPathName());
        failedOpen.getDynamicObject()->setProperty("discardChanges", true);
        const auto failedJob = mcpCall("vibera_project", failedOpen)["structuredContent"];
        check(failedJob["completionState"].toString() == "failed" && !bool(failedJob["documentChanged"]) &&
                  !bool(failedJob["error"]["effectMayHaveOccurred"]) &&
                  controller.projectIdentity == projectIdentity,
              "Real failed Open is terminal with no false document-commit witness");
        const auto afterDiscard = mcpCall("vibera_project", discardArgs)["structuredContent"];
        check(afterDiscard["completionState"].toString() == "completed" &&
                  bool(afterDiscard["documentChanged"]) && controller.projectIdentity != projectIdentity &&
                  !controller.dirty,
              "Explicit discard after actual acknowledgement completes canonical New, never a false accepted "
              "job");
        put("explicitDiscardCompleted", true);
        check(persist(true, {}), "Persist successful actual admission regression");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        persist(false, juce::String(error.what()));
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera
