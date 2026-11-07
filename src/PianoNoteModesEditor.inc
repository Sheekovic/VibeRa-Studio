// SPDX-License-Identifier: AGPL-3.0-or-later
// Included in the existing Editor class. CONTROL/UI only; no audio ownership.
juce::TextButton pianoInsertSlide{"Slide"}, pianoInsertPorta{"Porta"}, pianoProperties{"Properties"},
    pianoRecolour{"Recolor"}, pianoSelectColour{"By color"};
juce::ComboBox pianoColour;
bool insertSlide = false, insertPortamento = false, updatingNotePropertiesUi = false;
int insertColour = 0;
struct NotePropertiesSession
{
    juce::String operation, project, state{"pending"}, error;
    std::uint64_t revision = 0;
    Id pattern = 0, channel = 0;
    std::vector<Id> ids;
    juce::var baseline, baselineNotes, draft;
    bool local = false;
};
std::optional<NotePropertiesSession> notePropertiesSession;
std::unique_ptr<PianoNotePropertiesWindow> notePropertiesWindow;
int noteModeRows() const
{
    int rows = 1, x = 4;
    for (const int width : {100, 54, 54, 84, 68, 76})
    {
        if (x + width > getWidth() - rightInset() && x > 4)
        {
            ++rows;
            x = 4;
        }
        x += width + 4;
    }
    return rows;
}
bool localNoteModes(Id channel) const
{
    const auto *value = s.controller.project.channel(channel);
    return value && !value->instrument && !value->audioClip;
}
static juce::var noteModeArgs(const char *action)
{
    auto result = juce::var(new juce::DynamicObject);
    result.getDynamicObject()->setProperty("action", action);
    return result;
}
std::vector<Id> noteModeTargets(Id patternId, Id channelId, const juce::var &args, bool allIfEmpty) const
{
    const auto *pattern = s.controller.project.pattern(patternId);
    const auto *channel = s.controller.project.channel(channelId);
    if (!pattern || !channel || channel->audioClip)
        throw McpFailure("noteModes.unavailableTarget", "Existing score Pattern/Channel required");
    // CONTROL-local index; preserve explicit/selection order without IDs x notes scans.
    std::map<Id, const Note *> sources;
    for (const auto &note : pattern->notes)
        if (note.channel == channelId)
            sources.emplace(note.id, &note);
    std::vector<Id> ids;
    if (const auto *explicitIds = args["ids"].getArray())
    {
        ids.reserve(size_t(explicitIds->size()));
        for (const auto &id : *explicitIds)
            ids.push_back(juce::int64(id));
    }
    else
    {
        ids.reserve(s.selectedNotes.size());
        for (auto id : s.selectedNotes)
            if (sources.contains(id))
                ids.push_back(id);
    }
    if (ids.empty() && allIfEmpty)
        for (const auto &note : pattern->notes)
            if (note.channel == channelId)
                ids.push_back(note.id);
    if (ids.empty())
        throw McpFailure("noteModes.noTargets", "Select one or more source notes first");
    std::set<Id> seen;
    for (auto id : ids)
    {
        if (!sources.contains(id) || !seen.insert(id).second)
            throw McpFailure("noteModes.invalidTargets",
                             "Distinct source IDs in the captured Channel required");
    }
    return ids;
}
void syncNoteModesToolbar()
{
    if (type != 1)
        return;
    const bool local = localNoteModes(s.selectedChannel);
    pianoInsertSlide.setEnabled(local);
    pianoInsertPorta.setEnabled(local);
    pianoInsertSlide.setToggleState(local && insertSlide, juce::dontSendNotification);
    pianoInsertPorta.setToggleState(local && insertPortamento, juce::dontSendNotification);
    pianoColour.setSelectedId(insertColour + 1, juce::dontSendNotification);
    pianoColour.setColour(juce::ComboBox::textColourId, ink);
    pianoColour.setColour(juce::ComboBox::backgroundColourId,
                          panel.interpolatedWith(accent.withRotatedHue(float(insertColour) / 16.f), .15f));
}
void setupNoteModesToolbar()
{
    for (auto *button :
         {&pianoInsertSlide, &pianoInsertPorta, &pianoProperties, &pianoRecolour, &pianoSelectColour})
        addAndMakeVisible(*button);
    addAndMakeVisible(pianoColour);
    pianoColour.setComponentID("piano.noteColour");
    for (int index = 0; index < 16; ++index)
        pianoColour.addItem("Color " + juce::String(index + 1), index + 1);
    pianoColour.setTooltip(
        "Note-color / MIDI group1..16; current insertion color. Alt+C recolors selection.");
    pianoColour.onChange = [this]
    {
        auto args = noteModeArgs("configure");
        args.getDynamicObject()->setProperty("colourGroup", pianoColour.getSelectedId() - 1);
        s.pianoNoteModes(args);
    };
    const std::array<std::pair<juce::TextButton *, const char *>, 5> actions{
        {{&pianoInsertSlide, "piano.insertSlide"},
         {&pianoInsertPorta, "piano.insertPortamento"},
         {&pianoProperties, "piano.noteProperties"},
         {&pianoRecolour, "piano.changeColour"},
         {&pianoSelectColour, "piano.selectColour"}}};
    for (const auto &[button, action] : actions)
    {
        button->setComponentID(action);
        button->onClick = [this, action]
        {
            s.focus = Scope::piano;
            grabKeyboardFocus();
            s.performAction(action);
        };
    }
    pianoInsertSlide.setTooltip(
        "Slide control note [S]: silent, matching-color sampler notes only; Repeat refuses.");
    pianoInsertPorta.setTooltip("Porta inversion [O]: invert the Channel Porta flag for newly drawn notes.");
    pianoProperties.setTooltip("Note Properties: selected source notes, staged Apply / Reset / Cancel.");
    pianoRecolour.setTooltip(
        "Change selection to current color [Alt+C]; no selection means all notes in this Channel.");
    pianoSelectColour.setTooltip("Select source notes in the current color group [Shift+C].");
    syncNoteModesToolbar();
}
void layoutNoteModesToolbar()
{
    int x = 4, y = 32;
    const std::array<std::pair<juce::Component *, int>, 6> controls{{{&pianoColour, 100},
                                                                     {&pianoInsertSlide, 54},
                                                                     {&pianoInsertPorta, 54},
                                                                     {&pianoProperties, 84},
                                                                     {&pianoRecolour, 68},
                                                                     {&pianoSelectColour, 76}}};
    for (const auto &[control, width] : controls)
    {
        if (x + width > getWidth() - rightInset() && x > 4)
        {
            x = 4;
            y += 30;
        }
        control->setBounds(x, y, width, 24);
        x += width + 4;
    }
}
juce::var noteModesState() const
{
    auto result = juce::var(new juce::DynamicObject);
    auto &out = *result.getDynamicObject();
    const bool local = localNoteModes(s.selectedChannel);
    out.setProperty("completionState", "completed");
    out.setProperty("completionScope", "pianoSessionState");
    out.setProperty("projectIdentity", s.controller.projectIdentity);
    out.setProperty("revision", juce::int64(s.controller.revision));
    out.setProperty("pattern", juce::int64(s.selectedPattern));
    out.setProperty("channel", juce::int64(s.selectedChannel));
    out.setProperty("slide", insertSlide);
    out.setProperty("portamento", insertPortamento);
    out.setProperty("effectiveSlide", local && insertSlide);
    out.setProperty("effectivePortamento", local && insertPortamento);
    out.setProperty("colourGroup", insertColour);
    out.setProperty("midiChannel", insertColour + 1);
    out.setProperty("transitionSupported", local);
    out.setProperty("transitionAcknowledged", juce::var{});
    out.setProperty("nativePluginUiControl", false);
    if (notePropertiesSession)
    {
        const auto &session = *notePropertiesSession;
        auto properties = juce::var(new juce::DynamicObject);
        auto &value = *properties.getDynamicObject();
        value.setProperty("operation", session.operation);
        value.setProperty("completionState", session.state);
        value.setProperty("projectIdentity", session.project);
        value.setProperty("revision", juce::int64(session.revision));
        value.setProperty("pattern", juce::int64(session.pattern));
        value.setProperty("channel", juce::int64(session.channel));
        juce::Array<juce::var> ids;
        for (auto id : session.ids)
            ids.add(juce::int64(id));
        value.setProperty("ids", ids);
        value.setProperty("baseline", session.baseline.clone());
        value.setProperty("baselineNotes", session.baselineNotes.clone());
        value.setProperty("noteProperties", session.draft.clone());
        value.setProperty("transitionSupported", session.local);
        value.setProperty("timingAvailable", session.ids.size() == 1);
        value.setProperty("stale",
                          session.state == "pending" && (session.project != s.controller.projectIdentity ||
                                                         session.revision != s.controller.revision));
        value.setProperty("error", session.error);
        value.setProperty("visible", notePropertiesWindow && notePropertiesWindow->isVisible());
        out.setProperty("properties", properties);
    }
    else
        out.setProperty("properties", juce::var{});
    return result;
}
void syncNotePropertiesWindow()
{
    if (notePropertiesWindow && notePropertiesSession && !updatingNotePropertiesUi)
        notePropertiesWindow->panel->sync(
            notePropertiesSession->baseline, notePropertiesSession->draft, notePropertiesSession->local,
            notePropertiesSession->ids.size() == 1, notePropertiesSession->baselineNotes);
}

