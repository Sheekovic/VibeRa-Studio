// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoInline.h"
#include "Studio.h"
#include "MixerChainWorkflowContracts.h"
#include "EffectChain.h"
#include "../tests/SyntheticDevice.h"
#include <juce_cryptography/juce_cryptography.h>
#include <stdexcept>
namespace vibera
{
// Actual shared Studio/MCP dispatch, two original SDK workers and real preset files.
// No desktop input, playback PCM, installed-provider or listening qualification.
VIBERA_NOINLINE juce::Result Studio::verifyMixerChainWorkflow(const juce::File &folder,
                                                                   const juce::File &binary)
{
    if (!controllerTestMode || !pluginTestSettings || !workflowTestSettings ||
        !bool(getProperties()["viberaMixerChainWorkflowFixture"]) ||
        getProperties()["viberaMixerChainWorkflowOutput"].toString() != folder.getFullPathName() ||
        !folder.isDirectory() || afterPluginState || mixerChainBusy() || pendingExport || audio.rendering() ||
        juce::Component::getCurrentlyModalComponent())
        return juce::Result::fail("Chain workflow requires its isolated owned Studio and settings fixture");
    stopTimer();
    juce::Array<juce::var> observations;
    auto facts = juce::var(new juce::DynamicObject);
    auto put = [&](const char *key, juce::var value)
    { facts.getDynamicObject()->setProperty(key, std::move(value)); };
    auto check = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    auto persist = [&](bool passed, const juce::String &failure)
    {
        put("passed", passed);
        put("failure", failure);
        put("calls", observations);
        put("scope", "Original SDK and actual Studio MCP/component workflow, target-only native state and "
                     "real file bytes; no desktop/audio/listening evidence");
        return folder.getChildFile("chain-workflow.json").replaceWithText(juce::JSON::toString(facts, true));
    };
    // Every capture callback captures SafePointer+operation only, never this stack.
    // On failure retain/drain OWNED work through the same service before returning.
    const auto drain = [&]
    {
        const auto began = juce::Time::getMillisecondCounter();
        while ((mixerChainBusy() || afterPluginState) &&
               juce::uint32(juce::Time::getMillisecondCounter() - began) < 15000)
        {
            if (mixerChainBusy())
            {
                const auto token = mixerChainReadback()["state"]["activeOperation"].toString();
                auto cancel = juce::var(new juce::DynamicObject);
                cancel.getDynamicObject()->setProperty("action", "cancel");
                cancel.getDynamicObject()->setProperty("operation", token);
                (void)mixerChainOperation(cancel);
            }
            timerCallback();
            juce::Thread::sleep(1); // CONTROL only, no callbacks driven.
        }
    };
    try
    {
        std::vector<juce::PluginDescription> types;
        check(PluginCatalog::scanOne(binary, types).wasOk() && types.size() == 1 &&
                  types.front().name == "VibeRa Parameter Fixture" && !types.front().isInstrument,
              "Use exact scanned original SDK effect, no synthetic descriptor");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.patterns.front().notes.clear();
        const auto targetId = project.allocate(), unrelatedId = project.allocate();
        project.mixer[1].effects.push_back(
            {targetId, types.front().name, types.front().createXml()->toString(), {}, false, false});
        project.mixer[1].effects.front().wet = .375f;
        project.mixer[1].effectsBypassed = true;
        project.mixer[2].effects.push_back(
            {unrelatedId, types.front().name, types.front().createXml()->toString(), {}, false, true});
        check(project.validate().wasOk(), "Valid independent ordered source chains");
        controller.replace(std::move(project));
        selectedPattern = controller.project.patterns.front().id;
        selectedChannel = controller.project.channels.front().id;
        auto &manager = audio.engine.getDeviceManager();
        check(!manager.deviceManager.getCurrentAudioDevice(), "Refuse existing device; no hardware access");
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        auto *const device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        check(device != nullptr, "Only original in-memory device active");
        const auto preparationBegan = juce::Time::getMillisecondCounter();
        auto ready = juce::Result::fail("Not prepared");
        do
        {
            ready = audio.rebuild(controller.project, patternMode, selectedPattern);
            if (ready.failed())
                dispatchMessages(5);
        } while (ready.failed() &&
                 juce::uint32(juce::Time::getMillisecondCounter() - preparationBegan) < 30000);
        check(ready.wasOk(), "Actual native graph projection prepared");
        needsRebuild = false;
        const auto target = audio.preparedPluginHost(controller.project, targetId);
        const auto other = audio.preparedPluginHost(controller.project, unrelatedId);
        check(target && other && target != other && target->alive() && other->alive(),
              "Two independent actual native workers");
        juce::String hashes;
        for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                                 pluginWorkerExecutable(), binary})
            hashes += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
        check(folder.getChildFile("identities.txt").replaceWithText(hashes),
              "Retain exact app/worker/plugin input hashes");
        auto catalogue = [&](PluginHost &host)
        {
            PluginParameterCatalogue meta;
            const auto metadataBegan = juce::Time::getMillisecondCounter();
            while (juce::uint32(juce::Time::getMillisecondCounter() - metadataBegan) < 10000)
            {
                host.service();
                if (host.readParameterCatalogue(meta) && !meta.error)
                    return meta;
                juce::Thread::sleep(1);
            }
            throw std::runtime_error("Original native catalogue did not complete");
        };
        auto setGain = [&](PluginHost &host, float gain)
        {
            const auto meta = catalogue(host);
            const auto request = host.setParameter(0, gain, meta.generation);
            check(request != 0, "Real native parameter write accepted");
            PluginParameterResult observed;
            const auto parameterBegan = juce::Time::getMillisecondCounter();
            while (!host.readParameterResult(request, observed) &&
                   juce::uint32(juce::Time::getMillisecondCounter() - parameterBegan) < 5000)
            {
                host.service();
                juce::Thread::sleep(1);
            }
            check(observed.serial == request && observed.result == 0 && observed.value == gain,
                  "Exact native parameter acknowledgement");
        };
        setGain(*target, .25f);
        setGain(*other, .75f);
        // Settle already-performed native edits BEFORE recording nonmutating-workflow baseline.
        if (audio.consumePluginChanges())
            controller.markExternalChange();
        const auto before = controller.project;
        const auto identity = controller.projectIdentity;
        const auto revision = controller.revision;
        const auto dirty = controller.dirty;
        const auto undo = controller.undo.getNumActionsInCurrentTransaction();
        const bool canUndo = controller.undo.canUndo(), canRedo = controller.undo.canRedo();
        const auto targetPid = target->processId(), otherPid = other->processId();
        const auto targetReset = target->midiResetGeneration(), otherReset = other->midiResetGeneration();
        const auto position = audio.positionBeat();
        auto stable = [&]
        {
            check(controller.project.state().isEquivalentTo(before.state()) &&
                      controller.projectIdentity == identity && controller.revision == revision &&
                      controller.dirty == dirty && controller.undo.canUndo() == canUndo &&
                      controller.undo.canRedo() == canRedo &&
                      controller.undo.getNumActionsInCurrentTransaction() == undo,
                  "Copy/save/load preserve exact Project, revision, dirty and Undo/Redo");
            check(audio.parameterHost(targetId) == target && audio.parameterHost(unrelatedId) == other &&
                      target->processId() == targetPid && other->processId() == otherPid &&
                      target->midiResetGeneration() == targetReset &&
                      other->midiResetGeneration() == otherReset &&
                      manager.deviceManager.getCurrentAudioDevice() == device &&
                      audio.positionBeat() == position && !needsRebuild,
                  "No worker/device/transport/reset/rebuild change from chain workflow");
        };
        auto call = [&](juce::var args)
        {
            auto result = mcpCall("vibera_mixerchain", args)["structuredContent"];
            auto entry = juce::var(new juce::DynamicObject);
            entry.getDynamicObject()->setProperty("request", args);
            entry.getDynamicObject()->setProperty("result", result);
            observations.add(entry);
            check(persist(false, "Acceptance still running"), "Retain actual typed result before checking");
            return result;
        };
        auto request = [&](const char *action, int insert = 1)
        {
            auto args = juce::var(new juce::DynamicObject);
            args.getDynamicObject()->setProperty("action", action);
            if (juce::String(action) == "copy" || juce::String(action) == "save")
            {
                args.getDynamicObject()->setProperty("mixer", insert);
                args.getDynamicObject()->setProperty("projectIdentity", controller.projectIdentity);
                args.getDynamicObject()->setProperty("revision", juce::int64(controller.revision));
            }
            return args;
        };
        auto finish = [&](const juce::String &operation)
        {
            const auto began = juce::Time::getMillisecondCounter();
            auto args = request("job");
            args.getDynamicObject()->setProperty("operation", operation);
            juce::var observed;
            do
            {
                timerCallback();
                observed = mcpCall("vibera_mixerchain", args)["structuredContent"];
                if (observed["completionState"].toString() != "pending")
                    break;
                juce::Thread::sleep(1);
            } while (juce::uint32(juce::Time::getMillisecondCounter() - began) < 15000);
            auto record = juce::var(new juce::DynamicObject);
            record.getDynamicObject()->setProperty("result", observed);
            observations.add(record);
            check(persist(false, "Terminal observation retained before assertion"),
                  "Retain terminal outcomes");
            check(observed["completionState"].toString() != "pending",
                  "Actual job reached terminal inside CONTROL budget");
            check(observed["message"].toString().isNotEmpty() && status == observed["message"].toString(),
                  "Visible status matches the actual terminal chain outcome, never stale capture/pending");
            return observed;
        };
        const auto tools = mcpTools();
        check(std::any_of(tools.getArray()->begin(), tools.getArray()->end(),
                          [](const auto &item)
                          {
                              return item["name"].toString() == "vibera_mixerchain" &&
                                     item["inputSchema"]["properties"]["action"].isObject();
                          }),
              "Actual discovery exposes authoritative tool/schema");
        const auto presets = folder.getChildFile("presets"), alternate = folder.getChildFile("alternate");
        check(!presets.exists() && !alternate.exists() && presets.createDirectory().wasOk() &&
                  alternate.createDirectory().wasOk(),
              "Fresh owned local preset directories");
        auto configure = request("configureDirectory");
        configure.getDynamicObject()->setProperty("directory", presets.getFullPathName());
        check(call(configure)["completionState"].toString() == "completed", "Explicit directory configured");
        // UI semantic action and MCP use the same canonical start path.
        selectedInsert = 1;
        check(performAction("mixer.chain.copy"), "Real UI semantic Copy action is handled");
        auto pending = mixerChainReadback();
        const auto copyToken = pending["state"]["activeOperation"].toString();
        check(copyToken.isNotEmpty() && !mixerChainClipboardData() && bool(pending["state"]["busy"]),
              "Accepted Copy is pending, not fabricated clipboard completion");
        auto changedRoot = configure.clone();
        changedRoot.getDynamicObject()->setProperty("directory", alternate.getFullPathName());
        check(call(changedRoot)["code"].toString() == "effectChain.busy",
              "Active capture refuses root reconfiguration");
        const auto copied = finish(copyToken);
        check(copied["completionState"].toString() == "completed" && copied["provenance"].size() == 1 &&
                  copied["provenance"][0]["plugin"] == juce::var(juce::int64(targetId)) &&
                  copied["provenance"][0]["instance"].toString() == target->runtimeIdentity() &&
                  copied["provenance"][0]["stateSource"].toString() == "nativeAcknowledgement",
              "Exact target-only native capture provenance");
        const auto *clipboard = mixerChainClipboardData();
        check(clipboard && clipboard->chain.effects.size() == 1 && clipboard->chain.effectsBypassed &&
                  clipboard->chain.effects.front().wet == .375f &&
                  clipboard->chain.effects.front().state.isNotEmpty() &&
                  !audio.capturedPluginRevision(unrelatedId, other).has_value(),
              "Fresh opaque target state, portable mix fields, unrelated worker was not captured");
        const auto copiedChain = clipboard->chain;
        stable();
        const auto file = presets.getChildFile("target.vibfx");
        auto saveArgs = request("save");
        saveArgs.getDynamicObject()->setProperty("path", file.getFullPathName());
        const auto acceptedSave = call(saveArgs);
        check(acceptedSave["completionState"].toString() == "pending" && !file.exists(),
              "Save capture admission is not completed I/O");
        const auto saved = finish(acceptedSave["operation"].toString());
        check(saved["completionState"].toString() == "completed" && bool(saved["fileCommitted"]) &&
                  file.existsAsFile(),
              "Actual atomic preset commit completed");
        juce::MemoryBlock bytes;
        check(file.loadFileAsData(bytes), "Read genuine committed preset bytes");
        std::unique_ptr<effect_chain::Chain> decoded;
        check(
            effect_chain::decode({static_cast<const std::byte *>(bytes.getData()), bytes.getSize()}, decoded)
                    .wasOk() &&
                decoded && *decoded == copiedChain,
            "Exact codec round-trip preserves native state and mix fields");
        // Reopen an independent real SDK worker with portable bytes; no exact-state substring assertion.
        PluginHost reopened;
        const auto &slot = decoded->effects.front();
        PluginSpec spec{targetId, slot.name, slot.descriptor, slot.state, false, false};
        check(reopened.start(spec, 48000., 256, true).wasOk(),
              "Real original SDK reopens saved opaque state");
        const auto reopenedCatalogue = catalogue(reopened);
        const auto gain =
            std::find_if(reopenedCatalogue.parameters.begin(), reopenedCatalogue.parameters.end(),
                         [](const auto &p) { return p.id == 0; });
        check(gain != reopenedCatalogue.parameters.end() && gain->value == .25f,
              "Reopened native parameter exactly matches acknowledged original value");
        const auto otherCopy = call(request("copy", 2));
        check(finish(otherCopy["operation"].toString())["completionState"].toString() == "completed",
              "Independent other chain copied");
        check(mixerChainClipboardData()->chain != copiedChain,
              "Load has a distinguishable previous clipboard");
        auto load = request("loadClipboard");
        load.getDynamicObject()->setProperty("path", file.getFullPathName());
        const auto pendingLoad = call(load);
        check(pendingLoad["completionState"].toString() == "pending",
              "Background load admission remains pending");
        check(finish(pendingLoad["operation"].toString())["completionState"].toString() == "completed" &&
                  mixerChainClipboardData()->chain == copiedChain,
              "Actual file load publishes clipboard only");
        stable();
        const auto duplicate = call(saveArgs);
        const auto overwriteRefused = finish(duplicate["operation"].toString());
        check(overwriteRefused["completionState"].toString() == "failed" &&
                  overwriteRefused["code"].toString() == "chain.file.outputExists",
              "Default no-overwrite failure is exact and terminal");
        juce::MemoryBlock unchanged;
        check(file.loadFileAsData(unchanged) && unchanged == bytes,
              "Overwrite refusal preserves actual file bytes");
        saveArgs.getDynamicObject()->setProperty("overwrite", true);
        check(finish(call(saveArgs)["operation"].toString())["completionState"].toString() == "completed",
              "Explicit overwrite commits through actual job");
        const auto malformed = presets.getChildFile("malformed.vibfx");
        check(malformed.replaceWithText("Not a VibeRaFX chain"), "Owned malformed bytes retained");
        auto badLoad = request("loadClipboard");
        badLoad.getDynamicObject()->setProperty("path", malformed.getFullPathName());
        check(finish(call(badLoad)["operation"].toString())["completionState"].toString() == "failed" &&
                  mixerChainClipboardData()->chain == copiedChain,
              "Malformed load fails preserving clipboard");
        const auto cancelPending = call(request("copy"));
        auto cancel = request("cancel");
        cancel.getDynamicObject()->setProperty("operation", cancelPending["operation"]);
        check(call(cancel)["completionState"].toString() == "pending" && audio.pluginStateCapturePending(),
              "Cancel retains owned capture until real drain");
        check(finish(cancelPending["operation"].toString())["completionState"].toString() == "cancelled" &&
                  mixerChainClipboardData()->chain == copiedChain,
              "Capture cancellation publishes no clipboard replacement");
        auto unknown = request("job");
        unknown.getDynamicObject()->setProperty("operation", "not-a-session-job");
        check(call(unknown)["code"].toString() == "effectChain.unknownOperation", "Unknown job is typed");
        auto invalid = configure.clone();
        invalid.getDynamicObject()->setProperty("directory", folder.getChildFile("absent").getFullPathName());
        check(call(invalid)["completionState"].toString() == "failed" &&
                  mixerChainReadback()["state"]["directory"].toString() == presets.getFullPathName(),
              "Invalid reconfiguration preserves old root");
        check(call(changedRoot)["completionState"].toString() == "completed" &&
                  mixerChainReadback(copyToken)["completionState"].toString() == "completed",
              "Successful root reconfiguration preserves compact terminal history");
        stable();
        // Real component pixels from this unchanged two-chain fixture. No desktop
        // capture, musical arrangement, playback PCM or popup-ownership claim.
        const juce::Point<int> previousSize{getWidth(), getHeight()};
        const auto previousMaximised = maximisedEditor;
        const auto previousInsert = selectedInsert;
        maximisedEditor = 3;
        selectedInsert = 1;
        juce::Array<juce::var> snapshots;
        struct View
        {
            const char *file;
            int width, height;
            float scale;
        };
        for (const auto view : {View{"mixer-chain-1920x1044-100.png", 1920, 1044, 1.f},
                                View{"mixer-chain-2560x1392-150.png", 2560, 1392, 1.5f}})
        {
            setSize(view.width, view.height);
            resized();
            refresh();
            check(maximisedEditor == 3 && !workspace.isEmpty(),
                  "Actual Mixer workspace is maximised before component capture");
            const auto image = createComponentSnapshot(getLocalBounds(), true, view.scale);
            const auto expectedWidth = juce::roundToInt(view.width * view.scale);
            const auto expectedHeight = juce::roundToInt(view.height * view.scale);
            check(image.isValid() && image.getWidth() == expectedWidth && image.getHeight() == expectedHeight,
                  "Real component image has independently specified logical/scaled dimensions");
            const auto destination = folder.getChildFile(view.file);
            auto stream = destination.createOutputStream();
            check(stream && stream->setPosition(0) && stream->truncate().wasOk(),
                  "Owned PNG stream opens and truncates instead of appending");
            check(juce::PNGImageFormat{}.writeImageToStream(image, *stream),
                  "Encode actual Mixer component PNG");
            stream->flush();
            check(stream->getStatus().wasOk(), "Actual PNG final flush succeeds");
            stream.reset();
            auto row = juce::var(new juce::DynamicObject);
            auto add = [&](const char *key, juce::var value)
            { row.getDynamicObject()->setProperty(key, std::move(value)); };
            add("path", destination.getFullPathName());
            add("logicalWidth", view.width);
            add("logicalHeight", view.height);
            add("scale", view.scale);
            add("pixelWidth", image.getWidth());
            add("pixelHeight", image.getHeight());
            add("sha256", juce::SHA256(destination).toHexString());
            add("scope", "Refreshed real Studio/Mixer component; two original SDK chains, empty score; no "
                         "desktop/input/audio/popup evidence");
            snapshots.add(row);
            put("componentSnapshots", snapshots);
            check(persist(false, "Component pixels retained; remaining source-staleness assertion pending"),
                  "Persist actual snapshot identities before subsequent assertions");
        }
        maximisedEditor = previousMaximised;
        selectedInsert = previousInsert;
        setSize(previousSize.x, previousSize.y);
        resized();
        refresh();
        stable();
        // Genuine source-session replacement while native capture is outstanding.
        const auto stale = call(request("copy"));
        controller.replace(controller.project);
        const auto replacement = controller.projectIdentity;
        check(replacement != identity, "Canonical replace produced a genuinely different session");
        const auto staleOutcome = finish(stale["operation"].toString());
        check(staleOutcome["completionState"].toString() == "failed" &&
                  staleOutcome["code"].toString() == "effectChain.staleCapture" &&
                  mixerChainClipboardData()->chain == copiedChain &&
                  controller.projectIdentity == replacement &&
                  controller.project.state().isEquivalentTo(before.state()),
              "Stale capture fails without replacing clipboard or modifying canonical replacement");
        check(persist(true, {}), "Retain final actual workflow evidence");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        try
        {
            drain();
        }
        catch (...)
        {
        }
        (void)persist(false, error.what());
        return juce::Result::fail(error.what());
    }
    catch (...)
    {
        try
        {
            drain();
        }
        catch (...)
        {
        }
        (void)persist(false, "Unexpected chain workflow exception");
        return juce::Result::fail("Unexpected chain workflow exception");
    }
}
} // namespace vibera
