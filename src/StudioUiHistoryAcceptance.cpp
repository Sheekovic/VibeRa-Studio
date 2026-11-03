// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual-Studio component acceptance. No OS input, audio callback,
// physical device, installed plugin or global allocation-fault injection.
#include "Studio.h"
#include "Editors.h"
#include "InstrumentWindow.h"
#include "../tests/SyntheticDevice.h" // Existing bounded message-dispatch helper only.
#include <functional>
#include <stdexcept>
#include <tuple>

namespace vibera
{
namespace
{
struct OriginalUiHistoryBlocker final : juce::UndoableAction
{
    int &calls;
    explicit OriginalUiHistoryBlocker(int &counter) : calls(counter) {}
    bool perform() override
    {
        ++calls;
        return true;
    }
    bool undo() override
    {
        ++calls;
        return false;
    }
    int getSizeInUnits() override
    {
        return 1;
    }
};
struct UiHistoryFrame
{
    juce::String project, identity, owner, undo, redo;
    std::uint64_t revision = 0, serial = 0;
    bool gesture = false, dirty = false;
    int units = 0;
    std::vector<std::uintptr_t> undoActions, redoActions;
    bool operator==(const UiHistoryFrame &) const = default;
};
UiHistoryFrame frame(const Controller &controller)
{
    UiHistoryFrame result{controller.project.state().toXmlString(),
                          controller.projectIdentity,
                          controller.gestureOwner(),
                          controller.undo.getUndoDescriptions().joinIntoString("|"),
                          controller.undo.getRedoDescriptions().joinIntoString("|"),
                          controller.revision,
                          controller.gestureSerial(),
                          controller.hasGesture(),
                          controller.dirty,
                          controller.undo.getNumberOfUnitsTakenUpByStoredCommands(),
                          {},
                          {}};
    juce::Array<const juce::UndoableAction *> actions;
    controller.undo.getActionsInUndoTransaction(actions);
    for (auto *action : actions)
        result.undoActions.push_back(reinterpret_cast<std::uintptr_t>(action));
    actions.clear();
    controller.undo.getActionsInRedoTransaction(actions);
    for (auto *action : actions)
        result.redoActions.push_back(reinterpret_cast<std::uintptr_t>(action));
    return result;
}
} // namespace

juce::Result Studio::verifyUiHistoryRecovery(const juce::File &folder)
{
    if (!controllerTestMode || !pluginTestSettings || !workflowTestSettings ||
        !bool(getProperties()["viberaUiHistoryRecoveryFixture"]) ||
        getProperties()["viberaUiHistoryRecoveryOutput"].toString() != folder.getFullPathName())
        return juce::Result::fail(
            "UI history acceptance requires an explicitly owned disposable test Studio");
    const auto output = folder.getChildFile("ui-history-studio-verification.json");
    if (output.exists())
        return juce::Result::fail(
            "UI history receipt already exists; retain it and use a fresh output folder");
    if (auto result = folder.createDirectory(); result.failed())
        return result;
    if (midiTaking || recordArmed || afterPluginState || pendingExport || audio.rendering())
        return juce::Result::fail("UI history fixture requires idle recording, capture and export");

    const auto started = juce::Time::getMillisecondCounterHiRes();
    const bool timerWasRunning = isTimerRunning();
    stopTimer();
    int foreignCalls = 0;
    juce::String currentCase;
    juce::StringArray passed;
    juce::Array<juce::var> observations;
    auto check = [](bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    };
    auto pump = [] { dispatchMessages(20); };
    auto quantizerContent = [&]() -> juce::Component *
    {
        if (auto *window = dynamic_cast<juce::DialogWindow *>(quantizerWindow.getComponent()))
            return window->getContentComponent();
        return nullptr;
    };
    auto quantizerClick = [&](const char *id)
    {
        auto *view = quantizerContent();
        auto *button = view ? dynamic_cast<juce::Button *>(view->findChildWithID(id)) : nullptr;
        check(button && bool(button->onClick), "Actual Quantizer button is missing");
        button->onClick();
    };
    auto closeQuantizer = [&]
    {
        if (auto *view = quantizerContent())
            if (auto *button = dynamic_cast<juce::Button *>(view->findChildWithID("quantizer.cancel"));
                button && button->onClick)
                button->onClick();
        pump();
    };
    // These histories and windows belong only to this explicitly test-mode Studio.
    // Clearing foreign fixture history is NOT a product recovery operation.
    struct Cleanup
    {
        std::function<void()> fn;
        ~Cleanup()
        {
            try
            {
                fn();
            }
            catch (...)
            {
            }
        }
    } cleanup{[&]
              {
                  closeQuantizer();
                  (void)controller.cancelGestureChecked();
                  instrumentWindows.clear();
                  controller.undo.clearUndoHistory(); // Foreign action borrows foreignCalls until this point.
                  if (timerWasRunning)
                      startTimerHz(20);
              }};
    auto observe = [&](const char *stage)
    {
        auto *entry = new juce::DynamicObject;
        entry->setProperty("case", currentCase);
        entry->setProperty("stage", stage);
        entry->setProperty("status", status);
        entry->setProperty("gesture", controller.hasGesture());
        entry->setProperty("owner", controller.gestureOwner());
        entry->setProperty("serial", juce::int64(controller.gestureSerial()));
        entry->setProperty("revision", juce::int64(controller.revision));
        entry->setProperty("projectState", controller.project.state().toXmlString());
        entry->setProperty("foreignCalls", foreignCalls);
        entry->setProperty("undo", controller.undo.getUndoDescriptions().joinIntoString("|"));
        entry->setProperty("redo", controller.undo.getRedoDescriptions().joinIntoString("|"));
        entry->setProperty("quantizer", quantizerPreviewState);
        observations.add(juce::var(entry));
    };
    auto receipt = [&](bool success, const juce::String &failure)
    {
        auto *result = new juce::DynamicObject;
        result->setProperty("passed", success);
        result->setProperty("failure", failure);
        result->setProperty("failedOrCurrentCase", currentCase);
        result->setProperty(
            "evidenceKind",
            "Actual Studio/Controller and JUCE component handlers; genuine UndoManager admission refusal");
        result->setProperty("nativeOsInput", false);
        result->setProperty("audioCallbacks", false);
        result->setProperty("physicalMidiOrAudio", false);
        result->setProperty("listening", false);
        result->setProperty("allocationFaultInjection", false);
        result->setProperty("elapsedMs", juce::Time::getMillisecondCounterHiRes() - started);
        juce::Array<juce::var> names;
        for (const auto &name : passed)
            names.add(name);
        result->setProperty("passedCases", names);
        result->setProperty("observations", observations);
        return output.replaceWithText(juce::JSON::toString(juce::var(result), true));
    };
    try
    {
        customization = Customization::familiarDefaults();
        setSize(1600, 940);
        const auto boundaryProject = folder.getChildFile("original-ui-boundary.vibra");
        const auto boundaryWav = folder.getChildFile("original-ui-boundary.wav");
        auto originalProject = Project::empty();
        originalProject.name = "Original valid UI boundary guard fixture";
        check(!boundaryProject.exists() && saveProject(originalProject, boundaryProject).wasOk(),
              "Boundary guard requires a fresh genuine serialized Project");
        Project reopenedProject;
        check(loadProject(boundaryProject, reopenedProject).wasOk() &&
                  originalProject.state().isEquivalentTo(reopenedProject.state()),
              "Boundary Project must actually reopen before it is used for a refusal test");
        check(!boundaryWav.exists(), "Boundary WAV must not overwrite an earlier fixture");
        {
            auto stream = boundaryWav.createOutputStream();
            check(stream != nullptr, "Original boundary PCM WAV output stream");
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.release(), 48000, 1, 16, {}, 0));
            check(writer != nullptr, "Original boundary PCM WAV writer");
            juce::AudioBuffer<float> pcm(1, 480);
            for (int i = 0; i < 480; ++i)
                pcm.setSample(0, i, i % 2 ? -.125f : .125f);
            check(writer->writeFromAudioSampleBuffer(pcm, 0, 480), "Original boundary PCM WAV samples write");
        }
        {
            juce::WavAudioFormat format;
            auto stream = boundaryWav.createInputStream();
            check(stream != nullptr, "Original boundary PCM WAV reader input");
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
            check(reader && reader->lengthInSamples == 480 && reader->sampleRate == 48000 &&
                      reader->numChannels == 1,
                  "Boundary export guard must use a genuine valid original mono WAV");
        }
        juce::MemoryBlock projectBytes, wavBytes;
        check(boundaryProject.loadFileAsData(projectBytes) && boundaryWav.loadFileAsData(wavBytes),
              "Boundary fixture exact original byte identities are retained");
        auto unchangedBoundaryBytes = [&]
        {
            juce::MemoryBlock currentProject, currentWav;
            return boundaryProject.loadFileAsData(currentProject) && currentProject == projectBytes &&
                   boundaryWav.loadFileAsData(currentWav) && currentWav == wavBytes;
        };
        auto prepare = [&](bool melodic)
        {
            closeQuantizer();
            check(!quantizerContent(), "Previous Quantizer must finish before fixture reset");
            check(controller.cancelGestureChecked().wasOk(), "Previous fixture gesture cancels");
            instrumentWindows.clear();
            controller.undo.clearUndoHistory();
            foreignCalls = 0;
            auto project = Project::empty();
            project.name = "Original UI history admission fixture";
            auto &part = project.patterns.front();
            const auto target = project.channels.front().id;
            if (melodic)
                part.notes = {{project.allocate(), target, .08, .34, 60, .7f, false},
                              {project.allocate(), target, .61, .29, 64, .6f, false}};
            else
                part.notes = {{project.allocate(), target, 0, .25, 60, .2f, true},
                              {project.allocate(), target, .25, .25, 60, .6f, true}};
            controller.replace(project);
            projectFile = {};
            selectedPattern = project.patterns.front().id;
            selectedChannel = target;
            selectedChannels = {target};
            selectedNotes.clear();
            selectedClips.clear();
            focus = melodic ? Scope::piano : Scope::rack;
            snap = .25;
            refresh();
            resized();
            check(controller.project.validate().wasOk() && !controller.hasGesture() &&
                      !controller.undo.canUndo() && !controller.undo.canRedo(),
                  "Original fixture must begin valid and history-free");
        };
        auto blocker = [&]
        {
            controller.undo.beginNewTransaction("Original foreign UI admission blocker");
            check(controller.undo.perform(new OriginalUiHistoryBlocker(foreignCalls)) && foreignCalls == 1,
                  "Genuine foreign history fixture must actually perform once");
            observe("before-completion-with-foreign-blocker");
        };
        auto removeBlocker = [&]
        {
            juce::Array<const juce::UndoableAction *> actions;
            controller.undo.getActionsInUndoTransaction(actions);
            check(controller.undo.getUndoDescriptions().size() == 1 && actions.size() == 1 &&
                      dynamic_cast<const OriginalUiHistoryBlocker *>(actions.getFirst()) && foreignCalls == 1,
                  "Retry may clear only this test fixture's genuine foreign action");
            controller.undo.clearUndoHistory();
        };
        auto refused = [&](const UiHistoryFrame &expected)
        {
            observe("after-refusal");
            check(status.startsWith("history.unsupportedTransaction:") && frame(controller) == expected &&
                      foreignCalls == 1,
                  "Refused history must retain exact model, cursor, owner, serial, revision and foreign "
                  "action");
        };
        auto roundTrip = [&](const juce::ValueTree &before, const juce::ValueTree &after)
        {
            check(!controller.hasGesture() && controller.undo.getUndoDescriptions().size() == 1,
                  "Successful retry must produce one actual history transaction");
            check(bool(undoButton.onClick) && bool(redoButton.onClick),
                  "Actual toolbar history handlers exist");
            undoButton.onClick();
            check(controller.project.state().isEquivalentTo(before) && !controller.undo.canUndo() &&
                      controller.undo.canRedo(),
                  "One actual toolbar Undo restores exact Project and allocator");
            redoButton.onClick();
            check(controller.project.state().isEquivalentTo(after) && controller.undo.canUndo() &&
                      !controller.undo.canRedo(),
                  "Actual toolbar Redo restores exact successful preview");
        };
        auto finishCase = [&]
        {
            observe("completed");
            passed.add(currentCase);
        };

