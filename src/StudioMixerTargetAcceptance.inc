// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual JUCE component + canonical Controller acceptance. Included in
// Studio.cpp: no extra heavy translation unit, OS input, device or provider.
namespace
{
void targetMenuCheck(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
// Fixture-only universe mutation: production Mixer insert CRUD is not claimed.
// Exercise the real Controller snapshot/history primitive and notifications.
void targetMenuFixtureSnapshot(Controller &controller, Project next)
{
    targetMenuCheck(next.validate().wasOk(), "Fixture universe must remain a genuine valid Project");
    std::unique_ptr<PreparedControllerChange> prepared;
    const auto result = controller.prepareSnapshotChange(std::move(next), controller.project,
                                                         "Original target-menu fixture universe", true, false,
                                                         true, prepared);
    targetMenuCheck(result.wasOk() && prepared && controller.validatePreparedChange(*prepared).wasOk() &&
                        controller.commitPreparedChange(*prepared),
                    "Actual prepared Controller fixture transaction");
    targetMenuCheck(controller.deliverPendingNotifications(), "Actual fixture notifications delivered");
}
} // namespace

juce::Result Studio::Editor::verifyMixerTargetMenus(juce::StringArray &passed)
{
    if (type != 0 || !s.controllerTestMode || !bool(s.getProperties()["viberaMixerTargetsFixture"]) ||
        juce::Component::getCurrentlyModalComponent())
        return juce::Result::fail("Rack target acceptance requires isolated idle owned Rack");
    juce::Component::SafePointer<juce::Component> ownedMenu;
    try
    {
        const auto channel = s.selectedChannel;
        auto open = [&]
        {
            targetMenuCheck(!juce::Component::getCurrentlyModalComponent(),
                            "No foreign modal may be dismissed");
            rackOffset = 0;
            const juce::Point<float> point{float(rackLabelWidth() - 18), float(44 + rackRowHeight() / 2)};
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                                   juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                   this, this, now, point, now, 1, false);
            mouseDown(event); // Actual component handler; not native pointer automation.
            mouseUp(event);
            ownedMenu = juce::Component::getCurrentlyModalComponent();
            targetMenuCheck(bool(ownedMenu), "Actual Rack target indicator opens a JUCE PopupMenu");
        };
        auto complete = [&](int ordinal)
        {
            targetMenuCheck(ownedMenu &&
                                ownedMenu.getComponent() == juce::Component::getCurrentlyModalComponent(),
                            "Only this fixture's exact popup is completed");
            ownedMenu->exitModalState(ordinal);
            const auto deadline = juce::Time::getMillisecondCounter() + 1000;
            while (ownedMenu && juce::Time::getMillisecondCounter() < deadline)
                dispatchMessages(1);
            targetMenuCheck(!ownedMenu, "Actual JUCE popup completion callback retired the owned menu");
        };
        const int maximum = std::numeric_limits<int>::max();
        MixerTargetChoices initial(s.controller.project, s.controller.projectIdentity, channel);
        const auto large = initial.ordinalForTarget(maximum);
        targetMenuCheck(large > 0, "Sparse maximum target exists before Rack popup");
        open();
        auto reordered = s.controller.project;
        std::reverse(reordered.mixer.begin(), reordered.mixer.end());
        targetMenuFixtureSnapshot(s.controller, std::move(reordered));
        const auto history = s.controller.undo.getUndoDescriptions().size();
        complete(large);
        targetMenuCheck(s.controller.project.channel(channel)->mixer == maximum &&
                            s.controller.undo.getUndoDescriptions().size() == history + 1,
                        "Open Rack popup retains its original stable INT_MAX target after row reorder");
        passed.add("rack_actual_popup_reorder_stable_target_one_history");

        const MixerTargetChoices beforeRemoval(s.controller.project, s.controller.projectIdentity, channel);
        open();
        auto removed = s.controller.project;
        removed.channel(channel)->mixer = 1;
        std::erase_if(removed.mixer, [maximum](const auto &insert) { return insert.index == maximum; });
        targetMenuFixtureSnapshot(s.controller, std::move(removed));
        const auto beforeRemoved = s.controller.project.state().toXmlString();
        const auto removedRevision = s.controller.revision;
        complete(beforeRemoval.ordinalForTarget(maximum));
        targetMenuCheck(s.controller.project.state().toXmlString() == beforeRemoved &&
                            s.controller.revision == removedRevision &&
                            s.status.startsWith("routing.staleTarget:"),
                        "An actual open Rack popup refuses a deleted insert without alias or extra history");
        passed.add("rack_actual_popup_removed_target_refused");

        MixerTargetChoices cancellation(s.controller.project, s.controller.projectIdentity, channel);
        open();
        const auto beforeCancel = s.controller.project.state().toXmlString();
        const auto revisionCancel = s.controller.revision;
        complete(0);
        targetMenuCheck(s.controller.project.state().toXmlString() == beforeCancel &&
                            s.controller.revision == revisionCancel,
                        "Actual popup cancellation changes no Project or history");
        passed.add("rack_actual_popup_cancel");

        open();
        const auto oldMaster = cancellation.ordinalForTarget(0);
        auto replacement = s.controller.project;
        replacement.channel(channel)->mixer = 1;
        s.controller.replace(std::move(replacement)); // Genuine new session reuses numeric IDs.
        const auto identity = s.controller.projectIdentity;
        const auto beforeStale = s.controller.project.state().toXmlString();
        const auto revisionStale = s.controller.revision;
        complete(oldMaster);
        targetMenuCheck(
            s.controller.projectIdentity == identity &&
                s.controller.project.state().toXmlString() == beforeStale &&
                s.controller.revision == revisionStale && s.status.startsWith("routing.staleMenu:"),
            "Old actual Rack popup cannot route a replacement session reusing Channel/insert IDs");
        passed.add("rack_actual_popup_replacement_session_refused");

        const MixerTargetChoices beforeChannelRemoval(s.controller.project, s.controller.projectIdentity,
                                                      channel);
        open();
        auto noChannel = s.controller.project;
        noChannel.channels.clear();
        targetMenuFixtureSnapshot(s.controller, std::move(noChannel));
        const auto missingState = s.controller.project.state().toXmlString();
        const auto missingRevision = s.controller.revision;
        complete(beforeChannelRemoval.ordinalForTarget(0));
        targetMenuCheck(s.controller.project.state().toXmlString() == missingState &&
                            s.controller.revision == missingRevision &&
                            s.status.startsWith("routing.staleChannel:"),
                        "An actual open Rack popup refuses a removed original Channel with no mutation");
        passed.add("rack_actual_popup_removed_channel_refused");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        if (ownedMenu && ownedMenu.getComponent() == juce::Component::getCurrentlyModalComponent())
            ownedMenu->exitModalState(0);
        return juce::Result::fail(error.what());
    }
}

