// SPDX-License-Identifier: AGPL-3.0-or-later
// Actual component handlers/shared commands. No OS input, DSP, hardware or listening proof.
#include <juce_cryptography/juce_cryptography.h>
namespace vibera
{
__declspec(noinline) juce::Result Studio::verifyMixerStereoUi(const juce::File &folder)
{
    juce::StringArray cases;
    juce::Array<juce::var> captures;
    juce::Component::SafePointer<juce::AlertWindow> ownedDialog;
    const auto record = [&](bool passed, const juce::String &error)
    {
        auto *receipt = new juce::DynamicObject;
        receipt->setProperty("passed", passed); receipt->setProperty("failure", error);
        receipt->setProperty("cases", cases); receipt->setProperty("captures", captures);
        receipt->setProperty("nativeOsInput", false);
        receipt->setProperty("dsp", false); receipt->setProperty("listening", false);
        return folder.getChildFile("mixer-stereo-ui.json").replaceWithText(juce::JSON::toString(juce::var(receipt), true));
    };
    const auto check = [](bool ok, const char *message)
    { if (!ok) throw std::runtime_error(message); };
    try
    {
        check(controllerTestMode && folder.createDirectory().wasOk() &&
                  !juce::Component::getCurrentlyModalComponent() && !controller.hasGesture() &&
                  !midiTaking && !hasPendingMidiTake(), "Owned idle component acceptance fixture");
        stopTimer();
        controller.replace(Project::demo());
        setSize(1920, 1044); showEditor(3);
        if (maximisedEditor != 3) maximiseEditor(3);
        refresh();
        check(selectMixerInsert(1).wasOk(), "Actual stable Mixer source selection");
        auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
        const auto originalBounds = editor->getBounds();
        for (int height : {480, 700, 940, 1300})
        {
            editor->setSize(1000, height);
            const auto separation = editor->mixerStereoSeparationBounds(1);
            const auto phase = editor->mixerPhaseInvertBounds(1), swap = editor->mixerStereoSwapBounds(1);
            if (height == 480)
                check(separation.isEmpty() && phase.isEmpty() && swap.isEmpty(),
                      "Compact strips honestly hide inline stereo controls");
            else
                check(!separation.isEmpty() && separation.getY() >= 566 &&
                          phase.getHeight() >= 26 && phase.getWidth() >= 40 && swap.getWidth() >= 40 &&
                          phase.getBottom() <= height - 48 && !phase.intersects(swap) &&
                          !separation.intersects(phase) && !separation.intersects(swap) &&
                          !separation.intersects(editor->mixerGainReadoutBounds(1)) &&
                          !separation.intersects(editor->mixerMeterBounds(1)) &&
                          !swap.intersects(editor->mixerRouteButtonBounds(1)),
                      "Tall logical layouts reserve readable distinct controls below RMS and above route footer");
        }
        editor->setBounds(originalBounds); refresh();
        check(!editor->mixerStereoSeparationBounds(1).isEmpty(), "Maximized1080p fixture exposes actual stereo targets");
        cases.add("Compact/tall logical geometry and independent target separation");
        juce::Point<float> origin;
        auto event = [&](juce::Point<float> point, int modifiers = juce::ModifierKeys::leftButtonModifier,
                         int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, editor, editor, now, origin, now, clicks, true);
        };
        const auto separationValue = [&](int insert = 1) { return mixerInsert(controller.project, insert)->stereoSeparation; };
        const auto start = controller.project.state();
        origin = editor->mixerStereoSeparationBounds(1).getCentre().toFloat();
        editor->mouseDown(event(origin));
        editor->mouseDrag(event(origin.translated(0, -25)));
        check(controller.hasGesture() && controller.gestureOwner().startsWith("ui.mixer.stereo.") &&
                  std::abs(separationValue() - .25f) < 1.e-6 && selectMixerInsert(2).failed() && selectedInsert == 1,
              "Actual owned separation drag and shared selection refusal");
        editor->mouseDrag(event(origin.translated(0, -50)));
        editor->mouseUp(event(origin.translated(0, -50)));
        check(!controller.hasGesture() && std::abs(separationValue() - .5f) < 1.e-6 &&
                  controller.undo.getUndoDescription() == "Mixer stereo separation",
              "Multiple updates finish as one real transaction");
        focus = Scope::mixer;
        check(keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0)) &&
                  start.isEquivalentTo(controller.project.state()), "Actual contextual Ctrl+Z restores whole drag");
        check(keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)) &&
                  separationValue() == .5f, "Actual contextual Redo restores separation");
        check(controller.undoEditChecked().wasOk(), "Restore baseline for next original scenario");
        const auto undo = controller.undo.getUndoDescriptions(), redo = controller.undo.getRedoDescriptions();
        origin = editor->mixerStereoSeparationBounds(1).getCentre().toFloat();
        editor->mouseDown(event(origin));
        editor->mouseDrag(event(origin.translated(0, -50), juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
        check(std::abs(separationValue() - .05f) < 1.e-6, "Shift drag is fine and incremental");
        editor->cancelPointerGesture();
        check(start.isEquivalentTo(controller.project.state()) && !controller.hasGesture() &&
                  controller.undo.getUndoDescriptions() == undo && controller.undo.getRedoDescriptions() == redo,
              "Checked cancel restores exact document/history without consuming Redo");
        cases.add("Owned drag, fine control, one Undo/Redo and exact cancellation");
        juce::MouseWheelDetails wheel{}; wheel.deltaY = .5f;
        editor->mouseWheelMove(event(origin), wheel);
        check(std::abs(separationValue() - .1f) < 1.e-6, "Actual separation wheel step");
        editor->mouseWheelMove(event(origin, juce::ModifierKeys::shiftModifier), wheel);
        check(std::abs(separationValue() - .11f) < 1.e-6 && controller.undoEditChecked().wasOk() &&
                  std::abs(separationValue() - .1f) < 1.e-6 && controller.undoEditChecked().wasOk() &&
                  separationValue() == 0, "Shift wheel fine step, independent Undo units");
        CommandArgs amount; amount.mixer = 1; amount.value = .625;
        command("mixer.stereoSeparation", amount);
        editor->mouseDown(event(origin, juce::ModifierKeys::leftButtonModifier, 2));
        editor->mouseDoubleClick(event(origin, juce::ModifierKeys::leftButtonModifier, 2));
        editor->mouseUp(event(origin));
        check(separationValue() == 0 && !controller.hasGesture() && controller.undoEditChecked().wasOk() &&
                  separationValue() == .625f, "Double-click restores original stereo as one real Undo");
        cases.add("Wheel/fine wheel/double-click reset route through canonical commands");
        auto openPrecise = [&](int index)
        {
            mixerStereoDialog(index);
            auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
            check(dialog && dialog->getTextEditor("amount"), "Functional exact-value dialog");
            ownedDialog = dialog;
            return dialog;
        };
        auto precise = [&](const juce::String &text, int choice)
        {
            auto *dialog = openPrecise(1);
            dialog->getTextEditor("amount")->setText(text);
            dialog->exitModalState(choice); dispatchMessages(20);
        };
        const auto beforePrecise = controller.project.state();
        const auto preciseRevision = controller.revision;
        for (const auto &text : {".5junk", "nan", "1.01"})
        {
            precise(text, 1);
            check(beforePrecise.isEquivalentTo(controller.project.state()) && controller.revision == preciseRevision,
                  "Strict finite parsing/canonical range refuses invalid typed values without history");
        }
        precise("-.75", 0);
        check(beforePrecise.isEquivalentTo(controller.project.state()), "Exact-value Cancel is no change");
        auto *dialog = openPrecise(1);
        dialog->getTextEditor("amount")->setText("+0.375");
        check(selectMixerInsert(2).wasOk(), "Selection may change while exact dialog retains original target");
        dialog->exitModalState(1); dispatchMessages(20);
        check(separationValue(1) == .375f && separationValue(2) == 0 && selectedInsert == 2,
              "Async exact dialog applies original stable insert, never newer selected target");
        dialog = openPrecise(1); dialog->getTextEditor("amount")->setText(".875");
        CommandArgs gain; gain.mixer = 2; gain.value = .413;
        command("mixer.gain", gain);
        const auto newer = controller.project.state(); const auto newerRevision = controller.revision;
        dialog->exitModalState(1); dispatchMessages(20);
        check(newer.isEquivalentTo(controller.project.state()) && controller.revision == newerRevision,
              "Newer project revision cancels asynchronous typed intent without reverting newer work");
        cases.add("Precise finite/range validation, cancel and stable/stale async target");
        check(selectMixerInsert(1).wasOk(), "Restore source for icon controls");
        origin = editor->mixerPhaseInvertBounds(1).getCentre().toFloat();
        check(editor->mixerStereoTooltipAt(origin).contains("both"), "Polarity icon has honest descriptive tooltip");
        editor->mouseDown(event(origin)); editor->mouseUp(event(origin));
        check(mixerInsert(controller.project, 1)->phaseInverted && !mixerInsert(controller.project, 1)->stereoSwapped,
              "Separate polarity icon affects only canonical polarity");
        check(controller.undoEditChecked().wasOk() && !mixerInsert(controller.project, 1)->phaseInverted,
              "One polarity Undo");
        origin = editor->mixerStereoSwapBounds(1).getCentre().toFloat();
        check(editor->mixerStereoTooltipAt(origin).contains("left and right"), "Swap icon has honest descriptive tooltip");
        editor->mouseDown(event(origin)); editor->mouseUp(event(origin));
        check(mixerInsert(controller.project, 1)->stereoSwapped && !mixerInsert(controller.project, 1)->phaseInverted,
              "Separate swap icon affects only canonical channel order");
        cases.add("Separate original path icons, targets, state and Undo");
        const auto beforeStale = controller.project.state();
        origin = editor->mixerStereoSeparationBounds(1).getCentre().toFloat();
        editor->mouseDown(event(origin)); editor->mouseDrag(event(origin.translated(0, -20)));
        selectedInsert = 2; // Closed component stale-context injection; supported selection already refused above.
        editor->mouseDrag(event(origin.translated(0, -50))); editor->mouseUp(event(origin));
        check(beforeStale.isEquivalentTo(controller.project.state()) && selectedInsert == 2 && !controller.hasGesture(),
              "Context drift cancels owned baseline and preserves newer selection, never retargets another insert");
        check(selectMixerInsert(1).wasOk(), "Restore source before replacement case");
        editor->mouseDown(event(origin)); editor->mouseDrag(event(origin.translated(0, -10)));
        controller.replace(Project::demo());
        check(controller.beginGesture("foreign-stereo-owner"), "Genuine new-project gesture owner");
        const auto foreign = controller.project.state();
        editor->mouseUp(event(origin));
        check(controller.gestureOwner() == "foreign-stereo-owner" && foreign.isEquivalentTo(controller.project.state()),
              "Late old mouse release preserves unrelated replacement owner/project");
        check(controller.cancelGestureChecked("foreign-stereo-owner").wasOk(), "Release only the owned test gesture");
        cases.add("Stale selection/project/owner cancellation safety");


        // Actual Studio action -> app macro admission -> Customization runner ->
        // canonical commands. This is not vibera_batch or an MCP setup claim.
        const auto savedProfile = juce::JSON::toString(customization.state(), true);
        {
            struct RestoreProfile
            {
                Customization &target;
                Customization saved;
                juce::String &status;
                juce::String oldStatus;
                ~RestoreProfile() noexcept { target = std::move(saved); status.swapWith(oldStatus); }
            } restore{customization, customization, status, status};
            customization = Customization::familiarDefaults(); // Disposable profile; never save it.
            check(selectMixerInsert(1).wasOk(), "Original stable context for actual stereo macro");
            const auto macroPattern = selectedPattern, macroChannel = selectedChannel;
            const auto notesBeforeMacro = selectedNotes, clipsBeforeMacro = selectedClips,
                       channelsBeforeMacro = selectedChannels;
            const auto contextUnchanged = [&]
            {
                return selectedInsert == 1 && selectedPattern == macroPattern && selectedChannel == macroChannel &&
                    selectedNotes == notesBeforeMacro && selectedClips == clipsBeforeMacro && selectedChannels == channelsBeforeMacro;
            };
            const auto rollbackIdentity = [&]
            {
                return controller.project.state().toXmlString() + "/" + juce::String(int(controller.dirty)) + "/" +
                    controller.undo.getUndoDescriptions().joinIntoString("|") + "/" +
                    controller.undo.getRedoDescriptions().joinIntoString("|") + "/" +
                    juce::String(controller.undo.getNumberOfUnitsTakenUpByStoredCommands());
            };
            juce::String macroTrace;
            const auto traceMacro = [&](const char *phase)
            {
                const auto *target = mixerInsert(controller.project, 1);
                macroTrace += juce::String(phase) + " status=" + status + " revision=" +
                    juce::String(juce::int64(controller.revision)) + " gesture=" + juce::String(int(controller.hasGesture())) +
                    " owner=" + controller.gestureOwner() + " profileRunning=" + juce::String(int(customization.running)) +
                    " separation=" + juce::String(target->stereoSeparation, 6) +
                    " invert=" + juce::String(int(target->phaseInverted)) + " swap=" + juce::String(int(target->stereoSwapped)) +
                    " undo=" + controller.undo.getUndoDescriptions().joinIntoString("|") +
                    " redo=" + controller.undo.getRedoDescriptions().joinIntoString("|") + "\n";
                check(folder.getChildFile("mixer-stereo-macros.txt").replaceWithText(macroTrace),
                      "Retain actual Studio/runner macro outcomes before assertions");
            };
            CommandArgs amountStep; amountStep.mixer = 2; amountStep.value = .5;
            CommandArgs switchStep; switchStep.mixer = 2; switchStep.enabled = true;
            CommandArgs lastStep = amountStep; lastStep.value = -.25;
            Macro stereoMacro{"macro.original-stereo-ui-" + juce::Uuid().toString(), "Original stereo macro", {
                {"mixer.stereoSeparation", amountStep, true}, {"mixer.phaseInvert", switchStep, true},
                {"mixer.stereoSwap", switchStep, true}, {"mixer.stereoSeparation", lastStep, true}}};
            check(customization.addMacro(stereoMacro).wasOk(), "Actual Customization-owned stereo command sequence");
            const auto macroBaseline = controller.project.state();
            auto expectedMacro = controller.project;
            auto *expectedTarget = const_cast<MixerInsert *>(mixerInsert(expectedMacro, 1));
            expectedTarget->stereoSeparation = -.25f; expectedTarget->phaseInverted = true; expectedTarget->stereoSwapped = true;
            const auto undoCountBeforeMacro = controller.undo.getUndoDescriptions().size();
            std::vector<std::array<double, 3>> observedOrder;
            observedOrder.reserve(4);
            {
                struct RestoreObserver
                {
                    std::function<void()> &target;
                    std::function<void()> saved;
                    ~RestoreObserver() noexcept { target = std::move(saved); }
                } observer{controller.changed, controller.changed};
                controller.changed = [&]
                {
                    if (controller.hasGesture())
                    {
                        const auto *target = mixerInsert(controller.project, 1);
                        observedOrder.push_back({double(target->stereoSeparation), double(target->phaseInverted), double(target->stereoSwapped)});
                    }
                    if (observer.saved) observer.saved();
                };
                check(performAction(stereoMacro.id), "Actual Studio dispatch recognizes installed macro action");
            }
            traceMacro("actual_studio_success");
            check(status == "Macro complete" && !controller.hasGesture() && !customization.running && contextUnchanged() &&
                      observedOrder == std::vector<std::array<double, 3>>{{.5,0,0},{.5,1,0},{.5,1,1},{-.25,1,1}} &&
                      expectedMacro.state().isEquivalentTo(controller.project.state()) &&
                      controller.undo.getUndoDescriptions().size() == undoCountBeforeMacro + 1 &&
                      controller.undo.getUndoDescription() == stereoMacro.name,
                  "Actual ordered macro resolves poisoned mixer2 args through selected stable1 and records one Undo");
            check(performAction("edit.undo") && macroBaseline.isEquivalentTo(controller.project.state()) && contextUnchanged(),
                  "One actual Studio Undo restores all stereo macro operations and other targets exactly");
            check(performAction("edit.redo") && expectedMacro.state().isEquivalentTo(controller.project.state()) && contextUnchanged(),
                  "One actual Studio Redo restores the complete ordered macro");
            check(performAction("edit.undo") && macroBaseline.isEquivalentTo(controller.project.state()),
                  "Preserve real Redo before failure/cancellation oracles");
            cases.add("Actual Studio macro order/context and one grouped Undo/Redo (not semantic batch)");

            const auto owner = "ui.stereo.macro.owner." + juce::Uuid().toString();
            check(controller.beginGesture(owner), "Genuine independent Controller owner for app macro admission");
            const auto beforeOwned = rollbackIdentity(); const auto ownerRevision = controller.revision;
            juce::String expectedAdmissionCode;
            const auto expectedAdmission = admitMixerChainControl(expectedAdmissionCode);
            check(expectedAdmission.failed() && expectedAdmissionCode == "gesture_owned",
                  "Actual shared macro guard identifies this precise foreign-owner refusal");
            check(performAction(stereoMacro.id), "Studio handles a refused macro action without claiming execution");
            traceMacro("actual_studio_foreign_owner_refusal");
            check(status == expectedAdmission.getErrorMessage() && controller.gestureOwner() == owner && controller.hasGesture() &&
                      beforeOwned == rollbackIdentity() && controller.revision == ownerRevision && contextUnchanged() && !customization.running,
                  "Actual Studio macro admission preserves unrelated owner/document/history before any step");
            check(controller.cancelGestureChecked(owner).wasOk(), "Release only exact fixture owner");

            auto invalidMacro = stereoMacro;
            invalidMacro.id = "macro.original-stereo-invalid-" + juce::Uuid().toString();
            invalidMacro.name = "Original stereo rollback";
            invalidMacro.steps.resize(3);
            invalidMacro.steps[2] = {"mixer.stereoSeparation", amountStep, true};
            invalidMacro.steps[2].parameters.value = 2; // Canonical finite-range failure AFTER two valid steps.
            check(customization.addMacro(invalidMacro).wasOk(), "Registered macro defers target range validation to canonical command");
            const auto beforeFailure = rollbackIdentity();
            check(performAction(invalidMacro.id), "Actual Studio dispatch enters late-failing original macro");
            traceMacro("actual_studio_late_step_failure");
            check(status.startsWith("Step 3:") && beforeFailure == rollbackIdentity() &&
                      !controller.hasGesture() && !customization.running && contextUnchanged(),
                  "Actual late canonical failure cancels earlier stereo previews and preserves exact dirty/history/Redo");
            cases.add("Actual Studio foreign-owner admission and late-step canonical rollback");

            // Studio's synchronous public action has NO cancellation callback.
            // Exercise the supported actual Customization runner cancellation,
            // labeled separately; never present it as Studio/MCP cancel support.
            MacroContext cancelContext{selectedPattern, selectedChannel, selectedInsert, selectedNotes, selectedClips, selectedChannels};
            const auto originalCancelContext = cancelContext;
            const auto beforeCancel = rollbackIdentity();
            int cancelPolls = 0; bool twoStepsActuallyApplied = false;
            const auto cancelled = customization.runMacro(stereoMacro.id, controller, cancelContext, [&]
            {
                if (++cancelPolls != 3) return false;
                const auto *target = mixerInsert(controller.project, 1);
                twoStepsActuallyApplied = controller.hasGesture() && target->stereoSeparation == .5f &&
                    target->phaseInverted && !target->stereoSwapped;
                return true;
            });
            macroTrace += "runnerCancellationResult=" + cancelled.getErrorMessage() + "\n";
            traceMacro("actual_runner_cooperative_cancel");
            check(cancelled.failed() && cancelled.getErrorMessage() == "Macro cancelled" && cancelPolls == 3 &&
                      twoStepsActuallyApplied && beforeCancel == rollbackIdentity() && !controller.hasGesture() && !customization.running &&
                      contextUnchanged() && cancelContext.pattern == originalCancelContext.pattern &&
                      cancelContext.channel == originalCancelContext.channel && cancelContext.insert == originalCancelContext.insert &&
                      cancelContext.notes == originalCancelContext.notes && cancelContext.clips == originalCancelContext.clips &&
                      cancelContext.channels == originalCancelContext.channels,
                  "Supported runner cancellation after two real operations restores exact model/dirty/history/context");
            cases.add("Actual Customization runner cancellation only; public Studio cancellation not exposed");
        }
        check(juce::JSON::toString(customization.state(), true) == savedProfile && !customization.running,
              "Original profile restored exactly; no persisted macro/profile edits");

        // Independent populated original model, painted by the ACTUAL current
        // component. No level/meter/plugin/audio is invented for a screenshot.
        auto populated = Project::demo();
        populated.name = "Stereo targets / component render evidence";
        auto *drums = const_cast<MixerInsert *>(mixerInsert(populated, 1));
        auto *bass = const_cast<MixerInsert *>(mixerInsert(populated, 2));
        check(drums && bass, "Existing stable inserts in populated original demo");
        drums->name = "DRUM BUS"; drums->gain = .75f; drums->pan = -.125f;
        drums->stereoSeparation = .375f; drums->phaseInverted = true; drums->stereoSwapped = true;
        bass->name = "BASS"; bass->gain = .625f; bass->pan = .25f;
        bass->stereoSeparation = -.5f;
        for (std::size_t i = 0; i < populated.channels.size(); ++i)
            populated.channels[i].mixer = i == 3 ? 2 : 1;
        check(populated.validate().wasOk(), "Actual valid musical demo and saved stereo targets");
        controller.replace(std::move(populated));
        check(selectMixerInsert(1).wasOk(), "Actual selected original stereo target for captures");
        showEditor(3);
        if (maximisedEditor != 3) maximiseEditor(3);
        refresh();
        const auto renderedModel = controller.project.state().toXmlString();
        const auto renderedRevision = controller.revision;
        const auto rect = [](juce::Rectangle<int> bounds)
        {
            auto *value = new juce::DynamicObject;
            value->setProperty("x", bounds.getX()); value->setProperty("y", bounds.getY());
            value->setProperty("width", bounds.getWidth()); value->setProperty("height", bounds.getHeight());
            return juce::var(value);
        };
        const auto capture = [&](juce::Component &component, const char *name, float scale,
                                  const char *scope, bool inlineControls)
        {
            const auto *target = mixerInsert(controller.project, selectedInsert);
            check(selectedInsert == 1 && target && target->name == "DRUM BUS" &&
                      target->stereoSeparation == .375f && target->phaseInverted && target->stereoSwapped,
                  "Capture labels and icons reflect the exact selected canonical target");
            const auto targetName = target->name;
            const auto amount = target->stereoSeparation;
            const bool inverted = target->phaseInverted, swapped = target->stereoSwapped;
            const auto separation = editor->mixerStereoSeparationBounds(selectedInsert);
            const auto phase = editor->mixerPhaseInvertBounds(selectedInsert);
            const auto swap = editor->mixerStereoSwapBounds(selectedInsert);
            check(inlineControls ? !separation.isEmpty() && !phase.isEmpty() && !swap.isEmpty()
                                 : separation.isEmpty() && phase.isEmpty() && swap.isEmpty(),
                  "Actual capture geometry follows compact/tall availability");
            if (inlineControls)
                check(editor->getLocalBounds().contains(separation) && editor->getLocalBounds().contains(phase) &&
                          editor->getLocalBounds().contains(swap) && !separation.intersects(phase) &&
                          !separation.intersects(swap) && !phase.intersects(swap) &&
                          !separation.intersects(editor->mixerGainReadoutBounds(selectedInsert)) &&
                          !separation.intersects(editor->mixerMeterBounds(selectedInsert)) &&
                          !swap.intersects(editor->mixerRouteButtonBounds(selectedInsert)),
                      "Painted stereo targets are separate and inside their actual component");
            const auto file = folder.getChildFile(name);
            check(!file.exists(), "Fresh screenshot path preserves prior evidence");
            auto image = component.createComponentSnapshot(component.getLocalBounds(), true, scale);
            check(image.isValid() && image.getWidth() == juce::roundToInt(component.getWidth() * scale) &&
                      image.getHeight() == juce::roundToInt(component.getHeight() * scale),
                  "Fresh actual component image has the requested logical/raster dimensions");
            auto stream = file.createOutputStream();
            check(bool(stream), "Owned screenshot output stream");
            check(stream->setPosition(0) && stream->truncate().wasOk(), "Truncate PNG instead of appending images");
            check(juce::PNGImageFormat{}.writeImageToStream(image, *stream), "Encode actual Mixer image");
            stream->flush();
            check(stream->getStatus().wasOk(), "PNG output reports successful I/O");
            stream.reset();
            auto input = file.createInputStream();
            const unsigned char signature[8]{137,80,78,71,13,10,26,10};
            unsigned char bytes[8]{};
            check(file.getSize() > 8 && input && input->read(bytes, 8) == 8 &&
                      std::equal(signature, signature + 8, bytes),
                  "Fresh written file has a complete PNG signature");
            auto *entry = new juce::DynamicObject;
            entry->setProperty("file", name); entry->setProperty("scope", scope);
            entry->setProperty("logicalWidth", component.getWidth());
            entry->setProperty("logicalHeight", component.getHeight());
            entry->setProperty("rasterWidth", image.getWidth()); entry->setProperty("rasterHeight", image.getHeight());
            entry->setProperty("renderScale", scale); entry->setProperty("fileBytes", file.getSize());
            entry->setProperty("sha256", juce::SHA256(file).toHexString());
            entry->setProperty("selectedInsert", selectedInsert); entry->setProperty("selectedName", targetName);
            entry->setProperty("stereoSeparation", amount);
            entry->setProperty("expectedDisplayedSeparation", "+38%"); // Source renderer rounds .375 *100; inspect PNG separately.
            entry->setProperty("phaseInverted", inverted); entry->setProperty("stereoSwapped", swapped);
            entry->setProperty("inlineStereoControls", inlineControls);
            entry->setProperty("separationBoundsInMixer", rect(separation));
            entry->setProperty("phaseBoundsInMixer", rect(phase)); entry->setProperty("swapBoundsInMixer", rect(swap));
            entry->setProperty("mixerBounds", rect(editor->getBounds()));
            entry->setProperty("nativeOsInput", false); entry->setProperty("nativeWindowsDpi", false);
            entry->setProperty("dsp", false); entry->setProperty("humanInspection", false);
            captures.add(juce::var(entry));
            auto *progress = new juce::DynamicObject;
            progress->setProperty("completionState", "pending"); progress->setProperty("captures", captures);
            progress->setProperty("note", "Component renders only; inspect PNGs separately. Scale1.5 does not qualify physical Windows DPI.");
            check(folder.getChildFile("mixer-stereo-captures.json").replaceWithText(
                      juce::JSON::toString(juce::var(progress), true)), "Retain partial capture evidence before next layout");
            check(selectedInsert == 1 && controller.revision == renderedRevision && renderedModel == controller.project.state().toXmlString(),
                  "Rendering/layout never changes musical model or history revision");
        };
        // Actual workspace layout is recalculated at each logical client size.
        // These are app-content rasters; no synthetic native title bar is added.
        setSize(1920, 1044); refresh();
        capture(*this, "mixer-stereo-1080p.png", 1, "workspace-client", true);
        setSize(2560, 1392); refresh();
        capture(*this, "mixer-stereo-4k-150.png", 1.5f, "workspace-client", true);
        const auto workspaceMixerBounds = editor->getBounds();
        for (int height : {480, 700, 1300})
        {
            editor->setSize(1000, height);
            const auto name = "mixer-stereo-editor-1000x" + juce::String(height) + ".png";
            capture(*editor, name.toRawUTF8(), 1, "Mixer-component", height != 480);
        }
        editor->setBounds(workspaceMixerBounds);
        setSize(1920, 1044); refresh();
        auto *captureCompletion = new juce::DynamicObject;
        captureCompletion->setProperty("completionState", "completed"); captureCompletion->setProperty("captures", captures);
        captureCompletion->setProperty("humanInspection", false); captureCompletion->setProperty("nativeWindowsDpi", false);
        check(folder.getChildFile("mixer-stereo-captures.json").replaceWithText(
                  juce::JSON::toString(juce::var(captureCompletion), true)), "Complete capture creation only after all actual writes succeed");
        cases.add("Actual populated component PNGs:1080p,4K150% render, compact480/comfortable700/tall1300; no OS-DPI/inspection claim");
        check(record(true, {}), "Write actual component receipt");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        if (ownedDialog) ownedDialog->exitModalState(0); // Only dialog instance created by this fixture.
        record(false, error.what());
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera
