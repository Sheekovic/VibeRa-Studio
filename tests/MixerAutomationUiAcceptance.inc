// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/NoInline.h"
// Actual component/modal/shared-command checks. No native input or DSP qualification.
namespace vibera
{
VIBERA_NOINLINE juce::Result Studio::verifyMixerAutomationUi(const juce::File &folder)
{
    juce::StringArray cases;
    juce::Component::SafePointer<juce::AlertWindow> ownedDialog;
    const auto check = [](bool value, const char *text) { if (!value) throw std::runtime_error(text); };
    const auto receipt = [&](bool passed, const juce::String &failure)
    {
        auto *object = new juce::DynamicObject;
        object->setProperty("passed", passed); object->setProperty("failure", failure);
        object->setProperty("cases", cases); object->setProperty("nativeInput", false);
        object->setProperty("dsp", false); object->setProperty("listening", false);
        return folder.getChildFile("mixer-automation-ui.json").replaceWithText(juce::JSON::toString(juce::var(object), true));
    };
    try
    {
        check(controllerTestMode && folder.createDirectory().wasOk() && !controller.hasGesture() &&
            !juce::Component::getCurrentlyModalComponent() && !midiTaking && !hasPendingMidiTake(), "Owned fresh component fixture");
        stopTimer(); controller.replace(Project::demo()); setSize(1920, 1044); showEditor(3); refresh();
        const auto captureImage = [&](juce::Component &component, const char *name, float scale = 1.f)
        {
            const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, scale);
            check(image.isValid(), "Actual populated component render");
            const auto file = folder.getChildFile(name);
            check(!file.exists(), "New component image preserves prior evidence");
            auto stream = file.createOutputStream();
            check(stream && stream->setPosition(0) && stream->truncate().wasOk(), "Truncate owned PNG stream");
            check(juce::PNGImageFormat{}.writeImageToStream(image,*stream), "Encode actual automation interface");
            stream->flush(); check(stream->getStatus().wasOk(), "Flush complete component PNG");
        };
        auto *mixer = static_cast<Editor *>(windows[3]->getContentComponent());
        const auto capture = [&](int insert, MixerAutomationControl control)
        { return MixerAutomationUiIntent{controller.projectIdentity, controller.revision, insert, control}; };
        const auto run = [&](MixerAutomationUiAction action, MixerAutomationControl control, int insert, Id source = 0)
        { return performMixerAutomationIntent(capture(insert, control), action, source); };
        for (auto control : {MixerAutomationControl::gain, MixerAutomationControl::pan, MixerAutomationControl::stereoSeparation})
        {
            const auto before = controller.project.state(); const auto intent = capture(1, control);
            selectedInsert = 2; // New selection must not retarget original asynchronous intent.
            check(performMixerAutomationIntent(intent, MixerAutomationUiAction::initialize).wasOk(), "Initialize original stable control");
            const AutomationKey key{static_cast<AutomationParameter>(unsigned(control)), 1, 0};
            check(std::any_of(controller.project.automation.initialValues.begin(), controller.project.automation.initialValues.end(),
                [&](const auto &value) { return value.key == key; }), "Exact initialization key");
            check(controller.undoEditChecked().wasOk() && before.isEquivalentTo(controller.project.state()), "One exact initialization Undo");
            check(controller.redoEditChecked().wasOk() && run(MixerAutomationUiAction::removeInitialization, control, 1).wasOk() &&
                controller.project.automation.initialValues.empty(), "Redo and exact remove initialization");
        }
        cases.add("Stable delayed gain/pan/separation targets, initialization/removal and one Undo/Redo");
        check(run(MixerAutomationUiAction::create, MixerAutomationControl::pan, 1).wasOk(), "Create actual pan clip");
        const auto source = controller.project.automation.sources.back().id;
        const auto placement = controller.project.automation.placements.back().id;
        const auto *created = controller.project.automationSource(source);
        check(created && created->links.front().key == AutomationKey{AutomationParameter::mixerPan, 1, 0} &&
            created->links.front().minimum == -1 && created->links.front().maximum == 1 &&
            created->points.front().value == .5f && selectedClips == std::vector<Id>{placement}, "Bipolar domain, normalized centre and actual clip selection");
        auto *playlist = static_cast<Editor *>(windows[2]->getContentComponent());
        check(playlist->isPickerShown() && playlist->clipPicker()->sourceKind() == "automation" &&
            playlist->clipPicker()->selectedIds() == std::vector<Id>{source}, "Actual Playlist automation drawing source");
        check(run(MixerAutomationUiAction::link, MixerAutomationControl::stereoSeparation, 2, source).wasOk() &&
            run(MixerAutomationUiAction::select, MixerAutomationControl::stereoSeparation, 2, source).wasOk() &&
            run(MixerAutomationUiAction::select, MixerAutomationControl::gain, 2, source).failed(), "Exact linked-source navigation and foreign-link refusal");
        const auto choices = mixerAutomationUiChoices(controller.project, *controller.project.automationSource(source));
        std::size_t expectedChoice = 0;
        for (const auto control : {AutomationParameter::mixerGain, AutomationParameter::mixerPan,
                                   AutomationParameter::mixerStereoSeparation})
            for (const auto &insert : controller.project.mixer)
            {
                check(expectedChoice < choices.size() &&
                          choices[expectedChoice].target.key == AutomationKey{control, insert.index, 0},
                      "Every original gain/pan/separation choice retains its exact prefix order");
                ++expectedChoice;
            }
        for (const auto &insert : controller.project.mixer)
            for (const auto &edge : insert.routes)
            {
                check(expectedChoice < choices.size() &&
                          choices[expectedChoice].target.key == AutomationKey{AutomationParameter::mixerSend,
                                                                              insert.index,
                                                                              std::uint32_t(edge.target)} &&
                          choices[expectedChoice].target.manual == edge.gain,
                      "Appended send choice targets the exact saved source/destination and manual level");
                ++expectedChoice;
            }
        check(choices.size() == expectedChoice &&
                  choices[17].target.key == AutomationKey{AutomationParameter::mixerGain, 17, 0},
              "Legacy gain indices retained before explicit bipolar and send choices");
        cases.add("Actual pan creation, shared separation link, exact key selection and compatible gain rows");
        const auto open = [&]()
        {
            automationSourceDialog(source);
            ownedDialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
            check(ownedDialog != nullptr, "Actual properties modal opens"); return ownedDialog.getComponent();
        };
        const auto close = [&](int action) { ownedDialog->exitModalState(action); dispatchMessages(30); ownedDialog = nullptr; };
        const auto beforeProperties = controller.project.state();
        showEditor(2); if(maximisedEditor != 2) maximiseEditor(2); refresh();
        captureImage(*this,"automation-playlist-1080p.png");
        setSize(2560,1392); resized(); refresh();
        captureImage(*this,"automation-playlist-4k-150.png",1.5f);
        setSize(1920,1044); resized(); refresh();
        auto *dialog = open(); dialog->getComboBoxComponent("target")->setSelectedItemIndex(38, juce::sendNotificationSync);
        dialog->getTextEditor("name")->setText("Shared bipolar motion");
        dialog->getTextEditor("minimum")->setText("+0.75"); dialog->getTextEditor("maximum")->setText("-0.5");
        captureImage(*dialog,"automation-source-properties.png"); close(1);
        const auto edited = controller.project.state(); const auto *updated = controller.project.automationSource(source);
        check(updated && updated->name == "Shared bipolar motion" && updated->links.size() == 2 &&
            updated->links.back().key == AutomationKey{AutomationParameter::mixerStereoSeparation, 2, 0} &&
            updated->links.back().minimum == .75f && updated->links.back().maximum == -.5f && !controller.hasGesture(), "Properties exact rename/key/inverted-range commit");
        check(controller.undoEditChecked().wasOk() && beforeProperties.isEquivalentTo(controller.project.state()) &&
            controller.redoEditChecked().wasOk() && edited.isEquivalentTo(controller.project.state()), "Whole properties edit one Undo/Redo");
        dialog = open(); dialog->getComboBoxComponent("target")->setSelectedItemIndex(38, juce::sendNotificationSync);
        dialog->getTextEditor("name")->setText("Must roll back"); dialog->getTextEditor("minimum")->setText("-1.01"); close(1);
        check(edited.isEquivalentTo(controller.project.state()) && !controller.hasGesture(), "Invalid bipolar range rolls back rename and link");
        dialog = open(); dialog->getTextEditor("name")->setText("Cancelled"); close(0);
        check(edited.isEquivalentTo(controller.project.state()), "Dialog Cancel is no-op");
        dialog = open(); dialog->getComboBoxComponent("target")->setSelectedItemIndex(38, juce::sendNotificationSync); close(2);
        check(controller.project.automationSource(source)->links.size() == 1 && controller.undoEditChecked().wasOk() &&
            edited.isEquivalentTo(controller.project.state()), "Remove exact separation link with one Undo");
        cases.add("Actual properties inverted range, one Undo/Redo, invalid rollback, cancel and unlink");
        const auto stale = capture(1, MixerAutomationControl::pan);
        CommandArgs name; name.mixer = 2; name.text = "Newer insert name";
        check(controller.execute("mixer.rename", name).wasOk(), "Prepare newer revision");
        const auto newer = controller.project.state();
        check(performMixerAutomationIntent(stale, MixerAutomationUiAction::initialize).failed() &&
            newer.isEquivalentTo(controller.project.state()), "Stale menu cannot mutate newer work");
        dialog = open(); dialog->getTextEditor("name")->setText("Stale properties"); name.text = "Newest name";
        check(controller.execute("mixer.rename", name).wasOk(), "Prepare dialog staleness");
        const auto newest = controller.project.state(); close(1);
        check(newest.isEquivalentTo(controller.project.state()), "Stale dialog preserves newer work");
        check(controller.beginGesture("foreign.automation"), "Foreign owner fixture");
        check(run(MixerAutomationUiAction::create, MixerAutomationControl::pan, 1).failed() &&
            controller.gestureOwner() == "foreign.automation" && newest.isEquivalentTo(controller.project.state()), "UI cannot steal foreign gesture");
        check(controller.cancelGestureChecked("foreign.automation").wasOk(), "Release only owned fixture gesture");
        midiFileBusy = true; CommandArgs init; init.mixer = 1; init.automationControl = MixerAutomationControl::pan;
        const auto busy = controller.execute("automation.init", init); midiFileBusy = false;
        check(busy.failed() && newest.isEquivalentTo(controller.project.state()), "Canonical direct caller shares UI/MCP file admission");
        cases.add("Stale revision/dialog and foreign gesture/file busy preserve newer model");
        showEditor(3); refresh(); const auto point = mixer->mixerPanBounds(1).getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent right(juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1, 0, 0, 0, 0, mixer, mixer, now, point, now, 1, false);
        const auto beforeMenu = controller.project.state(); mixer->mouseDown(right);
        check(juce::PopupMenu::dismissAllActiveMenus(), "Actual pan right-click opens contextual automation menu");
        dispatchMessages(20); check(beforeMenu.isEquivalentTo(controller.project.state()), "Menu dismiss leaves model unchanged");
        cases.add("Actual pan component target and cancellable contextual menu, no native OS input");
        // BEGIN original sampler source-properties component acceptance.
        controller.replace(Project::empty());
        const auto samplerChannel = controller.project.channels.front().id;
        selectedChannel = controller.project.channels.back().id; // Explicit source owner must win.
        selectedPattern = controller.project.patterns.front().id;
        CommandArgs samplerArgs;
        samplerArgs.channel = samplerChannel;
        samplerArgs.samplerAutomationParameter = 0; // Present zero is a real Envelope enabled code.
        samplerArgs.length = 4;
        check(controller.execute("automation.sampler.create", samplerArgs).wasOk(),
              "Create actual code0 sampler source through shared canonical command");
        const auto samplerSource = controller.project.automation.sources.back().id;
        const AutomationKey samplerFlag{AutomationParameter::samplerModulation, samplerChannel, 0};
        const AutomationKey samplerPeriod{AutomationParameter::samplerModulation, samplerChannel, 17};
        const auto linkedSampler = [&](AutomationKey key) -> const AutomationLink *
        {
            const auto *item = controller.project.automationSource(samplerSource);
            if (!item)
                return nullptr;
            const auto found = std::find_if(item->links.begin(), item->links.end(),
                                            [&](const auto &link) { return link.key == key; });
            return found == item->links.end() ? nullptr : &*found;
        };
        const auto openSampler = [&]()
        {
            automationSourceDialog(samplerSource);
            ownedDialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
            check(ownedDialog != nullptr, "Actual sampler source-properties modal opens");
            return ownedDialog.getComponent();
        };
        const auto samplerPrefix =
            mixerAutomationUiChoices(controller.project, *controller.project.automationSource(samplerSource))
                .size();
        const auto samplerBeforeOpen = controller.project.state();
        const auto samplerUndo = controller.undo.getUndoDescriptions();
        const auto samplerRedo = controller.undo.getRedoDescriptions();
        const bool samplerDirty = controller.dirty;
        dialog = openSampler();
        auto *samplerChoices = dialog->getComboBoxComponent("target");
        check(samplerChoices->getNumItems() == int(samplerPrefix + 1) &&
                  samplerChoices->getSelectedItemIndex() == int(samplerPrefix) &&
                  samplerChoices->getText().contains("volume") &&
                  samplerChoices->getText().contains("Envelope enabled") &&
                  dialog->getTextEditor("minimum")->isEnabled() &&
                  dialog->getTextEditor("maximum")->isEnabled(),
              "Code0 full-key selection appends one linked sampler row after unchanged Mixer prefix, not "
              "Master");
        close(1);
        check(samplerBeforeOpen.isEquivalentTo(controller.project.state()) &&
                  controller.undo.getUndoDescriptions() == samplerUndo &&
                  controller.undo.getRedoDescriptions() == samplerRedo && controller.dirty == samplerDirty &&
                  linkedSampler(samplerFlag) && !controller.hasGesture(),
              "Opening/applying untouched code0 source neither links Master nor adds history/initialization");
        dialog = openSampler();
        dialog->getTextEditor("minimum")->setText("1");
        dialog->getTextEditor("maximum")->setText("0");
        close(1);
        const auto invertedFlag = controller.project.state();
        check(linkedSampler(samplerFlag) && linkedSampler(samplerFlag)->minimum == 1.f &&
                  linkedSampler(samplerFlag)->maximum == 0.f && controller.undoEditChecked().wasOk() &&
                  samplerBeforeOpen.isEquivalentTo(controller.project.state()) &&
                  controller.redoEditChecked().wasOk() &&
                  invertedFlag.isEquivalentTo(controller.project.state()),
              "Actual sampler code0 inverted discrete endpoint edit commits one exact Undo/Redo");
        cases.add("Sampler code0 real modal selection, no accidental Master edit, inverted range and exact "
                  "history");

        samplerArgs.target = samplerSource;
        samplerArgs.samplerAutomationParameter = 17;
        check(controller.execute("automation.sampler.link", samplerArgs).wasOk(),
              "Link actual LFO period as second target of existing source");
        samplerArgs.beat = double(float(.12345679104328156));
        samplerArgs.length = double(float(9.987654685974121));
        check(controller.execute("automation.sampler.range", samplerArgs).wasOk(),
              "Prepare independently chosen legal non-six-decimal sampler endpoint values");
        const float exactMinimum = linkedSampler(samplerPeriod)->minimum;
        const float exactMaximum = linkedSampler(samplerPeriod)->maximum;
        const auto beforeRename = controller.project.state();
        auto expectedRenamed = controller.project;
        expectedRenamed.automationSource(samplerSource)->name = "Exact sampler source name";
        dialog = openSampler();
        dialog->getComboBoxComponent("target")->setSelectedItemIndex(int(samplerPrefix + 1),
                                                                     juce::sendNotificationSync);
        dialog->getTextEditor("name")->setText("Exact sampler source name");
        captureImage(*dialog, "automation-sampler-source-properties.png");
        close(1);
        const auto samplerRenamed = controller.project.state();
        check(expectedRenamed.state().isEquivalentTo(controller.project.state()) &&
                  controller.project.automationSource(samplerSource)->name == "Exact sampler source name" &&
                  linkedSampler(samplerPeriod) && linkedSampler(samplerPeriod)->minimum == exactMinimum &&
                  linkedSampler(samplerPeriod)->maximum == exactMaximum &&
                  controller.undoEditChecked().wasOk() &&
                  beforeRename.isEquivalentTo(controller.project.state()) &&
                  controller.redoEditChecked().wasOk() &&
                  samplerRenamed.isEquivalentTo(controller.project.state()),
              "Name-only Apply preserves raw float endpoints and exact one-step Undo/Redo");
        dialog = openSampler();
        dialog->getComboBoxComponent("target")->setSelectedItemIndex(int(samplerPrefix + 1),
                                                                     juce::sendNotificationSync);
        dialog->getTextEditor("minimum")->setText("0.75");
        close(1);
        check(
            linkedSampler(samplerPeriod)->minimum == .75f &&
                linkedSampler(samplerPeriod)->maximum == exactMaximum &&
                controller.undoEditChecked().wasOk() &&
                samplerRenamed.isEquivalentTo(controller.project.state()),
            "Changing one endpoint preserves the untouched exact endpoint instead of its six-decimal label");
        cases.add("Actual sampler range precision, name-only Apply, partial endpoint edit and history");

        const auto beforeSamplerFailure = controller.project.state();
        const auto failureUndo = controller.undo.getUndoDescriptions();
        const auto failureRedo = controller.undo.getRedoDescriptions();
        const bool failureDirty = controller.dirty;
        const auto failureAllocator = controller.project.nextId;
        dialog = openSampler(); // Initial row is code0, whose domain is strictly discrete0/1.
        dialog->getTextEditor("name")->setText("Sampler rename must roll back");
        dialog->getTextEditor("minimum")->setText("0.5");
        close(1);
        check(
            beforeSamplerFailure.isEquivalentTo(controller.project.state()) &&
                controller.undo.getUndoDescriptions() == failureUndo &&
                controller.undo.getRedoDescriptions() == failureRedo && controller.dirty == failureDirty &&
                controller.project.nextId == failureAllocator && !controller.hasGesture(),
            "Fractional code0 endpoint failure atomically cancels rename/link/history without losing owner");
        dialog = openSampler();
        dialog->getTextEditor("name")->setText("Sampler parse must refuse");
        dialog->getTextEditor("minimum")->setText("not-a-number");
        close(1);
        check(beforeSamplerFailure.isEquivalentTo(controller.project.state()) &&
                  controller.undo.getUndoDescriptions() == failureUndo &&
                  controller.undo.getRedoDescriptions() == failureRedo && !controller.hasGesture(),
              "Malformed sampler output refuses before gesture publication");
        dialog = openSampler();
        dialog->getComboBoxComponent("target")->setSelectedItemIndex(int(samplerPrefix + 1),
                                                                     juce::sendNotificationSync);
        dialog->getTextEditor("name")->setText("Cancelled sampler properties");
        dialog->getTextEditor("minimum")->setText("0.5");
        close(0);
        check(beforeSamplerFailure.isEquivalentTo(controller.project.state()) &&
                  controller.undo.getUndoDescriptions() == failureUndo &&
                  controller.undo.getRedoDescriptions() == failureRedo && controller.dirty == failureDirty,
              "Actual sampler dialog Cancel preserves source, raw ranges, dirty and complete history");
        cases.add("Sampler fractional range rollback, malformed refusal and exact Cancel");

        auto dormant = controller.project;
        auto *dormantChannel = dormant.channel(samplerChannel);
        dormantChannel->audioClip = true;
        dormantChannel->audioDuration = 1;
        dormantChannel->source = "original-dormant-source-properties.wav";
        check(dormant.validate().wasOk(),
              "Original empty-score AudioClip role retains valid dormant sampler links");
        controller.replace(dormant);
        const auto dormantBefore = controller.project.state();
        auto expectedDormantRename = controller.project;
        expectedDormantRename.automationSource(samplerSource)->name = "Dormant sampler source retained";
        const auto dormantMinimum = linkedSampler(samplerPeriod)->minimum;
        const auto dormantMaximum = linkedSampler(samplerPeriod)->maximum;
        dialog = openSampler();
        check(dialog->getComboBoxComponent("target")->getSelectedItemIndex() == int(samplerPrefix) &&
                  dialog->getComboBoxComponent("target")->getText().contains("unavailable") &&
                  !dialog->getTextEditor("minimum")->isEnabled() &&
                  !dialog->getTextEditor("maximum")->isEnabled() &&
                  dialog->getButton(0)->getButtonText() == "Apply name only",
              "Actual dormant sampler row is selected, named unavailable and has disabled physical range "
              "fields");
        dialog->getTextEditor("name")->setText("Dormant sampler source retained");
        // Disabled controls are deliberately tampered only by this component fixture;
        // name-only Apply must never forward their text to a native/AudioClip target.
        dialog->getTextEditor("minimum")->setText("not-a-number");
        captureImage(*dialog, "automation-sampler-dormant-properties.png");
        close(1);
        const auto dormantRenamed = controller.project.state();
        check(expectedDormantRename.state().isEquivalentTo(controller.project.state()) &&
                  controller.project.automationSource(samplerSource)->name ==
                      "Dormant sampler source retained" &&
                  linkedSampler(samplerFlag) && linkedSampler(samplerPeriod) &&
                  linkedSampler(samplerPeriod)->minimum == dormantMinimum &&
                  linkedSampler(samplerPeriod)->maximum == dormantMaximum &&
                  controller.undoEditChecked().wasOk() &&
                  dormantBefore.isEquivalentTo(controller.project.state()) &&
                  controller.redoEditChecked().wasOk() &&
                  dormantRenamed.isEquivalentTo(controller.project.state()),
              "Dormant Apply is name-only, retains exact links/ranges and commits one real Undo/Redo");
        const auto initialCount = controller.project.automation.initialValues.size();
        dialog = openSampler();
        close(2);
        const auto dormantUnlinked = controller.project.state();
        check(!linkedSampler(samplerFlag) && linkedSampler(samplerPeriod) &&
                  controller.project.automation.initialValues.size() == initialCount &&
                  controller.undoEditChecked().wasOk() &&
                  dormantRenamed.isEquivalentTo(controller.project.state()) &&
                  controller.redoEditChecked().wasOk() &&
                  dormantUnlinked.isEquivalentTo(controller.project.state()),
              "Actual dormant Remove calls exact code0 unlink, preserves other link/initials and real "
              "Undo/Redo");
        cases.add("Dormant AudioClip role name-only UI, preserved ranges and canonical code0 unlink/history");
        // END original sampler source-properties component acceptance.
        check(receipt(true, {}), "Write owned receipt"); return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        if (ownedDialog) ownedDialog->exitModalState(0);
        midiFileBusy = false; receipt(false, error.what()); return juce::Result::fail(error.what());
    }
}
} // namespace vibera