public:
bool ownsNotePropertiesOperation(const juce::String &operation) const
{
    return notePropertiesSession && notePropertiesSession->operation == operation;
}
void noteModesError(const juce::String &message, const juce::String &operation)
{
    if (ownsNotePropertiesOperation(operation))
    {
        notePropertiesSession->error = message;
        if (notePropertiesWindow)
            notePropertiesWindow->panel->setError(message);
    }
}
void fillNoteInsertion(CommandArgs &args) const
{
    args.colourGroup = insertColour;
    args.slide = localNoteModes(args.channel) && insertSlide;
    args.portamento = localNoteModes(args.channel) && insertPortamento;
}
juce::Result verifyNoteModes(const juce::File &);
juce::var noteModes(const juce::var &args)
{
    validateMcpSchema(pianoNoteModesToolDefinition()["inputSchema"], args);
    if (args.hasProperty("projectIdentity") &&
        args["projectIdentity"].toString() != s.controller.projectIdentity)
        throw McpFailure("stale_project", "Project session changed; read current state");
    const auto action = args["action"].toString();
    const bool properties = action.startsWith("properties");
    for (const auto &field : args.getDynamicObject()->getProperties())
    {
        const auto key = field.name.toString();
        const bool applicable =
            key == "action" || key == "projectIdentity" ||
            (action == "configure" && (key == "slide" || key == "portamento" || key == "colourGroup")) ||
            ((action == "propertiesOpen" || action == "changeColour" || action == "selectColour") &&
             (key == "pattern" || key == "channel" || (key == "ids" && action != "selectColour"))) ||
            (properties && action != "propertiesOpen" && key == "operation") ||
            (action == "propertiesUpdate" && key == "noteProperties");
        if (!applicable)
            throw McpFailure("noteModes.invalidArguments", "Argument does not apply to this action: " + key);
    }
    auto result = noteModesState();
    bool changed = false;
    if (action == "configure")
    {
        if (!args.hasProperty("slide") && !args.hasProperty("portamento") && !args.hasProperty("colourGroup"))
            throw McpFailure("noteModes.invalidArguments", "Provide an explicit insertion setting");
        if ((bool(args["slide"]) || bool(args["portamento"])) && !localNoteModes(s.selectedChannel))
            throw McpFailure("noteModes.unsupportedTarget",
                             "Slide/Porta insertion requires the local sampler");
        if (args.hasProperty("slide"))
            insertSlide = bool(args["slide"]);
        if (args.hasProperty("portamento"))
            insertPortamento = bool(args["portamento"]);
        if (args.hasProperty("colourGroup"))
            insertColour = int(args["colourGroup"]);
    }
    else if (action == "changeColour" || action == "selectColour" || action == "propertiesOpen")
    {
        const Id patternId =
            args.hasProperty("pattern") ? Id(juce::int64(args["pattern"])) : s.selectedPattern;
        const Id channelId =
            args.hasProperty("channel") ? Id(juce::int64(args["channel"])) : s.selectedChannel;
        if (action == "selectColour")
        {
            const auto *pattern = s.controller.project.pattern(patternId);
            if (!pattern || !s.controller.project.channel(channelId))
                throw McpFailure("noteModes.unavailableTarget", "Existing Pattern/Channel required");
            if (patternId != s.selectedPattern)
                throw McpFailure("noteModes.context",
                                 "Select the target Pattern before changing its selection");
            std::vector<Id> ids;
            for (const auto &note : pattern->notes)
                if (note.channel == channelId && note.colourGroup == insertColour)
                    ids.push_back(note.id);
            s.selectItems(false, ids, "replace");
        }
        else
        {
            const auto ids = noteModeTargets(patternId, channelId, args, action == "changeColour");
            if (action == "changeColour")
            {
                CommandArgs command;
                command.pattern = patternId;
                command.channel = channelId;
                command.ids = ids;
                command.value = insertColour;
                const auto before = s.controller.revision;
                s.command("notes.colourGroup", command);
                if (s.status != s.routingReadyStatus)
                    throw McpFailure("noteModes.commandFailed", s.status);
                changed = before != s.controller.revision;
            }
            else
            {
                if (s.controller.hasGesture())
                    throw McpFailure("noteModes.busy", "Finish the active edit first");
                if (notePropertiesSession && notePropertiesSession->state == "pending")
                {
                    auto previousIds = notePropertiesSession->ids, requestedIds = ids;
                    std::sort(previousIds.begin(), previousIds.end());
                    std::sort(requestedIds.begin(), requestedIds.end());
                    if (notePropertiesSession->project == s.controller.projectIdentity &&
                        notePropertiesSession->pattern == patternId &&
                        notePropertiesSession->channel == channelId && previousIds == requestedIds)
                    {
                        if (notePropertiesWindow)
                        {
                            notePropertiesWindow->setVisible(true);
                            bringWindowToFront(*notePropertiesWindow);
                        }
                        auto current = noteModesState();
                        current.getDynamicObject()->setProperty("operation",
                                                                notePropertiesSession->operation);
                        current.getDynamicObject()->setProperty("completionState", "pending");
                        return current;
                    }
                    if (!notePropertiesSession->draft.getDynamicObject()->getProperties().isEmpty() ||
                        (notePropertiesWindow && notePropertiesWindow->panel->hasEdits()))
                        throw McpFailure("noteModes.busy",
                                         "Apply or Cancel the current Note Properties first");
                }
                NotePropertiesSession session;
                session.operation = juce::Uuid().toString();
                session.project = s.controller.projectIdentity;
                session.revision = s.controller.revision;
                session.pattern = patternId;
                session.channel = channelId;
                session.ids = ids;
                session.local = localNoteModes(channelId);
                const auto *pattern = s.controller.project.pattern(patternId);
                // The same captured IDs define both the first baseline and ordered readback.
                // No persistent cache/pointers cross this CONTROL preparation operation.
                std::map<Id, const Note *> sources;
                for (const auto &note : pattern->notes)
                    if (note.channel == channelId)
                        sources.emplace(note.id, &note);
                session.baseline = pianoNoteValues(*sources.at(ids.front()));
                juce::Array<juce::var> baselineNotes;
                baselineNotes.ensureStorageAllocated(int(ids.size()));
                for (auto id : ids)
                {
                    auto value = pianoNoteValues(*sources.at(id));
                    value.getDynamicObject()->setProperty("id", juce::int64(id));
                    baselineNotes.add(value);
                }
                session.baselineNotes = baselineNotes;
                session.draft = juce::var(new juce::DynamicObject);
                notePropertiesSession = std::move(session);
                notePropertiesWindow = std::make_unique<PianoNotePropertiesWindow>();
                const auto operation = notePropertiesSession->operation;
                const auto safe = juce::Component::SafePointer<Editor>(this);
                notePropertiesWindow->panel->edit = [safe, operation](const juce::var &patch)
                {
                    if (!safe)
                        return false;
                    auto request = noteModeArgs("propertiesUpdate");
                    request.getDynamicObject()->setProperty("operation", operation);
                    request.getDynamicObject()->setProperty("noteProperties", patch);
                    const juce::ScopedValueSetter<bool> guard(safe->updatingNotePropertiesUi, true);
                    return safe->s.pianoNoteModes(request)["completionState"].toString() != "failed";
                };
                const auto finish = [safe, operation](int button)
                {
                    if (!safe)
                        return;
                    auto request = noteModeArgs(button == 1   ? "propertiesApply"
                                                : button == 2 ? "propertiesReset"
                                                              : "propertiesCancel");
                    request.getDynamicObject()->setProperty("operation", operation);
                    safe->s.pianoNoteModes(request);
                };
                notePropertiesWindow->panel->finish = finish;
                notePropertiesWindow->cancel = [finish] { finish(3); };
                syncNotePropertiesWindow();
                notePropertiesWindow->setVisible(true);
                bringWindowToFront(*notePropertiesWindow);
            }
        }
    }
    else if (properties)
    {
        if (!notePropertiesSession || !args.hasProperty("operation") ||
            args["operation"].toString() != notePropertiesSession->operation)
            throw McpFailure("noteModes.staleOperation", "Exact current properties operation required");
        auto &session = *notePropertiesSession;
        if (action != "propertiesState" && session.state != "pending")
            throw McpFailure("noteModes.finished", "Properties are terminal; open a new operation");
        if (action == "propertiesCancel")
        {
            session.state = "cancelled";
            if (notePropertiesWindow)
                notePropertiesWindow->setVisible(false);
        }
        else if (action != "propertiesState")
        {
            if (session.project != s.controller.projectIdentity || session.revision != s.controller.revision)
                throw McpFailure("noteModes.staleContext",
                                 "Project changed; Cancel and reopen Note Properties");
            if (action == "propertiesUpdate")
            {
                if (!args.hasProperty("noteProperties"))
                    throw McpFailure("noteModes.invalidArguments", "Provide a staged property object");
                const auto patch = args["noteProperties"];
                if (!patch.getDynamicObject())
                    throw McpFailure("noteModes.invalidProperties", "Expected property object");
                NotePropertiesPatch decoded;
                if (!patch.getDynamicObject()->getProperties().isEmpty())
                    if (const auto parsed = decodeNotePropertiesPatch(patch, decoded); parsed.failed())
                        throw McpFailure("noteModes.invalidProperties", parsed.getErrorMessage());
                if (session.ids.size() != 1 &&
                    (patch.hasProperty("beat") || patch.hasProperty("length") || patch.hasProperty("pitch")))
                    throw McpFailure("noteModes.multipleTiming",
                                     "Time / pitch edit requires one captured source");
                auto merged = session.draft.clone();
                for (const auto &field : patch.getDynamicObject()->getProperties())
                    merged.getDynamicObject()->setProperty(field.name, field.value);
                session.draft = merged;
                session.error.clear();
                syncNotePropertiesWindow();
            }
            else if (action == "propertiesReset")
            {
                session.draft = juce::var(new juce::DynamicObject);
                session.error.clear();
                syncNotePropertiesWindow();
            }
            else if (action == "propertiesApply")
            {
                if (!session.draft.getDynamicObject()->getProperties().isEmpty())
                {
                    NotePropertiesPatch patch;
                    if (const auto parsed = decodeNotePropertiesPatch(session.draft, patch); parsed.failed())
                        throw McpFailure("noteModes.invalidProperties", parsed.getErrorMessage());
                    CommandArgs command;
                    command.pattern = session.pattern;
                    command.channel = session.channel;
                    command.ids = session.ids;
                    command.noteProperties = patch;
                    const auto before = s.controller.revision;
                    s.command("notes.properties", command);
                    if (s.status != s.routingReadyStatus)
                        throw McpFailure("noteModes.commandFailed", s.status);
                    changed = before != s.controller.revision;
                }
                session.state = "completed";
                if (notePropertiesWindow)
                    notePropertiesWindow->setVisible(false);
            }
        }
    }
    syncNoteModesToolbar();
    repaint();
    result = noteModesState();
    if (properties && notePropertiesSession)
    {
        result.getDynamicObject()->setProperty("operation", notePropertiesSession->operation);
        result.getDynamicObject()->setProperty("completionState", notePropertiesSession->state);
        result.getDynamicObject()->setProperty("completionScope", "capturedNoteProperties");
    }
    result.getDynamicObject()->setProperty("documentChanged", changed);
    return result;
}
void noteProperties(Id clicked)
{
    auto request = noteModeArgs("propertiesOpen");
    if (clicked && !selected(s.selectedNotes, clicked))
        request.getDynamicObject()->setProperty("ids", juce::Array<juce::var>{juce::int64(clicked)});
    s.pianoNoteModes(request);
}

private:
