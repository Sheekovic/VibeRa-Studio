// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Include after require, awaitProjection, SyntheticDevice and callback heap probes.
// Original source-only draft: actual public application/native lifetime operations.
#include "ColdMixerPruningFixture.h"
namespace vibera::test::routing_lifetime
{
struct Storage
{
    juce::String old = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
    explicit Storage(const juce::File &owned)
    {
        require(
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", owned.getFullPathName().toWideCharPointer()) == 0,
            "Owned lifetime fixture storage");
    }
    ~Storage()
    {
        _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", old.toWideCharPointer());
    }
};
struct Fixture
{
    Controller controller;
    std::unique_ptr<AudioEngine> audio;
    SyntheticDevice *device = nullptr;
    std::unique_ptr<cold_mixer_pruning::Proof> coldPruning;
    Id channel = 0, pattern = 0, missing = 0;
    explicit Fixture(const juce::File &folder, bool placeholder = false, bool patternMode = true,
                     std::function<void(Project &)> configure = {}, bool pruneCold = false,
                     int explicitProviderFrames = 0)
    {
        require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned lifetime scenario");
        auto &p = controller.project;
        p.channels.resize(1);
        auto &c = p.channels.front();
        c.source = "builtin:bass";
        c.mixer = 1;
        c.gain = 1;
        c.pan = 0;
        channel = c.id;
        pattern = p.patterns.front().id;
        p.patterns.front().length = 64;
        p.patterns.front().autoLength = false; // Exact placeholder and held-input horizon.
        p.patterns.front().notes.clear();
        p.placements = {{p.allocate(), pattern, 0, 0, 64, 0}};
        for (auto &insert : p.mixer)
        {
            insert.gain = 1;
            insert.pan = 0;
            insert.effects.clear();
        }
        if (placeholder)
        {
            juce::PluginDescription type;
            type.name = "Original absent instrument lifetime fixture";
            type.pluginFormatName = "VST3";
            type.fileOrIdentifier = folder.getChildFile("deliberately-not-installed.vst3").getFullPathName();
            type.uniqueId = 0x56625261;
            type.isInstrument = true;
            require(!juce::File(type.fileOrIdentifier).exists(),
                    "Genuine absent file, not a fake provider mock");
            PluginSpec spec;
            spec.id = p.allocate();
            spec.name = type.name;
            spec.descriptor = type.createXml()->toString();
            spec.state = juce::MemoryBlock("recoverable-state", 17).toBase64Encoding();
            spec.instrument = true;
            c.instrument = spec;
            missing = spec.id;
            p.patterns.front().notes.push_back({p.allocate(), channel, 0, 32, 60, 1, false});
        }
        if (configure)
            configure(p); // CONTROL before the first projection/context/start.
        require(p.channels.size() == 1 && p.channels.front().id == channel && !p.patterns.empty() &&
                    p.patterns.front().id == pattern,
                "Fixture configuration preserves cached original Channel/Pattern identities");
        require(p.validate().wasOk(), "Genuine valid independent routing lifetime project");
        audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"),
                                              explicitProviderFrames);
        auto &manager = audio->engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement settings("DEVICESETUP");
        settings.setAttribute("deviceType", "VibeRa synthetic");
        settings.setAttribute("audioOutputDeviceName", "In-memory output");
        settings.setAttribute("audioDeviceRate", 48000.);
        settings.setAttribute("audioDeviceBufferSize", 256);
        audio->engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, settings);
        manager.initialise(0, 2);
        dispatchMessages(20);
        device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device && device->getCurrentSampleRate() == 48000 &&
                    device->getCurrentBufferSizeSamples() == 256,
                "Actual configured synthetic lifetime device");
        require(awaitProjection(*audio, p, patternMode, pattern).wasOk(),
                "Actual lifetime source projection");
        if (pruneCold)
        {
            require(!placeholder && patternMode, "Cold diagnostic uses exactly ordinary no-placeholder Pattern fixture");
            coldPruning = cold_mixer_pruning::prepare(*audio, p, folder.getChildFile("cold-proof"));
        }
        audio->setMidiTarget(channel); // Existing public route allocates actual native context.
        require(audio->projectionDiagnostics().playbackContext != 0, "Actual allocated native context");
        audio->setLooping(false);
        audio->playStop();
        dispatchMessages(20);
    }
    te::Edit &nativeEdit()
    {
        const auto snapshot = audio->projectionDiagnostics();
        require(snapshot.edit != 0, "Live CONTROL-only diagnostic identity before dereference");
        // Closed test adapter only: the authoritative diagnostic is emitted from
        // exactly edit.get(), never user supplied or used after its owner dies.
        return *reinterpret_cast<te::Edit *>(snapshot.edit);
    }
    te::Selectable *sampler()
    {
        const auto snapshot = audio->projectionDiagnostics();
        require(snapshot.channels.size() == 1 && snapshot.channels.front().sampler != 0,
                "Exact current Plugin base-pointer diagnostic");
        return static_cast<te::Selectable *>(
            reinterpret_cast<te::Plugin *>(snapshot.channels.front().sampler));
    }
    void silentCallback(const char *phase)
    {
        juce::AudioBuffer<float> output(2, 256); // CONTROL preparation.
        const auto allocated = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
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
            device->process(output);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        bool exactSilence = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
                exactSilence &= output.getSample(c, n) == 0.f;
        require(exactSilence && callbackAllocations.load() == allocated &&
                    callbackDestructions.load() == deleted &&
                    ms <= 1000. * 256 / device->getCurrentSampleRate(),
                phase);
        dispatchMessages(6); // No control work inside the callback.
    }
};
struct Change
{
    std::unique_ptr<PreparedControllerChange> model;
    std::unique_ptr<AudioEngine::PreparedChannelRouting> audio;
};
inline Change prepare(Fixture &f, int destination)
{
    Change result;
    CommandArgs args;
    args.channel = f.channel;
    args.mixer = destination;
    const auto state = f.controller.project.state().toXmlString();
    const auto revision = f.controller.revision;
    require(f.controller.prepareStructuralCommand("channel.mixer", args, result.model).wasOk() &&
                result.model && !result.model->isNoOp(),
            "Actual canonical lifetime preparation");
    const auto prepared =
        f.audio->prepareChannelRouting(*result.model->destinationBeforeCommit(), result.audio);
    require(prepared.status == AudioEngine::ChannelRoutingStatus::ready && result.audio,
            ("Actual lifetime audio preparation: " + prepared.message).toStdString());
    require(state == f.controller.project.state().toXmlString() && revision == f.controller.revision,
            "Preparing native graph must not move canonical state/history");
    return result;
}
inline void post(Fixture &f, Change &change)
{
    require(f.controller.validatePreparedChange(*change.model).wasOk() &&
                f.audio->validateChannelRouting(*change.audio).status ==
                    AudioEngine::ChannelRoutingStatus::ready,
            "Final actual lifetime model/native admission");
    require(f.audio->postChannelRouting(*change.audio) == AudioEngine::ChannelRoutingStatus::posted &&
                f.controller.commitPreparedChange(*change.model),
            "Actual post and admitted canonical commit");
    require(change.audio->wasPosted(), "Preserved external ticket publication marker");
    f.controller.deliverPendingNotifications();
}
} // namespace vibera::test::routing_lifetime

