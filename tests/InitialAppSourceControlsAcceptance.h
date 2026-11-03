// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Include AFTER InitialAppSourceTransportAcceptance.h; shared original seed and
// settings helpers, not changed baseline oracles. No actual run by draft author.
namespace initial_app_controls_acceptance
{
using namespace vibera;
using initial_app_source_acceptance::capturedOriginal;
using initial_app_source_acceptance::Storage;
// Independent analytical oracle, not production DSP or JUCE smoother calls.
struct Ramp
{
    double previous = 1, target = 1;
    int start = 0, length = 720;
    double at(int frame) const noexcept
    {
        const double fraction = std::clamp(double(frame - start + 1) / length, 0., 1.);
        return previous + (target - previous) * fraction;
    }
    void set(double value, int frame) noexcept
    {
        if (value == target)
            return;
        previous = at(frame - 1);
        target = value;
        start = frame;
    }
};
struct StripOracle
{
    Ramp gain, mute, left, right;
    std::array<Ramp, 4> matrix;
    StripOracle()
    {
        matrix[0].previous = matrix[0].target = 1;
        matrix[1].previous = matrix[1].target = 0;
        matrix[2].previous = matrix[2].target = 0;
        matrix[3].previous = matrix[3].target = 1;
        for (auto &coefficient : matrix)
            coefficient.length = 240;
    }
    void set(const MixerInsert &insert, int frame)
    {
        gain.set(insert.gain, frame);
        mute.set(insert.muted ? 0 : 1, frame);
        const double pan = std::abs(insert.pan) <= .005 ? 0 : insert.pan;
        left.set(1 - pan, frame);
        right.set(1 + pan, frame);
        // Independent two-channel linear separation equation and discrete
        // orientation. Interpolate final manual coefficients over literal5ms.
        const double amount = insert.stereoSeparation;
        const double own = amount > 0 ? 1 - amount / 2 : 1;
        const double cross = amount > 0 ? amount / 2 : amount;
        const double sign = insert.phaseInverted ? -1 : 1;
        const std::array<double, 4> values =
            insert.stereoSwapped ? std::array<double, 4>{sign * cross, sign * own, sign * own, sign * cross}
                                 : std::array<double, 4>{sign * own, sign * cross, sign * cross, sign * own};
        for (size_t i = 0; i < matrix.size(); ++i)
            matrix[i].set(values[i], frame);
    }
    std::array<double, 2> sample(double inputLeft, double inputRight, int frame, bool automated, bool pattern,
                                 int transport, int sourceStartFrame) const
    {
        double l, r, volume, panLeft, panRight;
        if (automated && !pattern)
        {
            // Existing Pattern compiler emits NO automation lanes; Song reads
            // a four-beat placement with an independently scheduled steep
            // 256-frame segment against SOURCE musical time. Both native
            // PluginNode stages report editTime adjusted by literal KQ latency.
            const double phase = std::clamp(double(frame - transport - sourceStartFrame) / 256., 0., 1.);
            const float separation = float(.75 * phase);
            const float pan = float(-.5 + phase);
            volume = float(.5 + .5 * phase);
            l = separation >= 0 ? inputLeft + (inputRight - inputLeft) * separation / 2
                                : inputLeft + inputRight * separation;
            r = separation >= 0 ? inputRight + (inputLeft - inputRight) * separation / 2
                                : inputRight + inputLeft * separation;
            panLeft = 1 - pan;
            panRight = 1 + pan;
        }
        else
        {
            l = matrix[0].at(frame) * inputLeft + matrix[1].at(frame) * inputRight;
            r = matrix[2].at(frame) * inputLeft + matrix[3].at(frame) * inputRight;
            volume = gain.at(frame);
            panLeft = left.at(frame);
            panRight = right.at(frame);
        }
        const double audible = mute.at(frame);
        return {l * volume * audible * panLeft, r * volume * audible * panRight};
    }
};
struct Oracle
{
    StripOracle insert, master;
    bool automated = false;
    int sourceStartFrame = 0;
    void controls(const Project &project, int frame)
    {
        insert.set(*mixerInsert(project, 1), frame);
        master.set(*mixerInsert(project, 0), frame);
    }
    std::array<double, 2> sample(double l, double r, int frame, bool pattern, int transport) const
    {
        const auto first = insert.sample(l, r, frame, automated, pattern, transport, sourceStartFrame);
        return master.sample(first[0], first[1], frame, false, pattern, transport, sourceStartFrame);
    }
};
inline void run(const juce::File &folder, const PluginSpec &stored, const PluginSpec &originalEffect,
                bool patternMode, int providerFrames)
{
    using Status = AudioEngine::InitialSourceTransportStatus;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual app source attempt");
    Storage storage(folder.getChildFile("settings"));
    Controller controller;
    auto &project = controller.project;
    project.channels.resize(1);
    project.channels[0].instrument = stored;
    project.channels[0].instrument->id = project.allocate();
    project.channels[0].gain = 1;
    project.channels[0].pan = 0;
    project.channels[0].mixer = 1;
    const auto channel = project.channels[0].id;
    const auto instrument = project.channels[0].instrument->id;
    for (auto &insert : project.mixer)
    {
        insert.gain = 1;
        insert.pan = 0;
        insert.effects.clear();
    }
    auto unusedEffect = originalEffect;
    unusedEffect.id = project.allocate();
    project.mixer[2].effects.push_back(
        unusedEffect); // Native0, zero-input/pruned worker still belongs to the fixed bank.
    project.patterns.front().length = 64;
    project.patterns.front().autoLength = false; // Fixed held-input observation window.
    project.patterns.front().notes.clear();
    const auto pattern = project.patterns.front().id;
    project.placements = {{project.allocate(), pattern, 0, 0, 64, 0}};
    // Exact saved state is round-tripped BEFORE private worker construction.
    const auto file = folder.getChildFile("initial-state.vibra");
    Project restored;
    require(saveProject(project, file).wasOk() && loadProject(file, restored).wasOk() &&
                restored.state().isEquivalentTo(project.state()) &&
                restored.plugin(instrument)->state == stored.state,
            "Real project roundtrip retains actual captured opaque native state");
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    auto &manager = audio->engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio->engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(device && device->getCurrentBufferSizeSamples() == 256 &&
                device->getCurrentSampleRate() == 48000.,
            "Actual unchanged48k256 provider configuration");
    AudioEngine::InitialSourceTransportOptions options;
    options.quantum = 256;
    options.maximumSourceScalePpm = 1000000;
    options.explicitProviderFrames = providerFrames;
    options.sessionGeneration = patternMode ? 101 : 103;
    options.membershipGeneration = patternMode ? 107 : 109;
    const auto original = project.state();
    auto bad = options;
    bad.quantum = 257;
    require(audio->prepareInitialSourceTransport(project, bad).status == Status::invalidBudget &&
                audio->initialSourceTransportReadback().status == Status::none &&
                audio->projectionDiagnostics().edit == 0 && project.state().isEquivalentTo(original),
            "Refused initial quantum has no destination/native/model state mutation");
    bad = options;
    bad.sessionGeneration = 0;
    require(audio->prepareInitialSourceTransport(project, bad).status == Status::invalidProject &&
                audio->initialSourceTransportReadback().status == Status::none,
            "Invalid generation cannot publish a source identity");
    require(audio->prepareInitialSourceTransport(project, options).status == Status::configured,
            "Explicit initial source policy prepared without workers/context");
    const int expectedPackets = providerFrames == 256 ? 2 : 3;
    const int transport = expectedPackets * 256; // Independent declared512/768, never PCM-fitted.
    const auto configured = audio->initialSourceTransportReadback();
    require(configured.status == Status::configured && configured.quantum == 256 &&
                configured.packets == expectedPackets && configured.transportSamples == transport &&
                configured.physicalFrames == providerFrames &&
                configured.maximumSourceFrames == providerFrames && !configured.nativeKnown &&
                !configured.membershipKnown,
            "Configured declaration is not graph/worker/audio completion");
    require(awaitProjection(*audio, project, patternMode, pattern).wasOk(),
            "Actual initial AudioEngine rebuild path");
    const auto host = audio->parameterHost(instrument);
    require(host && host->alive() && host->transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                host->transportPacketCount() == expectedPackets &&
                host->transportLatencySamples() == transport && host->latencySamples() == transport &&
                host->declaredLatency().nativeSamples == 0 &&
                nativeParameter(parameterCatalogue(*host), 77).value == .25f,
            "Actual worker restored opaque gain and advertises literal KQ+native0");
    const auto pending = audio->initialSourceTransportReadback();
    require(pending.status == Status::pending && !pending.pairedClosed && pending.nativeKnown &&
                pending.membershipKnown && pending.memberCount == 2 &&
                pending.membership.phase == source_membership::Phase::prepared,
            "Actual native initial context/session installed before any processing, truthfully pending");
    const auto effectHost = audio->parameterHost(unusedEffect.id);
    require(effectHost && effectHost != host &&
                effectHost->transportPolicy() == PluginHost::TransportPolicy::preparedBurst &&
                effectHost->transportLatencySamples() == transport &&
                effectHost->declaredLatency().nativeSamples == 0,
            "Actual second native0 effect worker participates even when zero-input/pruned");
    const auto effectPid = effectHost->processId();
    const auto effectInstance = effectHost->runtimeIdentity();
    const auto pid = host->processId();
    const auto instance = host->runtimeIdentity();
    const auto reset = host->midiResetGeneration();
    const auto starts = device->starts, stops = device->stops;
    juce::AudioBuffer<float> actual(2, 131072), expected(2, 131072);
    actual.clear();
    expected.clear();
    std::array<std::unique_ptr<juce::AudioBuffer<float>>, 5> buffers;
    const std::array<int, 5> sizes{64, 65, 127, 256, 512};
    for (std::size_t i = 0; i < sizes.size(); ++i)
        buffers[i] = std::make_unique<juce::AudioBuffer<float>>(2, sizes[i]);
    juce::String csv = "phase,frame,frames,cursorBefore,cursorAfter,missed,dropped,error,ms,new,delete,held,"
                       "sourceSerial,closedSerial,quota\n";
    int framesWritten = 0;
    juce::int64 on = -1, off = -1;
    Oracle oracle;
    bool all = true;
    juce::String failure;
    try
    {
        audio->withInitialSourceTransportContext(
            [&](te::Edit &edit, te::EditPlaybackContext &context)
            {
                require(edit.initialSourceGeneration == options.sessionGeneration && edit.closedNativeIO &&
                            !context.nativeIOAttachmentRefused() && !context.sourceLifecycleInvalidated(),
                        "Actual immutable closedNativeIO excludes native attachments, not graph MIDI flags");
                require(edit.getMasterVolumePlugin()->getPanLaw() == te::PanLawLinear,
                        "Original linear-pan oracle requires actual prepared native linear law");
                for (auto *track : te::getAudioTracks(edit))
                    require(track->getVolumePlugin()->getPanLaw() == te::PanLawLinear,
                            "Every actual prepared track law matches the declared original oracle");
                auto *head = context.getNodePlayHead();
                require(head != nullptr, "Actual initial local playhead");
                audio->setMidiTarget(channel);
                audio->setLooping(false);
                audio->playStop();
                dispatchMessages(20);
                const auto origin = head->getReferenceSampleRange().getEnd();
                const auto initialStats = host->statistics();
                const auto drive = [&](const char *phase, int frames)
                {
                    juce::AudioBuffer<float> *output = nullptr;
                    for (std::size_t i = 0; i < sizes.size(); ++i)
                        if (sizes[i] == frames)
                            output = buffers[i].get();
                    require(output && frames <= providerFrames, "Prepared admitted actual physical span");
                    const auto before = audio->projectionDiagnostics();
                    const auto beforeCursor = host->packetFrame();
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    const auto start = juce::Time::getHighResolutionTicks();
                    {
                        struct Probe
                        {
                            bool previous = callbackAllocationProbe;
                            Probe()
                            {
                                callbackAllocationProbe = true;
                            }
                            ~Probe()
                            {
                                callbackAllocationProbe = previous;
                            }
                        } probe;
                        device->process(*output);
                    }
                    const auto ms =
                        SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
                    const auto after = audio->projectionDiagnostics();
                    const auto source = audio->initialSourceTransportReadback();
                    const auto range = head->getReferenceSampleRange();
                    double error = 0;
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < frames; ++n)
                        {
                            const auto index = juce::int64(framesWritten + n) - transport;
                            const bool held = on >= 0 && index >= on && (off < 0 || index < off);
                            const double rawLeft = held ? .25 * (61. / 512 + 1. / 4096) : 0.;
                            const double rawRight = held ? -.25 * (68. / 512 + 1. / 8192) : 0.;
                            const auto wanted = oracle.sample(rawLeft, rawRight, framesWritten + n,
                                                              patternMode, transport)[size_t(c)];
                            const float observed = output->getSample(c, n);
                            actual.setSample(c, framesWritten + n, observed);
                            expected.setSample(c, framesWritten + n, wanted);
                            error = std::max(error, std::abs(double(observed) - wanted));
                            if (!held && observed != 0.f)
                                error = std::numeric_limits<double>::infinity();
                            if (!std::isfinite(observed))
                                error = std::numeric_limits<double>::infinity();
                        }
                    framesWritten += frames;
                    dispatchMessages(6);
                    host->service(); // CONTROL after WHOLE physical callback.
                    const auto stats = host->statistics();
                    const int nativeSlices = (frames + 255) / 256;
                    const bool ok =
                        error <= 0.000003 && callbackAllocations.load() == allocations &&
                        callbackDestructions.load() == destructions && ms <= 1000. * frames / 48000. &&
                        before.channels.size() == 1 && after.channels.size() == 1 &&
                        after.channels[0].renderCalls == before.channels[0].renderCalls + nativeSlices &&
                        before.edit == after.edit && before.playbackContext == after.playbackContext &&
                        after.headPlaying && after.transportPlaying &&
                        range.getEnd() == origin + framesWritten &&
                        range.getStart() == origin + framesWritten - std::min(frames, 256) &&
                        source.status == Status::ready && source.pairedClosed &&
                        source.native.sourceProcessed == frames && source.native.physicalFrames == frames &&
                        source.native.sourceQuota == frames &&
                        source.membership.lastClosedSerial == source.native.physicalSerial &&
                        host->packetFrame() == framesWritten % 256 &&
                        stats.submitted == initialStats.submitted + std::uint64_t(framesWritten / 256) &&
                        stats.missed == initialStats.missed && stats.dropped == initialStats.dropped &&
                        stats.parametersRejected == initialStats.parametersRejected &&
                        host->transportOutcome().succeeded() && host->processId() == pid &&
                        host->runtimeIdentity() == instance && host->midiResetGeneration() == reset &&
                        effectHost->processId() == effectPid &&
                        effectHost->runtimeIdentity() == effectInstance &&
                        effectHost->transportOutcome().succeeded() && source.memberCount == 2 &&
                        device->starts == starts && device->stops == stops &&
                        device->getCurrentBufferSizeSamples() == 256;
                    all &= ok;
                    csv += juce::String(phase) + "," + juce::String(framesWritten - frames) + "," +
                           juce::String(frames) + "," + juce::String(beforeCursor) + "," +
                           juce::String(host->packetFrame()) + "," + juce::String(juce::int64(stats.missed)) +
                           "," + juce::String(juce::int64(stats.dropped)) + "," + juce::String(error, 12) +
                           "," + juce::String(ms, 6) + "," +
                           juce::String(callbackAllocations.load() - allocations) + "," +
                           juce::String(callbackDestructions.load() - destructions) + "," +
                           juce::String(after.channels[0].heldInputKeys) + "," +
                           juce::String(juce::int64(source.native.physicalSerial)) + "," +
                           juce::String(juce::int64(source.membership.lastClosedSerial)) + "," +
                           juce::String(source.native.sourceQuota) + "\n";
                    require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                            "Retain every actual app callback before oracle");
                    require(
                        ok,
                        "Actual initial app KQ PCM/clock/owner/deadline/scope failure; preserve evidence");
                };
                for (int i = 0; i < 8; ++i)
                    drive("all_startup_frames", 256);
                on = framesWritten;
                audio->midiInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("physical_on", 127);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 1,
                        "Actual physical key owner");
                audio->typingInput.handleIncomingMidiMessage(
                    nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
                drive("typing_same_pitch", 64);
                drive("partial_before_old_boundary", 64);
                drive("same_callback_crosses_logical_boundary", 65);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 2,
                        "Two actual owners, one native note");
                const auto publish = [&]
                {
                    require(audio->rebuild(project, patternMode, pattern).wasOk(),
                            "Canonical immutable Mixer targets published without reprepare");
                    oracle.controls(project, framesWritten);
                };
                const auto command = [&](const char *id, int insert, double value, bool enabled = true)
                {
                    CommandArgs a;
                    a.mixer = insert;
                    a.value = value;
                    a.enabled = enabled;
                    require(controller.execute(id, a).wasOk(), "Canonical Mixer control accepted");
                    publish();
                };
                const auto settled = [&](const char *label)
                {
                    for (int i = 0; i < 4; ++i)
                        drive(label, 256);
                };
                // Ensure the actual physical note has emerged through full KQ
                // before target changes; every startup sample above is checked.
                settled("held_before_controls");
                command("mixer.gain", 1, .5);
                settled("gain_15ms_full_ramp");
                require(controller.undoEditChecked().wasOk(), "Actual gain Undo");
                publish();
                settled("gain_undo_full_ramp");
                require(controller.redoEditChecked().wasOk(), "Actual gain Redo");
                publish();
                settled("gain_redo_full_ramp");
                command("mixer.pan", 1, .5);
                settled("linear_pan_15ms_full_ramp");
                command("mixer.stereoSeparation", 1, .5);
                settled("stereo_5ms_full_ramp");
                command("mixer.phaseInvert", 1, 0, true);
                settled("polarity_5ms_full_ramp");
                command("mixer.stereoSwap", 1, 0, true);
                settled("swap_5ms_full_ramp");
                command("mixer.mute", 1, 0, true);
                settled("mute_15ms_full_ramp");
                require(controller.undoEditChecked().wasOk(), "Actual mute Undo");
                publish();
                settled("unmute_15ms_full_ramp");
                const auto baselineGesture = project.state();
                const auto undoBeforeGesture = controller.undo.getUndoDescription();
                const juce::String owner = "initial-source-controls";
                require(controller.beginGesture(owner), "Actual owned gain gesture admitted");
                CommandArgs preview;
                preview.mixer = 1;
                preview.value = .25;
                require(controller.execute("mixer.gain", preview, owner).wasOk(), "Actual gain preview");
                publish();
                drive("gain_gesture_prefix", 127);
                preview.value = .75;
                require(controller.execute("mixer.gain", preview, owner).wasOk(),
                        "Actual gain rapid retarget");
                publish();
                drive("gain_gesture_retarget", 65);
                require(controller.cancelGestureChecked(owner).wasOk() &&
                            project.state().isEquivalentTo(baselineGesture) &&
                            controller.undo.getUndoDescription() == undoBeforeGesture,
                        "Escape restores exact model/history baseline");
                publish();
                settled("cancel_ramp_from_actual_current_target");
                command("mixer.pan", 0, -.25);
                settled("master_pan_15ms");
                require(controller.undoEditChecked().wasOk(), "Actual Master pan Undo");
                publish();
                settled("master_pan_undo");
                command("mixer.gain", 0, .75);
                settled("master_gain_15ms");
                require(controller.undoEditChecked().wasOk(), "Actual Master gain Undo");
                publish();
                settled("master_gain_undo");
                command("mixer.phaseInvert", 1, 0, false);
                command("mixer.stereoSwap", 1, 0, false);
                command("mixer.stereoSeparation", 1, 0);
                command("mixer.pan", 1, 0);
                command("mixer.gain", 1, 1);
                settled("manual_identity_before_typed_automation");
                // Genuine canonical organisation during the SAME held plugin
                // voice. Metadata never replaces a source/Host; group mute/solo
                // publishes actual per-insert targets through the same runtime.
                CommandArgs organisation;
                organisation.mixer = 1;
                organisation.text = "Held lead";
                require(controller.execute("mixer.rename", organisation).wasOk(), "Canonical held rename");
                publish();
                drive("rename_held_owner", 256);
                organisation.value = double(0xff5ac8d8u);
                require(controller.execute("mixer.colour", organisation).wasOk(), "Canonical held colour");
                publish();
                drive("colour_held_owner", 256);
                const auto orderedModel = project.state();
                organisation = {};
                organisation.mixer = 1;
                require(controller.execute("mixer.moveRight", organisation).wasOk(), "Canonical held order");
                publish();
                drive("display_order_held_owner", 256);
                require(controller.undoEditChecked().wasOk() && project.state().isEquivalentTo(orderedModel),
                        "Display order Undo restores exact values while native identities stay fixed");
                publish();
                drive("display_order_undo_held_owner", 256);
                command("mixer.mute", 3, 0, true);
                drive("unrelated_prior_mute", 256);
                organisation = {};
                organisation.ids = {1, 2};
                organisation.text = "Held sources";
                require(controller.execute("mixer.group.create", organisation).wasOk() &&
                            project.mixerGroups.size() == 1,
                        "Canonical group uses existing audible/pruned fixed members");
                const auto groupId = project.mixerGroups.front().id;
                publish();
                drive("group_create_held_owner", 256);
                const auto unmutedGroup = project.state();
                organisation = {};
                organisation.mixerGroup = groupId;
                organisation.enabled = true;
                require(controller.execute("mixer.group.mute", organisation).wasOk(),
                        "Canonical held group mute");
                publish();
                settled("group_mute_exact_full_ramp");
                require(controller.undoEditChecked().wasOk() && project.state().isEquivalentTo(unmutedGroup),
                        "One group mute Undo restores exact members/history");
                publish();
                settled("group_mute_undo_full_ramp");
                require(controller.execute("mixer.group.soloConnected", organisation).wasOk() &&
                            project.mixerSolo && project.mixerSolo->group == groupId,
                        "Actual connected group solo ownership");
                publish();
                drive("group_solo_keeps_held_source", 256);
                organisation.restore = true;
                require(controller.execute("mixer.group.unsolo", organisation).wasOk() &&
                            !project.mixerSolo && mixerInsert(project, 3)->muted &&
                            !mixerInsert(project, 1)->muted && !mixerInsert(project, 2)->muted,
                        "Actual group restore preserves pre-existing mute and both members");
                publish();
                drive("group_restore_keeps_held_source", 256);
                const auto groupedFile = folder.getChildFile("held-group.vibra");
                Project reopenedGroup;
                require(saveProject(project, groupedFile).wasOk() &&
                            loadProject(groupedFile, reopenedGroup).wasOk() &&
                            reopenedGroup.state().isEquivalentTo(project.state()) &&
                            reopenedGroup.mixerGroups.front().id == groupId,
                        "Actual group UUID/mutes and source state persist during the held voice");
                const auto beforeAutomation = project.state();
                // Chosen on CONTROL before source creation, never fitted from
                // output or head readback. The declared KQ shifts this exact
                // source segment to512 future physical frames after publication.
                oracle.sourceStartFrame = framesWritten + 512 - transport;
                require(oracle.sourceStartFrame >= 0 && oracle.sourceStartFrame + 256 < 96000,
                        "Original steep segment fits the genuine four-beat source");
                require(folder.getChildFile("automation-clock.txt")
                            .replaceWithText("publicationFrame=" + juce::String(framesWritten) +
                                             "\ntransport=" + juce::String(transport) +
                                             "\nsourceStartFrame=" + juce::String(oracle.sourceStartFrame) +
                                             "\nsourceFrames=256\nphysicalStartFrame=" +
                                             juce::String(framesWritten + 512) + "\n"),
                        "Retain independent source-clock inputs BEFORE native automation publication");
                require(controller.beginGesture(owner), "Typed automation one owned history transaction");
                for (const auto control : {MixerAutomationControl::gain, MixerAutomationControl::pan,
                                           MixerAutomationControl::stereoSeparation})
                {
                    CommandArgs a;
                    a.mixer = 1;
                    a.automationControl = control;
                    a.length = 4;
                    a.beat = 0;
                    require(controller.execute("automation.create", a, owner).wasOk(),
                            "Canonical typed Mixer automation source");
                    const auto &created = project.automation.sources.back();
                    const auto sourceId = created.id;
                    const auto firstPoint = created.points.front().id, lastPoint = created.points.back().id;
                    // Point operations target source/point IDs, not a Mixer
                    // target discriminator. Do not carry create-only fields
                    // into the shared canonical validator.
                    a = {};
                    a.target = firstPoint;
                    a.beat = 0;
                    a.value = control == MixerAutomationControl::gain  ? .25
                              : control == MixerAutomationControl::pan ? .25
                                                                       : .5;
                    require(controller.execute("automation.point.move", a, owner).wasOk(),
                            "Independent typed start value");
                    a.target = lastPoint;
                    a.beat = 4;
                    a.value = control == MixerAutomationControl::gain  ? .5
                              : control == MixerAutomationControl::pan ? .75
                                                                       : .875;
                    require(controller.execute("automation.point.move", a, owner).wasOk(),
                            "Independent typed end value");
                    a.target = sourceId;
                    a.beat = double(oracle.sourceStartFrame) / 24000.;
                    a.value = control == MixerAutomationControl::gain  ? .25
                              : control == MixerAutomationControl::pan ? .25
                                                                       : .5;
                    require(controller.execute("automation.point.add", a, owner).wasOk(),
                            "Canonical independently timed steep segment start");
                    a.beat = double(oracle.sourceStartFrame + 256) / 24000.;
                    a.value = control == MixerAutomationControl::gain  ? .5
                              : control == MixerAutomationControl::pan ? .75
                                                                       : .875;
                    require(controller.execute("automation.point.add", a, owner).wasOk(),
                            "Canonical independently timed steep segment end");
                    a.target = 0;
                    a.mixer = 1;
                    a.automationControl = control;
                    a.value = control == MixerAutomationControl::gain  ? .75
                              : control == MixerAutomationControl::pan ? .25
                                                                       : -.5;
                    require(controller.execute("automation.init", a, owner).wasOk(),
                            "Persist typed initialization in physical target units");
                }
                require(controller.endGestureChecked("Three typed Mixer controls", owner).wasOk(),
                        "Actual grouped automation completion");
                publish();
                oracle.automated = true;
                settled(patternMode ? "pattern_automation_persisted_but_no_active_lanes"
                                    : "song_literal_source_clock_automation");
                const auto automatedModel = project.state();
                const auto saved = folder.getChildFile("controls-automation.vibra");
                Project parsed;
                require(!saved.exists() && saveProject(project, saved).wasOk() &&
                            loadProject(saved, parsed).wasOk() &&
                            parsed.state().isEquivalentTo(automatedModel),
                        "Actual control/typed automation project roundtrip");
                require(controller.undoEditChecked().wasOk() &&
                            project.state().isEquivalentTo(beforeAutomation),
                        "One actual Undo removes all three typed sources");
                publish();
                oracle.automated = false;
                settled("typed_automation_group_undo");
                require(controller.redoEditChecked().wasOk() &&
                            project.state().isEquivalentTo(automatedModel),
                        "One actual Redo restores typed sources/IDs");
                publish();
                oracle.automated = true;
                settled("typed_automation_group_redo");
                require(controller.undoEditChecked().wasOk(), "Return to manual before final owner release");
                publish();
                oracle.automated = false;
                settled("manual_after_automation");
                const auto invalidBaseline = project.state();
                const auto invalidUndo = controller.undo.getUndoDescription(),
                           invalidRedo = controller.undo.getRedoDescription();
                CommandArgs invalid;
                invalid.mixer = 1;
                invalid.value = std::numeric_limits<double>::quiet_NaN();
                require(controller.execute("mixer.gain", invalid).failed() &&
                            project.state().isEquivalentTo(invalidBaseline) &&
                            controller.undo.getUndoDescription() == invalidUndo &&
                            controller.undo.getRedoDescription() == invalidRedo,
                        "Nonfinite changed control is atomically refused by shared canonical validation");
                invalid.value = 1.001;
                require(controller.execute("mixer.pan", invalid).failed() &&
                            project.state().isEquivalentTo(invalidBaseline) &&
                            controller.undo.getUndoDescription() == invalidUndo &&
                            controller.undo.getRedoDescription() == invalidRedo,
                        "Out-of-range pan remains atomically refused");
                const auto rejected = [&](const Project &candidate, const char *label)
                {
                    const auto before = audio->projectionDiagnostics();
                    const auto cursor = host->packetFrame();
                    require(
                        candidate.validate().wasOk(),
                        "Unsupported candidate is a genuine valid project, not an invalid-fixture refusal");
                    const auto refusal = audio->rebuild(candidate, patternMode, pattern);
                    require(refusal.failed() && refusal.getErrorMessage().startsWith("source.initialOnly:"),
                            label);
                    const auto after = audio->projectionDiagnostics();
                    require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                                host->packetFrame() == cursor && host->processId() == pid &&
                                host->runtimeIdentity() == instance &&
                                project.state().isEquivalentTo(invalidBaseline),
                            "Rejected unsupported topology/config preserves incumbent and model");
                };
                auto candidate = project;
                candidate.channel(channel)->mixer = 2;
                rejected(candidate, "Channel route remains frozen");
                candidate = project;
                candidate.mixer[1].routes.front().gain = .5f;
                rejected(candidate, "Send-level qualification remains separate");
                candidate = project;
                candidate.mixer[1].routes.push_back({2, 1});
                rejected(candidate, "New route cannot enter fixed membership graph");
                candidate = project;
                candidate.tempo += 1;
                rejected(candidate, "Tempo/source-clock preparation remains separate");
                candidate = project;
                candidate.plugin(unusedEffect.id)->bypass = true;
                rejected(candidate, "Effect bypass not qualified by Mixer-control exclusion");
                candidate = project;
                candidate.plugin(unusedEffect.id)->wet = .5f;
                rejected(candidate, "Effect wet qualification remains separate");
                drive("unsupported_changes_leave_actual_held_PCM", 256);
                // Real canonical score edit/Undo/Redo at a future beat: no invented
                // DSP input or silence exclusion. Held physical voice must continue.
                CommandArgs note;
                note.channel = channel;
                note.pattern = pattern;
                note.beat = 32;
                note.length = 1;
                note.pitch = 72;
                note.value = .5;
                require(controller.execute("notes.add", note).wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Initial policy keeps ordinary immutable note edits available");
                drive("note_edit_unaffected_held_voice", 256);
                require(controller.undoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Canonical note Undo retained");
                drive("note_undo_unaffected_held_voice", 256);
                require(controller.redoEditChecked().wasOk() &&
                            audio->rebuild(project, patternMode, pattern).wasOk(),
                        "Canonical note Redo retained");
                for (int i = 0; i < 5; ++i)
                    drive("held_full_PCM", 256);
                if (providerFrames == 512)
                    drive("genuine_whole_physical512_not_two_fake_callbacks", 512);
                audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("physical_off_typing_still_holds", 127);
                require(audio->projectionDiagnostics().channels[0].heldInputKeys == 1,
                        "Shared typing owner retains sound");
                off = framesWritten;
                audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
                drive("final_typing_off", 65);
                for (int i = 0; i < 5; ++i)
                    drive("all_release_tail_frames", 256);
                const auto catalogue = parameterCatalogue(*host, true);
                require(nativeParameter(catalogue, 90).value == 1. / 1024 &&
                            nativeParameter(catalogue, 91).value == 1. / 1024,
                        "Actual native one on/one off counters, no duplicate/retrigger or lost release");
                require(parameterResult(*host, host->setParameter(77, .33f, catalogue.generation)).result ==
                            0,
                        "Genuine native dirty control state before rejected reconfiguration");
                const auto beforeConfig = audio->initialSourceTransportReadback();
                require(
                    audio->prepareInitialSourceTransport(project, options).status == Status::invalidOwner &&
                        audio->initialSourceTransportReadback().deviceEpoch == beforeConfig.deviceEpoch &&
                        host->processId() == pid && host->runtimeIdentity() == instance &&
                        std::abs(nativeParameter(parameterCatalogue(*host, true), 77).value - .33f) < .00001f,
                    "Incumbent initial setup refusal cannot replace or reset exact worker/native state");
                auto changed = project;
                changed.channel(channel)->mixer = 2;
                std::unique_ptr<AudioEngine::PreparedChannelRouting> plan;
                require(
                    audio->prepareChannelRouting(changed, plan).status ==
                            AudioEngine::ChannelRoutingStatus::unsupportedDelta &&
                        !plan,
                    "Unqualified structural/membership/PDC changes explicitly refuse, no fake completion");
                // Genuine oversize physical callback: failure is NOT a successful
                // muted music callback. No Host/cursor/MIDI/reference input advances.
                juce::AudioBuffer<float> oversized(2, providerFrames + 256);
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < oversized.getNumSamples(); ++n)
                        oversized.setSample(c, n, .375f);
                const auto statsBefore = host->statistics();
                const auto cursor = host->packetFrame();
                const auto reference = head->getReferenceSampleRange();
                const auto beforeSource = audio->projectionDiagnostics();
                const auto allocations = callbackAllocations.load(),
                           destructions = callbackDestructions.load();
                const auto started = juce::Time::getHighResolutionTicks();
                {
                    struct Probe
                    {
                        bool previous = callbackAllocationProbe;
                        Probe()
                        {
                            callbackAllocationProbe = true;
                        }
                        ~Probe()
                        {
                            callbackAllocationProbe = previous;
                        }
                    } probe;
                    device->process(oversized);
                }
                const auto failedMs =
                    SyntheticCallbackTiming::milliseconds(started, juce::Time::getHighResolutionTicks());
                bool failedOutput = true;
                auto raw =
                    folder.getChildFile("oversized-explicit-failure-planar-f32.raw").createOutputStream();
                require(bool(raw) && raw->setPosition(0) && raw->truncate().wasOk(),
                        "Owned explicit failed-callback evidence");
                for (int c = 0; c < 2; ++c)
                {
                    for (int n = 0; n < oversized.getNumSamples(); ++n)
                        failedOutput &= oversized.getSample(c, n) == 0.f;
                    require(raw->write(oversized.getReadPointer(c),
                                       std::size_t(oversized.getNumSamples()) * sizeof(float)),
                            "All failed-callback samples retained");
                }
                raw->flush();
                require(raw->getStatus().wasOk(), "Failed-callback evidence flush/status");
                const auto failed = audio->initialSourceTransportReadback();
                require(failed.status == Status::sourceFault && failed.native.failed &&
                            failed.native.status == te::SourceBurstSession::Status::physicalBurstExceeded &&
                            host->packetFrame() == cursor &&
                            host->statistics().submitted == statsBefore.submitted &&
                            host->statistics().missed == statsBefore.missed &&
                            host->statistics().dropped == statsBefore.dropped &&
                            head->getReferenceSampleRange() == reference &&
                            audio->projectionDiagnostics().channels[0].renderCalls ==
                                beforeSource.channels[0].renderCalls &&
                            callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == destructions && failedOutput &&
                            failedMs <= 1000. * oversized.getNumSamples() / 48000.,
                        "Actual over-budget outer refuses before source/worker/clock mutation, typed sticky "
                        "failure");
            });
    }
    catch (const std::exception &error)
    {
        all = false;
        failure = error.what();
    }
    const auto write = [&](const char *name, const juce::AudioBuffer<float> &data)
    {
        auto output = folder.getChildFile(name).createOutputStream();
        require(bool(output) && output->setPosition(0) && output->truncate().wasOk(),
                "Fresh complete PCM stream");
        for (int c = 0; c < 2; ++c)
            require(output->write(data.getReadPointer(c), std::size_t(framesWritten) * sizeof(float)),
                    "All planar PCM samples retained");
        output->flush();
        require(output->getStatus().wasOk(), "Raw evidence flush/status");
    };
    write("actual-planar-f32.raw", actual);
    write("expected-planar-f32.raw", expected);
    require(folder.getChildFile("result.txt")
                .replaceWithText("pass=" + juce::String(int(all)) + "\ntransport=" + juce::String(transport) +
                                 "\nframes=" + juce::String(framesWritten) + "\nfailure=" + failure +
                                 "\nNOT qualified: default1Q, native fixedcost1frame, structural PDC, live "
                                 "membership replacement, installed providers, hardware/listening.\n"),
            "Actual terminal initial app result retained");
    require(all, "Initial app source experiment failed; retained complete PCM/clock/source evidence");
}

} // namespace initial_app_controls_acceptance
static void initialAppSourceControlsTests(const juce::File &folder, const PluginSpec &genuine,
                                          const PluginSpec &effect)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh initial source controls evidence");
    require(genuine.instrument && genuine.name == "VibeRa Instrument Runtime Fixture" && !effect.instrument &&
                effect.name == "VibeRa Parameter Fixture",
            "Exact original fixtures, no arbitrary provider PCM claim");
    const auto captured = initial_app_controls_acceptance::capturedOriginal(genuine);
    std::exception_ptr first;
    juce::String results;
    for (const int maximum : {256, 512})
        for (const bool pattern : {true, false})
        {
            const auto name = "bound-" + juce::String(maximum) + (pattern ? "-pattern" : "-song");
            try
            {
                initial_app_controls_acceptance::run(folder.getChildFile(name), captured, effect, pattern,
                                                     maximum);
                results += name + "=completed\n";
            }
            catch (const std::exception &error)
            {
                if (!first)
                    first = std::current_exception();
                results += name + "=failed:" + juce::String(error.what()) + "\n";
            }
            require(folder.getChildFile("cases.txt").replaceWithText(results),
                    "Retain every independent control scenario");
        }
    if (first)
        std::rethrow_exception(first);
}
