// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera by StudioController.cpp; CONTROL/message thread only.

// JUCE JSON::parse accepts only objects/arrays. Wrap a single lexically numeric
// value so empty, boolean, trailing text and multiple-value input stay invalid.
static juce::var samplerControllerNumericText(const juce::String &text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || !trimmed.containsOnly("-+0123456789.eE"))
        return {};
    const auto parsed = juce::JSON::parse("[" + trimmed + "]");
    const auto *items = parsed.getArray();
    if (!items || items->size() != 1)
        return {};
    const auto &number = items->getReference(0);
    return (number.isInt() || number.isInt64() || number.isDouble()) && std::isfinite(double(number))
               ? number
               : juce::var{};
}

static juce::String samplerControllerUnitLabel(const juce::String &units)
{
    if (units == "bipolarNormalized")
        return "bipolar amount (-1 to +1)";
    if (units == "bipolarTension")
        return "curve tension (-1 to +1)";
    if (units == "normalized")
        return "amount (0 to 1)";
    if (units == "boolean")
        return "Off/On (0/1)";
    if (units == "shapeIndex")
        return "waveform index (0 to 2)";
    return units;
}

class SamplerControllerAlert final : public juce::AlertWindow
{
  public:
    juce::ToggleButton pickup{"Pickup (avoid value jumps)"}, secondary;
    juce::TextEditor conflicts;
    SamplerControllerAlert(const juce::String &title, const juce::String &message, juce::Component *owner,
                           const juce::String &secondaryLabel, bool second)
        : AlertWindow(title, message, juce::MessageBoxIconType::NoIcon, owner), secondary(secondaryLabel)
    {
        // AlertWindow renders a separate label for custom components with a name.
        // Toggle captions already carry the label; retain that accessible text only once.
        pickup.setName({});
        secondary.setName({});
        pickup.setSize(380, 28);
        secondary.setSize(380, 28);
        pickup.setToggleState(true, juce::dontSendNotification);
        secondary.setToggleState(second, juce::dontSendNotification);
        pickup.setComponentID("sampler.controller.pickup");
        secondary.setComponentID("sampler.controller.secondary");
        addCustomComponent(&pickup);
        addCustomComponent(&secondary);
        if (secondaryLabel.startsWith("Replace"))
        {
            conflicts.setMultiLine(true, true);
            conflicts.setReadOnly(true);
            conflicts.setScrollbarsShown(true);
            conflicts.setComponentID("sampler.controller.conflicts");
            conflicts.setSize(380, 96);
            addCustomComponent(&conflicts);
        }
    }
};

void Studio::syncSamplerControllerBindings()
{
    const auto result = midiController.publishProjectBindings(controller.project, controller.projectIdentity);
    samplerControllerProjectionError = result.wasOk() ? juce::String{} : result.getErrorMessage();
    if (!samplerControllerLearnCommitting && samplerControllerLearn &&
        samplerControllerLearn->state == "pending")
    {
        auto &learn = *samplerControllerLearn;
        SamplerAutomationTarget target;
        const auto resolved = resolveSamplerAutomationTarget(controller.project, learn.candidate.channel,
                                                             learn.candidate.parameter, target);
        if (learn.project != controller.projectIdentity || learn.revision != controller.revision ||
            learn.projection != midiController.projectProjectionGeneration() || resolved.failed())
        {
            midiController.cancelProjectLearn(learn.serial);
            learn.state = "failed";
            learn.error = "controller.staleLearn: Project, target or mappings changed; reopen Learn";
        }
    }
    if (auto *window = samplerControllerLearnWindow.getComponent())
    {
        if (!samplerControllerLearn || samplerControllerLearn->token != samplerControllerLearnWindowToken)
        {
            window->setMessage(
                "This Learn operation has finished. Close this window; the newer operation is separate.");
            window->getButton("Link")->setEnabled(false);
            return;
        }
        const auto current = samplerControllerLearnState();
        juce::String message = current["message"].toString();
        if (samplerControllerLearn && samplerControllerLearn->captured)
        {
            const auto &candidate = samplerControllerLearn->candidate;
            message += "\n" + juce::String(candidate.port ? "Control" : "Performance") + " / MIDI Ch " +
                       juce::String(candidate.midiChannel) + " / CC " + juce::String(candidate.cc);
            juce::String conflictText = "No conflicting project links.";
            if (const auto *conflicts = current["conflictingProjectLinks"].getArray();
                conflicts && !conflicts->isEmpty())
            {
                conflictText = "Conflicting project links:";
                for (const auto &id : *conflicts)
                    for (const auto &link : controller.project.samplerControllerLinks)
                        if (link.id == juce::int64(id))
                            conflictText += "\n#" + juce::String(link.id) + " " + link.label;
            }
            if (auto *view = dynamic_cast<SamplerControllerAlert *>(window);
                view && view->conflicts.getText() != conflictText)
                view->conflicts.setText(conflictText, false);
        }
        window->setMessage(message);
        window->getButton("Link")->setEnabled(samplerControllerLearn &&
                                              samplerControllerLearn->state == "pending" &&
                                              samplerControllerLearn->captured);
    }
}
juce::var Studio::samplerControllerLearnState() const
{
    auto result = juce::var(new juce::DynamicObject);
    auto *out = result.getDynamicObject();
    if (!samplerControllerLearn)
    {
        out->setProperty("completionState", "idle");
        out->setProperty("message",
                         "Right-click a sampler envelope/LFO control and choose Link to controller.");
        return result;
    }
    const auto &learn = *samplerControllerLearn;
    out->setProperty("operation", learn.token);
    out->setProperty("completionState", learn.state);
    out->setProperty("projectIdentity", learn.project);
    out->setProperty("revision", juce::int64(learn.revision));
    out->setProperty("captured", learn.captured);
    out->setProperty("candidate", samplerControllerLinkState(learn.candidate));
    out->setProperty("documentChanged", learn.documentChanged);
    out->setProperty("projectUndoable", learn.documentChanged);
    out->setProperty("effectiveApplied", learn.effectiveApplied);
    out->setProperty("saved", false); // Learn changes the document; ordinary Save is separate.
    out->setProperty("completionScope", "projectControllerLink");
    const auto units = samplerControllerUnits(controller.project, learn.candidate);
    out->setProperty("units", units);
    const auto detail =
        learn.error.isNotEmpty()     ? learn.error
        : learn.state == "completed" ? "Controller link added. Save the project to retain it on disk."
        : learn.state == "cancelled" ? "Controller learning cancelled; no link added."
        : learn.captured             ? "Control identified. Link to this fixed sampler target, or Cancel."
                         : "Move one hardware CC. Performance notes remain playable; then confirm Link.";
    out->setProperty("message", learn.candidate.label + "\nValue: " + samplerControllerUnitLabel(units) +
                                    ".\n" + detail);
    if (learn.error.isNotEmpty())
    {
        auto error = juce::var(new juce::DynamicObject);
        error.getDynamicObject()->setProperty("code", learn.error.upToFirstOccurrenceOf(":", false, false));
        error.getDynamicObject()->setProperty("message", learn.error);
        out->setProperty("error", error);
    }
    juce::Array<juce::var> conflicts, replaced;
    if (learn.captured && learn.project == controller.projectIdentity)
        for (const auto &link : controller.project.samplerControllerLinks)
            if (link.id != learn.candidate.id && samplerControllerSourceOverlaps(learn.candidate, link))
                conflicts.add(juce::int64(link.id));
    for (const auto id : learn.replaced)
        replaced.add(juce::int64(id));
    out->setProperty("conflictingProjectLinks", conflicts);
    out->setProperty("replacedProjectLinks", replaced);
    return result;
}
juce::var Studio::samplerControllerState() const
{
    auto result = juce::var(new juce::DynamicObject);
    auto *out = result.getDynamicObject();
    out->setProperty("projectIdentity", controller.projectIdentity);
    out->setProperty("projectDirty", controller.dirty);
    out->setProperty("revision", juce::int64(controller.revision));
    out->setProperty("persistence", "projectDocument");
    out->setProperty("effective", midiController.projectBindingState());
    out->setProperty("learn", samplerControllerLearnState());
    out->setProperty("projectionError", samplerControllerProjectionError);
    juce::Array<juce::var> links;
    for (const auto &link : controller.project.samplerControllerLinks)
    {
        auto item = samplerControllerLinkState(link);
        SamplerAutomationTarget target;
        const auto resolved =
            resolveSamplerAutomationTarget(controller.project, link.channel, link.parameter, target, true);
        auto *row = item.getDynamicObject();
        row->setProperty("available", resolved.wasOk() && target.available);
        row->setProperty("name", resolved.wasOk() ? target.name : "Unavailable sampler target");
        row->setProperty("units", samplerControllerUnits(controller.project, link));
        row->setProperty("reason", resolved.failed()  ? resolved.getErrorMessage()
                                   : target.available ? ""
                                                      : "controller.samplerUnavailable: Link retained; "
                                                        "target is a native instrument or Audio Clip");
        links.add(item);
    }
    out->setProperty("links", links);
    return result;
}
juce::Result Studio::startSamplerControllerLearn(Id channel, std::uint32_t parameter)
{
    if (automationRecordingActive())
        return juce::Result::fail("automationRecording.busy: Finish the automation take before MIDI Learn");
    syncSamplerControllerBindings();
    if (samplerControllerProjectionError.isNotEmpty())
        return juce::Result::fail(samplerControllerProjectionError);
    if (controller.hasGesture() || (samplerControllerLearn && samplerControllerLearn->state == "pending") ||
        midiController.learnIndex >= 0)
        return juce::Result::fail("controller.learnBusy: Finish or cancel the current edit/learning first");
    SamplerAutomationTarget target;
    if (const auto result = resolveSamplerAutomationTarget(controller.project, channel, parameter, target);
        result.failed())
        return result;
    SamplerControllerLearn next;
    next.token = juce::Uuid().toString();
    next.serial = ++nextSamplerControllerLearn;
    if (!next.serial)
        next.serial = ++nextSamplerControllerLearn;
    next.project = controller.projectIdentity;
    next.revision = controller.revision;
    next.projection = midiController.projectProjectionGeneration();
    next.candidate.channel = channel;
    next.candidate.parameter = parameter;
    next.candidate.minimum = target.parameter.minimum;
    next.candidate.maximum = target.parameter.maximum;
    next.candidate.label = target.name.replaceCharacters("\r\n\t", "   ").substring(0, 120);
    samplerControllerLearn = std::move(next);
    const auto armed = midiController.armProjectLearn(
        samplerControllerLearn->serial,
        [safe = juce::Component::SafePointer<Studio>(this)](std::uint64_t serial, const ControlMessage &event)
        {
            if (!safe || !safe->samplerControllerLearn || safe->samplerControllerLearn->serial != serial)
                return;
            safe->syncSamplerControllerBindings();
            auto &learn = *safe->samplerControllerLearn;
            if (learn.state != "pending" || learn.captured)
                return;
            learn.candidate.port = event.port;
            learn.candidate.midiChannel = event.channel;
            learn.candidate.cc = event.number;
            learn.captured = true;
            safe->syncSamplerControllerBindings();
        });
    if (armed.failed())
    {
        samplerControllerLearn->state = "failed";
        samplerControllerLearn->error = armed.getErrorMessage();
    }
    return armed; // Pending identification; never a completed link.
}

juce::var Studio::samplerControllerOperation(const juce::var &args)
{
    const auto action = args["action"].toString();
    if (action != "samplerLearnStart" && action != "samplerLearnState" && action != "samplerLearnAccept" &&
        action != "samplerLearnCancel")
        throw McpFailure("controller.invalidAction", "Unsupported sampler Learn operation");
    validateMcpSchema(samplerControllerLearnArguments(action), args);
    if (args.hasProperty("projectIdentity") &&
        args["projectIdentity"].toString() != controller.projectIdentity)
        throw McpFailure("controller.staleProject", "Requested project has been replaced");
    syncSamplerControllerBindings();
    if (action == "samplerLearnStart")
    {
        const auto result = startSamplerControllerLearn(juce::int64(args["targetChannel"]),
                                                        std::uint32_t(int(args["samplerParameter"])));
        if (result.failed())
            throw McpFailure(result.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                             result.getErrorMessage());
        return samplerControllerLearnState();
    }
    const auto token = args["token"].toString();
    if (!samplerControllerLearn || samplerControllerLearn->token != token)
        throw McpFailure("controller.unknownLearn", "Exact current Learn operation required");
    if (action == "samplerLearnState")
        return samplerControllerLearnState();
    auto result = juce::Result::ok();
    if (action == "samplerLearnCancel")
        result = cancelSamplerControllerLearn(token);
    else
    {
        if (args.hasProperty("minimum") != args.hasProperty("maximum"))
            throw McpFailure("controller.invalidRange", "Supply both physical range endpoints or neither");
        const auto &candidate = samplerControllerLearn->candidate;
        const double minimum = args.hasProperty("minimum") ? double(args["minimum"]) : candidate.minimum;
        const double maximum = args.hasProperty("maximum") ? double(args["maximum"]) : candidate.maximum;
        const bool pickup = args.hasProperty("pickup") ? bool(args["pickup"]) : candidate.pickup;
        std::vector<Id> replacements;
        if (const auto *items = args["replacements"].getArray())
            for (const auto &item : *items)
                replacements.push_back(juce::int64(item));
        result = acceptSamplerControllerLearn(token, minimum, maximum, pickup, replacements);
    }
    if (result.failed())
        throw McpFailure(result.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                         result.getErrorMessage());
    return samplerControllerLearnState();
}
juce::Result Studio::cancelSamplerControllerLearn(const juce::String &token)
{
    if (!samplerControllerLearn || samplerControllerLearn->token != token)
        return juce::Result::fail("controller.unknownLearn: Exact current Learn operation required");
    auto &learn = *samplerControllerLearn;
    if (learn.state != "pending")
        return learn.state == "cancelled"
                   ? juce::Result::ok()
                   : juce::Result::fail("controller.learnTerminal: Learn already finished");
    midiController.cancelProjectLearn(learn.serial);
    learn.state = "cancelled";
    return juce::Result::ok();
}
juce::Result Studio::acceptSamplerControllerLearn(const juce::String &token, double minimum, double maximum,
                                                  bool pickup, const std::vector<Id> &replacements)
{
    syncSamplerControllerBindings();
    if (!samplerControllerLearn || samplerControllerLearn->token != token)
        return juce::Result::fail("controller.unknownLearn: Exact current Learn operation required");
    auto &learn = *samplerControllerLearn;
    if (learn.state != "pending" || !learn.captured)
        return juce::Result::fail(learn.error.isNotEmpty()
                                      ? learn.error
                                      : "controller.capturePending: Identify one external CC before Link");
    if (controller.hasGesture())
        return juce::Result::fail("controller.editBusy: Finish or cancel the current gesture first");
    auto candidate = learn.candidate;
    candidate.minimum = minimum;
    candidate.maximum = maximum;
    candidate.pickup = pickup;
    CommandArgs args;
    args.samplerControllerLink = candidate;
    args.samplerControllerReplacements = replacements;
    const juce::ScopedValueSetter<bool> committing(samplerControllerLearnCommitting, true);
    const auto result = controller.execute("channel.sampler.controller.create", args);
    if (result.failed())
    {
        learn.error = result.getErrorMessage();
        return result;
    }
    // Controller observers may refresh synchronously. Mark this operation before
    // reconciling so our own legitimate revision does not become a stale failure.
    learn.documentChanged = true;
    learn.state = "completed";
    learn.error.clear();
    learn.replaced = replacements;
    midiController.cancelProjectLearn(learn.serial);
    if (!controller.project.samplerControllerLinks.empty())
        learn.candidate = controller.project.samplerControllerLinks.back();
    syncSamplerControllerBindings();
    learn.effectiveApplied = samplerControllerProjectionError.isEmpty();
    if (!learn.effectiveApplied)
    {
        learn.state = "failed";
        learn.error = samplerControllerProjectionError;
        return juce::Result::fail(learn.error); // Document/history committed; explicit state tells the truth.
    }
    return juce::Result::ok();
}

void Studio::showSamplerControllerLearn(Id channel, std::uint32_t parameter)
{
    if (auto *existing = samplerControllerLearnWindow.getComponent())
    {
        bringWindowToFront(*existing);
        return;
    }
    const auto started = startSamplerControllerLearn(channel, parameter);
    if (started.failed())
    {
        status = started.getErrorMessage();
        repaint();
        return;
    }
    const auto token = samplerControllerLearn->token;
    samplerControllerLearnWindowToken = token;
    const auto &candidate = samplerControllerLearn->candidate;
    auto *dialog = new SamplerControllerAlert("Link sampler control", "", this,
                                              "Replace the listed conflicting project links", false);
    samplerControllerLearnWindow = dialog;
    dialog->setComponentID("sampler.controller.learn");
    dialog->addTextEditor("minimum", juce::String(candidate.minimum, 15), "Physical value at CC0");
    dialog->addTextEditor("maximum", juce::String(candidate.maximum, 15), "Physical value at CC127");
    dialog->addButton("Link", 1);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    syncSamplerControllerBindings();
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             view = juce::Component::SafePointer<SamplerControllerAlert>(dialog), token](int action)
            {
                if (!safe || !view || !safe->samplerControllerLearn ||
                    safe->samplerControllerLearn->token != token)
                    return;
                if (action != 1)
                {
                    const auto result = safe->cancelSamplerControllerLearn(token);
                    safe->status =
                        result.wasOk() ? "Controller learning cancelled" : result.getErrorMessage();
                    return;
                }
                // Strict scalar parsing rejects invalid/empty/bool text without numeric coercion.
                const auto low = samplerControllerNumericText(view->getTextEditorContents("minimum"));
                const auto high = samplerControllerNumericText(view->getTextEditorContents("maximum"));
                const auto numeric = [](const juce::var &v)
                { return (v.isInt() || v.isInt64() || v.isDouble()) && std::isfinite(double(v)); };
                juce::Result result =
                    juce::Result::fail("controller.invalidRange: Finite physical numbers required");
                if (numeric(low) && numeric(high))
                {
                    std::vector<Id> replacements;
                    const auto current = safe->samplerControllerLearnState();
                    if (view->secondary.getToggleState())
                        if (const auto *ids = current["conflictingProjectLinks"].getArray())
                            for (const auto &id : *ids)
                                replacements.push_back(juce::int64(id));
                    result = safe->acceptSamplerControllerLearn(token, double(low), double(high),
                                                                view->pickup.getToggleState(), replacements);
                }
                if (result.failed())
                {
                    // The modal is closing; release its capture instead of leaving an invisible learner
                    // armed.
                    if (safe->samplerControllerLearn && safe->samplerControllerLearn->token == token &&
                        safe->samplerControllerLearn->state == "pending")
                    {
                        safe->midiController.cancelProjectLearn(safe->samplerControllerLearn->serial);
                        safe->samplerControllerLearn->state = "failed";
                        safe->samplerControllerLearn->error = result.getErrorMessage();
                    }
                    safe->status = result.getErrorMessage();
                }
                else
                    safe->status = "Project controller link added; Save to retain it on disk";
                safe->refresh();
            }),
        true);
}

void Studio::samplerControllerLinksMenu()
{
    syncSamplerControllerBindings();
    juce::PopupMenu menu;
    menu.addSectionHeader("Project sampler controller links");
    std::vector<Id> ids;
    for (const auto &link : controller.project.samplerControllerLinks)
    {
        SamplerAutomationTarget target;
        const auto resolved =
            resolveSamplerAutomationTarget(controller.project, link.channel, link.parameter, target, true);
        auto label = resolved.wasOk() ? target.name : "Unavailable target";
        label += " / CC " + juce::String(link.cc) + " [" + juce::String(link.id) + "]";
        if (!link.enabled)
            label += " (disabled)";
        else if (!target.available)
            label += " (dormant)";
        ids.push_back(link.id);
        menu.addItem(int(ids.size()), label);
    }
    if (ids.empty())
        menu.addItem(1, "Right-click an envelope/LFO control to add a link", false);
    const auto project = controller.projectIdentity;
    menu.showMenuAsync({},
                       [safe = juce::Component::SafePointer<Studio>(this), ids, project](int choice)
                       {
                           if (safe && choice > 0 && choice <= int(ids.size()) &&
                               safe->controller.projectIdentity == project)
                               safe->samplerControllerLinkDialog(ids[size_t(choice - 1)]);
                       });
}

void Studio::samplerControllerLinkDialog(Id id)
{
    const auto found = std::find_if(controller.project.samplerControllerLinks.begin(),
                                    controller.project.samplerControllerLinks.end(),
                                    [id](const auto &link) { return link.id == id; });
    if (found == controller.project.samplerControllerLinks.end())
        return;
    if (auto *existing = samplerControllerPropertiesWindow.getComponent())
    {
        bringWindowToFront(*existing);
        return;
    }
    const auto original = *found;
    const auto project = controller.projectIdentity;
    const auto revision = controller.revision;
    SamplerAutomationTarget target;
    const auto resolved = resolveSamplerAutomationTarget(controller.project, original.channel,
                                                         original.parameter, target, true);
    auto *dialog = new SamplerControllerAlert(
        "Project sampler controller link",
        (resolved.wasOk() ? target.name : "Unavailable target") + "\nPhysical " +
            samplerControllerUnitLabel(samplerControllerUnits(controller.project, original)) +
            ". Fixed target; saved with this project. Removing a link is undoable.",
        this, "Link enabled", original.enabled);
    samplerControllerPropertiesWindow = dialog;
    dialog->setComponentID("sampler.controller.properties");
    dialog->addTextEditor("label", original.label, "Label");
    dialog->addComboBox("port", {"Performance input", "Control input"}, "Existing input");
    dialog->getComboBoxComponent("port")->setSelectedId(original.port + 1);
    juce::StringArray channels{"Any MIDI channel"}, layers;
    for (int channel = 1; channel <= 16; ++channel)
        channels.add("MIDI Ch " + juce::String(channel));
    for (int layer = 0; layer < 4; ++layer)
        layers.add("Layer " + juce::String(layer));
    dialog->addComboBox("channel", channels, "Incoming MIDI channel");
    dialog->getComboBoxComponent("channel")->setSelectedId(original.midiChannel + 1);
    dialog->addTextEditor("cc", juce::String(original.cc), "CC number");
    dialog->addComboBox("layer", layers, "Modifier layer");
    dialog->getComboBoxComponent("layer")->setSelectedId(original.layer + 1);
    dialog->addTextEditor("minimum", juce::String(original.minimum, 15), "Physical value at CC0");
    dialog->addTextEditor("maximum", juce::String(original.maximum, 15), "Physical value at CC127");
    dialog->pickup.setToggleState(original.pickup, juce::dontSendNotification);
    dialog->addButton("Apply", 1);
    dialog->addButton("Remove link", 2);
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             view = juce::Component::SafePointer<SamplerControllerAlert>(dialog), original, project,
             revision](int action)
            {
                if (!safe || !view || !action)
                    return;
                if (safe->controller.projectIdentity != project || safe->controller.revision != revision)
                {
                    safe->status = "controller.staleLink: Project changed; reopen link properties";
                    return;
                }
                CommandArgs args;
                args.target = original.id;
                juce::Result result = juce::Result::ok();
                const auto command =
                    action == 2 ? "channel.sampler.controller.remove" : "channel.sampler.controller.update";
                if (action != 2)
                {
                    auto values = juce::var(new juce::DynamicObject);
                    auto row = samplerControllerLinkState(original);
                    auto *fields = row.getDynamicObject();
                    fields->setProperty("label", view->getTextEditorContents("label"));
                    fields->setProperty("port", view->getComboBoxComponent("port")->getSelectedId() - 1);
                    fields->setProperty("midiChannel",
                                        view->getComboBoxComponent("channel")->getSelectedId() - 1);
                    fields->setProperty("layer", view->getComboBoxComponent("layer")->getSelectedId() - 1);
                    fields->setProperty("cc",
                                        samplerControllerNumericText(view->getTextEditorContents("cc")));
                    const auto minimumText = view->getTextEditorContents("minimum");
                    const auto maximumText = view->getTextEditorContents("maximum");
                    // A name/source-only edit must retain the original double endpoints,
                    // even when the displayed decimal uses fewer significant digits.
                    fields->setProperty("minimum", minimumText == juce::String(original.minimum, 15)
                                                       ? juce::var(original.minimum)
                                                       : samplerControllerNumericText(minimumText));
                    fields->setProperty("maximum", maximumText == juce::String(original.maximum, 15)
                                                       ? juce::var(original.maximum)
                                                       : samplerControllerNumericText(maximumText));
                    fields->setProperty("pickup", view->pickup.getToggleState());
                    fields->setProperty("enabled", view->secondary.getToggleState());
                    values.getDynamicObject()->setProperty("controllerLink", row);
                    values.getDynamicObject()->setProperty("target", juce::int64(original.id));
                    result = decodeSamplerControllerArguments(command, values, args);
                }
                if (result.wasOk())
                    result = safe->controller.execute(command, args);
                safe->syncSamplerControllerBindings();
                safe->status = result.wasOk() ? "Project controller link updated" : result.getErrorMessage();
                safe->refresh();
            }),
        true);
}