__declspec(noinline) static void routingLifetimeTests(const juce::File &folder)
{
    using namespace vibera::test::routing_lifetime;
    using Status = AudioEngine::ChannelRoutingStatus;
    require(folder.createDirectory().wasOk(), "Owned routing lifetime evidence");
    Storage storage(folder.getChildFile("storage"));
    juce::String receipt = "Actual public API/native ownership checks; serialized CONTROL/DSP.\n";
    // External plans outlive the Engine in all three ownership states. There is
    // NO test PluginPtr/Host strong pin keeping deleted Edit resources alive.
    for (int state = 0; state < 3; ++state)
    {
        auto f = std::make_unique<Fixture>(folder.getChildFile("engine-owner-" + juce::String(state)));
        for (int n = 0; n < 4; ++n)
            f->silentCallback("Baseline no-note silence/RT");
        juce::WeakReference<te::Selectable> oldEdit(&f->nativeEdit());
        juce::WeakReference<te::Selectable> original(f->sampler());
        std::weak_ptr<const Sample> sample = f->audio->channelSample(f->channel);
        auto change = prepare(*f, 2);
        if (state != 0)
            post(*f, change);
        juce::WeakReference<te::Selectable> current(f->sampler());
        if (state == 2)
        {
            f->silentCallback("Actual selected routing graph before owner destruction");
            f->audio->reclaimPluginHosts();
            require(f->audio->channelRoutingReadback().status == Status::selected, "Actual chosen ticket");
        }
        else if (state == 1)
            require(f->audio->channelRoutingReadback().status == Status::posted,
                    "Still unselected when owner dies");
        f->audio.reset(); // Production destructor must drain private/returned holders BEFORE Edit dies.
        require(oldEdit.get() == nullptr && original.get() == nullptr && current.get() == nullptr &&
                    sample.expired(),
                "Engine destruction releases native Edit/plugins/PCM while external plan remains alive");
        // A different live Engine must reject the released ticket without ever
        // dereferencing its old Edit/context; avoids calling a destroyed Engine.
        Fixture replacement(folder.getChildFile("replacement-" + juce::String(state)));
        require(replacement.audio->validateChannelRouting(*change.audio).status == Status::ownerGone &&
                    replacement.audio->postChannelRouting(*change.audio) == Status::ownerGone,
                "Released external plan retains explicit ownerGone across another Engine instance");
        change.audio.reset(); // Late caller destruction after original Engine/Edit are already gone.
        replacement.silentCallback("Unrelated fresh engine remains usable after late ticket destruction");
        require(sample.expired(), "Late caller does not resurrect native sample owners");
        receipt += "engine-owner-" + juce::String(state) + " PASS\n";
        require(folder.getChildFile("lifetime.txt").replaceWithText(receipt),
                "Retain each completed ownership case");
    }
    {
        Fixture f(folder.getChildFile("full-edit-replacement"));
        f.silentCallback("Baseline before explicit full replacement");
        juce::WeakReference<te::Selectable> oldEdit(&f.nativeEdit());
        juce::WeakReference<te::Selectable> original(f.sampler());
        auto change = prepare(f, 2);
        const auto state = f.controller.project.state().toXmlString();
        const auto revision = f.controller.revision;
        require(f.audio->rebuildFullProjection(f.controller.project, true, f.pattern).wasOk(),
                "Genuine explicit prepared-before-stop full Edit replacement");
        require(!oldEdit.get() && !original.get() &&
                    f.audio->validateChannelRouting(*change.audio).status == Status::ownerGone &&
                    f.audio->postChannelRouting(*change.audio) == Status::ownerGone &&
                    f.controller.project.state().toXmlString() == state && f.controller.revision == revision,
                "Full rebuild invalidates old native plan without committing its destination/history");
        change.audio.reset();
        f.silentCallback("Replacement remains runnable after caller releases old native plan");
        receipt += "full-edit-replacement PASS\n";
        require(folder.getChildFile("lifetime.txt").replaceWithText(receipt), "Retain full replacement case");
    }
    {
        Fixture f(folder.getChildFile("context-owner-gone"));
        f.silentCallback("Baseline before native context destruction");
        auto *context = f.nativeEdit().getTransport().getCurrentPlaybackContext();
        require(context, "Actual native context before explicit lifecycle destruction");
        juce::WeakReference<te::EditPlaybackContext> oldContext(context);
        juce::WeakReference<te::Selectable> sameEdit(&f.nativeEdit());
        auto change = prepare(f, 2);
        f.audio->stop(); // Genuine lifecycle, not a routine route workaround.
        f.nativeEdit()
            .getTransport()
            .freePlaybackContext(); // CONTROL-only, no callbacks concurrently driven.
        require(!oldContext.get() && sameEdit.get(), "Actual context died while owning Edit remains alive");
        require(f.audio->validateChannelRouting(*change.audio).status == Status::staleContext &&
                    f.audio->postChannelRouting(*change.audio) != Status::posted &&
                    !change.audio->wasPosted(),
                "Weak context identity refuses stale graph without raw-context use or false completion");
        change.audio
            .reset(); // Native prepared graph destructor must tolerate dead player/context while Edit lives.
        f.audio->setMidiTarget(f.channel);
        require(f.audio->projectionDiagnostics().playbackContext != 0 && !oldContext.get(),
                "A genuine fresh context can be allocated after private graph disposal");
        f.audio->playStop();
        dispatchMessages(20);
        f.silentCallback("Fresh context survives late old ticket destruction");
        receipt += "context-owner-gone PASS\n";
        require(folder.getChildFile("lifetime.txt").replaceWithText(receipt),
                "Retain context ownership case");
    }
}