juce::Result Studio::verifyMixerTargetChoices(const juce::File &folder)
{
    if (!controllerTestMode || !pluginTestSettings || !workflowTestSettings ||
        !bool(getProperties()["viberaMixerTargetsFixture"]) ||
        getProperties()["viberaMixerTargetsOutput"].toString() != folder.getFullPathName() ||
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice() ||
        juce::Component::getCurrentlyModalComponent())
        return juce::Result::fail("Target menus require owned isolated fresh no-device Studio and output");
    const auto receipt = folder.getChildFile("mixer-target-choices-verification.json");
    if (receipt.exists())
        return juce::Result::fail("Retain target-menu evidence; use a fresh attempt");
    if (const auto result = folder.createDirectory(); result.failed())
        return result;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const bool timerWasRunning = isTimerRunning();
    stopTimer();
    juce::String stage = "initial", failure;
    juce::StringArray passed;
    try
    {
        targetMenuCheck(!midiTaking && !recordArmed && !afterPluginState && !pendingExport &&
                            !audio.rendering(),
                        "Idle capture/recording/export required");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.channels.front().mixer = 1;
        project.patterns.front().notes.clear();
        project.placements.clear();
        const int maximum = std::numeric_limits<int>::max();
        targetMenuCheck(project.mixer.size() > 3 && project.mixer.back().index != 0,
                        "Replace a dormant existing insert only");
        project.mixer.back().index = maximum;
        targetMenuCheck(project.validate().wasOk(), "Actual valid sparse INT_MAX Project");
        controller.replace(std::move(project));
        selectedChannel = controller.project.channels.front().id;
        selectedPattern = controller.project.patterns.front().id;
        refresh();
        openInstrument(selectedChannel);
        auto *window = instrumentWindows.at(selectedChannel).get();
        targetMenuCheck(window, "Actual sample/built-in channel-settings window exists");
        auto *combo =
            dynamic_cast<juce::ComboBox *>(window->controlsComponent()->findChildWithID("instrument.mixer"));
        targetMenuCheck(combo, "Actual sampler Mixer ComboBox is discoverable by stable component ID");
        const MixerTargetChoices initial(controller.project, controller.projectIdentity, selectedChannel);
        const int large = initial.ordinalForTarget(maximum), master = initial.ordinalForTarget(0);
        targetMenuCheck(large > 0 && large < maximum && master > 0 && master != large,
                        "Large stable index uses ordinary bounded item token");
        stage = "actual-combo-route-history";
        const auto history = controller.undo.getUndoDescriptions().size();
        const auto revision = controller.revision;
        combo->setSelectedId(large, juce::sendNotificationSync);
        targetMenuCheck(controller.project.channel(selectedChannel)->mixer == maximum &&
                            controller.revision == revision + 1 &&
                            controller.undo.getUndoDescriptions().size() == history + 1,
                        "Actual ComboBox onChange commits canonical stable target with exactly one real "
                        "history transaction");
        const auto route = channelRoutingState(lastRoutingOperation);
        targetMenuCheck(route["completionState"].toString() == "completed" &&
                            bool(route["documentCommitted"]) &&
                            route["audioDisposition"].toString() == "deferredNoDevice" &&
                            !bool(route["nativeCompletionKnown"]),
                        "Component shares semantic document-only route receipt without false DSP completion");
        targetMenuCheck(
            performAction("edit.undo") && controller.project.channel(selectedChannel)->mixer == 1 &&
                performAction("edit.redo") && controller.project.channel(selectedChannel)->mixer == maximum,
            "Actual shared Undo/Redo restores exact stable Mixer target");
        passed.add("sampler_actual_combo_intmax_one_history_undo_redo_shared_receipt");

        stage = "live-universe-rename-add-reorder";
        CommandArgs rename;
        rename.mixer = maximum;
        rename.text = "Maximum renamed";
        targetMenuCheck(controller.execute("mixer.rename", rename).wasOk(), "Canonical Mixer rename");
        window->refreshFromProject();
        targetMenuCheck(combo->getItemText(combo->indexOfItemId(large)) == "Maximum renamed" &&
                            combo->getSelectedId() == large,
                        "Open sampler refreshes names without changing stable tokens");
        auto added = controller.project;
        const int newIndex = 137;
        targetMenuCheck(std::none_of(added.mixer.begin(), added.mixer.end(),
                                     [](const auto &insert) { return insert.index == newIndex; }),
                        "Original addition fixture index is unused");
        added.mixer.emplace_back(newIndex, "New universe insert");
        std::reverse(added.mixer.begin(), added.mixer.end());
        targetMenuFixtureSnapshot(controller, std::move(added));
        window->refreshFromProject();
        // Locate by exact distinct fixture label, not current row position.
        int newOrdinal = 0;
        for (int i = 0; i < combo->getNumItems(); ++i)
            if (combo->getItemText(i) == "New universe insert")
                newOrdinal = combo->getItemId(i);
        targetMenuCheck(
            newOrdinal > large && combo->getSelectedId() == large &&
                combo->getItemText(combo->indexOfItemId(large)) == "Maximum renamed",
            "Add/reorder refresh exposes new insert with fresh token and preserves same-ID semantics");
        passed.add("sampler_open_universe_rename_add_reorder");

        stage = "queued-selection-universe-refresh";
        combo->setSelectedId(newOrdinal, juce::sendNotificationAsync);
        auto second = controller.project;
        second.mixer.emplace_back(138, "Second new universe insert");
        std::rotate(second.mixer.begin(), second.mixer.begin() + 1, second.mixer.end());
        targetMenuFixtureSnapshot(controller, std::move(second));
        const auto queuedHistory = controller.undo.getUndoDescriptions().size();
        window->refreshFromProject(); // Deterministic refresh BEFORE AsyncUpdater delivery.
        targetMenuCheck(int(combo->getSelectedIdAsValue().getValue()) == newOrdinal,
                        "UI universe refresh preserves an already-queued actual user token");
        dispatchMessages(5);
        targetMenuCheck(controller.project.channel(selectedChannel)->mixer == newIndex &&
                            controller.undo.getUndoDescriptions().size() == queuedHistory + 1,
                        "Actual queued ComboBox callback routes original stable target, not reordered row");
        passed.add("sampler_queued_actual_callback_refresh_keeps_original_target");

        stage = "queued-removed-target-refusal";
        combo->setSelectedId(large, juce::sendNotificationAsync);
        auto removed = controller.project;
        std::erase_if(removed.mixer, [maximum](const auto &insert) { return insert.index == maximum; });
        removed.mixer.emplace_back(139, "Replacement distinct insert");
        targetMenuFixtureSnapshot(controller, std::move(removed));
        const auto removedState = controller.project.state().toXmlString();
        const auto removedRevision = controller.revision;
        window->refreshFromProject();
        dispatchMessages(5);
        targetMenuCheck(
            controller.project.state().toXmlString() == removedState &&
                controller.revision == removedRevision && status.startsWith("routing.staleTarget:") &&
                controller.project.channel(selectedChannel)->mixer == newIndex,
            "Deleted pending target returns typed staleTarget, cannot alias new insert or create history");
        passed.add("sampler_removed_pending_target_no_alias_no_history");

        stage = "sampler-stale-session";
        // Removing a different source is not allowed to silently rebind this
        // window. Controller replacement deliberately retains it only in fixture.
        auto replacement = controller.project;
        replacement.channel(selectedChannel)->mixer = 1;
        controller.replace(std::move(replacement));
        const auto staleState = controller.project.state().toXmlString();
        const auto staleRevision = controller.revision;
        combo->setSelectedId(master, juce::sendNotificationSync);
        targetMenuCheck(controller.project.state().toXmlString() == staleState &&
                            controller.revision == staleRevision && status.startsWith("routing.staleMenu:"),
                        "Old sampler window cannot route replacement session reusing numeric IDs");
        passed.add("sampler_actual_callback_replacement_session_refused");
        instrumentWindows.clear();

        stage = "rack-actual-popup";
        auto rackProject = controller.project;
        rackProject.mixer.emplace_back(maximum, "Rack maximum");
        controller.replace(std::move(rackProject));
        selectedChannel = controller.project.channels.front().id;
        selectedPattern = controller.project.patterns.front().id;
        refresh();
        showEditor(0);
        auto *rack = dynamic_cast<Editor *>(windows[0]->getContentComponent());
        targetMenuCheck(rack && rack->getHeight() > 100 && rack->getWidth() > 220,
                        "Actual laid-out Rack required");
        const auto rackResult = rack->verifyMixerTargetMenus(passed);
        targetMenuCheck(rackResult.wasOk(), rackResult.getErrorMessage().toRawUTF8());
        targetMenuCheck(!audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice() &&
                            !audio.projectionDiagnostics().edit &&
                            !audio.projectionDiagnostics().playbackContext,
                        "All component/history scenarios remain genuinely no-device/no-graph");
    }
    catch (const std::exception &error)
    {
        failure = error.what();
    }
    catch (...)
    {
        failure = "Unexpected target-menu component failure";
    }
    instrumentWindows.clear();
    if (timerWasRunning)
        startTimerHz(30);
    auto evidence = std::make_unique<juce::DynamicObject>();
    evidence->setProperty("passed", failure.isEmpty());
    evidence->setProperty("stage", stage);
    evidence->setProperty("failure", failure);
    evidence->setProperty("cases", passed);
    evidence->setProperty("scope", "Actual JUCE ComboBox/PopupMenu callbacks and canonical "
                                   "Controller/semantic readback; fixture-only universe CRUD");
    evidence->setProperty("nativeInput", false);
    evidence->setProperty("hardware", false);
    evidence->setProperty("audioCallbacks", false);
    evidence->setProperty("listening", false);
    evidence->setProperty("elapsedSeconds", (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
    if (!receipt.replaceWithText(juce::JSON::toString(juce::var(evidence.release()), true)))
        return juce::Result::fail("Target-menu receipt write failed");
    return failure.isEmpty() ? juce::Result::ok() : juce::Result::fail(failure);
}
