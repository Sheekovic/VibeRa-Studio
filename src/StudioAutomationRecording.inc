// SPDX-License-Identifier: AGPL-3.0-or-later
// Included once inside namespace vibera, in Studio.cpp. CONTROL/message thread only.
bool Studio::automationRecordingActive() const noexcept
{
    return bool(automationTouch);
}
juce::Result Studio::automationRecordingAdmission() const
{
    return automationTouch
               ? juce::Result::fail("automationRecording.busy: Finish/retry/cancel the owned automation take")
               : juce::Result::ok();
}
juce::var Studio::automationRecordingState(int offset, int limit) const
{
    if (!automationTouch && !automationTouchTerminal.isVoid())
        return automationTouchTerminal;
    offset = juce::jlimit(0, 65536, offset);
    limit = juce::jlimit(1, 512, limit);
    auto *o = new juce::DynamicObject;
    juce::var result(o);
    o->setProperty("completionState", automationTouch ? "pending" : "completed");
    o->setProperty("cancelRequested", automationTouchCancel);
    o->setProperty("phase", "idle");
    o->setProperty("active", bool(automationTouch));
    o->setProperty("documentChanged", false);
    o->setProperty("undoable", false);
    o->setProperty("nativeParameterRecording", false);
    o->setProperty("dspSettlingAcknowledged", false);
    o->setProperty("completionScope", "canonicalSongAutomationTake");
    o->setProperty("revision", juce::int64(controller.revision));
    if (automationTouch)
    {
        const auto &t = *automationTouch;
        static constexpr const char *phases[]{"armed", "capturing", "sealing", "ready", "invalid", "failed"};
        o->setProperty("phase", phases[int(t.phase)]);
        o->setProperty("token", t.token);
        o->setProperty("insert", t.insert);
        o->setProperty("eventCount", int(t.events.size()));
        o->setProperty("sourceChannel", juce::int64(t.channel));
        o->setProperty("sourceEpoch", juce::int64(t.epoch));
        o->setProperty("originBeat", t.originBeat);
        juce::Array<juce::var> events;
        for (int i = offset; i < int(t.events.size()) && events.size() < limit; ++i)
        {
            const auto &event = t.events[size_t(i)];
            auto *e = new juce::DynamicObject;
            e->setProperty("kind", int(event.key.kind));
            e->setProperty("owner", juce::int64(event.key.owner));
            e->setProperty("parameter", juce::int64(event.key.parameter));
            e->setProperty("seconds", event.seconds);
            e->setProperty("value", event.value);
            events.add(juce::var(e));
        }
        o->setProperty("events", events);
        o->setProperty("eventsOffset", offset);
        o->setProperty("eventsTruncated", offset + events.size() < int(t.events.size()));
        if (t.pendingEvent)
        {
            auto *pending = new juce::DynamicObject;
            pending->setProperty("kind", int(t.pendingEvent->key.kind));
            pending->setProperty("owner", juce::int64(t.pendingEvent->key.owner));
            pending->setProperty("parameter", juce::int64(t.pendingEvent->key.parameter));
            pending->setProperty("seconds", t.pendingEvent->seconds);
            pending->setProperty("value", t.pendingEvent->value);
            o->setProperty("pendingEvent", juce::var(pending));
        }
        juce::Array<juce::var> targets;
        for (const auto &target : t.targets)
            targets.add(mixerAutomationTargetState(target));
        o->setProperty("targets", targets);
        if (t.error.isNotEmpty())
        {
            auto *e = new juce::DynamicObject;
            e->setProperty("code", t.error.upToFirstOccurrenceOf(":", false, false));
            e->setProperty("message", t.error);
            e->setProperty("retryable", t.phase == MixerAutomationTake::Phase::failed);
            o->setProperty("error", juce::var(e));
            o->setProperty("completionState", "failed");
        }
    }
    return result;
}
juce::Result Studio::startAutomationRecording(int insert, int lane)
{
    if ((samplerControllerLearn && samplerControllerLearn->state == "pending") || automationTouch ||
        nativeScalarBusy() || servicingAutomationTouch || controller.hasGesture() || noteSlicePending() ||
        noteStretchPending() || devicePreparationBusy() || routingPending() || mixerChainApplying() ||
        afterPluginState || pendingExport || audio.rendering() || midiFileBusy || pluginRecovery ||
        hasPendingMidiTake() || !audioImports.idle() || audioImportAfterTake ||
        midiController.learnIndex >= 0 || nextAutomationTouchSession == std::numeric_limits<unsigned>::max())
        return juce::Result::fail("automationRecording.busy: Finish pending work before arming");
    if (patternMode || audio.looping())
        return juce::Result::fail("automationRecording.unsupportedMode: Song without loop required; Pattern "
                                  "events and overdub remain separate work");
    auto source = audio.midiInput.retainRecordingLane();
    auto *device = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    MidiClock::Position clock;
    if (!source || !source->enabled.load() || !source->clock.read(clock) || clock.wall <= 0 || !device)
        return juce::Result::fail("automationRecording.clockUnavailable: Select a prepared input source and "
                                  "wait for its real audio clock");
    std::unique_ptr<MixerAutomationTake> next;
    const auto session = nextAutomationTouchSession + 1;
    if (auto result =
            MixerAutomationTake::prepare(controller.project, controller.dirty, insert, lane,
                                         controller.projectIdentity, juce::Uuid().toString(), session, next);
        result.failed())
        return result;
    next->channel = source->channel;
    next->epoch = source->epoch.load();
    next->rate = device->getCurrentSampleRate();
    next->dropped = audio.midiInput.controls.dropped();
    next->controlGenerations = audio.midiInput.controls.mappingGenerations();
    next->controllerCcGeneration = audio.midiInput.controls.controllerGeneration();
    next->externalChangesBefore = controller.externalChangeSerial();
    next->controllerProfileBefore = juce::JSON::toString(midiController.state());
    auto nativeBefore = audio.pluginRuntimeStates();
    automationTouchNativeBefore.swap(nativeBefore);
    automationTouchSource = std::move(source);
    automationTouchDevice = device;
    automationTouch = std::move(next);
    nextAutomationTouchSession = session;
    automationTouchTerminal = juce::var{}; // No fallible work after ownership publication.
    serviceAutomationRecording();
    return juce::Result::ok();
}
std::optional<juce::Result> Studio::automationRecordingCommand(const juce::String &id,
                                                               const CommandArgs &args)
{
    if (!automationTouch)
        return {};
    auto &t = *automationTouch;
    if (servicingAutomationTouch)
        return juce::Result::fail("automationRecording.reentry: Owned recording is being reconciled");
    const auto key = MixerAutomationTake::keyForCommand(id, args.mixer, args.target);
    if (!key || !t.target(*key))
        return automationRecordingAdmission();
    if (t.phase != MixerAutomationTake::Phase::capturing &&
        !(t.phase == MixerAutomationTake::Phase::sealing && automationTouchInputSession == t.session))
        return juce::Result::fail(
            "automationRecording.notCapturing: Start Song playback or finish/cancel retained take");
    MidiClock::Position clock;
    double seconds = 0;
    if (automationTouchInputSession)
    {
        if (automationTouchInputSession != t.session || !automationTouchInputValid)
            return juce::Result::fail(
                "automationRecording.staleInput: Producer timestamp belongs to another take");
        seconds = automationTouchInputSeconds;
    }
    else
    {
        if (!automationTouchSource->clock.read(clock) || !clock.playing)
            return juce::Result::fail(
                "automationRecording.clockUnavailable: Value edit requires playing source clock");
        seconds = clock.seconds + juce::jlimit(0., clock.block,
                                               juce::Time::getMillisecondCounterHiRes() / 1000. - clock.wall);
    }
    MixerAutomationTake::Event event{*key, seconds, float(args.value)};
    if (auto result = t.admitEvent(event); result.failed())
    {
        if (result.getErrorMessage().startsWith("automationRecording.invalidClock:") ||
            result.getErrorMessage().startsWith("automationRecording.capacity:"))
        {
            t.pendingEvent = event;
            audio.midiInput.controls.endAutomationStamp();
            t.phase = MixerAutomationTake::Phase::invalid;
            t.inputFailed = true;
            t.error = result.getErrorMessage();
        }
        return result;
    }
    t.pendingEvent = event;
    const auto failed = [&](const juce::Result &result) -> std::optional<juce::Result>
    {
        audio.midiInput.controls.endAutomationStamp();
        t.phase = MixerAutomationTake::Phase::invalid;
        t.inputFailed = true;
        t.error = result.getErrorMessage();
        return result;
    };
    try
    {
        if (auto source = automationRecordingSourceCurrent(); source.failed())
            return failed(source);
        const juce::ScopedValueSetter<bool> owner(servicingAutomationTouch, true);
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = controller.prepareAutomationTouchPreview(t, id, args, prepared); result.failed())
            return failed(result);
        auto expected = std::make_unique<Project>(*prepared->destinationBeforeCommit());
        if (auto result = controller.validatePreparedChange(*prepared); result.failed())
            return failed(result);
        if (!controller.commitPreparedChange(*prepared))
            return failed(juce::Result::fail("automationRecording.staleSource: Preview commit refused"));
        t.accepted(event, expected); // Nonallocating, before observer delivery/reentry.
        controller.deliverPendingNotifications();
        return juce::Result::ok();
    }
    catch (...)
    {
        return failed(juce::Result::fail(
            "automationRecording.preparationFailed: Pending input and partial take retained"));
    }
}
juce::Result Studio::automationRecordingSourceCurrent() const
{
    if (!automationTouch || !automationTouchSource)
        return juce::Result::fail("automationRecording.missingTake: No active take");
    const auto &t = *automationTouch;
    auto *device = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (controller.projectIdentity != t.identity || !t.sourceCurrent(controller.project) ||
        (!t.cutoffFrozen &&
         (patternMode || audio.looping() || audio.midiInput.retainRecordingLane() != automationTouchSource ||
          automationTouchSource->epoch.load() != t.epoch || automationTouchSource->channel != t.channel ||
          !device || device != automationTouchDevice || device->getCurrentSampleRate() != t.rate ||
          audio.midiInput.controls.dropped() != t.dropped ||
          audio.midiInput.controls.mappingGenerations() != t.controlGenerations ||
          audio.midiInput.controls.controllerGeneration() != t.controllerCcGeneration ||
          midiController.learnIndex >= 0 ||
          juce::JSON::toString(midiController.state()) != t.controllerProfileBefore)))
        return juce::Result::fail(
            "automationRecording.staleSource: Source/Project/device/input changed; captured take retained");
    return juce::Result::ok();
}
void Studio::serviceAutomationRecording()
{
    if (!automationTouch || servicingAutomationTouch)
        return;
    try
    {
        auto &t = *automationTouch;
        if (t.phase == MixerAutomationTake::Phase::invalid || t.phase == MixerAutomationTake::Phase::failed)
            return;
        if (auto current = automationRecordingSourceCurrent(); current.failed())
        {
            audio.midiInput.controls.endAutomationStamp();
            t.phase = MixerAutomationTake::Phase::invalid;
            t.inputFailed = true;
            t.error = current.getErrorMessage();
            return;
        }
        MidiClock::Position clock;
        if (!automationTouchSource->clock.read(clock))
            return;
        if (t.phase == MixerAutomationTake::Phase::armed && clock.playing && audio.playing())
        {
            t.originSeconds =
                clock.seconds +
                juce::jlimit(0., clock.block, juce::Time::getMillisecondCounterHiRes() / 1000. - clock.wall);
            t.originBeat = audio.positionBeat();
            if (!audio.midiInput.controls.beginAutomationStamp(automationTouchSource->clock, t.session))
                return;
            t.phase = MixerAutomationTake::Phase::capturing;
        }
        if (t.phase == MixerAutomationTake::Phase::sealing && audio.midiInput.controls.automationStampIdle())
        {
            (void)finishAutomationRecording(automationTouchCancel);
        }
        else if (t.phase == MixerAutomationTake::Phase::capturing && !audio.playing())
            (void)finishAutomationRecording(false);
    }
    catch (...)
    {
        if (automationTouch)
        {
            audio.midiInput.controls.endAutomationStamp();
            automationTouch->phase = MixerAutomationTake::Phase::invalid;
            automationTouch->inputFailed = true;
            automationTouch->error = "automationRecording.preparationFailed: Partial take retained; cancel "
                                     "restores owned baseline";
        }
    }
}
juce::Result Studio::finishAutomationRecording(bool cancel)
{
    if (!automationTouch)
        return juce::Result::ok();
    if (servicingAutomationTouch || automationTouchDraining)
        return juce::Result::fail("automationRecording.reentry: Finish is already owned");
    auto &t = *automationTouch;
    automationTouchCancel = automationTouchCancel || cancel;
    cancel = automationTouchCancel;
    audio.midiInput.controls.endAutomationStamp();
    if (!audio.midiInput.controls.quiesceAutomationStamp())
    {
        t.phase = MixerAutomationTake::Phase::invalid;
        t.inputFailed = true;
        t.error = "automationRecording.quiescenceFailed: Producer did not close; partial take and clock "
                  "owner retained";
        return juce::Result::fail(t.error);
    }
    try
    {
        if (!cancel)
        {
            if (t.inputFailed)
                return juce::Result::fail(t.error.isNotEmpty()
                                              ? t.error
                                              : "automationRecording.invalidCapture: Partial input retained");
            if (auto source = automationRecordingSourceCurrent(); source.failed())
            {
                t.phase = MixerAutomationTake::Phase::invalid;
                t.inputFailed = true;
                t.error = source.getErrorMessage();
                return source;
            }
        }
        if (t.phase != MixerAutomationTake::Phase::ready && t.phase != MixerAutomationTake::Phase::failed &&
            t.phase != MixerAutomationTake::Phase::invalid)
        {
            MidiClock::Position clock;
            if (!automationTouchSource->clock.read(clock))
                return juce::Result::fail(
                    "automationRecording.clockUnavailable: Cutoff unavailable; take retained");
            t.stopSeconds =
                clock.seconds +
                (clock.playing ? juce::jlimit(0., clock.block,
                                              juce::Time::getMillisecondCounterHiRes() / 1000. - clock.wall)
                               : 0.);
            t.cutoffFrozen = true;
            t.phase = MixerAutomationTake::Phase::sealing;
            {
                const juce::ScopedValueSetter<bool> draining(automationTouchDraining, true);
                midiController.drain();
            }
            if (t.phase == MixerAutomationTake::Phase::invalid && !cancel)
                return juce::Result::fail(t.error);
            if (t.phase != MixerAutomationTake::Phase::invalid)
                t.phase = MixerAutomationTake::Phase::ready;
        }
        else if (cancel)
        {
            const juce::ScopedValueSetter<bool> draining(automationTouchDraining, true);
            midiController.drain(); // Reject invalid stamps; never replay them after retiring the take.
        }
        const juce::ScopedValueSetter<bool> owner(servicingAutomationTouch, true);
        // Opaque native edits are outside this recorder/history. Preserve their
        // newer dirty ownership even when cancelling an otherwise clean baseline.
        if ((cancel || t.events.empty()) && audio.pluginRuntimeStates() != automationTouchNativeBefore)
            t.beforeDirty = true;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = controller.prepareAutomationTouchFinish(t, cancel, prepared); result.failed())
        {
            t.phase = MixerAutomationTake::Phase::failed;
            t.error = result.getErrorMessage();
            return result;
        }
        auto terminal = automationRecordingState();
        auto *o = terminal.getDynamicObject();
        o->removeProperty("error");
        o->setProperty("active", false);
        o->setProperty("phase", cancel ? "cancelled" : "completed");
        o->setProperty("completionState", cancel ? "cancelled" : "completed");
        o->setProperty("documentChanged", !cancel && !t.events.empty());
        o->setProperty("undoable", !cancel && !t.events.empty());
        o->setProperty("revision", juce::int64(controller.revision + (prepared->isNoOp() ? 0 : 1)));
        if (auto result = controller.validatePreparedChange(*prepared); result.failed())
        {
            t.phase = MixerAutomationTake::Phase::failed;
            t.error = result.getErrorMessage();
            return result;
        }
        if (!controller.commitPreparedChange(*prepared))
        {
            t.phase = MixerAutomationTake::Phase::failed;
            t.error = "automationRecording.staleSource: Final publication refused; take retained";
            return juce::Result::fail(t.error);
        }
        automationTouchTerminal = std::move(terminal);
        auto retired = std::move(automationTouch);
        auto source = std::move(automationTouchSource);
        automationTouchDevice = nullptr;
        automationTouchCancel = false;
        controller.deliverPendingNotifications();
        return juce::Result::ok();
    }
    catch (...)
    {
        if (automationTouch)
        {
            t.phase = MixerAutomationTake::Phase::failed;
            t.error = "automationRecording.preparationFailed: Take retained; retry or cancel";
        }
        return juce::Result::fail(
            "automationRecording.preparationFailed: Inspect actual recording state before retry");
    }
}
juce::var Studio::automationRecordingOperation(const juce::var &args)
{
    if (!args.isObject() || !args["action"].isString())
        throw McpFailure("automationRecording.invalidArguments", "Typed action required");
    const auto action = args["action"].toString();
    juce::StringArray allowed{"action"};
    if (action == "state")
        allowed.addArray({"offset", "limit"});
    if (action == "arm")
        allowed.addArray({"insert", "lane", "projectIdentity", "revision"});
    else if (action == "value")
        allowed.addArray({"token", "control", "sendTarget", "value"});
    else if (action == "finish" || action == "retry" || action == "cancel")
        allowed.add("token");
    else if (action != "state")
        throw McpFailure("automationRecording.invalidArguments", "Unknown recording action");
    for (const auto &p : args.getDynamicObject()->getProperties())
        if (!allowed.contains(p.name.toString()))
            throw McpFailure("automationRecording.invalidArguments", "Field does not belong to this action");
    juce::Result result = juce::Result::ok();
    if (action == "arm")
    {
        if (!args["insert"].isInt() || (args.hasProperty("lane") && !args["lane"].isInt()) ||
            !args["projectIdentity"].isString() ||
            args["projectIdentity"].toString() != controller.projectIdentity ||
            (!args["revision"].isInt() && !args["revision"].isInt64()) ||
            juce::int64(args["revision"]) != juce::int64(controller.revision))
            throw McpFailure("automationRecording.staleContext",
                             "Current projectIdentity/revision and stable insert required");
        result = startAutomationRecording(int(args["insert"]), int(args.getProperty("lane", 0)));
    }
    else if (action != "state")
    {
        if (!automationTouch || !args["token"].isString() ||
            args["token"].toString() != automationTouch->token)
            throw McpFailure("automationRecording.staleToken", "Exact active recording token required");
        if (action == "value")
        {
            const auto control = parseMixerAutomationControl(args["control"]);
            if (!control ||
                (!args["value"].isInt() && !args["value"].isInt64() && !args["value"].isDouble()) ||
                (*control == MixerAutomationControl::send) != args.hasProperty("sendTarget") ||
                (args.hasProperty("sendTarget") && !args["sendTarget"].isInt()))
                throw McpFailure("automationRecording.invalidArguments",
                                 "Typed supported control/value and send destination only for send required");
            CommandArgs a;
            a.mixer = automationTouch->insert;
            a.value = double(args["value"]);
            a.target = args.hasProperty("sendTarget") ? int(args["sendTarget"]) : 0;
            const auto id = *control == MixerAutomationControl::gain               ? "mixer.gain"
                            : *control == MixerAutomationControl::pan              ? "mixer.pan"
                            : *control == MixerAutomationControl::stereoSeparation ? "mixer.stereoSeparation"
                                                                                   : "mixer.send";
            result = controller.execute(id, a);
        }
        else
            result = finishAutomationRecording(action == "cancel");
    }
    if (result.failed())
        throw McpFailure(result.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                         result.getErrorMessage());
    int offset = 0, limit = 128;
    if (action == "state")
    {
        if ((args.hasProperty("offset") &&
             (!args["offset"].isInt() || int(args["offset"]) < 0 || int(args["offset"]) > 65536)) ||
            (args.hasProperty("limit") &&
             (!args["limit"].isInt() || int(args["limit"]) < 1 || int(args["limit"]) > 512)))
            throw McpFailure("automationRecording.invalidArguments",
                             "State offset0..65536 and limit1..512 required");
        offset = int(args.getProperty("offset", 0));
        limit = int(args.getProperty("limit", 128));
    }
    return automationRecordingState(offset, limit);
}