// Intentionally strong acceptance: current source is expected to refuse until
// BOTH the sharing and retained-initialise missing-Host guards are implemented.
__declspec(noinline) static void missingInstrumentRoutingLifetimeTests(const juce::File &folder)
{
    using namespace vibera::test::routing_lifetime;
    using Status = AudioEngine::ChannelRoutingStatus;
    require(folder.createDirectory().wasOk(), "Owned genuine absent instrument evidence");
    Storage storage(folder.getChildFile("storage"));
    for (bool patternMode : {true, false})
    {
        const auto output = folder.getChildFile(patternMode ? "missing-pattern" : "missing-song");
        Fixture f(output, true, patternMode, {}, false, 256);
        const auto state = f.controller.project.plugin(f.missing)->state;
        const auto descriptor = f.controller.project.plugin(f.missing)->descriptor;
        require(!f.audio->parameterHost(f.missing) &&
                    f.audio->pluginStatus(f.missing).startsWith("Placeholder:"),
                "Actual failed native startup is explicit missing placeholder, not a mock/real worker");
        f.audio->midiInput.handleIncomingMidiMessage(nullptr,
                                                     juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
        f.audio->typingInput.handleIncomingMidiMessage(nullptr,
                                                       juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
        f.silentCallback("Missing instrument must remain EXACT silence despite score/physical/typing notes");
        require(f.audio->projectionDiagnostics().channels.front().heldInputKeys == 2,
                "Silent placeholder still maintains real physical/typing input ownership");
        auto change = prepare(f, 2); // Must really work; expected refusal is not redefined as success.
        post(f, change);
        f.silentCallback("Routed missing placeholder cannot emit builtin fallback or lose input owners");
        f.audio->reclaimPluginHosts();
        require(f.audio->channelRoutingReadback().status == Status::selected &&
                    !f.audio->parameterHost(f.missing) &&
                    f.audio->projectionDiagnostics().channels.front().heldInputKeys == 2 &&
                    f.controller.project.plugin(f.missing)->state == state &&
                    f.controller.project.plugin(f.missing)->descriptor == descriptor,
                "Missing null-Host identity/opaque recoverable state and held owners survive real adoption");
        f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        f.silentCallback("One silent owner remains while typing holds");
        require(f.audio->projectionDiagnostics().channels.front().heldInputKeys == 1,
                "Independent placeholder owner release");
        f.audio->typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        f.silentCallback("Final silent owner release without stuck held state");
        require(f.audio->projectionDiagnostics().channels.front().heldInputKeys == 0,
                "All missing instrument inputs released");
        const auto project = output.getChildFile("missing-routed.vibra");
        require(!project.exists() && saveProject(f.controller.project, project).wasOk(),
                "Owned placeholder project save");
        Project reopened;
        require(loadProject(project, reopened).wasOk() &&
                    reopened.state().isEquivalentTo(f.controller.project.state()) &&
                    reopened.plugin(f.missing)->state == state &&
                    reopened.plugin(f.missing)->descriptor == descriptor,
                "Actual routed placeholder roundtrip retains exact descriptor/state without a Host");
    }
}

// Requires the separately guarded native PreparationFence successor + app adapter.
// Genuine setAudioDeviceSetup invokes normal stopped/aboutToStart/AsyncUpdater;
// no direct test-only fence call or fake device-readback mutation is used.
__declspec(noinline) static void routingDeviceFenceTests(const juce::File &folder)
{
    using namespace vibera::test::routing_lifetime;
    using Status = AudioEngine::ChannelRoutingStatus;
    require(folder.createDirectory().wasOk(), "Owned device-preparation fence evidence");
    Storage storage(folder.getChildFile("storage"));
    for (bool committed : {false, true})
    {
        Fixture f(folder.getChildFile(committed ? "committed-overlay" : "private-unposted"));
        for (int n = 0; n < 4; ++n)
            f.silentCallback("Baseline before genuine synthetic rate change");
        auto change = prepare(f, 2);
        if (committed)
            post(f, change); // Leave exact publication intentionally awaiting first callback.
        const auto before = f.controller.project.state().toXmlString();
        const auto revision = f.controller.revision;
        const auto units = f.controller.undo.getNumberOfUnitsTakenUpByStoredCommands();
        const auto undo = f.controller.undo.getUndoDescriptions();
        const auto redo = f.controller.undo.getRedoDescriptions();
        auto &manager = f.audio->engine.getDeviceManager();
        unsigned foreignCalls = 0;
        const auto foreign = [](void *owner) noexcept
        {
            ++*static_cast<unsigned *>(owner);
            return true;
        };
        require(!manager.installPreparationFence(&foreignCalls, foreign) &&
                    !manager.removePreparationFence(&foreignCalls),
                "Unrelated owner cannot replace/remove exact engine preparation fence");
        auto setup = manager.deviceManager.getAudioDeviceSetup();
        setup.sampleRate = 44100.;
        setup.bufferSize = 256; // Genuine supported change; no buffer enlargement.
        auto trace = [&](const char *phase)
        {
            require(folder.getChildFile(committed ? "committed-device-phase.txt" : "private-device-phase.txt")
                        .replaceWithText(phase),
                    "Retain actual device lifecycle phase");
        };
        trace("before actual setAudioDeviceSetup");
        require(manager.deviceManager.setAudioDeviceSetup(setup, true).isEmpty(),
                "Actual supported CONTROL device setup request");
        require(!manager.retryPreparationFence(&foreignCalls), "Foreign owner cannot queue device recovery");
        trace("setup returned; before actual AsyncUpdater dispatch");
        dispatchMessages(40); // Actual Tracktion prepareToStart AsyncUpdater on CONTROL.
        trace("actual AsyncUpdater returned; validating device/ownership");
        f.device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(f.device && f.device->getCurrentSampleRate() == 44100. &&
                    f.device->getCurrentBufferSizeSamples() == 256 && foreignCalls == 0 &&
                    manager.getPreparationFenceStatus() ==
                        (committed ? te::DeviceManager::PreparationFenceStatus::refused
                                   : te::DeviceManager::PreparationFenceStatus::passed),
                "Actual new rate and acknowledged original owner fence, no foreign invocation");
        const auto staged = manager.getPreparationDeviceConfiguration();
        require(staged.sampleRate == 44100. && staged.blockSize == 256,
                "Actual configuration is staged before refusal for full restoration");
        if (committed)
            require(f.audio->retryDevicePreparation().failed(),
                    "Effective overlay cannot resume device before full restoration");
        require(f.audio->validateChannelRouting(*change.audio).status == Status::ownerGone &&
                    f.audio->postChannelRouting(*change.audio) == Status::ownerGone &&
                    change.audio->wasPosted() == committed &&
                    f.controller.project.state().toXmlString() == before &&
                    f.controller.revision == revision &&
                    f.controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units &&
                    f.controller.undo.getUndoDescriptions() == undo &&
                    f.controller.undo.getRedoDescriptions() == redo,
                "Pre-reprepare draining invalidates native lease before rate init, preserving exact "
                "canonical state/history");
        if (committed)
            require(
                f.audio->channelRoutingReadback().status == Status::deviceChanged &&
                    f.audio->channelRoutingReadback().overlayActive,
                "Interrupted committed overlay is explicitly unresolved, never silently baseline/completed");
        change.audio.reset(); // Caller may discard only AFTER real device changed.
        trace("old external plan released; before explicit full restoration");
        require(f.audio->rebuildFullProjection(f.controller.project, true, f.pattern).wasOk(),
                "Explicit canonical full restoration after genuine device lifecycle");
        trace("explicit restoration returned; before actual callback");
        if (committed)
        {
            require(f.audio->retryDevicePreparation().wasOk(),
                    "Exact owner queues retry only after explicit full projection");
            require(!manager.retryPreparationFence(&foreignCalls) &&
                        !manager.retryPreparationFence(f.audio.get()) &&
                        manager.getPreparationFenceStatus() ==
                            te::DeviceManager::PreparationFenceStatus::refused,
                    "Foreign and duplicate retries refuse; queueing is not completion");
            dispatchMessages(40);
            require(manager.getPreparationFenceStatus() == te::DeviceManager::PreparationFenceStatus::passed,
                    "Actual AsyncUpdater acknowledges completed normal device preparation");
        }
        f.silentCallback("Restored projection works after changed-device old plan disposal");
        require(f.controller.project.channel(f.channel)->mixer == (committed ? 2 : 1),
                "Explicit restoration preserves committed route and discards uncommitted destination");
    }
}
