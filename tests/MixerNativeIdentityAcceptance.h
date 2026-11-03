// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerNativeIdentity.h"
#include "MixerRouting.h"
#include "MixerRoutePlugins.h"
#include <limits>

// Include after awaitProjection/dispatchMessages, SyntheticDevice and the
// existing callback new/delete probe. No new runner or provider mechanism.
namespace vibera::test::mixer_native_identity
{
inline void check(bool ok, const juce::String &message)
{
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
inline void tableTests()
{
    const auto ordinary = Project::empty();
    MixerNativeIdentity original;
    using E = MixerNativeIdentity::Error;
    check(MixerNativeIdentity::prepare(ordinary, original) == E::none, "Default fixed Mixer table");
    for (int id = 0; id < 18; ++id)
        check(original.bus(id) == 10000 + id && original.slot(id) == id,
              "Existing default18 native buses must remain byte-compatible");
    auto sparse = ordinary;
    sparse.mixer = {
        {std::numeric_limits<int>::max(), "Last", 1}, {0, "Master", 1}, {401, "Bus", 1}, {7, "Source", 1}};
    for (auto &channel : sparse.channels)
        channel.mixer = 7;
    check(sparse.validate().wasOk(), "Actual Project permits sparse stable IDs and nonleading Master");
    MixerNativeIdentity table;
    check(MixerNativeIdentity::prepare(sparse, table) == E::none && table.bus(0) == 10000 &&
              table.bus(7) == 10001 && table.bus(401) == 10002 &&
              table.bus(std::numeric_limits<int>::max()) == 10003 && !table.bus(1) && !table.idAtSlot(-1) &&
              !table.idAtSlot(4),
          "Sparse bounded injective native table");
    auto reorder = sparse;
    std::reverse(reorder.mixer.begin(), reorder.mixer.end());
    MixerNativeIdentity reordered;
    check(MixerNativeIdentity::prepare(reorder, reordered) == E::none && table.sameUniverse(reordered),
          "Display order cannot change native slot/bus identities");
    Project restored;
    MixerNativeIdentity restoredTable;
    check(Project::parse(reorder.state(), restored).wasOk() &&
              restored.state().isEquivalentTo(reorder.state()) &&
              MixerNativeIdentity::prepare(restored, restoredTable) == E::none &&
              table.sameUniverse(restoredTable),
          "Existing schema20 round trip preserves sparse identities and display order");
    check(MixerNativeIdentity::alignDisplayOrder(reorder, sparse) &&
              reorder.state().isEquivalentTo(sparse.state()),
          "Comparison normalizes ONLY row order");
    reorder = sparse;
    std::reverse(reorder.mixer.begin(), reorder.mixer.end());
    reorder.mixer.front().gain = .25f;
    check(MixerNativeIdentity::alignDisplayOrder(reorder, sparse) &&
              !reorder.state().isEquivalentTo(sparse.state()),
          "Gain differences must never normalize away");
    auto changed = sparse;
    changed.mixer.front().index = 999;
    const auto changedBefore = changed.state().toXmlString();
    check(!MixerNativeIdentity::alignDisplayOrder(changed, sparse) &&
              changed.state().toXmlString() == changedBefore,
          "Different universe refuses without mutation");
    auto bad = sparse;
    bad.mixer.front().index = 7;
    const auto before = table;
    check(MixerNativeIdentity::prepare(bad, table) == E::duplicateId && table.sameUniverse(before),
          "Duplicate ID refusal preserves previous table");
    bad = sparse;
    bad.mixer[1].index = 5;
    check(MixerNativeIdentity::prepare(bad, table) == E::missingMaster && table.sameUniverse(before),
          "Missing Master refusal preserves previous table");
    bad = sparse;
    bad.mixer.front().index = -1;
    check(MixerNativeIdentity::prepare(bad, table) == E::negativeId && table.sameUniverse(before),
          "Negative stable ID refusal preserves previous table");
    auto full = ordinary;
    full.mixer.clear();
    for (int id = 0; id < 129; ++id)
        full.mixer.emplace_back(id, juce::String(id), 1);
    check(MixerNativeIdentity::prepare(full, table) == E::none && table.bus(128) == 10128,
          "Bounded full129 table does not enter another app bus range");
    full.mixer.emplace_back(129, "Overflow", 1);
    check(MixerNativeIdentity::prepare(full, table) == E::count && table.bus(128) == 10128,
          "Capacity refusal preserves exact prior maximum table");
}
inline float sourceFrame(juce::int64 frame) noexcept
{
    return .125f + float(frame % 251 - 125) / 8192.f;
}
inline void writeSample(const juce::File &file)
{
    check(!file.exists(), "Original same-ID PCM must not overwrite an existing file");
    juce::AudioBuffer<float> pcm(2, 48000 * 4);
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
            pcm.setSample(channel, frame, sourceFrame(frame));
    auto stream = file.createOutputStream();
    check(bool(stream), "Owned original sample output");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), 48000, 2, 32, {}, 0));
    check(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
          "Write independent prime251 binary-fraction stereo source");
}
inline std::map<int, te::AudioTrack *> nativeTracks(AudioEngine &audio, const Project &p)
{
    const auto identity = audio.projectionDiagnostics().edit;
    check(identity != 0, "CONTROL-owned actual Edit diagnostic required");
    auto &edit = *reinterpret_cast<te::Edit *>(identity); // Existing bounded fixture convention.
    const auto all = te::getAudioTracks(edit);
    MixerNativeIdentity layout;
    check(MixerNativeIdentity::prepare(p, layout) == MixerNativeIdentity::Error::none &&
              size_t(all.size()) == p.channels.size() + p.mixer.size() - 1,
          "Actual native fixed universe");
    std::map<int, te::AudioTrack *> result;
    for (int slot = 1; slot < layout.size(); ++slot)
    {
        const int id = *layout.idAtSlot(slot), bus = *layout.bus(id);
        auto *track = all[int(p.channels.size()) + slot - 1];
        check(track->state.hasProperty("viberaMixerIndex") && int(track->state["viberaMixerIndex"]) == id &&
                  int(track->state["viberaMixerBus"]) == bus,
              "Actual track stamp follows stable ID");
        int returns = 0;
        for (auto *plugin : track->pluginList)
        {
            if (auto *receive = dynamic_cast<te::AuxReturnPlugin *>(plugin))
            {
                ++returns;
                check(int(receive->busNumber) == bus, "Actual AuxReturn stable bus");
            }
            if (auto *send = dynamic_cast<MixerSendPlugin *>(plugin))
                check(layout.bus(send->destination()) == int(send->busNumber) &&
                          int(send->state["viberaSource"]) == id,
                      "Actual send stable source/destination bus");
        }
        check(returns == 1, "Exactly one original native return per stable insert");
        result.emplace(id, track);
    }
    return result;
}

