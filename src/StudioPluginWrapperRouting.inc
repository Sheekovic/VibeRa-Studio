// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTROL-only bridge. Native bus negotiation/publication remains owned by the
// existing canonical plugin.io structural transaction, never the audio callback.
juce::Result Studio::requestPluginAudioIo(Id plugin, const juce::String &instance, const PluginMainIo &io,
                                          juce::String &operation, juce::String &errorCode)
{
    operation.clear();
    errorCode.clear();
    const auto refuse = [&](const char *code, const juce::String &message)
    {
        errorCode = code;
        return juce::Result::fail(message);
    };
    if (!io.valid())
        return refuse("pluginIo.invalidArguments", "Complete supported bus indices/channel mapping required");
    const auto host = audio.preparedPluginHost(controller.project, plugin);
    const auto *spec = controller.project.plugin(plugin);
    if (!spec || !host || !host->alive() || host->runtimeIdentity() != instance)
        return refuse("stale_instance", "Prepare and rediscover this exact plugin worker");
    if (recordArmed || midiTaking || hasPendingMidiTake() || automationRecordingActive())
        return refuse("recording_busy",
                      "Finish/retry/cancel recording before reconfiguring native audio I/O");
    PluginBusCatalogue buses;
    if (!host->readBusCatalogue(buses) || !buses.valid())
        return refuse("pluginIo.unavailable", "Native bus catalogue is unavailable");
    if ((buses.inputCount ? io.inputBus >= buses.inputCount : io.inputBus != 0) ||
        io.outputBus >= buses.outputCount)
        return refuse("pluginIo.invalidArguments", "Selected native bus does not exist in this worker");
    auto candidate = controller.project;
    if (auto valid = setPluginMainIo(candidate, plugin, io); valid.failed())
        return refuse("pluginIo.invalidArguments", valid.getErrorMessage());
    if (std::any_of(pluginOperations.begin(), pluginOperations.end(),
                    [&](const auto &job) { return job.plugin == plugin && job.state == "pending"; }))
        return refuse("busy", "Another operation for this plugin is still pending");
    if (pluginOperations.size() >= 64)
    {
        const auto terminal = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                           [](const auto &job) { return job.state != "pending"; });
        if (terminal == pluginOperations.end())
            return refuse("capacity_exceeded", "All plugin operation slots are in use");
        pluginOperations.erase(terminal);
    }
    // Build every request/owner before the structural transaction can start.
    auto args = juce::var(new juce::DynamicObject);
    auto selection = juce::var(new juce::DynamicObject);
    selection.getDynamicObject()->setProperty("inputBus", io.inputBus);
    selection.getDynamicObject()->setProperty("outputBus", io.outputBus);
    selection.getDynamicObject()->setProperty("inputChannels", io.inputChannels);
    selection.getDynamicObject()->setProperty("monoSource", io.monoSource);
    args.getDynamicObject()->setProperty("target", juce::int64(plugin));
    args.getDynamicObject()->setProperty("pluginIo", selection);
    auto request = juce::var(new juce::DynamicObject);
    request.getDynamicObject()->setProperty("command", "plugin.io");
    request.getDynamicObject()->setProperty("args", args);
    request.getDynamicObject()->setProperty("projectIdentity", controller.projectIdentity);
    request.getDynamicObject()->setProperty("revision", juce::int64(controller.revision));
    PluginOperation job;
    job.id = juce::Uuid().toString();
    job.kind = "audioIo";
    job.action = "plugin.io";
    job.plugin = plugin;
    job.project = controller.projectIdentity;
    job.instance = instance;
    job.host = host;
    job.io = io;
    job.started = juce::Time::getMillisecondCounter();
    job.revision = controller.revision;
    const auto token = job.id;
    const auto revision = controller.revision;
    pluginOperations.reserve(pluginOperations.size() + 1);
    pluginOperations.push_back(std::move(job));
    operation = token;
    juce::String child;
    const auto owner = [&]() -> PluginOperation &
    {
        return *std::find_if(pluginOperations.begin(), pluginOperations.end(),
                             [&](const auto &row) { return row.id == token; });
    };
    const auto fail = [&](const juce::String &code, const juce::String &message)
    {
        auto &parent = owner();
        parent.childOperation = child;
        parent.message = message;
        if (child.isNotEmpty())
            return; // Actual canonical transaction, not response construction, owns the outcome.
        parent.code = code;
        parent.state = "failed";
        parent.historyCommitted = false; // No owned child exists; another job may have advanced revision.
        parent.completedRevision = controller.revision;
        parent.host.reset();
    };
    try
    {
        const auto response = mcpCall("vibera_command", request, &child);
        auto &parent = owner();
        parent.childOperation = child;
        if (child.isNotEmpty())
            parent.message =
                "Native I/O reconfiguration pending; editor closure/capture/publication are owned";
        else
        {
            const auto result = response["structuredContent"];
            const auto *current = controller.project.plugin(plugin);
            const bool unchanged = !bool(response["isError"]) &&
                                   result["completionState"].toString() == "completed" && current &&
                                   pluginMainIoFor(*current) == io && controller.revision == revision;
            parent.state = unchanged ? "completed" : "failed";
            parent.code = unchanged ? "" : "pluginIo.admissionFailed";
            parent.message = unchanged ? "Native I/O already matches; document unchanged"
                                       : "Canonical I/O command did not admit a structural transaction";
            parent.historyCommitted = false; // Only an owned structural receipt can witness this commit.
            parent.completedRevision = controller.revision;
            parent.host.reset();
        }
    }
    catch (const McpFailure &error)
    {
        fail(error.code, error.what());
    }
    catch (const std::exception &error)
    {
        fail("pluginIo.preparationFailed", error.what());
    }
    catch (...)
    {
        fail("pluginIo.preparationFailed", "Inspect the retained canonical I/O operation after an exception");
    }
    status = owner().message;
    return juce::Result::ok(); // An owned terminal failure is still an observable admitted operation.
}