        auto *rack = static_cast<Editor *>(windows[0]->getContentComponent());
        check(rack, "Actual Rack editor exists");
        auto velocityStart = [&]
        {
            setRackGraphVisible(true);
            auto *graph = rack->rackVelocityGraph();
            check(graph, "Actual velocity graph exists");
            const auto area = graph->chart();
            const juce::Point<float> point{float(graph->left + (.5 - graph->offset) * graph->width),
                                           area.getBottom() - .45f * area.getHeight()};
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), point,
                                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0,
                                        0, 0, graph, graph, now, point, now, 1, false);
            graph->mouseDown(down);
            check(graph->ownsGesture(), "Actual velocity mouse handler acquires its controller gesture");
            return std::pair{graph, down};
        };
        for (bool cancel : {false, true})
        {
            currentCase =
                cancel ? "velocity-failed-end-Escape" : "velocity-failed-end-command-refusal-retry-history";
            prepare(false);
            const auto before = controller.project.state();
            auto [graph, event] = velocityStart();
            const auto after = controller.project.state();
            check(!after.isEquivalentTo(before), "Original velocity handler changes genuine musical data");
            blocker();
            const auto pending = frame(controller);
            graph->mouseUp(event);
            refused(pending);
            check(graph->ownsGesture(), "Refused velocity completion retains graph ownership");
            CommandArgs unrelated;
            unrelated.channel = selectedChannel;
            unrelated.value = -.33;
            command("channel.pan", unrelated);
            refused(pending);
            check(graph->ownsGesture(), "Unrelated Studio command cannot release failed velocity owner");
            if (cancel)
            {
                check(performAction("selection.cancel"), "Actual Escape action dispatches");
                check(!controller.hasGesture() && !graph->ownsGesture() &&
                          controller.project.state().isEquivalentTo(before) &&
                          controller.undo.getUndoDescriptions().joinIntoString("|") == pending.undo &&
                          foreignCalls == 1,
                      "Velocity Escape restores exact baseline without consuming foreign history");
            }
            else
            {
                removeBlocker(); // Remove only the fixture blocker, then use real retry.
                graph->mouseUp(event);
                check(!graph->ownsGesture(), "Successful velocity retry releases ownership");
                roundTrip(before, after);
            }
            finishCase();
        }
        for (bool cancel : {false, true})
        {
            currentCase =
                cancel ? "swing-failed-end-Escape" : "swing-failed-end-command-refusal-retry-history";
            prepare(false);
            const auto before = controller.project.state();
            auto *swing = rack->rackSwingControl();
            check(swing && swing->onDragStart && swing->onDragEnd, "Actual Swing slider handlers exist");
            swing->onDragStart();
            swing->setValue(65, juce::sendNotificationSync);
            check(swing->gestureActive() && controller.project.swing == .65,
                  "Actual Swing preview changes global amount");
            const auto after = controller.project.state();
            blocker();
            const auto pending = frame(controller);
            swing->onDragEnd();
            refused(pending);
            check(swing->gestureActive(), "Refused Swing end retains slider ownership");
            CommandArgs unrelated;
            unrelated.channel = selectedChannel;
            unrelated.value = .31;
            command("channel.gain", unrelated);
            refused(pending);
            if (cancel)
            {
                check(performAction("selection.cancel"), "Actual Swing Escape action dispatches");
                check(!controller.hasGesture() && !swing->gestureActive() &&
                          controller.project.state().isEquivalentTo(before) &&
                          controller.undo.getUndoDescriptions().joinIntoString("|") == pending.undo &&
                          foreignCalls == 1,
                      "Swing Escape restores exact baseline without consuming foreign history");
            }
            else
            {
                removeBlocker();
                swing->onDragEnd();
                check(!swing->gestureActive(), "Successful Swing retry releases slider ownership");
                roundTrip(before, after);
            }
            finishCase();
        }
        for (bool cancel : {false, true})
        {
            currentCase = cancel ? "quantizer-failed-Accept-Cancel"
                                 : "quantizer-failed-Accept-retains-modal-retry-history";
            prepare(true);
            const auto before = controller.project.state();
            check(performAction("notes.quantize"), "Actual Quantizer action dispatches");
            auto *originalView = quantizerContent();
            auto *originalWindow = dynamic_cast<juce::DialogWindow *>(quantizerWindow.getComponent());
            check(originalView && originalWindow && originalWindow->isVisible() &&
                      originalWindow->isCurrentlyModal() &&
                      controller.gestureOwner().startsWith("quantizer-") &&
                      quantizerPreviewState["completionState"].toString() == "pending",
                  "Real Quantizer owns a visible modal preview");
            const auto after = controller.project.state();
            check(!after.isEquivalentTo(before),
                  "Original off-grid score must produce a genuine quantization preview");
            blocker();
            const auto pending = frame(controller);
            quantizerClick("quantizer.accept");
            refused(pending);
            check(quantizerContent() == originalView && quantizerWindow.getComponent() == originalWindow &&
                      originalWindow->isVisible() && originalWindow->isCurrentlyModal() &&
                      quantizerPreviewState["completionState"].toString() == "pending" &&
                      quantizerPreviewState["error"]["code"].toString() == "history_refused" &&
                      quantizerPreviewState["error"]["message"].toString().startsWith(
                          "history.unsupportedTransaction:"),
                  "Failed Accept must retain same modal, owner, baseline and pending error readback");
            if (cancel)
            {
                quantizerClick("quantizer.cancel");
                pump();
                check(!controller.hasGesture() && controller.project.state().isEquivalentTo(before) &&
                          quantizerPreviewState["completionState"].toString() == "cancelled" &&
                          controller.undo.getUndoDescriptions().joinIntoString("|") == pending.undo &&
                          foreignCalls == 1,
                      "Actual Quantizer Cancel restores exact notes without consuming foreign history");
            }
            else
            {
                removeBlocker();
                quantizerClick("quantizer.accept");
                pump();
                check(!quantizerContent() &&
                          quantizerPreviewState["completionState"].toString() == "completed" &&
                          quantizerPreviewState["error"].isVoid(),
                      "Successful Accept retry closes and clears pending error");
                roundTrip(before, after);
            }
            finishCase();
        }

        for (const auto &[id, value] :
             std::initializer_list<std::pair<const char *, double>>{{"instrument.volume", .32},
                                                                    {"instrument.sampler.cents", 125},
                                                                    {"instrument.sampler.maxPoly", 3}})
            for (bool cancel : {false, true})
            {
                currentCase =
                    juce::String(id) + (cancel ? "-failed-end-Escape" : "-failed-end-Return-retry-history");
                prepare(false);
                const auto before = controller.project.state();
                openInstrument(selectedChannel);
                auto found = instrumentWindows.find(selectedChannel);
                check(found != instrumentWindows.end() && found->second,
                      "Actual built-in/sampler settings window opens");
                auto *controls = found->second->controlsComponent();
                auto *slider = dynamic_cast<juce::Slider *>(controls->findChildWithID(id));
                check(slider && slider->isEnabled() && slider->onDragStart && slider->onDragEnd,
                      "Actual enabled sampler control handlers exist");
                slider->onDragStart();
                slider->setValue(value, juce::sendNotificationSync);
                const auto after = controller.project.state();
                check(controller.hasGesture() && !after.isEquivalentTo(before),
                      "Sampler component changes genuine canonical musical settings");
                blocker();
                const auto pending = frame(controller);
                slider->onDragEnd();
                refused(pending);
                check(found->second->isVisible(), "Failed sampler end keeps the instrument window visible");
                // These are real production boundaries, not fake direct model
                // operations. An uncommitted local preview must never be saved
                // or discarded just because it is not a Rack/MIDI gesture.
                const auto oldProjectFile = projectFile;
                auto boundaryRetained = [&]
                {
                    observe("after-sampler-file-boundary-refusal");
                    check(frame(controller) == pending && projectFile == oldProjectFile &&
                              foreignCalls == 1 && unchangedBoundaryBytes() && !afterPluginState &&
                              !pendingExport && !audio.rendering() && found->second->isVisible(),
                          "Refused sampler file boundary must retain exact model/history/owner/serial and "
                          "original files");
                };
                bool saveCompleted = false, saveFailed = false;
                saveTo(boundaryProject,
                       [&](juce::Result result)
                       {
                           saveCompleted = true;
                           saveFailed = result.failed();
                       });
                check(saveCompleted && saveFailed, "Actual saveTo must report failure synchronously before "
                                                   "native capture or file replacement");
                boundaryRetained();
                bool captureCompleted = false, captureFailed = false;
                withPluginState(
                    [&](juce::Result result, Project)
                    {
                        captureCompleted = true;
                        captureFailed = result.failed();
                    });
                check(captureCompleted && captureFailed,
                      "Independent native-state capture must refuse the retained local gesture");
                boundaryRetained();
                check(openProjectFile(boundaryProject).failed(),
                      "Actual openProjectFile must refuse before replacing the retained local baseline");
                boundaryRetained();
                newProject(true);
                check(status.startsWith("history.pendingGesture:"),
                      "Actual newProject must report retained local edit refusal");
                boundaryRetained();
                bool proceeded = false;
                requestClose([&] { proceeded = true; });
                check(!proceeded && status.startsWith("history.pendingGesture:"),
                      "Actual requestClose must not discard or bypass the retained local baseline");
                boundaryRetained();
                exportTo(boundaryWav);
                check(status.startsWith("history.pendingGesture:"),
                      "Actual exportTo must refuse before snapshot/render/overwrite");
                boundaryRetained();
                if (cancel)
                {
                    check(controls->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
                          "Actual Controls Escape handles retained serial after mouse-up");
                    check(!controller.hasGesture() && controller.project.state().isEquivalentTo(before) &&
                              controller.undo.getUndoDescriptions().joinIntoString("|") == pending.undo &&
                              foreignCalls == 1,
                          "Sampler Escape restores exact baseline without consuming foreign history");
                }
                else
                {
                    removeBlocker();
                    check(controls->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)),
                          "Actual Controls Return retries retained completion");
                    roundTrip(before, after);
                }
                finishCase();
            }
        check(passed.size() == 12, "All original component failure scenarios must execute");
        check(receipt(true, {}), "Cannot persist actual UI history verification receipt");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        (void)receipt(false, error.what());
        return juce::Result::fail("UI history recovery: " + currentCase + ": " + error.what());
    }
    catch (...)
    {
        (void)receipt(false, "Unknown exception");
        return juce::Result::fail("UI history recovery: " + currentCase + ": unknown exception");
    }
}
} // namespace vibera
