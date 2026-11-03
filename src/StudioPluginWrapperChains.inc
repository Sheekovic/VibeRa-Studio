// SPDX-License-Identifier: AGPL-3.0-or-later
// Message-thread wrapper entry points over existing canonical chain/sidechain
// services. Native close receipts remain owned through cancellation and failure.
juce::Result Studio::pluginChainContext(const PluginOperation &job) const
{
    if (job.project != controller.projectIdentity || !job.revision || *job.revision != controller.revision)
        return juce::Result::fail(
            "effectChain.staleCapture: Project/revision changed; reopen the wrapper action");
    const auto *insert = mixerInsert(controller.project, job.mixer);
    if (!insert || job.slot < 0 || job.slot >= int(insert->effects.size()) ||
        insert->effects[size_t(job.slot)].id != job.plugin ||
        insert->effects[size_t(job.slot)].descriptor != job.descriptor)
        return juce::Result::fail("effectChain.staleTarget: Containing effect slot changed");
    if (!job.host || !job.host->alive() || job.host->runtimeIdentity() != job.instance ||
        audio.preparedPluginHost(controller.project, job.plugin) != job.host)
        return juce::Result::fail("stale_instance: Rediscover the exact effect worker");
    if (recordArmed || midiTaking || hasPendingMidiTake() || automationRecordingActive())
        return juce::Result::fail("recording_busy: Finish/retry/cancel recording before a chain operation");
    return juce::Result::ok();
}

juce::Result Studio::requestPluginChain(Id plugin, const juce::String &instance, const juce::String &action,
                                        const juce::var &options, bool interactive, juce::String &operation,
                                        juce::String &errorCode)
{
    operation.clear();
    errorCode.clear();
    const auto refuse = [&](const char *code, const juce::String &message)
    {
        errorCode = code;
        return juce::Result::fail(message);
    };
    if (action != "copy" && action != "paste" && action != "save" && action != "loadApply" &&
        action != "configureDirectory" && action != "sidechain")
        return refuse("invalid_arguments", "Choose an existing containing-chain or detector operation");
    const auto host = audio.preparedPluginHost(controller.project, plugin);
    const auto *spec = controller.project.plugin(plugin);
    if (!spec || !host || !host->alive() || host->runtimeIdentity() != instance)
        return refuse("stale_instance", "Prepare and rediscover the exact effect worker");
    int insert = -1, slot = -1;
    for (const auto &row : controller.project.mixer)
        for (size_t i = 0; i < row.effects.size(); ++i)
            if (row.effects[i].id == plugin)
            {
                insert = row.index;
                slot = int(i);
            }
    if (insert < 0)
        return refuse("unavailable_target", "Containing Mixer chain requires an effect, not an instrument");
    if (recordArmed || midiTaking || hasPendingMidiTake() || automationRecordingActive())
        return refuse("recording_busy", "Finish/retry/cancel recording before a chain operation");
    if (interactive && (chooser || pluginChainUiOperation.isNotEmpty()))
        return refuse("busy", "Finish the existing file chooser or wrapper choice first");
    if (std::any_of(pluginOperations.begin(), pluginOperations.end(),
                    [&](const auto &job) { return job.state == "pending" && job.plugin == plugin; }))
        return refuse("busy", "Another exact-effect operation is pending");
    if (pluginOperations.size() >= 64)
    {
        const auto old = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                      [](const auto &job) { return job.state != "pending"; });
        if (old == pluginOperations.end())
            return refuse("capacity_exceeded", "All plugin operation slots are in use");
        pluginOperations.erase(old);
    }
    PluginOperation job;
    job.id = juce::Uuid().toString();
    job.kind = "chain";
    job.action = action;
    job.plugin = plugin;
    job.project = controller.projectIdentity;
    job.revision = controller.revision;
    job.instance = instance;
    job.descriptor = spec->descriptor;
    job.host = host;
    job.mixer = insert;
    job.slot = slot;
    job.started = juce::Time::getMillisecondCounter();
    job.chainOptions = juce::var(new juce::DynamicObject);
    for (const auto *field : {"path", "directory", "overwrite", "detector"})
        if (options.hasProperty(field))
            job.chainOptions.getDynamicObject()->setProperty(field, options[field]);
    job.choosing = interactive && (action == "save" || action == "loadApply" ||
                                   action == "configureDirectory" || action == "sidechain");
    job.phase = job.choosing ? "choosing" : "preparing";
    job.message = job.choosing ? "Choose the exact containing-chain action; no native mutation yet"
                               : "Containing-chain operation admitted; native closure/capture pending";
    pluginOperations.reserve(pluginOperations.size() + 1);
    operation = job.id;
    pluginOperations.push_back(std::move(job));
    try
    {
        if (interactive)
        {
            if (action == "sidechain")
                choosePluginDetector(operation);
            else if (action == "save" || action == "loadApply" || action == "configureDirectory")
                choosePluginChainPath(operation);
        }
    }
    catch (...)
    {
        const auto owner = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                        [&](const auto &row) { return row.id == operation; });
        if (owner != pluginOperations.end())
        {
            owner->code = "effectChain.chooserFailed";
            owner->message = "Choice preparation failed; retained operation owns cancellation";
            owner->cancelRequested = true;
            if (pluginChainUiOperation == operation)
                cancelPluginChainUi(operation);
            else
                owner->choosing = false;
        }
    }
    return juce::Result::ok();
}