// Actual current native graph; no extra graph is processed and no adoption is
// inferred. Pure presentation reorder should retain the existing graph/context.
// originalProvider is optional ONLY for the genuine original SDK delay fixture,
// not a compatibility claim about installed third-party instruments/effects.
__declspec(noinline) inline void liveCase(const juce::File &folder, bool patternMode,
                                          const PluginSpec *originalProvider = nullptr)
{
    check(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned same-ID scenario");
    struct Storage
    {
        juce::String old = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
        explicit Storage(const juce::File &root)
        {
            check(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE",
                             root.getFullPathName().toWideCharPointer()) == 0,
                  "Disposable native settings");
        }
        ~Storage()
        {
            _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", old.toWideCharPointer());
        }
    } storage(folder.getChildFile("settings"));
    const auto sample = folder.getChildFile("original-prime251.wav");
    writeSample(sample);
    Controller controller;
    auto &p = controller.project;
    p.channels.resize(1);
    p.channelGroups.clear();
    p.placements.clear();
    auto &channel = p.channels.front();
    const auto channelId = channel.id, pattern = p.patterns.front().id;
    channel.source = sample.getFullPathName();
    channel.mixer = 7;
    channel.gain = 1;
    channel.pan = 0;
    channel.sampler.loop = false;
    channel.sampler.envelope = true;
    channel.sampler.attack = channel.sampler.decay = channel.sampler.release = 0;
    channel.sampler.sustain = 1;
    p.patterns.front().length = 64;
    p.patterns.front().notes.clear();
    p.mixer = {{std::numeric_limits<int>::max(), "Unused original provider", 1},
               {0, "Master", 1},
               {401, "Bus", 1},
               {7, "Source", 1}};
    for (auto &insert : p.mixer)
        if (insert.index == 7)
            insert.routes = {{401, .5f}};
    Id effect = 0;
    if (originalProvider)
    {
        check(originalProvider->name == "VibeRa Mixer Latency Fixture" && !originalProvider->instrument &&
                  originalProvider->state.isEmpty() && !originalProvider->bypass &&
                  originalProvider->wet == 1,
              "Actual original SDK delay provider, no mock or installed-provider claim");
        auto spec = *originalProvider;
        effect = spec.id = p.allocate();
        p.mixer.front().effects = {spec};
    }
    check(p.validate().wasOk(), "Valid sparse reordered actual Project before first projection");
    auto audioOwner = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
    auto &audio = *audioOwner; // Keep fixed MIDI queues off the main thread's stack.
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    check(device && device->getCurrentSampleRate() == 48000 && device->getCurrentBufferSizeSamples() == 256 &&
              te::getDefaultPanLaw() == te::PanLawLinear,
          "Unchanged48k256 linear pan prerequisites");
    check(awaitProjection(audio, p, patternMode, pattern).wasOk(), "Actual fixed-universe native projection");
    const auto bindings = nativeTracks(audio, p);
    auto host = effect ? audio.preparedPluginHost(p, effect) : std::shared_ptr<PluginHost>{};
    check(!effect || (host && host->latencySamples() == 287),
          "Original default31 plus256 worker delay exact");
    const auto worker = host ? host->runtimeIdentity() : juce::String{};
    const auto pid = host ? host->processId() : 0;
    audio.setMidiTarget(channelId);
    audio.setLooping(false);
    audio.playStop();
    dispatchMessages(20);
    // Fixed empty-source initialization before the ONE original NoteOn. This
    // does not hide any edit/adoption block: no warmup is allowed after an
    // operation below. Initial PCM is retained separately, without claiming its
    // native first-initialization allocations are callback-qualified here.
    {
        juce::AudioBuffer<float> empty(2, 256), startup(2, 8 * 256);
        for (int block = 0; block < 8; ++block)
        {
            device->process(empty);
            for (int c = 0; c < 2; ++c)
                startup.copyFrom(c, block * 256, empty, c, 0, 256);
            dispatchMessages(6);
        }
        const auto raw = folder.getChildFile("initial-empty-planar-f32le.raw");
        check(!raw.exists(), "Fresh owned native-initialization evidence");
        auto stream = raw.createOutputStream();
        check(bool(stream), "Create initial native PCM receipt");
        for (int c = 0; c < 2; ++c)
            check(stream->write(startup.getReadPointer(c), size_t(8 * 256) * sizeof(float)),
                  "Retain every initial empty-source PCM frame");
        check(startup.getMagnitude(0, startup.getNumSamples()) == 0,
              "Actual fixed native initialization has no source signal to conceal");
    }
    const auto owner = audio.projectionDiagnostics();
    check(owner.channels.size() == 1 && owner.headPlaying && owner.transportPlaying,
          "Actual single source and playing native context before measured callbacks");
    const auto starts = device->starts, stops = device->stops;
    juce::AudioBuffer<float> output(2, 256), captured(2, 256 * 40);
    captured.clear();
    int calls = 0, noteAt = -1;
    bool pass = true;
    double maximumError = 0, maximumMs = 0;
    juce::String trace = "phase,callback,sourceDelta,held,headAdvance,error,ms,newCalls,deleteCalls,"
                         "submitted,processed,missed,dropped\n";
    const auto drive = [&](const char *phase, int count)
    {
        for (int n = 0; n < count; ++n)
        {
            const auto before = audio.projectionDiagnostics();
            const auto oldPackets = host ? host->statistics() : PluginHost::Statistics{};
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            const auto began = juce::Time::getHighResolutionTicks();
            {
                struct Probe
                {
                    bool old = callbackAllocationProbe;
                    Probe()
                    {
                        callbackAllocationProbe = true;
                    }
                    ~Probe()
                    {
                        callbackAllocationProbe = old;
                    }
                } probe;
                device->process(output);
            }
            const auto ms =
                SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
            const auto newCalls = callbackAllocations.load() - allocations,
                       deleteCalls = callbackDestructions.load() - destructions;
            const auto after = audio.projectionDiagnostics();
            const auto packets = host ? host->statistics() : PluginHost::Statistics{};
            const auto advance =
                std::llround(after.headSeconds * 48000) - std::llround(before.headSeconds * 48000);
            float error = 0;
            for (int c = 0; c < 2; ++c)
            {
                captured.copyFrom(c, calls * 256, output, c, 0, 256);
                for (int s = 0; s < 256; ++s)
                {
                    const auto frame = juce::int64(calls - noteAt) * 256 + s - (host ? 287 : 0);
                    const float expected = noteAt >= 0 && frame >= 0 ? .5f * sourceFrame(frame) : 0;
                    const float value = output.getSample(c, s);
                    error = std::isfinite(value) ? std::max(error, std::abs(value - expected))
                                                 : std::numeric_limits<float>::infinity();
                }
            }
            const bool channelOwner =
                after.channels.size() == 1 && before.channels.size() == 1 &&
                after.channels.front().renderCalls == before.channels.front().renderCalls + 1 &&
                after.channels.front().sampler == owner.channels.front().sampler &&
                after.channels.front().sample == owner.channels.front().sample &&
                (noteAt < 0 || after.channels.front().heldInputKeys == 1);
            pass &=
                error == 0 && newCalls == 0 && deleteCalls == 0 && ms <= 256000. / 48000. && advance == 256 &&
                channelOwner && after.edit == owner.edit && after.playbackContext == owner.playbackContext &&
                after.headPlaying && after.transportPlaying && after.replacements == owner.replacements &&
                after.samplerCreations == owner.samplerCreations && device->starts == starts &&
                device->stops == stops &&
                (!host || (host->runtimeIdentity() == worker && host->processId() == pid &&
                           host->latencySamples() == 287 && packets.submitted == oldPackets.submitted + 1 &&
                           packets.missed == oldPackets.missed && packets.dropped == oldPackets.dropped));
            maximumError = std::max(maximumError, double(error));
            maximumMs = std::max(maximumMs, ms);
            trace += juce::String(phase) + "," + juce::String(calls) + "," +
                     juce::String(juce::int64(after.channels.front().renderCalls -
                                              before.channels.front().renderCalls)) +
                     "," + juce::String(after.channels.front().heldInputKeys) + "," + juce::String(advance) +
                     "," + juce::String(error, 9) + "," + juce::String(ms, 9) + "," +
                     juce::String(juce::int64(newCalls)) + "," + juce::String(juce::int64(deleteCalls)) +
                     "," + juce::String(juce::int64(packets.submitted)) + "," +
                     juce::String(juce::int64(packets.processed)) + "," +
                     juce::String(juce::int64(packets.missed)) + "," +
                     juce::String(juce::int64(packets.dropped)) + "\n";
            ++calls;
            dispatchMessages(6); // Outside DSP; never wait for worker in callback.
        }
        check(folder.getChildFile("callbacks.csv").replaceWithText(trace),
              "Retain all original callback evidence");
    };
    drive("empty_initial", 4);
    noteAt = calls;
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    drive("one_original_held_note", 4);
    auto destination = p;
    std::reverse(destination.mixer.begin(), destination.mixer.end());
    const auto apply = [&](const Project &target)
    {
        std::unique_ptr<PreparedControllerChange> change;
        check(controller
                      .prepareSnapshotChange(target, p, "Original same-ID display order", true, false, true,
                                             change)
                      .wasOk() &&
                  change && controller.validatePreparedChange(*change).wasOk() &&
                  controller.commitPreparedChange(*change),
              "Actual canonical snapshot/history order change");
        controller.deliverPendingNotifications();
        check(audio.rebuild(p, patternMode, pattern).wasOk() && nativeTracks(audio, p) == bindings,
              "Same stable IDs retain actual native tracks/returns/sends through reorder");
    };
    apply(destination);
    drive("reorder_immediate", 4);
    check(controller.undoEditChecked().wasOk() && audio.rebuild(p, patternMode, pattern).wasOk() &&
              nativeTracks(audio, p) == bindings,
          "Actual Undo retains stable native mapping");
    drive("undo_immediate", 4);
    check(controller.redoEditChecked().wasOk() && audio.rebuild(p, patternMode, pattern).wasOk() &&
              nativeTracks(audio, p) == bindings,
          "Actual Redo retains stable native mapping");
    drive("redo_immediate", 4);
    {
        auto routedOrder = p;
        routedOrder.channels.front().mixer = 401;
        std::reverse(routedOrder.mixer.begin(), routedOrder.mixer.end());
        const auto canonical = p.state().toXmlString();
        const auto revision = controller.revision;
        std::unique_ptr<AudioEngine::PreparedChannelRouting> candidate;
        const auto prepared = audio.prepareChannelRouting(routedOrder, candidate);
        check(prepared.status == AudioEngine::ChannelRoutingStatus::ready && candidate &&
                  !candidate->wasPosted() && p.state().toXmlString() == canonical &&
                  controller.revision == revision,
              "Actual private joint route/order preparation retains canonical model; not adoption");
        candidate.reset(); // CONTROL-only discard, while original held voice continues.
    }
    drive("joint_route_order_discard", 4);
    // Existing route preparation MUST refuse changed universe, without any
    // model publication, graph selection, workers or held-voice mutation.
    auto incompatible = p;
    incompatible.mixer.emplace_back(19, "Unadmitted universe change", 1);
    std::unique_ptr<AudioEngine::PreparedChannelRouting> refused;
    const auto refusal = audio.prepareChannelRouting(incompatible, refused);
    check(refusal.status == AudioEngine::ChannelRoutingStatus::unsupportedDelta && !refused,
          "Variable Mixer universe cannot enter fixed-universe prepared routing");
    drive("universe_refusal_continues", 4);
    auto *stamped = bindings.at(7);
    const int originalStamp = int(stamped->state["viberaMixerBus"]);
    stamped->state.setProperty("viberaMixerBus", 9999, nullptr);
    auto route = p;
    route.channels.front().mixer = 401;
    const auto invalid = audio.prepareChannelRouting(route, refused);
    stamped->state.setProperty("viberaMixerBus", originalStamp, nullptr);
    check(invalid.status == AudioEngine::ChannelRoutingStatus::staleProjection && !refused,
          "Foreign native binding refuses before graph preparation");
    drive("binding_refusal_continues", 4);
    {
        const auto raw = folder.getChildFile("callbacks-planar-f32le.raw");
        check(!raw.exists(), "Fresh owned raw callback evidence output");
        auto stream = raw.createOutputStream();
        check(bool(stream), "Create actual callback PCM evidence");
        for (int c = 0; c < 2; ++c)
            check(stream->write(captured.getReadPointer(c), size_t(calls * 256) * sizeof(float)),
                  "Retain every actual callback frame before aggregate assertion");
    }
    check(folder.getChildFile("summary.txt")
              .replaceWithText(
                  "Actual same-ID presentation reorder only; no physical "
                  "device/listening/installed-provider claim.\ninitialEmptyCallbacks=8\ncallbacks=" +
                  juce::String(calls) + "\nmaxError=" + juce::String(maximumError, 12) +
                  "\nmaxCallbackMs=" + juce::String(maximumMs, 9) + "\n"),
          "Retain evidence before unchanged exact musical aggregate");
    if (host)
    {
        const auto submitted = host->statistics().submitted;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
        while (host->statistics().processed < submitted && host->alive() &&
               juce::Time::getMillisecondCounterHiRes() < deadline)
            dispatchMessages(2);
        const auto packets = host->statistics();
        pass &= packets.processed == submitted && packets.missed == 0 && packets.dropped == 0;
    }
    check(pass,
          "All actual callbacks preserve exact PCM, held voice, owners, packet/clock/deadline invariants");
}
} // namespace vibera::test::mixer_native_identity
