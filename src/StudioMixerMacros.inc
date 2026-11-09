// SPDX-License-Identifier: AGPL-3.0-or-later
// Included by StudioMcp.cpp inside namespace vibera. CONTROL only.
juce::String Studio::macroConfigurationGeneration() const
{
    MacroConfigurationSnapshot value;
    return readMacroConfiguration(customization,profileFile(),value).wasOk() ? value.generation : juce::String{};
}
juce::Result Studio::changeMacro(MacroConfigurationChange change, const Macro &macro,
                                 const juce::String &id, const juce::String &generation)
{
    return changeMacroConfiguration(customization,profileFile(),change,macro,id,generation);
}
juce::Result Studio::runSavedMixerMacro(const juce::String &id, const juce::String &generation)
{
    MacroConfigurationSnapshot value;
    if (auto r = readMacroConfiguration(customization,profileFile(),value); r.failed()) return r;
    if (generation.isEmpty() || generation != value.generation)
        return juce::Result::fail("macro.staleConfiguration: Re-read the saved recipe before running");
    if (!value.exists || !value.matches)
        return juce::Result::fail("macro.notSaved: Save/reconcile this exact recipe before running");
    const auto macro = std::find_if(customization.macros.begin(),customization.macros.end(),
                                    [&](const auto &entry) { return entry.id == id; });
    if (macro == customization.macros.end()) return juce::Result::fail("macro.unavailable: Stable recipe ID missing");
    if (!mixerMacroSupported(*macro))
        return juce::Result::fail("macro.unsupported: Recipe includes a command unavailable in the actual Customization runner");
    juce::String code;
    if (auto admitted = admitMixerInsertOrder(code); admitted.failed()) return admitted;
    lastRoutingDispatchOperation = 0;
    try { runMacro(id); } // Actual shared application/Customization runner and history/cancel policy.
    catch (...)
    {
        // An accepted coordinator owns its operation even if later UI formatting
        // throws. Return the real operation instead of orphaning accepted work.
        if (lastRoutingDispatchOperation) return juce::Result::ok();
        return juce::Result::fail("macro.runInterrupted: Inspect project/history and any retained edit owner; no rollback or DSP completion is asserted");
    }
    return status == "Macro complete" || (lastRoutingDispatchOperation &&
               (status == routingPendingStatus || status == routingReadyStatus))
        ? juce::Result::ok() : juce::Result::fail("macro.runFailed: " + status);
}
juce::var Studio::mcpMacroOperation(const juce::var &args)
{
    const auto action = args["action"].toString();
    auto fail = [](const juce::Result &result)
    { throw McpFailure(result.getErrorMessage().upToFirstOccurrenceOf(":",false,false),result.getErrorMessage()); };
    MacroConfigurationSnapshot view;
    bool configurationCommitted = false;
    if (auto r = readMacroConfiguration(customization,profileFile(),view); r.failed()) fail(r);
    if (action != "state")
    {
        if (args.hasProperty("includeSchemas")) throw McpFailure("invalid_arguments","includeSchemas applies only to state");
        if (!args.hasProperty("generation") || args["generation"].toString() != view.generation)
            throw McpFailure("macro.staleConfiguration","Current generation required before macro work");
    }
    if (action == "create" || action == "update")
    {
        if (!args.hasProperty("macro") || args.hasProperty("id"))
            throw McpFailure("invalid_arguments","create/update requires macro, not a separate id");
        auto input = juce::JSON::parse(juce::JSON::toString(args["macro"]));
        for (auto &step : *input["steps"].getArray())
        {
            const auto command = step["action"].toString();
            if (!mixerMacroCommandSupported(command)) throw McpFailure("macro.unsupported","Command unavailable in actual Customization runner: " + command);
            const auto schema = mixerMacroArguments(command,bool(step.getProperty("currentContext",true)));
            validateMcpSchema(schema,step["parameters"]);
            if (!step.hasProperty("currentContext")) step.getDynamicObject()->setProperty("currentContext",true);
            for (const auto &property : schema["properties"].getDynamicObject()->getProperties())
                if (property.value.hasProperty("default") && !step["parameters"].hasProperty(property.name))
                    step["parameters"].getDynamicObject()->setProperty(property.name,property.value["default"]);
        }
        auto profile = customization.state();
        int version = std::max(5, int(profile["version"]));
        for (const auto &step : *input["steps"].getArray())
        {
            const auto parameters = step["parameters"];
            if (parameters.hasProperty("samplerParameter"))
                version = std::max(version, 17);
            if (parameters.hasProperty("slide") || parameters.hasProperty("portamento") ||
                parameters.hasProperty("colourGroup") || parameters.hasProperty("noteProperties"))
                version = std::max(version, 18);
            if (parameters.hasProperty("automationSendTarget"))
                version = std::max(version, 6);
            if (parameters.hasProperty("appearance") || parameters.hasProperty("channelSort"))
                version = std::max(version, 7);
            if (parameters.hasProperty("soloScope"))
                version = std::max(version, 8);
            if (parameters.hasProperty("recipes"))
                version = std::max(version, 9);
            if (parameters.hasProperty("retainSteps"))
                version = std::max(version, 10);
            if (parameters.hasProperty("recordedNotes"))
            {
                version = std::max(version, 12);
                if (const auto *notes = parameters["recordedNotes"].getArray())
                    for (const auto &note : *notes)
                    {
                        if (note.hasProperty("slide") || note.hasProperty("portamento") ||
                            note.hasProperty("colourGroup"))
                            version = std::max(version, 18);
                        if (note.hasProperty("shiftPercent"))
                            version = std::max(version, 13);
                        if (note.hasProperty("repeatMode"))
                            version = std::max(version, 14);
                        if (note.hasProperty("releaseVelocity"))
                            version = std::max(version, 15);
                        if (note.hasProperty("modX") || note.hasProperty("modY"))
                            version = std::max(version, 16);
                    }
            }
        }
        profile.getDynamicObject()->setProperty("version", version);
        profile.getDynamicObject()->setProperty("macros",juce::Array<juce::var>{input});
        Customization decoded;
        if (auto r = decoded.importState(profile); r.failed()) throw McpFailure("macro.validationFailed",r.getErrorMessage());
        if (decoded.macros.size() != 1) throw McpFailure("macro.validationFailed","Exactly one recipe required");
        if (auto r = changeMacro(action == "create" ? MacroConfigurationChange::create : MacroConfigurationChange::update,
                                 decoded.macros.front(),{},view.generation); r.failed()) fail(r);
        configurationCommitted = true;
    }
    else if (action == "remove" || action == "run")
    {
        if (!args.hasProperty("id") || args.hasProperty("macro"))
            throw McpFailure("invalid_arguments","remove/run requires stable id only");
        const auto id = args["id"].toString();
        const auto recipe = std::find_if(customization.macros.begin(),customization.macros.end(),
                                         [&](const auto &macro) { return macro.id == id; });
        if (recipe == customization.macros.end()) throw McpFailure("macro.unavailable","Stable recipe ID missing");
        if (!mixerMacroSupported(*recipe)) throw McpFailure("macro.unsupported","Recipe contains unsupported Customization command");
        if (auto r = action == "remove" ? changeMacro(MacroConfigurationChange::remove,{},id,view.generation)
                                        : runSavedMixerMacro(id,view.generation); r.failed()) fail(r);
        configurationCommitted = action == "remove";
    }
    else if (action == "exportProfile" || action == "importProfile")
    {
        // Same Customization save/load as the Settings > Shortcuts Export / Import buttons.
        if (args.hasProperty("macro") || args.hasProperty("id") || args.hasProperty("shortcut"))
            throw McpFailure("invalid_arguments","Profile actions take path (and overwrite) only");
        const auto path = args["path"].toString();
        if (!juce::File::isAbsolutePath(path)) throw McpFailure("invalid_arguments","path must be absolute");
        const juce::File file(path);
        if (action == "exportProfile")
        {
            if (file.exists() && !bool(args["overwrite"])) throw McpFailure("exists","File exists; pass overwrite:true");
            if (auto r = customization.save(file.withFileExtension("json")); r.failed()) throw McpFailure("shortcut.saveFailed", r.getErrorMessage());
        }
        else
        {
            if (customization.running) throw McpFailure("macro.busy","A macro is running");
            auto candidate = customization;
            if (auto r = candidate.load(file); r.failed()) throw McpFailure("shortcut.importFailed", r.getErrorMessage());
            customization = std::move(candidate);
            if (auto r = customization.save(profileFile()); r.failed()) throw McpFailure("shortcut.saveFailed", r.getErrorMessage());
            configurationCommitted = true;
        }
    }
    else if (action == "bindShortcut" || action == "unbindShortcut" || action == "resetShortcuts")
    {
        // Same Customization calls as Settings > Shortcuts, then the same profile save.
        if (args.hasProperty("macro") || args.hasProperty("id"))
            throw McpFailure("invalid_arguments","Shortcut actions take shortcut (and generation) only");
        if (customization.running) throw McpFailure("macro.busy","A macro is running");
        if (action == "resetShortcuts")
        {
            if (args.hasProperty("shortcut")) throw McpFailure("invalid_arguments","resetShortcuts takes no shortcut");
            auto macros = customization.macros;
            const bool restore = customization.restoreMuteAfterSolo;
            customization = Customization::familiarDefaults();
            customization.macros = std::move(macros);
            customization.restoreMuteAfterSolo = restore;
        }
        else
        {
            const auto s = args["shortcut"];
            if (!s.isObject()) throw McpFailure("invalid_arguments","shortcut is required");
            const auto command = s["command"].toString();
            const int scope = int(s["scope"]);
            if (scope < 0 || scope > int(Scope::sampler)) throw McpFailure("invalid_arguments","Invalid scope");
            if (action == "unbindShortcut")
                customization.unbind(command, Scope(scope));
            else
            {
                const int key = int(s["key"]);
                if (key < 1 || key > 65535) throw McpFailure("invalid_arguments","key is 1..65535");
                int mods = 0;
                if (bool(s["ctrl"])) mods |= juce::ModifierKeys::ctrlModifier;
                if (bool(s["shift"])) mods |= juce::ModifierKeys::shiftModifier;
                if (bool(s["alt"])) mods |= juce::ModifierKeys::altModifier;
                const auto r = customization.bind(command, Scope(scope), juce::KeyPress(key, juce::ModifierKeys(mods), 0),
                                                  bool(s["alternate"]));
                if (r.failed())
                    throw McpFailure(r.getErrorMessage().startsWith("Shortcut conflicts") ? "shortcut.conflict" : "shortcut.invalid",
                                     r.getErrorMessage());
            }
        }
        if (auto r = customization.save(profileFile()); r.failed()) throw McpFailure("shortcut.saveFailed", r.getErrorMessage());
        configurationCommitted = true;
    }
    else if (action != "state") throw McpFailure("invalid_arguments","Unknown macro action");
    if (action == "state" && (args.hasProperty("macro") || args.hasProperty("generation")))
        throw McpFailure("invalid_arguments","state takes optional id/includeSchemas only");
    // Run never modifies configuration. Preserve its admitted snapshot so a
    // later disk read failure cannot hide an already accepted async route job.
    if (action != "run") if (auto r = readMacroConfiguration(customization,profileFile(),view); r.failed())
    {
        if (configurationCommitted)
            throw McpFailure("macro.configurationCommittedReadbackFailed",
                "Settings publication completed; readback failed. Re-inspect state before retrying. " + r.getErrorMessage());
        fail(r);
    }
    auto result = obj(); set(result,"completionState","completed");
    set(result,"completionScope",action == "run" ? "canonicalMacroRun" : action == "state" ? "macroConfigurationReadback" : "persistedMacroConfiguration");
    set(result,"generation",view.generation); set(result,"profileVersion",view.current["version"]);
    set(result,"profileReadbackScope",action == "run" ? "preRunAdmission" : "completionReadback");
    set(result,"profileExists",view.exists); set(result,"profileMatchesMemory",view.matches);
    set(result,"configurationCommitted",configurationCommitted);
    set(result,"configurationUndoable",false); set(result,"dspSettlingAcknowledged",false);
    set(result,"cancellation","UI draft is discardable before Save. Synchronous canonical run uses actual one-Undo history. A routing-only run exposes its real operation and pending/terminal state; poll vibera_mixer routingJob, then checked Undo after publication. Mixed routing/content sequences remain explicitly refused by the existing runner.");
    if (action == "run" && lastRoutingDispatchOperation)
    {
        const auto routing = channelRoutingState(lastRoutingDispatchOperation);
        set(result,"operation",juce::int64(lastRoutingDispatchOperation)); set(result,"routing",routing);
        set(result,"completionState",routing["completionState"]);
        if (routing.hasProperty("error")) set(result,"error",routing["error"]);
    }
    juce::Array<juce::var> macros, supported;
    bool foundTarget = !args.hasProperty("id") || action == "remove";
    for (size_t i = 0; i < customization.macros.size(); ++i)
    {
        const auto &recipe = customization.macros[i];
        if (action == "state" && args.hasProperty("id") && recipe.id != args["id"].toString()) continue;
        foundTarget = true;
        auto row = obj(); set(row,"id",recipe.id); set(row,"name",recipe.name); set(row,"stepCount",int(recipe.steps.size()));
        set(row,"editable",mixerMacroSupported(recipe)); set(row,"persisted",view.exists && view.matches);
        if (args.hasProperty("id") || action == "create" || action == "update")
        {
            juce::Array<juce::var> steps;
            for (const auto &stored : *view.current["macros"][int(i)]["steps"].getArray())
            {
                if (!mixerMacroCommandSupported(stored["action"].toString())) { steps.add(stored); continue; }
                auto step = obj(), parameters = obj();
                set(step,"action",stored["action"]); set(step,"currentContext",stored["currentContext"]);
                const auto schema = mixerMacroArguments(stored["action"].toString(),bool(stored["currentContext"]));
                for (const auto &property : schema["properties"].getDynamicObject()->getProperties())
                    if (stored["parameters"].hasProperty(property.name))
                        parameters.getDynamicObject()->setProperty(property.name,stored["parameters"][property.name]);
                set(step,"parameters",parameters); steps.add(step);
            }
            set(row,"steps",steps);
        }
        macros.add(row);
    }
    if (!foundTarget) throw McpFailure("macro.unavailable","Stable recipe ID missing");
    for (const auto &command : commandRegistry())
        if (mixerMacroCommandSupported(command.id))
        {
            auto row = obj(); set(row,"id",command.id);
            if (bool(args["includeSchemas"]))
            { set(row,"explicitArguments",mixerMacroArguments(command.id,false)); set(row,"currentArguments",mixerMacroArguments(command.id,true)); }
            supported.add(row);
        }
    set(result,"macros",macros); set(result,"supportedCommands",supported);
    if (action.endsWith("Shortcut") || action == "resetShortcuts" || action.endsWith("Profile") || bool(args["includeShortcuts"]))
    {
        juce::Array<juce::var> shortcuts;
        for (const auto &b : customization.bindings)
        {
            auto row = obj(); set(row,"command",b.action); set(row,"scope",int(b.scope));
            set(row,"key",b.key.getKeyCode()); set(row,"ctrl",b.key.getModifiers().isCtrlDown());
            set(row,"shift",b.key.getModifiers().isShiftDown()); set(row,"alt",b.key.getModifiers().isAltDown());
            set(row,"text",b.key.getTextDescription()); shortcuts.add(row);
        }
        set(result,"shortcuts",shortcuts);
    }
    set(result,"projectRevision",juce::int64(controller.revision)); set(result,"projectIdentity",controller.projectIdentity);
    return textResult(result);
}