void Studio::cancelPluginChainUi(const juce::String &operation)
{
    if (pluginChainUiOperation != operation)
        return;
    if (pluginChainConfirmation)
        pluginChainConfirmation->exitModalState(0); // Actual callback acknowledges closed confirmation.
    else if (chooser)
    {
        chooser.reset(); // JUCE destroys/cancels its native browser and suppresses its callback.
        pluginChainUiOperation.clear();
        for (auto &job : pluginOperations)
            if (job.id == operation)
                job.choosing = false;
    }
    else
        juce::PopupMenu::dismissAllActiveMenus(); // Only this process's owned modal menu; callback drains.
}

void Studio::choosePluginChainPath(const juce::String &operation)
{
    const auto found = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                    [&](const auto &job) { return job.id == operation; });
    if (found == pluginOperations.end() || found->state != "pending")
        return;
    const auto directory = mixerChainReadback()["state"]["directory"].toString();
    if (found->action != "configureDirectory" && directory.isEmpty())
    {
        found->choosing = false;
        found->code = "effectChain.directoryUnavailable";
        found->message = "Choose Chain directory first, then save/load a containing chain";
        found->cancelRequested = true;
        return;
    }
    const bool configure = found->action == "configureDirectory", saving = found->action == "save";
    chooser = std::make_unique<juce::FileChooser>(configure ? "Choose existing LOCAL chain preset directory"
                                                  : saving  ? "Save containing Mixer chain"
                                                            : "Load and replace containing Mixer chain",
                                                  juce::File(directory),
                                                  configure ? juce::String{} : juce::String("*.vibfx"));
    pluginChainUiOperation = operation;
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(configure ? juce::FileBrowserComponent::openMode |
                                         juce::FileBrowserComponent::canSelectDirectories
                                   : (saving ? juce::FileBrowserComponent::saveMode |
                                                   juce::FileBrowserComponent::warnAboutOverwriting
                                             : juce::FileBrowserComponent::openMode) |
                                         juce::FileBrowserComponent::canSelectFiles,
                         [safe, operation](const juce::FileChooser &fc)
                         {
                             if (!safe || safe->pluginChainUiOperation != operation)
                                 return;
                             const auto selected = fc.getResult();
                             safe->chooser.reset();
                             safe->finishPluginChainChoice(operation, selected);
                         });
}

