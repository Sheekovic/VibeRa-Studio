// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera. Message/CONTROL thread only.
bool Studio::nativeScalarPending(const juce::String &operation) const
{
    return operation.isNotEmpty() &&
           std::any_of(pluginOperations.begin(), pluginOperations.end(),
                       [&](const auto &job) { return job.id == operation && job.state == "pending"; });
}
juce::Result Studio::nativeScalarAdmission(bool recordingWriteOnly)
{
    const bool liveTake =
        recordingWriteOnly && recordArmed && midiTaking && audio.midiInput.capture.active.load();
    if (controller.hasGesture() || (!liveTake && (recordArmed || midiTaking || hasPendingMidiTake())) ||
        noteSlicePending() || noteStretchPending() || mixerChainBusy() || devicePreparationBusy() ||
        routingPending() || afterPluginState || pendingExport || audio.rendering() || midiFileBusy ||
        pluginRecovery || !audioImports.idle() || audioImportAfterTake ||
        std::any_of(
            pluginOperations.begin(), pluginOperations.end(), [&](const auto &job)
            { return job.state == "pending" && (!nativeScalar || job.id != nativeScalar->operation); }))
        return juce::Result::fail("parameterHistory.busy: Finish recording, capture or structural work "
                                  "before native scalar history");
    const juce::ScopedValueSetter<bool> own(nativeScalarCaptureDispatch, true);
    return finishRackEditsChecked(); // Other recorders keep their own refusal; only our self-guard is exempt.
}
juce::Result Studio::requestNativeScalar(Id plugin, std::uint32_t parameter, unsigned generation, float value,
                                         const juce::String &instance, bool gestureOpen,
                                         juce::String &operation)
{
    if (!std::isfinite(value) || value < 0 || value > 1)
        return juce::Result::fail("parameterHistory.value: Normalized finite value required");
    if (!nativeScalar && !servicingNativeScalar)
        pollPluginOperations(); // Drain acknowledged editor jobs as well as older native gestures before
                                // admitting a manual write. A completed native reply is not an active edit.
    if (nativeScalar)
    {
        auto &a = *nativeScalar;
        if (operation != a.operation || a.plugin != plugin || a.instance != instance ||
            a.generation != generation || a.edits.size() != 1 || a.edits.front().parameter != parameter ||
            a.capturing || a.failure.isNotEmpty() || !a.gestureOpen)
            return juce::Result::fail(
                "parameterHistory.busy: Another native scalar ACK/capture is still owned");
        if (a.recordingWriteOnly && (!midiTaking || !recordArmed || !audio.midiInput.capture.active.load() ||
                                     audio.midiInput.capture.session.load() != a.captureSession))
            return juce::Result::fail("parameterHistory.recordingChanged: Observe the original native ACK; "
                                      "start a new edit after the take settles");
        a.queued = value;
        a.gestureOpen = gestureOpen;
        a.gestureActivity = juce::Time::getMillisecondCounter();
        return juce::Result::ok();
    }
    const bool recordingWriteOnly = recordArmed && midiTaking && audio.midiInput.capture.active.load();
    if (auto ready = nativeScalarAdmission(recordingWriteOnly); ready.failed())
        return ready;
    if (auto ready = finishRackEditsChecked(); ready.failed())
        return ready;
    const auto host = audio.preparedPluginHost(controller.project, plugin);
    PluginParameterCatalogue catalogue;
    if (!controller.project.plugin(plugin) || !host || !host->alive() ||
        host->runtimeIdentity() != instance || !host->readParameterCatalogue(catalogue) || catalogue.error ||
        catalogue.generation != generation)
        return juce::Result::fail("parameterHistory.staleTarget: Exact live Host and catalogue required");
    const auto p = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                [&](const auto &p) { return p.id == parameter; });
    if (p == catalogue.parameters.end() || !p->writable() || (p->flags & (1u << 15)))
        return juce::Result::fail("parameterHistory.unsupportedParameter: Supported writable scalar "
                                  "required; program selectors excluded");
    if (host->pendingParameterRequests() || host->editorBusy() ||
        std::any_of(pluginOperations.begin(), pluginOperations.end(),
                    [](const auto &job) { return job.state == "pending"; }))
        return juce::Result::fail("parameterHistory.busy: Existing native control operation is still owned; "
                                  "parameterReceipts=" + juce::String(host->pendingParameterRequests()) +
                                  " editorReceipts=" + juce::String(int(host->editorBusy())));
    if (pluginOperations.size() >= 64)
    {
        const auto old = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                      [](const auto &j) { return j.state != "pending"; });
        if (old == pluginOperations.end())
            return juce::Result::fail("parameterHistory.capacity: Plugin receipts full");
        pluginOperations.erase(old);
    }
    auto next = std::make_unique<NativeScalarWork>();
    next->operation = juce::Uuid().toString();
    next->project = controller.projectIdentity;
    next->source = controller.project;
    next->revision = controller.revision;
    next->plugin = plugin;
    next->host = host;
    next->instance = instance;
    next->generation = generation;
    next->queued = value;
    next->gestureOpen = gestureOpen;
    next->gestureActivity = juce::Time::getMillisecondCounter();
    next->recordingWriteOnly = recordingWriteOnly;
    next->captureSession = recordingWriteOnly ? audio.midiInput.capture.session.load() : 0;
    next->captureIds = {plugin};
    next->edits.push_back({plugin, parameter, 0, 0, {}});
    (void)host->lastParameterGesture(); // One Host-owned IPC consumer; retain native completions.
    next->losses = host->completedParameterGestureLosses();
    PluginOperation job;
    job.id = next->operation;
    job.kind = "set";
    job.action = recordingWriteOnly ? "manualRecording" : "manual";
    job.plugin = plugin;
    job.project = next->project;
    job.instance = instance;
    job.generation = generation;
    job.parameter = parameter;
    job.host = host;
    job.started = juce::Time::getMillisecondCounter();
    job.revision = controller.revision;
    job.message = "Native scalar write pending; no history completion yet";
    auto token = next->operation;
    pluginOperations.push_back(std::move(job)); // Fallible bookkeeping before any physical request.
    nativeScalar = std::move(next);
    lastNativeScalarOperation = token;
    operation = std::move(token);
    return juce::Result::ok();
}
juce::Result Studio::requestNativePatch(Id plugin, unsigned generation, const juce::String &instance,
    std::span<const std::pair<std::uint32_t,float>> values, juce::String &operation)
{
    // One atomic patch carries a whole Synth preset (the protocol bounds it at ipc::patchValues).
    if(values.empty() || values.size()>size_t(ipc::patchValues))
        return juce::Result::fail("parameterHistory.patchRefused: a patch needs 1.." + juce::String(ipc::patchValues) + " values");
    if(recordArmed || midiTaking || hasPendingMidiTake())
        return juce::Result::fail("parameterHistory.patchRefused: a bounded patch requires stopped recording");
    const auto host=audio.preparedPluginHost(controller.project,plugin);
    PluginParameterCatalogue catalogue;
    if(!host || !host->readParameterCatalogue(catalogue) || catalogue.error || catalogue.generation!=generation)
        return juce::Result::fail("parameterHistory.staleCatalogue");
    std::set<std::uint32_t> unique;
    std::vector<NativeScalarEdit> edits;
    std::vector<std::pair<std::uint32_t,float>> batch(values.begin(),values.end());
    for(const auto &[id,value]:values)
    {
        const auto found=std::find_if(catalogue.parameters.begin(),catalogue.parameters.end(),[&](const auto &p){return p.id==id;});
        if(!unique.insert(id).second || !std::isfinite(value) || value<0 || value>1 ||
            found==catalogue.parameters.end() || !found->writable() || (found->flags&(1u<<15)))
            return juce::Result::fail("parameterHistory.invalidPatch");
        edits.push_back({plugin,id,0,0,{}});
    }
    auto result=requestNativeScalar(plugin,values.front().first,generation,values.front().second,instance,false,operation);
    if(result.failed()) return result;
    nativeScalar->batch=std::move(batch); nativeScalar->edits=std::move(edits);
    return juce::Result::ok();
}
void Studio::finishNativeScalarGesture(const juce::String &operation)
{
    if (nativeScalar && nativeScalar->operation == operation)
        nativeScalar->gestureOpen = false;
}
juce::Result Studio::cancelNativeScalar(const juce::String &operation)
{
    if (!nativeScalar || nativeScalar->operation != operation)
        return juce::Result::fail("parameterHistory.terminal: Observe original operation");
    if (nativeScalar->dispatched || nativeScalar->applied || nativeScalar->capturing)
        return juce::Result::fail(
            "parameterHistory.tooLate: Native edit may have applied; ACK/history observation remains owned");
    completeNativeScalar("cancelled", "",
                         "Undispatched native scalar operation cancelled; no native/model change");
    return juce::Result::ok();
}
void Studio::completeNativeScalar(const char *state, const char *code, const juce::String &message)
{
    if (!nativeScalar)
        return;
    auto found = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                              [&](const auto &j) { return j.id == nativeScalar->operation; });
    if (found == pluginOperations.end())
        return; // Retain actual owner rather than abandon a physical request.
    juce::String nextState(state), nextCode(code), nextMessage(message);
    found->state.swapWith(nextState);
    found->code.swapWith(nextCode);
    found->message.swapWith(nextMessage);
    found->completedRevision = controller.revision;
    found->nativeApplied = nativeScalar->applied;
    if (!nativeScalar->edits.empty())
        found->scalarAfter = nativeScalar->edits.front().after;
    found->host.reset();
    nativeScalar.reset(); // CONTROL only, after ACK and owned capture callback have drained.
    status = found->message;
}
void Studio::finishNativeScalarCapture(const juce::String &operation, juce::Result result, Project snapshot)
{
    if (!nativeScalar || nativeScalar->operation != operation)
        return;
    auto &a = *nativeScalar;
    a.capturing = false;
    try
    {
        if (result.failed())
        {
            completeNativeScalar("failed", "parameterHistory.captureFailed",
                                 "Native edit applied; history unavailable: " + result.getErrorMessage());
            return;
        }
        const auto revision = audio.capturedPluginRevision(a.plugin, a.host);
        PluginParameterCatalogue catalogue;
        const auto *captured = snapshot.plugin(a.plugin);
        if (a.project != controller.projectIdentity || a.revision != controller.revision ||
            !a.source.state().isEquivalentTo(controller.project.state()) || !a.host->alive() ||
            audio.preparedPluginHost(controller.project, a.plugin) != a.host ||
            a.host->runtimeIdentity() != a.instance || !revision ||
            *revision != a.host->parameterRevision() || !captured ||
            captured->state != a.host->recoveryState() || !a.host->readParameterCatalogue(catalogue) ||
            catalogue.error || catalogue.generation != a.generation ||
            a.losses != a.host->completedParameterGestureLosses())
        {
            completeNativeScalar(
                "failed", "parameterHistory.staleCapture",
                "Native edit applied; exact capture/source witness changed and history was not committed");
            return;
        }
        for (const auto &edit : a.edits)
        {
            const auto p = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                        [&](const auto &p) { return p.id == edit.parameter; });
            if (p == catalogue.parameters.end() || !p->writable() || (p->flags & (1u << 15)))
            {
                completeNativeScalar("failed", "parameterHistory.unsupportedParameter",
                                     "Native edit applied; supported scalar schema changed before history");
                return;
            }
        }
        std::vector<NativeScalarEdit> changed;
        changed.reserve(a.edits.size());
        for(const auto &edit:a.edits) if(edit.before!=edit.after) changed.push_back(edit);
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto r = controller.prepareNativeScalarEdit(snapshot, changed, prepared); r.failed())
        {
            completeNativeScalar("failed", "parameterHistory.prepareFailed",
                                 "Native edit applied; history unavailable: " + r.getErrorMessage());
            return;
        }
        if (auto r = controller.validatePreparedChange(*prepared); r.failed())
        {
            completeNativeScalar("failed", "parameterHistory.staleSource",
                                 "Native edit applied; canonical source changed before history");
            return;
        }
        if (!controller.commitPreparedChange(*prepared))
        {
            completeNativeScalar("failed", "parameterHistory.commitRefused",
                                 "Native edit applied; history commit refused");
            return;
        }
        const auto found = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                        [&](const auto &j) { return j.id == operation; });
        if (found != pluginOperations.end())
            found->historyCommitted = true;
        completeNativeScalar(
            "completed", "",
            "Native scalar acknowledged and one captured canonical history action committed");
        controller.deliverPendingNotifications();
    }
    catch (...)
    {
        if (nativeScalar)
            completeNativeScalar("failed", "parameterHistory.preparationFailed",
                                 "Native edit applied; history preparation failed. Inspect actual "
                                 "Project/native state before retry");
    }
}
void Studio::pollNativeScalarOperations()
{
    if (servicingNativeScalar)
        return;
    const juce::ScopedValueSetter<bool> own(servicingNativeScalar, true);
    try
    {
        if (!nativeScalar)
        {
            // Existing sole Host IPC drain returns all complete supported gestures.
            // Native edits are already real: incompatible ownership refuses history,
            // never the audible edit, and preserves external dirty ownership.
            for (const auto &runtime : audio.pluginRuntimeStates())
            {
                const auto host = audio.preparedPluginHost(controller.project, runtime.plugin);
                if (!host || !host->alive())
                    continue;
                (void)host->lastParameterGesture();
                const auto losses = host->completedParameterGestureLosses();
                auto &seen = nativeScalarGestureLosses[runtime.plugin];
                const bool lost = seen.first == host->runtimeIdentity() && seen.second != losses;
                seen = {host->runtimeIdentity(), losses};
                std::vector<NativeScalarEdit> edits;
                PluginGesture event;
                unsigned generation = 0, epoch = 0;
                juce::String failure;
                for (int i = 0; i < 256 && host->popCompletedParameterGesture(event); ++i)
                {
                    controller.markExternalChange();
                    if (!generation)
                    {
                        generation = event.generation;
                        epoch = event.epoch;
                    }
                    if (generation != event.generation || epoch != event.epoch || lost)
                        failure = "Native gesture loss/schema/epoch changed; native edit retained without "
                                  "fabricated history";
                    auto existing = std::find_if(edits.begin(), edits.end(),
                                                 [&](const auto &e) { return e.parameter == event.id; });
                    if (existing == edits.end())
                    {
                        if (edits.size() == 64)
                            failure = "Native gesture history batch capacity exceeded";
                        else
                            edits.push_back({runtime.plugin, event.id, event.before, event.after, {}});
                    }
                    else
                    {
                        if (existing->after != event.before)
                            failure = "Native scalar changed outside the witnessed gesture batch";
                        existing->after = event.after;
                    }
                }
                if (lost)
                {
                    controller.markExternalChange();
                    status = "Native gesture notifications lost; native state dirty, history unavailable";
                }
                if (edits.empty())
                    continue;
                PluginParameterCatalogue catalogue;
                if (auto r = nativeScalarAdmission(); r.failed())
                    failure = r.getErrorMessage();
                if (!host->readParameterCatalogue(catalogue) || catalogue.error ||
                    catalogue.generation != generation)
                    failure = "Native gesture catalogue changed before fresh capture";
                for (const auto &e : edits)
                {
                    const auto p = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                                [&](const auto &p) { return p.id == e.parameter; });
                    if (p == catalogue.parameters.end() || !p->writable() || (p->flags & (1u << 15)))
                        failure = "Native gesture is outside supported writable scalar history";
                }
                auto next = std::make_unique<NativeScalarWork>();
                next->operation = juce::Uuid().toString();
                next->project = controller.projectIdentity;
                next->source = controller.project;
                next->revision = controller.revision;
                next->plugin = runtime.plugin;
                next->host = host;
                next->instance = host->runtimeIdentity();
                next->generation = generation;
                next->epoch = epoch;
                next->losses = host->completedParameterGestureLosses();
                next->captureIds = {runtime.plugin};
                next->edits = std::move(edits);
                next->applied = true;
                PluginOperation job;
                job.id = next->operation;
                job.kind = "set";
                job.action = "nativeGesture";
                job.plugin = runtime.plugin;
                job.project = next->project;
                job.instance = next->instance;
                job.generation = generation;
                job.parameter = next->edits.front().parameter;
                job.nativeApplied = job.nativeDispatched = job.scalarBeforeKnown = true;
                job.scalarBefore = next->edits.front().before;
                job.scalarAfter = next->edits.front().after;
                job.scalarEdits = next->edits;
                job.host = host;
                job.started = juce::Time::getMillisecondCounter();
                if (pluginOperations.size() >= 64)
                {
                    auto old = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                            [](const auto &j) { return j.state != "pending"; });
                    if (old == pluginOperations.end())
                    {
                        status = "Native edit retained; history receipt capacity unavailable";
                        continue;
                    }
                    pluginOperations.erase(old);
                }
                pluginOperations.push_back(std::move(job));
                lastNativeScalarOperation = next->operation;
                nativeScalar = std::move(next);
                if (failure.isNotEmpty())
                    completeNativeScalar("failed", "parameterHistory.nativeHistoryUnavailable", failure);
                break;
            }
        }
        if (!nativeScalar || nativeScalar->capturing)
            return;
        auto &a = *nativeScalar;
        const auto job = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                      [&](const auto &j) { return j.id == a.operation; });
        if (job == pluginOperations.end())
            return;
        if (a.project != controller.projectIdentity ||
            (!a.recordingWriteOnly && (a.revision != controller.revision ||
                                       !a.source.state().isEquivalentTo(controller.project.state()))) ||
            !a.host->alive() || audio.preparedPluginHost(controller.project, a.plugin) != a.host ||
            a.host->runtimeIdentity() != a.instance)
            a.failure = "Native scalar source/Host changed; inspect applied state, no history was committed";
        if (a.inFlight)
        {
            PluginParameterResult ack;
            if (!a.host->readParameterResult(a.inFlight, ack))
                return;
            a.inFlight = 0;
            if (ack.result != 0)
            {
                a.failure = "Native scalar write failed; inspect actual retained state";
                if (ack.result >= 3 && a.project == controller.projectIdentity)
                    controller.markExternalChange(); // Write may precede failed readback/worker loss.
            }
            else
            {
                if (a.project == controller.projectIdentity)
                    controller.markExternalChange();
                a.applied = job->nativeApplied = true;
                if (!ack.beforeKnown && !a.recordingWriteOnly)
                    a.failure = "Actual native BEFORE unavailable; applied value retained without history";
                else if (!a.batch.empty())
                {
                    if(ack.patchCount!=a.batch.size()) a.failure="Native patch acknowledgement shape changed";
                    else for(std::size_t i=0;i<a.batch.size();++i)
                    {
                        if(ack.patch[i].id!=a.edits[i].parameter) {a.failure="Native patch acknowledgement identity changed";break;}
                        a.edits[i].before=ack.patch[i].previousValue;a.edits[i].after=ack.patch[i].value;
                    }
                    job->scalarBefore=ack.previousValue;
                    job->scalarBeforeKnown=true;
                }
                else if (ack.beforeKnown && !job->scalarBeforeKnown)
                {
                    a.edits.front().before = ack.previousValue;
                    job->scalarBefore = ack.previousValue;
                    job->scalarBeforeKnown = true;
                }
                else if (!a.recordingWriteOnly && a.edits.front().after != ack.previousValue)
                    a.failure = "Native value changed outside this owned gesture; applied value retained "
                                "without history";
                if(a.batch.empty()) a.edits.front().after = ack.value;
                job->scalarAfter = ack.value;
                job->scalarEdits = a.edits;
            }
        }
        if (a.failure.isNotEmpty())
        {
            auto message = a.failure;
            completeNativeScalar("failed", "parameterHistory.historyUnavailable", message);
            return;
        }
        PluginParameterCatalogue catalogue;
        (void)a.host->lastParameterGesture();
        if (!a.host->readParameterCatalogue(catalogue) || catalogue.error ||
            catalogue.generation != a.generation || a.host->completedParameterGestureLosses() != a.losses)
        {
            completeNativeScalar(
                "failed", "parameterHistory.staleCatalogue",
                "Native schema/notifications changed; applied edit remains dirty, history unavailable");
            return;
        }
        if (a.queued)
        {
            if (a.recordingWriteOnly &&
                (!midiTaking || !recordArmed || !audio.midiInput.capture.active.load() ||
                 audio.midiInput.capture.session.load() != a.captureSession))
            {
                completeNativeScalar(
                    "failed", "parameterHistory.recordingChanged",
                    "Recording session changed; queued write refused, previous native ACK retained");
                return;
            }
            a.inFlight = a.batch.empty()?a.host->setParameter(a.edits.front().parameter,*a.queued,a.generation):a.host->setParameterPatch(a.batch,a.generation);
            if (!a.inFlight)
            {
                completeNativeScalar("failed", "parameterHistory.writeRefused",
                                     "Native scalar write was not admitted; inspect previous applied values");
                return;
            }
            a.dispatched = job->nativeDispatched = true;
            a.queued.reset();
            return;
        }
        if (a.gestureOpen && juce::Time::getMillisecondCounter() - a.gestureActivity >= 30000u)
            a.gestureOpen = false; // Preserve applied native values; finish their real history owner.
        if (a.gestureOpen)
            return;
        if (!a.applied)
        {
            completeNativeScalar("cancelled", "", "No native scalar change was applied");
            return;
        }
        if (a.recordingWriteOnly)
        {
            completeNativeScalar(
                "completed", "",
                "Native parameter acknowledged during MIDI recording; no state capture or parameter "
                "history. Existing automation/manual fallback is unchanged; Save captures current state.");
            return;
        }
        if (std::all_of(a.edits.begin(), a.edits.end(), [](const auto &e) { return e.before == e.after; }))
        {
            completeNativeScalar("completed", "",
                                 "Native scalar acknowledged; no changed scalar history edge needed");
            return;
        }
        if (auto r = nativeScalarAdmission(); r.failed())
        {
            completeNativeScalar("failed", "parameterHistory.captureBusy",
                                 "Native edit applied; history capture refused: " + r.getErrorMessage());
            return;
        }
        const auto token = a.operation;
        auto safe = juce::Component::SafePointer<Studio>(this);
        std::function<void(juce::Result, Project)> completion =
            [safe, token](juce::Result result, Project captured)
        {
            if (safe)
                safe->finishNativeScalarCapture(token, result, std::move(captured));
        };
        a.capturing = true;
        const juce::ScopedValueSetter<bool> dispatch(nativeScalarCaptureDispatch, true);
        try
        {
            withPluginState(completion, &a.captureIds);
        }
        catch (...)
        {
            if (nativeScalar && nativeScalar->operation == token)
            {
                if (audio.pluginStateCapturePending())
                {
                    if (!afterPluginState)
                        afterPluginState = std::move(completion);
                }
                else
                {
                    nativeScalar->capturing = false;
                    completeNativeScalar("failed", "parameterHistory.captureException",
                                         "Native edit applied; state capture failed, history unavailable");
                }
            }
        }
    }
    catch (...)
    {
        if (nativeScalar && !nativeScalar->capturing)
        {
            nativeScalar->failure = "Native scalar bookkeeping failed; actual request/outcome remains owned";
            nativeScalar->gestureOpen = false;
            nativeScalar->queued.reset();
            // A physical request remains retained until a later poll drains it.
            if (!nativeScalar->inFlight)
                completeNativeScalar("failed", "parameterHistory.preparationFailed", nativeScalar->failure);
        }
    }
}
