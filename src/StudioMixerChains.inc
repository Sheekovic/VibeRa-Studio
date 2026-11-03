// SPDX-License-Identifier: AGPL-3.0-or-later
// Included by Studio.cpp in namespace vibera. CONTROL/message-thread only.
struct Studio::MixerChainWorkflow
{
    struct Receipt
    {
        juce::String operation, action, phase{"capture"}, completion{"pending"}, code, message;
        juce::String project, path, temporaryPath;
        std::uint64_t revision = 0;
        int insert = 0;
        bool fileCommitted = false, temporaryRemaining = false;
        bool documentChanged = false, undoable = false, nativeRecoveryRequired = false;
        juce::String audioState;
        unsigned long nativeError = 0, cleanupError = 0;
        std::uint64_t recoveryOperation = 0;
        juce::Array<juce::var> provenance; // Compact labels only; no opaque bytes or native handles.
        juce::Array<juce::var> editorClosures; // Actual owned close ACKs; no opaque state.
    };
    struct Active
    {
        size_t index = 0;
        std::unique_ptr<effect_chain::CaptureIntent> intent;
        std::vector<Id> ids;
        std::vector<std::shared_ptr<PluginHost>> hosts;
        juce::String fileOperation;
        bool overwrite = false, cancel = false, capture = false, pollException = false;
        bool applying = false, preparationStarted = false, notified = false;
        bool playRequested = false;
        bool closingEditors = false;
        juce::int64 activationStarted = 0;
        bool patternMode = false;
        Id pattern = 0;
        std::uint64_t recoveryOperation = 0;
        Project source, capturedSource, destination;
        std::vector<int> captureTargets;
        std::size_t captureTarget = 0;
        std::optional<HistoryDirection> history;
        juce::String insertCommand;
        CommandArgs insertArguments;
        bool insertHistory = false;
        bool fullCapture = false, routingHistory = false, scalarHistory = false, ioHistory = false;
        bool structuralHistory = false;
        juce::String structuralCommand;
        CommandArgs structuralArguments;
        Id sidechainTarget = 0;
        std::optional<PluginDetectorAssignment> detector;
        Id replacementTarget = 0;
        juce::String replacementDescriptor;
        std::unique_ptr<effect_chain::Chain> incoming;
        std::unique_ptr<PreparedControllerChange> model;
        juce::Result applyError = juce::Result::ok();
        struct SourcePin
        {
            Id id = 0;
            std::shared_ptr<PluginHost> host;
            juce::String instance, descriptor, recovery;
            unsigned revision = 0;
            std::optional<unsigned> capturedRevision;
            PluginPcmIdentity pcm;
            PluginLatencyStamp declaration;
            PluginBusRequest buses;
            unsigned resetGeneration = 0;
            bool closeWanted = false, closeRecorded = false;
            unsigned closeSerial = 0;
            std::optional<PluginEditorResult> closeResult;
        };
        std::vector<SourcePin> sourcePins;
        std::uint64_t deviceEpoch = 0;
        double rate = 0;
        int block = 0;
        // Preallocated before installing any callback; exception path performs only flag writes.
        juce::String exceptionCode{"effectChain.captureException"};
        juce::String exceptionMessage{"Capture failed; draining owned native requests before terminal"};
    };
    std::unique_ptr<effect_chain::PresetFileJobs> files;
    std::vector<std::unique_ptr<Receipt>> receipts; // Session history, no silent eviction.
    std::unique_ptr<Active> active;
    std::unique_ptr<effect_chain::Clipboard> clipboard;
};
namespace chain_workflow_detail
{
inline juce::var object()
{
    return juce::var(new juce::DynamicObject);
}
inline void put(juce::var &v, const char *key, juce::var value)
{
    v.getDynamicObject()->setProperty(key, std::move(value));
}
inline juce::var result(const juce::String &completion, const juce::String &code = {},
                        const juce::String &message = {})
{
    auto out = object();
    put(out, "completionState", completion);
    put(out, "code", code);
    put(out, "message", message);
    put(out, "documentChanged", false);
    put(out, "undoable", false);
    return out;
}
inline juce::String code(const juce::Result &r)
{
    const auto text = r.getErrorMessage();
    if (text.containsChar(':'))
        return text.upToFirstOccurrenceOf(":", false, false);
    return text.startsWith("effectChain.") || text.startsWith("chain.file.") ? text : "effectChain.failed";
}
inline const char *phase(effect_chain::PresetFileJobs::Phase p) noexcept
{
    using P = effect_chain::PresetFileJobs::Phase;
    switch (p)
    {
    case P::queued:
        return "queued";
    case P::running:
        return "running";
    case P::cancelling:
        return "cancelling";
    case P::committing:
        return "committing";
    case P::completed:
        return "completed";
    case P::failed:
        return "failed";
    case P::cancelled:
        return "cancelled";
    }
    return "failed";
}
} // namespace chain_workflow_detail
bool Studio::mixerChainBusy() const noexcept
{
    return mixerChains && mixerChains->active != nullptr;
}
bool Studio::mixerChainApplying() const noexcept
{
    return mixerChainBusy() && mixerChains->active->applying;
}
bool Studio::mixerChainRecovering() const noexcept
{
    return mixerChainApplying() && mixerChains->active->recoveryOperation != 0;
}
const effect_chain::Clipboard *Studio::mixerChainClipboardData() const noexcept
{
    return mixerChains ? mixerChains->clipboard.get() : nullptr;
}
bool Studio::mixerChainCaptureException() noexcept
{
    if (!mixerChains || !mixerChains->active || !mixerChains->active->capture)
        return false;
    // Retain the exact installed callback while native work is pending. A poll may
    // also throw after native drain, before dispatching that callback.
    mixerChains->active->pollException = true;
    if (!audio.pluginStateCapturePending())
    {
        afterPluginState = {};
        mixerChains->active->capture = false;
    }
    return true;
}
juce::Result Studio::admitMixerChainCapture()
{
    // No live projection exists in the initial device-free document workspace.
    // An ordinary model edit can leave its deferred update flag set; Apply will
    // prepare destination PCM before publication. Genuine device loss cannot
    // use canDeferChannelRouting and retains all normal recovery admission.
    const bool initialDeferred = audio.canDeferChannelRouting();
    if (mixerChainBusy() || devicePreparationBusy() || routingApplicationPreparing ||
        routingCoordinator.mutationBlocked() || afterPluginState || audio.pluginStateCapturePending() ||
        pendingExport || audio.rendering() || midiFileBusy || mcpExportOperation.isNotEmpty() ||
        pluginRecovery || !audioImports.idle() || audioImportAfterTake || pollingAudioImports ||
        (needsRebuild && !initialDeferred))
        return juce::Result::fail("effectChain.busy: Finish active projection, capture or file work first");
    if (controller.hasGesture() || midiTaking || hasPendingMidiTake() || noteStretchPending() ||
        noteSlicePending() || mcpPointerTarget || pluginControlsPending() || nativeScalar ||
        std::any_of(pluginOperations.begin(), pluginOperations.end(), [](const auto &job)
                    { return job.state == "pending" && (job.kind == "set" || job.kind == "editor"); }))
        return juce::Result::fail(
            "effectChain.controlBusy: Finish the gesture, take or native controls first");
    for (const auto &runtime : audio.pluginRuntimeStates())
        if (auto host = audio.parameterHost(runtime.plugin))
        {
            // Only the exact immutable I/O request currently dispatching this
            // canonical command may own its wrapper mailbox. Other workers,
            // scalar gestures and native editor/parameter work remain protected.
            float desired = 0;
            bool gestureOpen = false;
            const bool ownRouting =
                servicingPluginWrapper && pluginWrapperPending &&
                mixerStructuralCommandDispatch == "plugin.io" && mixerStructuralDispatch &&
                mixerStructuralDispatch->pluginIo && mixerStructuralDispatch->target == runtime.plugin &&
                pluginWrapperPending->plugin == runtime.plugin && pluginWrapperPending->host == host &&
                pluginWrapperPending->project == controller.projectIdentity &&
                pluginWrapperPending->instance == host->runtimeIdentity() &&
                pluginWrapperPending->request.intent.action == PluginWrapperAction::audioIo &&
                *mixerStructuralDispatch->pluginIo == pluginWrapperPending->request.intent.io &&
                host->readWrapperDesired(pluginWrapperPending->request.serial, desired, gestureOpen);
            const bool ownChain =
                pluginChainAdmissionOperation.isNotEmpty() && pluginWrapperPending &&
                pluginWrapperPending->operation == pluginChainAdmissionOperation &&
                pluginWrapperPending->plugin == runtime.plugin && pluginWrapperPending->host == host &&
                pluginWrapperPending->project == controller.projectIdentity &&
                pluginWrapperPending->instance == host->runtimeIdentity() &&
                pluginWrapperPending->request.intent.action >= PluginWrapperAction::chainCopy &&
                pluginWrapperPending->request.intent.action <= PluginWrapperAction::sidechain &&
                host->readWrapperDesired(pluginWrapperPending->request.serial, desired, gestureOpen);
            if (host->pendingParameterRequests() || host->editorBusy() ||
                (host->wrapperState().pending && !ownRouting && !ownChain))
                return juce::Result::fail("effectChain.controlBusy: Native parameter/editor work is pending");
        }
    return juce::Result::ok();
}
juce::var Studio::mixerChainReadback(const juce::String &operation) const
{
    using namespace chain_workflow_detail;
    auto out = result("completed");
    if (operation.isNotEmpty())
    {
        if (!mixerChains)
            return result("failed", "effectChain.unknownOperation", "Unknown session operation");
        const auto found = std::find_if(mixerChains->receipts.begin(), mixerChains->receipts.end(),
                                        [&](const auto &r) { return r->operation == operation; });
        if (found == mixerChains->receipts.end())
            return result("failed", "effectChain.unknownOperation", "Unknown session operation");
        const auto &r = **found;
        out = result(r.completion, r.code, r.message);
        put(out, "operation", r.operation);
        put(out, "action", r.action);
        put(out, "phase", r.phase);
        put(out, "projectIdentity", r.project);
        put(out, "revision", juce::int64(r.revision));
        put(out, "mixer", r.insert);
        put(out, "path", r.path);
        put(out, "fileCommitted", r.fileCommitted);
        put(out, "nativeError", juce::int64(r.nativeError));
        put(out, "temporaryPath", r.temporaryPath);
        put(out, "temporaryRemaining", r.temporaryRemaining);
        put(out, "cleanupError", juce::int64(r.cleanupError));
        put(out, "provenance", r.provenance);
        if (!r.editorClosures.isEmpty())
            put(out, "editorClosures", r.editorClosures);
        put(out, "documentChanged", r.documentChanged);
        put(out, "undoable", r.undoable);
        put(out, "nativeRecoveryRequired", r.nativeRecoveryRequired);
        if (r.recoveryOperation)
            put(out, "recoveryOperation", juce::int64(r.recoveryOperation));
        if (r.audioState.isNotEmpty())
            put(out, "audioState", r.audioState);
    }
    auto state = object(), clipboard = object();
    put(state, "directory",
        mixerChains && mixerChains->files ? mixerChains->files->configuredDirectory() : juce::String{});
    put(state, "available",
        mixerChains && mixerChains->files && mixerChains->files->configuredDirectory().isNotEmpty());
    put(state, "busy", mixerChainBusy());
    put(state, "activeOperation",
        mixerChainBusy() ? mixerChains->receipts[mixerChains->active->index]->operation : juce::String{});
    put(state, "sessionHistory", mixerChains ? int(mixerChains->receipts.size()) : 0);
    put(state, "clipboardAvailable", mixerChains && mixerChains->clipboard != nullptr);
    if (mixerChains && mixerChains->clipboard)
    {
        const auto &c = *mixerChains->clipboard;
        put(clipboard, "name", c.chain.name);
        put(clipboard, "effectsBypassed", c.chain.effectsBypassed);
        put(clipboard, "sourceProjectIdentity", c.sourceProjectIdentity);
        put(clipboard, "sourceRevision", juce::int64(c.sourceRevision));
        put(clipboard, "sourceInsert", c.sourceInsert);
        juce::Array<juce::var> slots;
        for (const auto &s : c.chain.effects)
        {
            auto slot = object();
            put(slot, "name", s.name);
            put(slot, "bypass", s.bypass);
            put(slot, "wet", s.wet);
            slots.add(slot);
        }
        put(clipboard, "slots", slots);
    }
    put(state, "clipboard", clipboard);
    put(out, "state", state);
    return out;
}
void Studio::finishMixerChainCapture(const juce::String &operation, juce::Result captured,
                                     Project snapshot) noexcept
{
    if (mixerChainApplying())
    {
        finishMixerChainApplyCapture(operation, captured, std::move(snapshot));
        return;
    }
    if (!mixerChainBusy())
        return;
    auto &w = *mixerChains;
    auto &a = *w.active;
    if (!a.capture || w.receipts[a.index]->operation != operation)
        return;
    a.capture = false; // Actual AudioEngine poll already released its owned native request.
    try
    {
        auto next = std::make_unique<MixerChainWorkflow::Receipt>(*w.receipts[a.index]);
        if (a.cancel || a.pollException || captured.failed())
        {
            next->completion = a.cancel ? "cancelled" : "failed";
            next->phase = next->completion;
            next->code = a.cancel          ? "effectChain.cancelled"
                         : a.pollException ? a.exceptionCode
                                           : chain_workflow_detail::code(captured);
            next->message = a.cancel          ? "Owned native capture drained; clipboard and file preserved"
                            : a.pollException ? a.exceptionMessage
                                              : captured.getErrorMessage();
            status = next->message;
            w.receipts[a.index].swap(next);
            w.active.reset();
            return;
        }
        std::vector<effect_chain::CaptureWitness> witnesses;
        const auto current = audio.pluginRuntimeStates();
        witnesses.reserve(a.ids.size());
        for (size_t i = 0; i < a.ids.size(); ++i)
        {
            PluginRuntimeState now{a.ids[i], {}, 0, false};
            if (auto found = std::find_if(current.begin(), current.end(),
                                          [&](const auto &r) { return r.plugin == a.ids[i]; });
                found != current.end())
                now = *found;
            const auto host = audio.parameterHost(a.ids[i]);
            if (host != a.hosts[i])
            {
                next->completion = "failed";
                next->phase = "failed";
                next->code = "effectChain.staleRuntime";
                next->message = "Exact plugin owner changed during capture";
                status = next->message;
                w.receipts[a.index].swap(next);
                w.active.reset();
                return;
            }
            witnesses.push_back({now, host ? audio.capturedPluginRevision(a.ids[i], host) : std::nullopt,
                                 host ? host->pendingParameterRequests() : 0, host && host->editorBusy()});
        }
        std::unique_ptr<effect_chain::Clipboard> value;
        const auto finish =
            effect_chain::finishCapture(*a.intent, controller.project, controller.projectIdentity,
                                        controller.revision, snapshot, witnesses, value);
        if (finish.failed())
        {
            next->completion = "failed";
            next->phase = "failed";
            next->code = chain_workflow_detail::code(finish);
            next->message = finish.getErrorMessage();
            status = next->message;
            w.receipts[a.index].swap(next);
            w.active.reset();
            return;
        }
        for (const auto &r : value->receipts)
        {
            auto item = chain_workflow_detail::object();
            chain_workflow_detail::put(item, "plugin", juce::int64(r.originalPlugin));
            chain_workflow_detail::put(item, "instance", r.originalInstance);
            if (r.revision)
                chain_workflow_detail::put(item, "revision", juce::int64(*r.revision));
            chain_workflow_detail::put(item, "stateSource",
                                       r.provenance == effect_chain::StateProvenance::nativeAcknowledgement
                                           ? "nativeAcknowledgement"
                                       : r.provenance == effect_chain::StateProvenance::retainedRecoveryBytes
                                           ? "retainedRecoveryBytes"
                                           : "retainedProjectBytes");
            next->provenance.add(item);
        }
        if (next->action == "copy")
        {
            next->completion = "completed";
            next->phase = "completed";
            next->message = "Fresh targeted native state copied; no destination or history change";
            status = next->message;
            w.clipboard.swap(value);
            w.receipts[a.index].swap(next);
            w.active.reset();
            return;
        }
        // The helper encodes this exact immutable capture on CONTROL before admission.
        next->phase = "queued";
        next->message = "Preset write admitted; poll until terminal";
        juce::String fileOperation;
        const auto submitted = w.files->submitSave(next->path, value->chain, a.overwrite, fileOperation);
        if (submitted.failed())
        {
            next->completion = "failed";
            next->phase = "failed";
            next->code = chain_workflow_detail::code(submitted);
            next->message = submitted.getErrorMessage();
            status = next->message;
            w.receipts[a.index].swap(next);
            w.active.reset();
            return;
        }
        a.fileOperation.swapWith(fileOperation); // No ownership gap after helper admitted.
        w.receipts[a.index].swap(next);
        a.intent.reset();
        a.ids.clear();
        a.hosts.clear(); // CONTROL releases completed capture pins only.
    }
    catch (...)
    {
        // Capture is already drained. Keep receipt/active ownership for explicit failure service.
        a.pollException = true;
    }
}
void Studio::serviceMixerChainFiles() noexcept
{
    if (!mixerChainBusy())
        return;
    auto &w = *mixerChains;
    auto &a = *w.active;
    if (a.capture)
        return;
    if (a.applying && a.fileOperation.isEmpty())
    {
        serviceMixerChainApply();
        return;
    }
    try
    {
        auto next = std::make_unique<MixerChainWorkflow::Receipt>(*w.receipts[a.index]);
        if (a.fileOperation.isEmpty())
        {
            next->completion = "failed";
            next->phase = "failed";
            next->code = a.exceptionCode;
            next->message = a.exceptionMessage;
            status = next->message;
            w.receipts[a.index].swap(next);
            w.active.reset();
            return;
        }
        const auto observed = w.files->read(a.fileOperation);
        if (!observed)
            return; // No invented completion; retained ownership stays visible/pending.
        const auto &o = *observed;
        next->phase = chain_workflow_detail::phase(o.phase);
        next->fileCommitted = o.fileCommitted;
        next->nativeError = o.nativeError;
        next->temporaryPath = o.temporaryPath;
        next->temporaryRemaining = o.temporaryRemaining;
        next->cleanupError = o.cleanupError;
        if (!effect_chain::PresetFileJobs::terminal(o.phase))
        {
            status = next->message;
            w.receipts[a.index].swap(next);
            return;
        }
        using P = effect_chain::PresetFileJobs::Phase;
        next->completion = o.phase == P::completed   ? "completed"
                           : o.phase == P::cancelled ? "cancelled"
                                                     : "failed";
        next->code = o.error == effect_chain::PresetFileJobs::Error::none
                         ? juce::String{}
                         : juce::String(effect_chain::PresetFileJobs::errorCode(o.error));
        next->message = o.detail.isNotEmpty() ? o.detail
                        : o.phase == P::completed ? "Effect-chain preset saved"
                        : o.phase == P::cancelled ? "Effect-chain preset operation cancelled"
                                                   : next->code;
        std::unique_ptr<effect_chain::Clipboard> loaded;
        if (o.phase == P::completed && next->action == "loadClipboard")
        {
            if (!o.loaded)
                return; // Helper terminal contract violated; do not publish empty data.
            loaded = std::make_unique<effect_chain::Clipboard>();
            loaded->chain = *o.loaded;
            next->message = "Preset loaded to portable clipboard; no Paste, Project or audio change";
        }
        std::unique_ptr<effect_chain::Chain> incoming;
        if (o.phase == P::completed && next->action == "loadApply" && !a.cancel)
        {
            if (!o.loaded)
                return;
            incoming = std::make_unique<effect_chain::Chain>(*o.loaded);
            next->completion = "pending";
            next->phase = "capture";
            next->message = "Preset decoded; fresh destination capture and audio preparation pending";
        }
        // All fallible compact receipt/clipboard construction precedes acknowledging the helper.
        status = next->message;
        if (w.files->forget(a.fileOperation).failed())
            return;
        if (loaded)
            w.clipboard.swap(loaded);
        w.receipts[a.index].swap(next);
        if (a.applying && o.phase == P::completed)
        {
            a.incoming.swap(incoming);
            a.fileOperation.clear();
            serviceMixerChainApply();
            return;
        }
        w.active.reset();
    }
    catch (...)
    { /* Preserve helper receipt AND exact active ownership for the next CONTROL poll. */
    }
}
juce::Result Studio::beginMixerChainApplyCapture()
{
    auto &a = *mixerChains->active;
    if (a.captureTarget >= a.captureTargets.size())
        return juce::Result::ok();
    if (auto current = mixerChainApplySourceCurrent(false); current.failed())
        return current;
    const auto insert = a.captureTargets[a.captureTarget];
    const auto *target = mixerInsert(controller.project, insert);
    if (!target)
        return juce::Result::fail("effectChain.target: Capture target disappeared");
    const auto runtimes = audio.pluginRuntimeStates();
    std::vector<PluginRuntimeState> owners;
    std::vector<Id> ids;
    std::vector<std::shared_ptr<PluginHost>> hosts;
    std::vector<const PluginSpec *> selected;
    if (a.fullCapture)
    {
        for (const auto &channel : controller.project.channels)
            if (channel.instrument)
                selected.push_back(&*channel.instrument);
        for (const auto &row : controller.project.mixer)
            for (const auto &slot : row.effects)
                selected.push_back(&slot);
    }
    else
        for (const auto &slot : target->effects)
            selected.push_back(&slot);
    for (const auto *chosen : selected)
    {
        const auto &slot = *chosen;
        const auto found = std::find_if(runtimes.begin(), runtimes.end(),
                                        [&](const auto &v) { return v.plugin == slot.id; });
        owners.push_back(found == runtimes.end() ? PluginRuntimeState{slot.id, {}, 0, false} : *found);
        ids.push_back(slot.id);
        hosts.push_back(audio.parameterHost(slot.id));
    }
    std::unique_ptr<effect_chain::CaptureIntent> intent;
    if (!a.fullCapture)
        if (auto begin = effect_chain::beginCapture(controller.project, controller.projectIdentity,
                                                    controller.revision, insert, owners, intent);
            begin.failed())
            return begin;
    const auto token = mixerChains->receipts[a.index]->operation;
    auto safe = juce::Component::SafePointer<Studio>(this);
    std::function<void(juce::Result, Project)> callback = [safe, token](juce::Result result, Project captured)
    {
        if (safe)
            safe->finishMixerChainApplyCapture(token, result, std::move(captured));
    };
    a.intent = std::move(intent);
    a.ids.swap(ids);
    a.hosts.swap(hosts);
    a.capture = true; // Every fallible callback owner is already constructed.
    const juce::ScopedValueSetter<bool> own(mixerChainCaptureDispatch, true);
    try
    {
        withPluginState(callback, a.fullCapture ? nullptr : &a.ids);
    }
    catch (...)
    {
        a.pollException = true;
        if (audio.pluginStateCapturePending())
        {
            if (!afterPluginState)
                afterPluginState = std::move(callback);
        }
        else
            a.capture = false;
    }
    return juce::Result::ok();
}
void Studio::finishMixerChainApplyCapture(const juce::String &operation, juce::Result result,
                                          Project snapshot) noexcept
{
    if (!mixerChainApplying())
        return;
    auto &w = *mixerChains;
    auto &a = *w.active;
    if (!a.capture || w.receipts[a.index]->operation != operation)
        return;
    a.capture = false; // Actual owned native request has drained.
    try
    {
        if (result.failed())
        {
            a.applyError = result;
            return;
        }
        if (a.cancel || a.pollException)
            return;
        // This exact owned capture legitimately refreshes recovery bytes.
        // Accept only its acknowledged revision before validating pinned owners.
        for (auto &pin : a.sourcePins)
            if (std::find(a.ids.begin(), a.ids.end(), pin.id) != a.ids.end() && pin.host)
            {
                const auto capturedRevision = audio.capturedPluginRevision(pin.id, pin.host);
                const auto *capturedSpec = snapshot.plugin(pin.id);
                if (!capturedRevision || *capturedRevision != pin.revision || !capturedSpec ||
                    capturedSpec->state != pin.host->recoveryState())
                {
                    a.applyError =
                        juce::Result::fail("effectChain.staleCapture: Exact native acknowledgement changed");
                    return;
                }
                pin.capturedRevision = capturedRevision;
                pin.recovery = pin.host->recoveryState();
            }
        if (auto valid = mixerChainApplySourceCurrent(false); valid.failed())
        {
            a.applyError = valid;
            return;
        }
        if (a.fullCapture)
        {
            if (auto valid = snapshot.validate(); valid.failed())
            {
                a.applyError = valid;
                return;
            }
            auto normalized = snapshot;
            for (std::size_t i = 0; i < a.ids.size(); ++i)
            {
                const auto id = a.ids[i];
                const auto *old = a.source.plugin(id);
                auto *captured = normalized.plugin(id);
                const auto host = audio.parameterHost(id);
                if (!old || !captured || old->descriptor != captured->descriptor || host != a.hosts[i])
                {
                    a.applyError = juce::Result::fail("effectChain.staleRuntime: Full capture owner changed");
                    return;
                }
                // Earlier sourcePins loop verifies exact acknowledged revision/recovery
                // bytes for each actual Host. Missing owners retain only recovery bytes.
                captured->state = old->state;
            }
            if (!normalized.state().isEquivalentTo(a.source.state()))
            {
                a.applyError = juce::Result::fail(
                    "effectChain.invalidCapture: Full snapshot changed beyond opaque native state");
                return;
            }
            for (const auto id : a.ids)
            {
                auto item = chain_workflow_detail::object();
                chain_workflow_detail::put(item, "plugin", juce::int64(id));
                const auto host = audio.parameterHost(id);
                chain_workflow_detail::put(item, "instance", host ? host->runtimeIdentity() : juce::String{});
                chain_workflow_detail::put(item, "stateSource",
                                           host ? "nativeAcknowledgement" : "retainedProjectBytes");
                if (host)
                {
                    const auto revision = audio.capturedPluginRevision(id, host);
                    if (!revision || *revision != host->parameterRevision())
                    {
                        a.applyError = juce::Result::fail(
                            "effectChain.staleCapture: Full snapshot lacks native acknowledgement");
                        return;
                    }
                    chain_workflow_detail::put(item, "revision", juce::int64(*revision));
                }
                w.receipts[a.index]->provenance.add(item);
            }
            a.capturedSource = std::move(snapshot);
        }
        else
        {
            std::vector<effect_chain::CaptureWitness> witnesses;
            const auto runtimes = audio.pluginRuntimeStates();
            for (std::size_t i = 0; i < a.ids.size(); ++i)
            {
                const auto host = audio.parameterHost(a.ids[i]);
                if (host != a.hosts[i])
                {
                    a.applyError = juce::Result::fail("effectChain.staleRuntime: Captured owner changed");
                    return;
                }
                const auto found = std::find_if(runtimes.begin(), runtimes.end(),
                                                [&](const auto &v) { return v.plugin == a.ids[i]; });
                witnesses.push_back(
                    {found == runtimes.end() ? PluginRuntimeState{a.ids[i], {}, 0, false} : *found,
                     host ? audio.capturedPluginRevision(a.ids[i], host) : std::nullopt,
                     host ? host->pendingParameterRequests() : 0, host && host->editorBusy()});
            }
            std::unique_ptr<effect_chain::Clipboard> capture;
            if (auto finished =
                    effect_chain::finishCapture(*a.intent, controller.project, controller.projectIdentity,
                                                controller.revision, snapshot, witnesses, capture);
                finished.failed())
            {
                a.applyError = finished;
                return;
            }
            auto target = std::find_if(a.capturedSource.mixer.begin(), a.capturedSource.mixer.end(),
                                       [&](const auto &v) { return v.index == a.intent->insert; });
            if (target == a.capturedSource.mixer.end())
            {
                a.applyError = juce::Result::fail("effectChain.target: Captured target disappeared");
                return;
            }
            for (auto &slot : target->effects)
            {
                const auto *capturedSpec = snapshot.plugin(slot.id);
                if (!capturedSpec)
                {
                    a.applyError = juce::Result::fail("effectChain.target: Captured slot disappeared");
                    return;
                }
                slot.state = capturedSpec->state;
            }
            for (const auto &receipt : capture->receipts)
            {
                auto item = chain_workflow_detail::object();
                chain_workflow_detail::put(item, "plugin", juce::int64(receipt.originalPlugin));
                chain_workflow_detail::put(item, "instance", receipt.originalInstance);
                if (receipt.revision)
                    chain_workflow_detail::put(item, "revision", juce::int64(*receipt.revision));
                chain_workflow_detail::put(
                    item, "stateSource",
                    receipt.provenance == effect_chain::StateProvenance::nativeAcknowledgement
                        ? "nativeAcknowledgement"
                    : receipt.provenance == effect_chain::StateProvenance::retainedRecoveryBytes
                        ? "retainedRecoveryBytes"
                        : "retainedProjectBytes");
                w.receipts[a.index]->provenance.add(item);
            }
        }
        for (auto &pin : a.sourcePins)
            if (std::find(a.ids.begin(), a.ids.end(), pin.id) != a.ids.end() && pin.host)
            {
                pin.capturedRevision = audio.capturedPluginRevision(pin.id, pin.host);
                pin.recovery = pin.host->recoveryState();
                if (!pin.capturedRevision || *pin.capturedRevision != pin.revision)
                {
                    a.applyError = juce::Result::fail("effectChain.staleCapture: Native revision changed");
                    return;
                }
            }
        ++a.captureTarget;
        a.intent.reset();
        a.ids.clear();
        a.hosts.clear();
        w.receipts[a.index]->phase = "capture";
    }
    catch (...)
    {
        a.pollException = true;
    }
}
juce::Result Studio::mixerChainApplySourceCurrent(bool finalNative) const
{
    if (!mixerChainApplying())
        return juce::Result::fail("effectChain.ownerGone: Apply owner is unavailable");
    const auto &a = *mixerChains->active;
    const auto &r = *mixerChains->receipts[a.index];
    if (a.cancel)
        return juce::Result::fail("effectChain.cancelled: Apply cancelled before publication");
    if (controller.projectIdentity != r.project || controller.revision != r.revision ||
        !controller.project.state().isEquivalentTo(a.source.state()) || controller.hasGesture() ||
        patternMode != a.patternMode || selectedPattern != a.pattern || recordArmed || midiTaking ||
        hasPendingMidiTake())
        return juce::Result::fail(
            "effectChain.staleCapture: Exact project, mode, revision or recording changed");
    const auto &manager = audio.engine.getDeviceManager().deviceManager;
    auto *device = manager.getCurrentAudioDevice();
    if (bool(device) != bool(a.deviceEpoch))
        return juce::Result::fail("effectChain.deviceChanged: Device ownership changed during Apply");
    if (device)
    {
        juce::AudioDeviceManager::PhysicalCallbackSnapshot actual;
        if (!manager.readPhysicalCallbackSnapshot(actual) || !actual.active ||
            actual.deviceEpoch != a.deviceEpoch || device->getCurrentSampleRate() != a.rate ||
            device->getCurrentBufferSizeSamples() != a.block)
            return juce::Result::fail("effectChain.deviceChanged: Prepared device configuration changed");
    }
    for (const auto &pin : a.sourcePins)
    {
        const auto *spec = controller.project.plugin(pin.id);
        if (!spec || spec->descriptor != pin.descriptor)
            return juce::Result::fail("effectChain.sourceChanged: Pinned plugin descriptor changed");
        // buildEdit temporarily installs candidate maps in final admission.
        // Inspect pinned SOURCE owners directly there, never parameterHost().
        if (!finalNative && audio.parameterHost(pin.id) != pin.host)
            return juce::Result::fail("effectChain.staleRuntime: Source mapping changed");
        const auto delivery = pin.host ? pin.host->statistics() : PluginHost::Statistics{};
        // Only this operation's pre-capture close phase can refresh native
        // state. Editor close may legitimately finalize a vendor parameter.
        const bool closing = a.closingEditors && pin.closeWanted;
        const bool ownedClosePending = closing && pin.closeSerial && !pin.closeResult;
        if (pin.host &&
            (!pin.host->alive() || pin.host->runtimeIdentity() != pin.instance ||
             (!closing &&
              (pin.host->parameterRevision() != pin.revision || pin.host->recoveryState() != pin.recovery)) ||
             pin.host->pcmIdentity() != pin.pcm || pin.host->declaredLatency() != pin.declaration ||
             pin.host->busLayout() != pin.buses ||
             // Full projection capture/history may reset instrument notes
             // during its accepted structural gap. The reset counter is not
             // native preset identity. Other paths retain reset continuity.
             (!a.fullCapture && pin.host->midiResetGeneration() != pin.resetGeneration) ||
             // Lifetime audio missed/dropped counts include the explicitly
             // accepted state-capture interruption. Keep those diagnostics;
             // they must not permanently disable every subsequent Undo/Apply.
             // Current transport health and MIDI/automation/input corruption
             // still refuse publication. Ordinary playback gates remain strict.
             delivery.midiOverflow || delivery.parametersRejected || delivery.auxiliaryRejected ||
             pin.host->automationStatistics().failed() || !pin.host->transportOutcome().succeeded() ||
             pin.host->pendingParameterRequests() || (pin.host->editorBusy() && !ownedClosePending) ||
             (pin.host->editorWindowState().exists && !closing)))
            return juce::Result::fail(
                "effectChain.staleRuntime: Native state/editor/control changed after admission; alive=" +
                juce::String(int(pin.host->alive())) + " parameterRevision=" +
                juce::String(int(pin.host->parameterRevision() != pin.revision)) + " recovery=" +
                juce::String(int(pin.host->recoveryState() != pin.recovery)) + " pcm=" +
                juce::String(int(pin.host->pcmIdentity() != pin.pcm)) + " latency=" +
                juce::String(int(pin.host->declaredLatency() != pin.declaration)) + " buses=" +
                juce::String(int(pin.host->busLayout() != pin.buses)) + " midiReset=" +
                juce::String(int(pin.host->midiResetGeneration() != pin.resetGeneration)) + " delivery=" +
                juce::String(int(pin.host->deliveryFailed())) + " transport=" +
                juce::String(int(!pin.host->transportOutcome().succeeded())) + " control=" +
                juce::String(pin.host->pendingParameterRequests()) + " editor=" +
                juce::String(int(pin.host->editorBusy() || pin.host->editorWindowState().exists)));
        if (pin.host && pin.capturedRevision && *pin.capturedRevision != pin.host->parameterRevision())
            return juce::Result::fail("effectChain.staleCapture: Native revision changed after capture");
    }
    return juce::Result::ok();
}
void Studio::serviceMixerChainApply() noexcept
{
    if (!mixerChainApplying() || mixerChains->active->capture)
        return;
    auto &w = *mixerChains;
    auto &a = *w.active;
    auto &r = *w.receipts[a.index];
    const auto terminal = [&](const char *completion, const juce::Result &result)
    {
        auto next = std::make_unique<MixerChainWorkflow::Receipt>(r);
        next->completion = completion;
        next->phase = completion;
        next->code = result.failed() ? chain_workflow_detail::code(result) : juce::String{};
        next->message =
            result.failed() ? result.getErrorMessage()
            : a.model && a.model->isNoOp()
                ? "Unchanged structural command; no model, history or audio work"
                : "Mixer structural change applied through canonical history and actual audio activation";
        status = next->message;
        w.receipts[a.index].swap(next);
        w.active.reset(); // CONTROL only, after actual capture/file/preparation drain.
    };
    try
    {
        if (a.model && a.model->wasCommitted())
        {
            r.documentChanged = !a.model->isNoOp();
            r.undoable = r.documentChanged;
            if (!a.notified)
            {
                a.notified = true;
                if (!mixerInsert(controller.project, selectedInsert))
                    selectedInsert =
                        a.insertArguments.mixerDelete && a.insertArguments.mixerDelete->replacement &&
                                mixerInsert(controller.project, *a.insertArguments.mixerDelete->replacement)
                            ? *a.insertArguments.mixerDelete->replacement
                            : 0;
                controller.deliverPendingNotifications();
                engineRevision = controller.revision;
                needsRebuild = false;
            }
            const auto source = audio.projectionDiagnostics().pluginSourceState;
            const bool sourceActive = source == "active" || source == "notRequired";
            r.audioState = audio.documentProjectionDeferred()               ? "deferredNoDevice"
                           : sourceActive                                   ? "active"
                           : source == "pending" || source == "unavailable" ? "pending"
                                                                            : "fault";
            // CONTROL/QPC only. Device callbacks can disappear without advancing
            // the native gate, so a published job must not stay busy forever.
            if (a.activationStarted && !audio.documentProjectionDeferred() &&
                (!sourceActive || devicePreparationBusy()) &&
                juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() -
                                                         a.activationStarted) >= 10.)
            {
                r.nativeRecoveryRequired = true;
                r.audioState = "fault";
                terminal(
                    "failed",
                    juce::Result::fail(
                        "effectChain.activationTimeout: Intended model/history retained; no actual audio "
                        "activation within10000ms. Restore audio; any separately owned device-preparation "
                        "operation remains available in devicePreparation readback."));
                return;
            }
            if (a.recoveryOperation && devicePreparationBusy())
                return;
            if (r.nativeRecoveryRequired && source.isEmpty() && !audio.documentProjectionDeferred())
            {
                r.audioState = "fault";
                terminal("failed", juce::Result::fail("effectChain.audioRecoveryRequired: Intended model "
                                                      "committed; audio publication failed"));
                return;
            }
            if (source == "fault" || audio.deviceRestorationRequired())
            {
                r.nativeRecoveryRequired = true;
                r.phase = "recovering";
                if (!a.recoveryOperation)
                {
                    const auto admitted = beginDevicePreparation(a.playRequested);
                    if (admitted.failed())
                    {
                        terminal("failed", admitted);
                        return;
                    }
                    a.recoveryOperation = devicePreparation->operation;
                    r.recoveryOperation = a.recoveryOperation;
                }
                if (devicePreparationBusy())
                    return;
                const auto recovered = devicePreparationState(a.recoveryOperation);
                if (recovered["completionState"].toString() != "completed")
                {
                    terminal("failed", juce::Result::fail("effectChain.audioRecoveryFailed: Intended model "
                                                          "retained; inspect device preparation"));
                    return;
                }
                return; // Require a subsequent ACTUAL source activation observation.
            }
            r.phase = "activating";
            if (source == "pending" || source == "unavailable")
                return;
            if (!sourceActive && !audio.documentProjectionDeferred())
            {
                r.nativeRecoveryRequired = true;
                terminal("failed",
                         juce::Result::fail("effectChain.audioUnavailable: Model committed; restore audio"));
                return;
            }
            if (!audio.documentProjectionDeferred())
            {
                const auto restored = routingCoordinator.fullProjectionRestored({a.patternMode, a.pattern});
                if (!restored.accepted())
                {
                    r.nativeRecoveryRequired = true;
                    terminal("failed", juce::Result::fail("effectChain.audioRecoveryFailed: Canonical model "
                                                          "committed; routing restoration refused"));
                    return;
                }
            }
            r.nativeRecoveryRequired = false;
            projectionError.clear();
            terminal("completed", juce::Result::ok());
            return;
        }
        if (a.closingEditors)
        {
            // Source/context/device identity is still exact, but native state
            // pins for these editors become final only after their real close.
            if (!a.cancel && !a.pollException && a.applyError.wasOk())
                if (auto current = mixerChainApplySourceCurrent(false); current.failed())
                    a.applyError = current;
            bool pendingClose = false;
            for (auto &pin : a.sourcePins)
            {
                if (!pin.closeWanted)
                    continue;
                const bool aborting = a.cancel || a.pollException || a.applyError.failed();
                if (!pin.closeSerial && !aborting)
                {
                    const auto admitted = pin.host->requestEditor(pin.instance, PluginEditorAction::close);
                    // Store ownership before any fallible receipt preparation.
                    pin.closeSerial = admitted.serial;
                    if (!pin.closeSerial)
                    {
                        a.applyError = juce::Result::fail(
                            "effectChain.editorCloseRefused: Native close was not admitted; model retained");
                        continue;
                    }
                }
                if (!pin.closeSerial)
                    continue; // Cancellation never starts another native action.
                if (!pin.closeResult)
                {
                    if (aborting)
                        (void)pin.host->cancelEditorRequest(pin.instance, pin.closeSerial);
                    PluginEditorResult observed;
                    if (!pin.host->readEditorResult(pin.instance, pin.closeSerial, observed))
                    {
                        pendingClose = true;
                        continue; // Retain exact dispatched ownership until Host terminal/deadline.
                    }
                    pin.closeResult = observed;
                }
                const auto &observed = *pin.closeResult;
                if (!pin.closeRecorded)
                {
                    auto evidence = chain_workflow_detail::object();
                    chain_workflow_detail::put(evidence, "plugin", juce::int64(pin.id));
                    chain_workflow_detail::put(evidence, "instance", pin.instance);
                    chain_workflow_detail::put(evidence, "serial", juce::int64(pin.closeSerial));
                    chain_workflow_detail::put(evidence, "outcome",
                                               pluginEditorOutcomeName(observed.outcome));
                    chain_workflow_detail::put(evidence, "nativeCompletionKnown", observed.completionKnown);
                    chain_workflow_detail::put(evidence, "dispatched", observed.dispatched);
                    chain_workflow_detail::put(evidence, "windowExists", observed.window.exists);
                    r.editorClosures.add(evidence);
                    pin.closeRecorded = true;
                }
                if (observed.outcome != PluginEditorOutcome::closed || !observed.completionKnown ||
                    !observed.window.known || observed.window.exists)
                    a.applyError = juce::Result::fail("effectChain.editorCloseFailed: Native editor closure "
                                                      "was not confirmed; model retained");
                // A Host timeout owns its worker shutdown. Retain the Host while
                // any actual editor execution is still reported busy.
                if (pin.host->editorBusy())
                    pendingClose = true;
            }
            if (pendingClose)
            {
                r.phase = a.cancel || a.pollException || a.applyError.failed() ? "drainingEditors"
                                                                               : "closingEditors";
                return;
            }
            if (!a.cancel && !a.pollException && a.applyError.wasOk())
            {
                for (auto &pin : a.sourcePins)
                    if (pin.closeWanted)
                    {
                        pin.revision = pin.host->parameterRevision();
                        pin.recovery = pin.host->recoveryState();
                        pin.capturedRevision.reset();
                    }
            }
            a.closingEditors = false; // All admitted native close receipts are drained.
            r.phase = "capture";
            // Next poll uses the unchanged strict pins before any fresh capture.
            return;
        }
        if (a.cancel || a.pollException || a.applyError.failed())
        {
            r.phase = "draining";
            if (a.preparationStarted)
            {
                bool drained = false;
                const auto drain = audio.drainFullProjectionPreparation(a.destination, true, drained);
                if (drain.failed())
                {
                    a.applyError = drain;
                    return;
                }
                if (!drained)
                    return;
            }
            terminal(
                a.cancel ? "cancelled" : "failed",
                a.cancel ? juce::Result::fail(
                               "effectChain.cancelled: Owned preparation drained; incumbent model retained")
                : a.applyError.failed() ? a.applyError
                                        : juce::Result::fail(a.exceptionCode + ": " + a.exceptionMessage));
            return;
        }
        if (auto valid = mixerChainApplySourceCurrent(false); valid.failed())
        {
            a.applyError = valid;
            return;
        }
        if (a.captureTarget < a.captureTargets.size())
        {
            if (auto begun = beginMixerChainApplyCapture(); begun.failed())
                a.applyError = begun;
            return;
        }
        if (!a.model)
        {
            const auto prepared =
                a.ioHistory ? controller.preparePluginIoHistory(*a.history, a.capturedSource, a.model)
                : a.structuralHistory
                    ? controller.prepareCapturedStructuralHistory(*a.history, a.capturedSource, a.model)
                : a.scalarHistory
                    ? controller.prepareNativeScalarHistory(*a.history, a.capturedSource, a.model)
                : a.routingHistory
                    ? controller.prepareCapturedMixerRoutingHistory(*a.history, a.capturedSource, a.model)
                : a.structuralCommand.isNotEmpty()
                    ? controller.prepareCapturedMixerCommand(a.structuralCommand, a.structuralArguments,
                                                             a.capturedSource, a.model)
                : a.insertHistory
                    ? controller.prepareMixerInsertHistory(*a.history, a.capturedSource, a.model)
                : a.insertCommand.isNotEmpty()
                    ? controller.prepareMixerInsertCommand(a.insertCommand, a.insertArguments,
                                                           a.capturedSource, a.model)
                : a.history ? controller.prepareMixerEffectHistory(*a.history, a.capturedSource, a.model)
                : a.sidechainTarget ? controller.prepareMixerEffectSidechain(
                                          a.capturedSource, a.sidechainTarget, a.detector, a.model)
                : a.replacementTarget
                    ? controller.prepareMixerEffectReplacement(a.capturedSource, a.replacementTarget,
                                                               a.replacementDescriptor, a.model)
                    : controller.prepareMixerChainReplacement(a.capturedSource, r.insert, *a.incoming,
                                                              a.model);
            if (prepared.failed())
            {
                a.applyError = prepared;
                return;
            }
            a.destination = *a.model->destinationBeforeCommit();
            if (a.model->isNoOp())
            {
                if (auto valid = controller.validatePreparedChange(*a.model); valid.failed())
                {
                    a.applyError = valid;
                    return;
                }
                if (!controller.commitPreparedChange(*a.model))
                {
                    a.applyError = juce::Result::fail("history.stalePreparedChange: No-op source changed");
                    return;
                }
                controller.deliverPendingNotifications();
                r.audioState.clear(); // No activation was performed; omit the audioState field.
                terminal("completed", juce::Result::ok());
                return;
            }
        }
        r.phase = "preparing";
        r.audioState = "preparing";
        const std::function<juce::Result()> finalAdmission = [this]
        { return mixerChainApplySourceCurrent(true); };
        const juce::ScopedValueSetter<bool> own(routingApplicationPreparing, true);
        a.preparationStarted = true; // Exact owned drain also handles a throwing startup call.
        const auto prepared = audio.rebuildFullProjection(a.destination, a.patternMode, a.pattern, controller,
                                                          *a.model, finalAdmission);
        if (a.model->wasCommitted())
        {
            a.activationStarted = juce::Time::getHighResolutionTicks();
            r.documentChanged = !a.model->isNoOp();
            r.undoable = r.documentChanged;
            r.phase = "activating";
            r.audioState = "pending";
            r.nativeRecoveryRequired = prepared.failed();
            if (prepared.failed())
            {
                r.code = chain_workflow_detail::code(prepared);
                r.message = prepared.getErrorMessage();
            }
            return; // Never call commit or rebuild this already published ticket again.
        }
        if (AudioEngine::preparationPending(prepared))
        {
            a.preparationStarted = true;
            return;
        }
        if (prepared.failed())
        {
            a.applyError = prepared;
            return;
        }
        a.applyError = juce::Result::fail(
            "effectChain.commitMissing: Native result did not commit the canonical ticket");
    }
    catch (...)
    {
        a.pollException = true;
    }
}
juce::var Studio::mixerChainOperation(const juce::var &args, juce::String *admittedOperation)
try
{
    using namespace chain_workflow_detail;
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!args.isObject() || !args["action"].isString())
        return result("failed", "effectChain.invalidArguments", "An action object is required");
    const auto action = args["action"].toString();
    // Cancellation takes ownership before this CONTROL call can advance a
    // prepared job to its native publication point.
    if (action != "cancel")
        serviceMixerChainFiles();
    juce::StringArray allowed{"action"};
    const bool structural =
        action == "structural" && mixerStructuralDispatch && mixerStructuralCommandDispatch.isNotEmpty();
    const bool routingHistory = action == "routingHistory" && mixerRoutingHistoryDispatch.has_value();
    const bool scalarHistory = action == "scalarHistory" && nativeScalarHistoryDispatch.has_value();
    const bool ioHistory = action == "ioHistory" && pluginIoHistoryDispatch.has_value();
    const bool structuralHistory = action == "structuralHistory" && structuralHistoryDispatch.has_value();
    const bool sidechain = action == "sidechain" && mixerEffectSidechainDispatch;
    const bool replacing = action == "replace" && mixerEffectReplacementDispatch;
    const bool insertCommand =
        (action == "insertCreate" || action == "insertDelete") && mixerInsertLifecycleDispatch;
    const bool insertHistory = action == "insertHistory" && mixerInsertHistoryDispatch.has_value();
    const bool applying = structuralHistory || ioHistory || scalarHistory || structural || routingHistory ||
                          sidechain || insertCommand || insertHistory || replacing || action == "paste" ||
                          action == "loadApply" ||
                          (action == "history" && mixerChainHistoryDispatch.has_value());
    if (sidechain)
        allowed.add("target");
    if (replacing)
        allowed.addArray({"target", "descriptor"});
    if (action == "copy" || action == "save" || applying)
        allowed.addArray({"mixer", "projectIdentity", "revision"});
    if (action == "save" || action == "loadClipboard" || action == "loadApply")
        allowed.add("path");
    if (action == "save")
        allowed.add("overwrite");
    if (action == "configureDirectory")
        allowed.add("directory");
    if (action == "job" || action == "cancel")
        allowed.add("operation");
    for (const auto &property : args.getDynamicObject()->getProperties())
        if (!allowed.contains(property.name.toString()))
            return result("failed", "effectChain.invalidArguments", "Unknown argument for this chain action");
    for (const auto *key : {"operation", "projectIdentity", "path", "directory", "descriptor"})
        if (args.hasProperty(key) && !args[key].isString())
            return result("failed", "effectChain.invalidArguments", "String argument required");
    if (action == "state")
        return mixerChainReadback();
    if (action == "job" || action == "cancel")
    {
        const auto token = args["operation"].toString();
        if (token.isEmpty() || token.length() > 100)
            return result("failed", "effectChain.invalidArguments", "Exact operation token is required");
        if (action == "job")
            return mixerChainReadback(token);
        if (!mixerChainBusy() || mixerChains->receipts[mixerChains->active->index]->operation != token)
        {
            const auto old = mixerChainReadback(token);
            return old["code"].toString() == "effectChain.unknownOperation"
                       ? old
                       : result("failed", "effectChain.terminal",
                                "Operation is already terminal; inspect job");
        }
        auto &a = *mixerChains->active;
        if (a.applying)
        {
            if (a.model && a.model->wasCommitted())
            {
                auto out = result(
                    "failed", "effectChain.tooLate",
                    "Canonical/native publication already occurred; inspect the original job and recovery");
                put(out, "operation", token);
                return out;
            }
            a.cancel = true;
            mixerChains->receipts[a.index]->phase = "cancelling";
            if (a.fileOperation.isNotEmpty())
                (void)mixerChains->files->cancel(a.fileOperation);
            serviceMixerChainFiles();
            return mixerChainReadback(token);
        }
        if (a.capture)
        {
            a.cancel = true;
            mixerChains->receipts[a.index]->phase = "cancelling";
            return mixerChainReadback(token);
        }
        const auto cancellation = mixerChains->files->cancel(a.fileOperation);
        using C = effect_chain::PresetFileJobs::Cancellation;
        if (cancellation == C::tooLate)
        {
            auto out = result("failed", "effectChain.tooLate",
                              "Cancellation refused: file commit began; the original job remains "
                              "authoritative and may complete");
            put(out, "operation", token);
            return out;
        }
        if (cancellation == C::unknownOperation)
            return result("failed", "effectChain.unknownOperation", "Helper operation is unavailable");
        serviceMixerChainFiles();
        return mixerChainReadback(token);
    }
    if (action != "copy" && action != "save" && action != "loadClipboard" && action != "configureDirectory" &&
        !applying)
        return result("failed", "effectChain.unsupportedOperation", "Unsupported chain action");
    if (mixerChainBusy() || afterPluginState || audio.pluginStateCapturePending())
        return result("failed", "effectChain.busy",
                      "Finish or cancel active owned work before starting another operation");
    if (!mixerChains)
        mixerChains = std::make_shared<MixerChainWorkflow>();
    auto &w = *mixerChains;
    if (action == "configureDirectory")
    {
        const auto directory = args["directory"].toString();
        if (directory.isEmpty() || directory.length() > 200)
            return result("failed", "effectChain.invalidArguments",
                          "Existing absolute LOCAL directory required");
        if (!w.files)
            w.files = std::make_unique<effect_chain::PresetFileJobs>(directory);
        const auto configured = w.files->configureDirectory(directory);
        if (configured.failed())
            return result("failed", code(configured), configured.getErrorMessage());
        return mixerChainReadback();
    }
    if (args.hasProperty("overwrite") && !args["overwrite"].isBool())
        return result("failed", "effectChain.invalidArguments", "overwrite must be boolean");
    const bool fileAction = action == "save" || action == "loadClipboard" || action == "loadApply";
    if (fileAction && !w.files)
        return result("failed", "effectChain.directoryUnavailable",
                      "Configure one existing preset directory first");
    if (fileAction && (args["path"].toString().isEmpty() || args["path"].toString().length() > 240))
        return result("failed", "effectChain.invalidArguments", "Absolute preset file path required");
    auto pending = std::make_unique<MixerChainWorkflow::Active>();
    auto receipt = std::make_unique<MixerChainWorkflow::Receipt>();
    receipt->operation = juce::Uuid().toString();
    receipt->action = action;
    receipt->path = args["path"].toString();
    receipt->message =
        insertCommand || insertHistory ? "Insert membership preparation pending; poll until terminal"
        : action == "loadClipboard"    ? "Preset read admitted; poll until terminal"
                                    : "Targeted native capture pending; no clipboard or file completion yet";
    pending->overwrite = bool(args.getProperty("overwrite", false));
    if (action == "copy" || action == "save" || applying)
    {
        const auto revision = args["revision"];
        const auto insert = args["mixer"];
        if ((!revision.isInt() && !revision.isInt64() && !revision.isDouble()) ||
            !std::isfinite(double(revision)) || double(revision) < 0 ||
            double(revision) > 9007199254740991. || std::floor(double(revision)) != double(revision) ||
            (!insert.isInt() && !insert.isInt64() && !insert.isDouble()) || !std::isfinite(double(insert)) ||
            double(insert) < 0 || double(insert) > std::numeric_limits<int>::max() ||
            std::floor(double(insert)) != double(insert))
            return result("failed", "effectChain.invalidArguments",
                          "Exact revision and nonnegative integer Mixer index required");
        if (args["projectIdentity"].toString() != controller.projectIdentity ||
            std::uint64_t(double(revision)) != controller.revision)
            return result("failed", "effectChain.staleCapture",
                          "Project identity/revision changed before admission");
        const auto admission = admitMixerChainCapture();
        if (admission.failed())
            return result("failed", code(admission), admission.getErrorMessage());
        const auto target = std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                                         [&](const auto &m) { return m.index == int(insert); });
        if (target == controller.project.mixer.end())
            return result("failed", "effectChain.target", "Unknown Mixer insert");
        receipt->project = controller.projectIdentity;
        receipt->revision = controller.revision;
        receipt->insert = int(insert);
        if (applying)
        {
            // Losing a previously active device is not initial offline editing.
            // Refuse before capture/publication; explicit audio recovery owns it.
            if (!audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice() &&
                !audio.canDeferChannelRouting())
                return result("failed", "routing.deviceChanged",
                              "Restore the audio device before structural Mixer changes");
            if (recordArmed || !audio.deferredWorkerPreparationIdle())
                return result("failed", "effectChain.controlBusy",
                              "Disarm recording and finish native preparation before Apply");
            if (action == "paste" && !w.clipboard)
                return result("failed", "effectChain.clipboardUnavailable",
                              "Copy or load a chain before Paste");
            pending->applying = true;
            pending->playRequested = audio.playing();
            pending->source = pending->capturedSource = controller.project;
            pending->patternMode = patternMode;
            pending->pattern = selectedPattern;
            if (action == "paste")
                pending->incoming = std::make_unique<effect_chain::Chain>(w.clipboard->chain);
            if (sidechain)
            {
                const auto &request = *mixerEffectSidechainDispatch;
                if (!request.detector || request.target != Id(double(args["target"])) ||
                    std::none_of(target->effects.begin(), target->effects.end(),
                                 [&](const auto &slot) { return slot.id == request.target; }))
                    return result("failed", "sidechain.staleTarget", "Reopen the effect detector menu");
                const auto projection = audio.projectionDiagnostics();
                if (!projection.playbackContext || projection.pluginSourceState != "active" ||
                    audio.deviceRestorationRequired())
                    return result("failed", "sidechain.deviceRequired",
                                  "Prepare an active device/source before changing detector layout");
                pending->sidechainTarget = request.target;
                pending->detector = *request.detector;
            }
            if (replacing)
            {
                const auto id = args["target"];
                if ((!id.isInt() && !id.isInt64() && !id.isDouble()) || !std::isfinite(double(id)) ||
                    double(id) < 1 || double(id) > 1000000000 || std::floor(double(id)) != double(id))
                    return result("failed", "effects.unavailableTarget", "Existing Mixer effect ID required");
                const auto found = std::find_if(target->effects.begin(), target->effects.end(),
                                                [&](const auto &slot) { return slot.id == Id(double(id)); });
                if (found == target->effects.end())
                    return result("failed", "effects.unavailableTarget",
                                  "Replacement slot changed; reopen its menu");
                const auto descriptor = args["descriptor"].toString();
                if (descriptor.isEmpty() || descriptor.getNumBytesAsUTF8() > 65536)
                    return result("failed", "effects.invalidReplacementDescriptor",
                                  "Bounded VibeRa or VST3 effect descriptor required");
                const auto xml = juce::XmlDocument::parse(descriptor);
                juce::PluginDescription type;
                if (!xml || !type.loadFromXml(*xml) ||
                    (type.pluginFormatName != "VST3" && !effects::isNativeDescription(type)) ||
                    type.isInstrument)
                    return result("failed", "effects.invalidReplacementDescriptor",
                                  "Choose a VibeRa or VST3 effect");
                pending->replacementTarget = found->id;
                pending->replacementDescriptor = descriptor;
            }
            if (structuralHistory)
            {
                pending->structuralHistory = pending->fullCapture = true;
                pending->history = structuralHistoryDispatch;
                pending->captureTargets.push_back(
                    0); // Exact all-native snapshot, including removed instrument.
            }
            else if (ioHistory)
            {
                pending->ioHistory = pending->fullCapture = true;
                pending->history = pluginIoHistoryDispatch;
                pending->captureTargets.push_back(0);
            }
            else if (scalarHistory)
            {
                pending->scalarHistory = pending->fullCapture = true;
                pending->history = nativeScalarHistoryDispatch;
                pending->captureTargets.push_back(0); // Actual all-native fresh capture, including instruments.
            }
            else if (structural || routingHistory)
            {
                pending->fullCapture = true;
                pending->captureTargets.push_back(0); // One all-plugin snapshot, including instruments.
                if (structural)
                {
                    pending->structuralCommand = mixerStructuralCommandDispatch;
                    pending->structuralArguments = *mixerStructuralDispatch;
                }
                else
                {
                    pending->routingHistory = true;
                    pending->history = mixerRoutingHistoryDispatch;
                }
            }
            else if (insertCommand)
            {
                pending->insertCommand =
                    action == "insertCreate" ? "mixer.insert.create" : "mixer.insert.delete";
                pending->insertArguments = *mixerInsertLifecycleDispatch;
                if (action == "insertDelete")
                    pending->captureTargets.push_back(int(insert));
                // Create has no outgoing native effect target to capture.
            }
            else if (insertHistory)
            {
                pending->insertHistory = true;
                pending->history = mixerInsertHistoryDispatch;
                // Surviving native bytes populate both history edges. Absent owners
                // retain their acknowledged original Undo bytes.
                for (const auto &row : controller.project.mixer)
                    if (!row.effects.empty())
                        pending->captureTargets.push_back(row.index);
            }
            else if (action == "history")
            {
                pending->history = mixerChainHistoryDispatch;
                HistoryDestination history;
                if (auto preview = controller.previewHistoryDestination(*pending->history, history);
                    preview.failed())
                    return result("failed", code(preview), preview.getErrorMessage());
                for (const auto &old : controller.project.mixer)
                {
                    const auto *next = mixerInsert(history.destination(), old.index);
                    if (next &&
                        (old.effects != next->effects || old.effectsBypassed != next->effectsBypassed))
                        pending->captureTargets.push_back(old.index);
                }
            }
            else
                pending->captureTargets.push_back(int(insert));
            if (action == "history" && audio.initialSourceTransportReadback().status ==
                                           AudioEngine::InitialSourceTransportStatus::none)
            {
                pending->fullCapture = true;
                pending->captureTargets = {0};
            }
            // Append and main-I/O changes own editor closure on their same
            // structural job. Capture pins become final after the actual ACK.
            const bool closeEditors =
                structural && (mixerStructuralCommandDispatch == "plugin.effect.insert" ||
                               mixerStructuralCommandDispatch == "plugin.io");
            for (const auto &runtime : audio.pluginRuntimeStates())
            {
                const auto *spec = controller.project.plugin(runtime.plugin);
                if (!spec)
                    continue;
                auto host = audio.parameterHost(runtime.plugin);
                if (host && (host->editorBusy() || host->pendingParameterRequests() ||
                             (host->editorWindowState().exists && !closeEditors)))
                    return result("failed", "effectChain.controlBusy",
                                  "Finish active native editor/parameter work before Apply");
                pending->sourcePins.push_back({runtime.plugin,
                                               host,
                                               host ? host->runtimeIdentity() : juce::String{},
                                               spec->descriptor,
                                               host ? host->recoveryState() : juce::String{},
                                               host ? host->parameterRevision() : 0,
                                               {}});
                if (host)
                {
                    auto &pin = pending->sourcePins.back();
                    pin.pcm = host->pcmIdentity();
                    pin.declaration = host->declaredLatency();
                    pin.buses = host->busLayout();
                    pin.resetGeneration = host->midiResetGeneration();
                    pin.closeWanted = closeEditors && host->editorWindowState().exists;
                    pending->closingEditors = pending->closingEditors || pin.closeWanted;
                }
            }
            auto &manager = audio.engine.getDeviceManager().deviceManager;
            if (auto *device = manager.getCurrentAudioDevice())
            {
                juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
                if (!manager.readPhysicalCallbackSnapshot(physical) || !physical.active ||
                    !physical.deviceEpoch)
                    return result("failed", "effectChain.deviceChanged",
                                  "No coherent active device for Apply");
                pending->deviceEpoch = physical.deviceEpoch;
                pending->rate = device->getCurrentSampleRate();
                pending->block = device->getCurrentBufferSizeSamples();
            }
        }
        else
        {
            const auto actual = audio.pluginRuntimeStates();
            std::vector<PluginRuntimeState> owners;
            for (const auto &slot : target->effects)
            {
                const auto found = std::find_if(actual.begin(), actual.end(),
                                                [&](const auto &r) { return r.plugin == slot.id; });
                owners.push_back(found != actual.end() ? *found : PluginRuntimeState{slot.id, {}, 0, false});
                pending->ids.push_back(slot.id);
                pending->hosts.push_back(audio.parameterHost(slot.id));
            }
            const auto begun =
                effect_chain::beginCapture(controller.project, controller.projectIdentity,
                                           controller.revision, int(insert), owners, pending->intent);
            if (begun.failed())
                return result("failed", code(begun), begun.getErrorMessage());
            receipt->project = controller.projectIdentity;
            receipt->revision = controller.revision;
            receipt->insert = int(insert);
            pending->capture = true;
        }
    }
    pending->index = w.receipts.size();
    const auto token = receipt->operation;
    // Prepare every fallible callback owner BEFORE publishing active.capture.
    std::function<void(juce::Result, Project)> captureCompletion;
    if (!applying && action != "loadClipboard")
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        captureCompletion = [safe, token](juce::Result captured, Project snapshot)
        {
            if (safe)
                safe->finishMixerChainCapture(token, captured, std::move(snapshot));
        };
    }
    w.receipts.push_back(std::move(receipt));
    w.active.swap(pending);
    if (admittedOperation)
        *admittedOperation = token; // Retain ownership before dispatch/response can throw.
    if (action == "loadClipboard" || action == "loadApply")
    {
        auto submitted = w.files->submitLoad(w.receipts[w.active->index]->path, w.active->fileOperation);
        if (submitted.failed())
        {
            auto &r = *w.receipts[w.active->index];
            r.completion = "failed";
            r.phase = "failed";
            r.code = code(submitted);
            r.message = submitted.getErrorMessage();
            w.active.reset();
        }
        else
            w.receipts[w.active->index]->phase = "queued";
    }
    else if (applying)
    {
        w.receipts[w.active->index]->phase = w.active->closingEditors ? "closingEditors" : "capture";
        serviceMixerChainApply();
    }
    else
    {
        try
        {
            withPluginState(captureCompletion, &w.active->ids);
        }
        catch (...)
        {
            if (w.active)
            {
                w.active->pollException = true;
                if (audio.pluginStateCapturePending())
                {
                    // withPluginState admission threw after installing actual native work.
                    // Move the preconstructed callback into its otherwise empty owned slot.
                    if (!afterPluginState)
                        afterPluginState = std::move(captureCompletion);
                }
                else
                {
                    w.active->capture = false;
                    serviceMixerChainFiles();
                }
            }
        }
    }
    return mixerChainReadback(token);
}
catch (const std::exception &error)
{
    if (mixerChainBusy())
    {
        auto out = mixerChainReadback(mixerChains->receipts[mixerChains->active->index]->operation);
        chain_workflow_detail::put(out, "message",
                                   "Request response preparation failed while owned work remains pending; "
                                   "poll this exact operation");
        return out;
    }
    return chain_workflow_detail::result("failed", "effectChain.preparationFailed",
                                         juce::String(error.what()));
}
catch (...)
{
    if (mixerChainBusy())
    {
        auto out = mixerChainReadback(mixerChains->receipts[mixerChains->active->index]->operation);
        chain_workflow_detail::put(out, "message",
                                   "Request response preparation failed while owned work remains pending; "
                                   "poll this exact operation");
        return out;
    }
    return chain_workflow_detail::result(
        "failed", "effectChain.preparationFailed",
        "Chain preparation did not complete; inspect state/job before retry");
}
bool Studio::performMixerChainAction(const juce::String &id)
{
    const auto *action = mixerChainAction(id);
    if (!action)
        return false;
    if (id == "mixer.chain.directory")
        chooseMixerChainDirectory();
    else if (id == "mixer.chain.save" || id == "mixer.chain.loadClipboard" || id == "mixer.chain.loadApply")
        chooseMixerChainFile(action, selectedInsert);
    else
    {
        auto args = chain_workflow_detail::object();
        chain_workflow_detail::put(args, "action", action);
        if (id == "mixer.chain.copy" || id == "mixer.chain.paste")
        {
            chain_workflow_detail::put(args, "mixer", selectedInsert);
            chain_workflow_detail::put(args, "projectIdentity", controller.projectIdentity);
            chain_workflow_detail::put(args, "revision", juce::int64(controller.revision));
        }
        else if (mixerChainBusy())
            chain_workflow_detail::put(args, "operation",
                                       mixerChains->receipts[mixerChains->active->index]->operation);
        const auto outcome = mixerChainOperation(args);
        status = outcome["message"].toString();
    }
    return true;
}
void Studio::chooseMixerChainDirectory()
{
    if (mixerChainBusy() || afterPluginState || chooser)
    {
        status = "Chain or file chooser work is active";
        return;
    }
    const auto current = mixerChains && mixerChains->files
                             ? juce::File(mixerChains->files->configuredDirectory())
                             : juce::File{};
    chooser = std::make_unique<juce::FileChooser>("Choose existing LOCAL chain preset directory", current);
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectDirectories,
                         [safe](const juce::FileChooser &fc)
                         {
                             if (!safe)
                                 return;
                             const auto file = fc.getResult();
                             if (file != juce::File{})
                             {
                                 auto args = chain_workflow_detail::object();
                                 chain_workflow_detail::put(args, "action", "configureDirectory");
                                 chain_workflow_detail::put(args, "directory", file.getFullPathName());
                                 const auto out = safe->mixerChainOperation(args);
                                 safe->status = out["completionState"].toString() == "completed"
                                                    ? "Chain preset directory configured"
                                                    : out["message"].toString();
                             }
                             safe->chooser.reset();
                         });
}
void Studio::chooseMixerChainFile(const juce::String &action, int insert)
{
    if (!mixerChains || !mixerChains->files)
    {
        status = "Choose the chain preset directory first";
        return;
    }
    if (mixerChainBusy() || afterPluginState || chooser)
    {
        status = "Chain or file chooser work is active";
        return;
    }
    const auto project = controller.projectIdentity;
    const auto revision = controller.revision;
    chooser = std::make_unique<juce::FileChooser>(
        action == "save"        ? "Save chain preset"
        : action == "loadApply" ? "Load chain preset and replace selected insert chain"
                                : "Load chain preset to clipboard (no Apply)",
        juce::File(mixerChains->files->configuredDirectory()), "*.vibfx");
    auto safe = juce::Component::SafePointer<Studio>(this);
    const bool saving = action == "save";
    chooser->launchAsync(
        (saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                : juce::FileBrowserComponent::openMode) |
            juce::FileBrowserComponent::canSelectFiles,
        [safe, project, revision, insert, action, saving](const juce::FileChooser &fc)
        {
            if (!safe)
                return;
            const auto file = fc.getResult();
            if (file != juce::File{})
            {
                auto args = chain_workflow_detail::object();
                chain_workflow_detail::put(args, "action", action);
                chain_workflow_detail::put(args, "path", file.getFullPathName());
                if (saving || action == "loadApply")
                {
                    chain_workflow_detail::put(args, "mixer", insert);
                    chain_workflow_detail::put(args, "projectIdentity", project);
                    chain_workflow_detail::put(args, "revision", juce::int64(revision));
                }
                if (saving && file.existsAsFile())
                {
                    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                     .withTitle("Replace chain preset?")
                                                     .withMessage(file.getFullPathName())
                                                     .withButton("Replace")
                                                     .withButton("Cancel"),
                                                 [safe, args](int choice) mutable
                                                 {
                                                     if (!safe || choice != 1)
                                                         return;
                                                     chain_workflow_detail::put(args, "overwrite", true);
                                                     const auto out = safe->mixerChainOperation(args);
                                                     safe->status = out["message"].toString();
                                                 });
                }
                else
                {
                    const auto out = safe->mixerChainOperation(args);
                    safe->status = out["message"].toString();
                }
            }
            safe->chooser.reset();
        });
}