void Studio::finishPluginChainChoice(const juce::String &operation, const juce::File &file,
                                     bool overwriteApproved)
{
    const auto found = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                    [&](const auto &job) { return job.id == operation; });
    if (found == pluginOperations.end() || found->state != "pending")
        return;
    auto &job = *found;
    pluginChainConfirmation = nullptr;
    if (file == juce::File{} || job.cancelRequested)
    {
        job.cancelRequested = true;
        job.choosing = false;
        pluginChainUiOperation.clear();
        return;
    }
    if (const auto current = pluginChainContext(job); current.failed())
    {
        job.code = current.getErrorMessage().upToFirstOccurrenceOf(":", false, false);
        job.message = current.getErrorMessage();
        job.cancelRequested = true;
        job.choosing = false;
        pluginChainUiOperation.clear();
        return;
    }
    if (job.action == "save" && file.existsAsFile() && !overwriteApproved)
    {
        auto *confirmation = new juce::AlertWindow("Replace chain preset?", file.getFullPathName(),
                                                   juce::MessageBoxIconType::WarningIcon);
        confirmation->addButton("Replace", 1);
        confirmation->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        pluginChainConfirmation = confirmation;
        job.phase = "confirmingOverwrite";
        auto safe = juce::Component::SafePointer<Studio>(this);
        confirmation->enterModalState(true,
                                      juce::ModalCallbackFunction::create(
                                          [safe, operation, file](int answer)
                                          {
                                              if (safe && safe->pluginChainUiOperation == operation)
                                                  safe->finishPluginChainChoice(
                                                      operation, answer == 1 ? file : juce::File{},
                                                      answer == 1);
                                          }),
                                      true);
        return;
    }
    job.chainOptions.getDynamicObject()->setProperty(
        job.action == "configureDirectory" ? "directory" : "path", file.getFullPathName());
    if (job.action == "save")
        job.chainOptions.getDynamicObject()->setProperty("overwrite", overwriteApproved);
    job.choosing = false;
    job.phase = "preparing";
    pluginChainUiOperation.clear();
}

void Studio::choosePluginDetector(const juce::String &operation)
{
    const auto found = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                    [&](const auto &job) { return job.id == operation; });
    if (found == pluginOperations.end() || found->state != "pending")
        return;
    const auto *spec = controller.project.plugin(found->plugin);
    PluginBusCatalogue buses;
    if (!spec || !found->host->readBusCatalogue(buses) || !buses.valid())
    {
        found->code = "sidechain.unavailable";
        found->message = "Actual native input bus catalogue unavailable";
        found->choosing = false;
        found->cancelRequested = true;
        return;
    }
    juce::PopupMenu menu;
    std::vector<std::optional<PluginDetectorAssignment>> choices{std::nullopt};
    menu.addItem(1, "Disconnect detector", bool(spec->detector), !spec->detector);
    for (const auto &source : controller.project.mixer)
        if (source.index > 0 && source.index != found->mixer)
        {
            juce::PopupMenu routes;
            for (int i = 0; i < buses.inputCount; ++i)
            {
                const auto &bus = buses.entries[size_t(i)];
                if (bus.index < 1 || bus.index == spec->mainInputBus)
                    continue;
                for (const int channels : {2, 1})
                {
                    const PluginDetectorAssignment detector{source.index, bus.index, channels};
                    auto candidate = controller.project;
                    const bool valid = setPluginDetector(candidate, found->plugin, detector).wasOk();
                    const bool selected = spec->detector == detector;
                    choices.push_back(detector);
                    routes.addItem(int(choices.size()),
                                   juce::String::fromUTF8(bus.name) + " [" + juce::String(bus.index) +
                                       "] / " + (channels == 2 ? "Stereo" : "Mono"),
                                   valid && !selected, selected);
                }
            }
            if (routes.getNumItems())
                menu.addSubMenu(source.name + " [" + juce::String(source.index) + "]", routes);
        }
    if (choices.size() == 1)
        menu.addItem(2, "No other supported native detector input buses", false);
    pluginChainUiOperation = operation;
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.showMenuAsync({},
                       [safe, operation, choices = std::move(choices)](int answer)
                       {
                           if (!safe || safe->pluginChainUiOperation != operation)
                               return;
                           safe->pluginChainUiOperation.clear();
                           const auto parent =
                               std::find_if(safe->pluginOperations.begin(), safe->pluginOperations.end(),
                                            [&](const auto &job) { return job.id == operation; });
                           if (parent == safe->pluginOperations.end() || parent->state != "pending")
                               return;
                           parent->choosing = false;
                           if (answer <= 0 || size_t(answer) > choices.size() || parent->cancelRequested)
                           {
                               parent->cancelRequested = true;
                               return;
                           }
                           auto value = juce::var{};
                           if (const auto detector = choices[size_t(answer - 1)])
                           {
                               value = juce::var(new juce::DynamicObject);
                               value.getDynamicObject()->setProperty("sourceMixer", detector->sourceMixer);
                               value.getDynamicObject()->setProperty("inputBus", detector->inputBus);
                               value.getDynamicObject()->setProperty("channels", detector->channels);
                           }
                           parent->chainOptions.getDynamicObject()->setProperty("detector", value);
                           parent->phase = "preparing";
                       });
}

void Studio::servicePluginChain(PluginOperation &job)
{
    const auto finish = [&](const juce::String &state)
    {
        job.state = job.phase = state;
        job.completedRevision = controller.revision;
        for (auto &close : job.closes)
            close.host.reset();
        job.host.reset();
        if (job.project == controller.projectIdentity)
            status = job.message;
    };
    const auto fail = [&](const juce::Result &error)
    {
        if (job.code.isEmpty())
        {
            job.code = error.getErrorMessage().upToFirstOccurrenceOf(":", false, false);
            job.message = error.getErrorMessage();
        }
        job.cancelRequested = true;
    };
    // An admitted canonical child owns completion even after it removes/replaces
    // the requesting slot. Never apply old-Host liveness as a completion oracle.
    if (job.childOperation.isNotEmpty())
    {
        const auto child = mixerChainReadback(job.childOperation);
        job.historyCommitted = bool(child["documentChanged"]);
        if (child["completionState"].toString() != "pending")
        {
            job.code = child["code"].toString();
            job.message = child["message"].toString();
            finish(child["completionState"].toString());
        }
        return;
    }
    if (const auto current = pluginChainContext(job); current.failed())
        fail(current);
    if (job.cancelRequested && job.choosing)
        cancelPluginChainUi(job.id);
    if (job.choosing)
        return; // Human choice has no invented native timeout/completion.
    try
    {
        if (!job.cancelRequested && !job.closesPrepared)
        {
            const juce::ScopedValueSetter<juce::String> own(pluginChainAdmissionOperation, job.id);
            if (auto admission = admitMixerChainCapture(); admission.failed())
                fail(admission);
            if (!job.cancelRequested && job.action != "configureDirectory")
            {
                const bool all =
                    job.action == "paste" || job.action == "loadApply" || job.action == "sidechain";
                const auto *insert = mixerInsert(controller.project, job.mixer);
                for (const auto &runtime : audio.pluginRuntimeStates())
                {
                    if (!all && std::none_of(insert->effects.begin(), insert->effects.end(),
                                             [&](const auto &slot) { return slot.id == runtime.plugin; }))
                        continue;
                    auto host = audio.preparedPluginHost(controller.project, runtime.plugin);
                    if (host && host->editorWindowState().exists)
                        job.closes.push_back({runtime.plugin, host, host->runtimeIdentity(), 0, {}});
                }
            }
            job.closesPrepared = true; // All fallible pin allocation precedes the first native request.
        }
        bool waiting = false;
        for (auto &close : job.closes)
        {
            if (close.result)
                continue;
            if (!close.serial && !job.cancelRequested)
            {
                if (!close.host->alive() || close.host->runtimeIdentity() != close.instance ||
                    audio.preparedPluginHost(controller.project, close.plugin) != close.host)
                    fail(juce::Result::fail("stale_instance: Editor owner changed before close"));
                else
                {
                    const auto admitted =
                        close.host->requestEditor(close.instance, PluginEditorAction::close);
                    close.serial = admitted.serial; // Own the serial before any fallible diagnostic work.
                    if (!close.serial)
                        fail(juce::Result::fail("effectChain.controlBusy: Native close refused"));
                }
            }
            if (!close.serial)
                continue;
            if (job.cancelRequested)
                close.host->cancelEditorRequest(close.instance, close.serial);
            PluginEditorResult result;
            if (close.host->readEditorResult(close.instance, close.serial, result))
            {
                close.result = result; // Consuming read; retain the POD before constructing errors/JSON.
                job.nativeDispatched = job.nativeDispatched || result.dispatched;
                if (!job.cancelRequested &&
                    (result.outcome != PluginEditorOutcome::closed || !result.completionKnown ||
                     !result.window.known || result.window.exists))
                    fail(juce::Result::fail(
                        "effectChain.editorCloseFailed: Native editor did not acknowledge closure"));
            }
            else
                waiting = true;
        }
        job.phase = waiting ? "closingEditors" : "preparing";
        if (waiting)
            return;
        if (job.cancelRequested)
        {
            if (job.code.isEmpty())
                job.message = "Containing-chain operation cancelled; owned closes drained";
            finish(job.code.isEmpty() ? "cancelled" : "failed");
            return;
        }
        if (const auto current = pluginChainContext(job); current.failed())
        {
            fail(current);
            finish("failed");
            return;
        }
        auto args = juce::var(new juce::DynamicObject);
        auto *values = args.getDynamicObject();
        values->setProperty("action", job.action);
        for (const auto &field : job.chainOptions.getDynamicObject()->getProperties())
            if (field.name != juce::Identifier("detector"))
                values->setProperty(field.name, field.value);
        if (job.action != "configureDirectory")
        {
            values->setProperty("mixer", job.mixer);
            values->setProperty("projectIdentity", job.project);
            values->setProperty("revision", juce::int64(*job.revision));
        }
        const juce::ScopedValueSetter<juce::String> own(pluginChainAdmissionOperation, job.id);
        if (job.action == "sidechain")
        {
            CommandArgs commandArgs;
            commandArgs.target = job.plugin;
            const auto detector = job.chainOptions["detector"];
            std::optional<PluginDetectorAssignment> assignment;
            if (detector.isObject())
                assignment = PluginDetectorAssignment{int(detector["sourceMixer"]), int(detector["inputBus"]),
                                                      int(detector["channels"])};
            commandArgs.detector.emplace(assignment);
            const juce::ScopedValueSetter<juce::String *> admission(canonicalStructuralAdmissionOperation,
                                                                    &job.childOperation);
            command("plugin.effect.sidechain", commandArgs);
            if (job.childOperation.isEmpty())
                fail(juce::Result::fail(status));
        }
        else
        {
            const auto outcome = mixerChainOperation(args, &job.childOperation);
            if (job.childOperation.isEmpty())
            {
                if (job.action == "configureDirectory" &&
                    outcome["completionState"].toString() == "completed" &&
                    juce::File(outcome["state"]["directory"].toString()) ==
                        juce::File(job.chainOptions["directory"].toString()))
                {
                    job.message = "Existing containing-chain preset directory configured";
                    finish("completed");
                    return;
                }
                fail(juce::Result::fail(
                    outcome["completionState"].toString() == "completed"
                        ? juce::String(
                              "effectChain.configurationMismatch: Configured directory differs from request")
                        : outcome["code"].toString() + ": " + outcome["message"].toString()));
            }
        }
        if (job.childOperation.isNotEmpty())
            job.phase = "canonical";
        else
            finish("failed");
    }
    catch (const std::exception &error)
    {
        fail(juce::Result::fail("effectChain.preparationFailed: " + juce::String(error.what())));
    }
    catch (...)
    {
        fail(juce::Result::fail("effectChain.preparationFailed: Inspect retained native/child ownership"));
    }
    // Exception leaves all admitted serials/child tokens owned for the next poll.
}
