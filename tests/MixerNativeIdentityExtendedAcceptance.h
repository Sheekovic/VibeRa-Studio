// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerNativeIdentityAcceptance.h"

// Additive original source only. Include at the same point as the existing
// native identity acceptance, after SyntheticDevice/awaitProjection and the
// callback C++ allocation probe. No new product seam, provider or throw hook.
namespace vibera::test::mixer_native_identity_extended
{
using mixer_native_identity::check;
using mixer_native_identity::nativeTracks;
using mixer_native_identity::sourceFrame;
using mixer_native_identity::writeSample;

struct Storage final
{
    juce::String previous = juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
    explicit Storage(const juce::File &folder)
    {
        check(_wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", folder.getFullPathName().toWideCharPointer()) ==
                  0,
              "Owned extended native identity settings");
    }
    ~Storage()
    {
        _wputenv_s(L"VIBERA_NATIVE_METER_TEST_STORAGE", previous.toWideCharPointer());
    }
};

// Heap-own Session as well as AudioEngine: never add large fixed MIDI queues or
// broad acceptance fixtures to main's default Windows stack frame.
class Session final
{
    juce::File folder;
    Storage storage;
    Controller controller;
    std::unique_ptr<AudioEngine> owner;
    SyntheticDevice *device = nullptr;
    std::map<int, te::AudioTrack *> bindings;
    AudioEngine::ProjectionDiagnostics baseline;
    juce::AudioBuffer<float> output{2, 256}, captured{2, 64 * 256};
    std::array<MixerMeterReading, 129> stripWitness{}; // CONTROL snapshot immediately after actual callback.
    juce::String trace =
        "phase,callback,error,ms,newCalls,deleteCalls,sourceDelta,held,headAdvance,edit,context\n";
    int starts = 0, stops = 0, calls = 0, noteAt = -1;
    std::uint64_t admittedSamplerCreations = 0;
    juce::String preparationTrace;
    void preparationStage(const char *stage)
    {
        preparationTrace += juce::String(juce::Time::getMillisecondCounterHiRes(), 3) + "," + stage + "\n";
        check(folder.getChildFile("preparation.csv").replaceWithText(preparationTrace),
              "Retain actual CONTROL/native preparation stage before potentially expensive work");
    }
    bool pass = true, patternMode, completeUniverse;
    double maxError = 0, maxMs = 0;

  public:
    Session(const juce::File &root, bool pattern, bool fullUniverse)
        : folder(root), storage(root.getChildFile("settings")), patternMode(pattern),
          completeUniverse(fullUniverse)
    {
        check(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned extended identity case");
        const auto sample = folder.getChildFile("original-prime251.wav");
        writeSample(sample);
        auto &p = controller.project;
        p.channels.resize(1);
        p.channelGroups.clear();
        p.placements.clear();
        p.audioPlacements.clear();
        auto &channel = p.channels.front();
        channel.source = sample.getFullPathName();
        channel.instrument.reset();
        channel.audioClip = false;
        channel.gain = 1;
        channel.pan = 0;
        channel.sampler = {};
        channel.sampler.root = 60;
        channel.sampler.envelope = true;
        channel.sampler.attack = channel.sampler.decay = channel.sampler.release = 0;
        channel.sampler.sustain = 1;
        p.patterns.resize(1);
        p.patterns.front().notes.clear();
        p.patterns.front().length = 64;
        p.patterns.front().autoLength = false; // Fixed held-input measurement horizon.
        p.tempo = 120;
        if (fullUniverse)
        {
            p.mixer.clear();
            for (int id = 0; id < 129; ++id)
            {
                p.mixer.emplace_back(
                    id, id == 0 ? juce::String("Master") : juce::String("Original ") + juce::String(id), 1.f);
                if (id > 1)
                    p.mixer.back().routes = {{id - 1, 1.f}};
            }
            std::rotate(p.mixer.begin(), p.mixer.begin() + 65, p.mixer.end());
            channel.mixer = 128;
        }
        else
        {
            p.mixer = {{std::numeric_limits<int>::max(), "Last ID", .125f},
                       {7, "Other", .25f},
                       {0, "Master", .5f},
                       {401, "Audible", .5f}};
            channel.mixer = 401;
        }
        for (auto &row : p.mixer)
            if (row.index == 0 || row.index == channel.mixer)
                row.gain = .5f;
        check(p.validate().wasOk(), "Actual valid fixed-universe musical model");
        owner = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"));
        auto &manager = audio().engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio().engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        check(device && device->getCurrentSampleRate() == 48000 &&
                  device->getCurrentBufferSizeSamples() == 256 && te::getDefaultPanLaw() == te::PanLawLinear,
              "Actual48k256 original synthetic device; no hardware claim");
        preparationStage("before_build_projection");
        check(awaitProjection(audio(), p, patternMode, p.patterns.front().id).wasOk(),
              "Prepare actual native graph for complete requested universe");
        preparationStage("after_build_projection");
        bindings = nativeTracks(audio(), p);
        check(bindings.size() == p.mixer.size() - 1 && (!fullUniverse || bindings.size() == 128),
              "Actual native track/return universe, not only table arithmetic");
        preparationStage("before_first_context_allocation");
        audio().setMidiTarget(channel.id);
        preparationStage("after_first_context_allocation");
        audio().setLooping(false);
        preparationStage("before_play");
        audio().playStop();
        preparationStage("after_play");
        dispatchMessages(20);
        juce::AudioBuffer<float> startup(2, 8 * 256);
        for (int block = 0; block < 8; ++block)
        {
            preparationStage("before_initial_empty_callback");
            device->process(output);
            preparationStage("after_initial_empty_callback");
            for (int c = 0; c < 2; ++c)
                startup.copyFrom(c, block * 256, output, c, 0, 256);
            dispatchMessages(6);
        }
        check(startup.getMagnitude(0, startup.getNumSamples()) == 0,
              "Fixed separately recorded empty native startup precedes the original NoteOn");
        std::vector<float> packed(size_t(startup.getNumSamples()) * 2);
        for (int c = 0; c < 2; ++c)
            std::copy_n(startup.getReadPointer(c), startup.getNumSamples(),
                        packed.data() + size_t(c * startup.getNumSamples()));
        check(folder.getChildFile("initial-empty-planar-f32le.raw")
                  .replaceWithData(packed.data(), packed.size() * sizeof(float)),
              "Retain all initial frames separately, no startup RT claim");
        baseline = audio().projectionDiagnostics();
        admittedSamplerCreations = baseline.samplerCreations;
        check(baseline.channels.size() == 1 && baseline.headPlaying && baseline.transportPlaying,
              "Single actual source and selected playing graph");
        starts = device->starts;
        stops = device->stops;
        captured.clear();
    }
    AudioEngine &audio() noexcept
    {
        return *owner;
    }
    Project &project() noexcept
    {
        return controller.project;
    }
    Controller &history() noexcept
    {
        return controller;
    }
    bool mode() const noexcept
    {
        return patternMode;
    }
    const auto &nativeBindings() const noexcept
    {
        return bindings;
    }
    void requireAllStripsProcessed(const juce::String &phase)
    {
        juce::String rows = "insert,blocks,peakLeft,peakRight\n";
        bool all = true;
        // The positive period251 source covers its full cycle in each256-frame
        // block. All insert nodes see .5*source; only Master adds the second .5.
        const float sourcePeak = .125f + 125.f / 8192.f;
        for (int id = 0; id <= 128; ++id)
        {
            const auto &meter = stripWitness[size_t(id)];
            const float expected = (id == 0 ? .25f : .5f) * sourcePeak;
            all &=
                meter.available && meter.blocks > 0 && meter.peak[0] == expected && meter.peak[1] == expected;
            rows += juce::String(id) + "," + juce::String(juce::int64(meter.blocks)) + "," +
                    juce::String(meter.peak[0], 12) + "," + juce::String(meter.peak[1], 12) + "\n";
        }
        check(folder.getChildFile(phase + "-strip-processing.csv").replaceWithText(rows),
              "Retain actual full native-universe DSP meter witnesses");
        check(all,
              "Every real129-strip native gain/meter node processed the independently specified held signal");
    }
    void noteOn()
    {
        check(noteAt < 0, "Exactly one original held MIDI NoteOn");
        noteAt = calls;
        audio().midiInput.handleIncomingMidiMessage(nullptr,
                                                    juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    }
    void drive(const char *phase, int count)
    {
        check(count > 0 && calls + count <= 64, "Bounded original PCM capture capacity");
        for (int n = 0; n < count; ++n)
        {
            const auto before = audio().projectionDiagnostics();
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            const auto began = juce::Time::getHighResolutionTicks();
            {
                struct Probe final
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
            if (completeUniverse)
                for (int id = 0; id <= 128; ++id)
                    stripWitness[size_t(id)] = audio().mixerMeter(id);
            const auto newCalls = callbackAllocations.load() - allocations;
            const auto deleteCalls = callbackDestructions.load() - destructions;
            const auto after = audio().projectionDiagnostics();
            const auto advance = std::llround((after.headSeconds - before.headSeconds) * 48000.);
            float error = 0;
            for (int c = 0; c < 2; ++c)
            {
                captured.copyFrom(c, calls * 256, output, c, 0, 256);
                for (int s = 0; s < 256; ++s)
                {
                    const auto frame = juce::int64(calls - noteAt) * 256 + s;
                    // Literal independent route: source unity -> assigned strip
                    // .5 -> Master .5. In the full-universe case, every intervening
                    // insert/edge is unity and was prepared during empty startup;
                    // there is no post-operation settling exclusion or phase fit.
                    const float expected = noteAt >= 0 ? .25f * sourceFrame(frame) : 0;
                    const float actual = output.getSample(c, s);
                    error = std::isfinite(actual) ? std::max(error, std::abs(actual - expected))
                                                  : std::numeric_limits<float>::infinity();
                }
            }
            const bool hasOne = before.channels.size() == 1 && after.channels.size() == 1;
            const auto sourceDelta =
                hasOne ? after.channels.front().renderCalls - before.channels.front().renderCalls : 0;
            const int held = after.channels.size() == 1 ? after.channels.front().heldInputKeys : -1;
            const bool source =
                hasOne && after.channels.front().renderCalls == before.channels.front().renderCalls + 1 &&
                after.channels.front().sampler == baseline.channels.front().sampler &&
                after.channels.front().sample == baseline.channels.front().sample &&
                (noteAt < 0 || after.channels.front().heldInputKeys == 1);
            pass &= error == 0 && source && newCalls == 0 && deleteCalls == 0 && advance == 256 &&
                    ms <= 256000. / 48000. && after.edit == baseline.edit &&
                    after.playbackContext == baseline.playbackContext && after.headPlaying &&
                    after.transportPlaying && after.replacements == baseline.replacements &&
                    after.samplerCreations == admittedSamplerCreations && device->starts == starts &&
                    device->stops == stops;
            maxError = std::max(maxError, double(error));
            maxMs = std::max(maxMs, ms);
            trace += juce::String(phase) + "," + juce::String(calls) + "," + juce::String(error, 9) + "," +
                     juce::String(ms, 9) + "," + juce::String(juce::int64(newCalls)) + "," +
                     juce::String(juce::int64(deleteCalls)) + "," + juce::String(juce::int64(sourceDelta)) +
                     "," + juce::String(held) + "," + juce::String(advance) + "," +
                     juce::String(juce::int64(after.edit)) + "," +
                     juce::String(juce::int64(after.playbackContext)) + "\n";
            ++calls;
            dispatchMessages(6);
        }
        retain(); // Actual first post-operation blocks are captured, never excluded.
    }
    void retain()
    {
        std::vector<float> packed(size_t(calls * 256) * 2);
        for (int c = 0; c < 2; ++c)
            std::copy_n(captured.getReadPointer(c), calls * 256, packed.data() + size_t(c * calls * 256));
        check(folder.getChildFile("measured-planar-f32le.raw")
                      .replaceWithData(packed.data(), packed.size() * sizeof(float)) &&
                  folder.getChildFile("callbacks.csv").replaceWithText(trace) &&
                  folder.getChildFile("live-summary.txt")
                      .replaceWithText("callbacks=" + juce::String(calls) +
                                       "\nmaxError=" + juce::String(maxError, 12) +
                                       "\nmaxCallbackMs=" + juce::String(maxMs, 9) +
                                       "\naggregate=" + (pass ? "PASS\n" : "FAIL\n")),
              "Retain full PCM/clock/allocation/deadline evidence before final assertion");
    }
    bool acknowledgeOfflineSourceCreation(const AudioEngine::ProjectionDiagnostics &before,
                                          const AudioEngine::ProjectionDiagnostics &after)
    {
        // Each independently built one-Channel render Edit creates exactly one
        // offline sampler. It does not replace the separately checked live owner.
        if (before.samplerCreations != admittedSamplerCreations ||
            after.samplerCreations != before.samplerCreations + 1)
            return false;
        admittedSamplerCreations = after.samplerCreations;
        return true;
    }
    void finish()
    {
        retain();
        check(nativeTracks(audio(), project()) == bindings, "Final actual native binding map retained");
        check(pass, "Every measured block exact original PCM, sole held source, owners, clock and unchanged "
                    "deadline");
    }
};

__declspec(noinline) inline void sparseOfflineCase(const juce::File &folder, bool patternMode)
{
    auto fixture = std::make_unique<Session>(folder, patternMode, false);
    auto &audio = fixture->audio();
    fixture->drive("initial_measured_empty", 2);
    fixture->noteOn();
    fixture->drive("held_before_offline", 4);
    auto score = fixture->project();
    const auto channel = score.channels.front().id, pattern = score.patterns.front().id;
    score.patterns.front().length = 1;
    score.patterns.front().autoLength = false; // Exact half-second sparse offline oracle.
    score.patterns.front().notes = {{score.allocate(), channel, 0, 1, 60, 1}};
    score.placements = {{score.allocate(), pattern, 0, 0, 1, 0}};
    check(score.validate().wasOk(), "Exact half-second scheduled original sparse export");
    const auto canonical = fixture->project().state().toXmlString();
    const auto historyRevision = fixture->history().revision;
    const auto hosts = audio.retainedPluginHostCount();
    juce::String results = "block,frames,maxError,firstMismatchFrame,canonicalUnchanged,bindingsUnchanged,"
                           "samplersBefore,samplersAfter,exactOneOfflineCreation\n";
    bool pass = true;
    for (int block : {64, 512})
    {
        // Second independent build reverses complete presentation rows. It must
        // still produce the literal same .25*prime251 musical signal.
        if (block == 512)
            std::reverse(score.mixer.begin(), score.mixer.end());
        const auto before = audio.projectionDiagnostics();
        const auto wav = folder.getChildFile("sparse-original-" + juce::String(block) + ".wav");
        check(!wav.exists() && audio.renderSync(score, wav, 48000, 32, block).wasOk(),
              "Actual independent offline Edit and atomic WAV commit");
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
        check(reader && reader->sampleRate == 48000 && reader->numChannels == 2 &&
                  reader->lengthInSamples == 24000,
              "Exact one beat at120BPM stereo48k WAV duration");
        juce::AudioBuffer<float> rendered(2, 24000);
        check(reader->read(&rendered, 0, 24000, 0, true, true), "Read entire actual exported musical PCM");
        double error = 0;
        int firstMismatch = -1;
        for (int c = 0; c < 2; ++c)
            for (int frame = 0; frame < 24000; ++frame)
            {
                const float actual = rendered.getSample(c, frame);
                const double e = std::isfinite(actual) ? std::abs(double(actual) - .25 * sourceFrame(frame))
                                                       : std::numeric_limits<double>::infinity();
                if (e != 0 && (firstMismatch < 0 || frame < firstMismatch))
                    firstMismatch = frame;
                error = std::max(error, e);
            }
        const auto after = audio.projectionDiagnostics();
        const bool exactOfflineCreation = fixture->acknowledgeOfflineSourceCreation(before, after);
        const bool canonicalRetained = fixture->project().state().toXmlString() == canonical &&
                                       fixture->history().revision == historyRevision;
        const bool mapRetained = nativeTracks(audio, fixture->project()) == fixture->nativeBindings();
        pass &= error == 0 && exactOfflineCreation && canonicalRetained && mapRetained &&
                after.edit == before.edit && after.playbackContext == before.playbackContext &&
                after.channels.size() == 1 && before.channels.size() == 1 &&
                after.channels.front().sampler == before.channels.front().sampler &&
                after.channels.front().sample == before.channels.front().sample &&
                after.channels.front().renderCalls == before.channels.front().renderCalls &&
                after.channels.front().heldInputKeys == 1 && after.headSeconds == before.headSeconds &&
                after.transportPlaying && after.headPlaying && audio.retainedPluginHostCount() == hosts;
        results += juce::String(block) + ",24000," + juce::String(error, 12) + "," +
                   juce::String(firstMismatch) + "," + juce::String(int(canonicalRetained)) + "," +
                   juce::String(int(mapRetained)) + "," + juce::String(juce::int64(before.samplerCreations)) +
                   "," + juce::String(juce::int64(after.samplerCreations)) + "," +
                   juce::String(int(exactOfflineCreation)) + "\n";
        check(folder.getChildFile("offline-oracle.csv").replaceWithText(results),
              "Retain independent export oracle before assertion");
        fixture->drive(block == 64 ? "immediately_after_offline64" : "immediately_after_offline512", 4);
    }
    fixture->finish();
    check(pass, "Every sparse exported sample exact, unchanged live native map/source/history before and "
                "after offline work");
}

__declspec(noinline) inline void fullUniverseCase(const juce::File &folder, bool patternMode)
{
    auto fixture = std::make_unique<Session>(folder, patternMode, true);
    auto &audio = fixture->audio();
    auto &controller = fixture->history();
    auto &p = fixture->project();
    fixture->drive("full129_initial_measured_empty", 2);
    fixture->noteOn();
    fixture->drive("full129_original_held", 4);
    fixture->requireAllStripsProcessed("before-reorder");
    auto reordered = p;
    std::reverse(reordered.mixer.begin(), reordered.mixer.end());
    std::unique_ptr<PreparedControllerChange> change;
    check(controller
                  .prepareSnapshotChange(reordered, p, "Original full129 display order", true, false, true,
                                         change)
                  .wasOk() &&
              change && controller.validatePreparedChange(*change).wasOk() &&
              controller.commitPreparedChange(*change),
          "Actual canonical129-strip row reorder/history");
    controller.deliverPendingNotifications();
    check(audio.rebuild(p, fixture->mode(), p.patterns.front().id).wasOk(),
          "Existing complete native universe reorder");
    fixture->drive("full129_reorder_first_block", 4);
    check(controller.undoEditChecked().wasOk() &&
              audio.rebuild(p, fixture->mode(), p.patterns.front().id).wasOk(),
          "Actual full native-universe reorder Undo");
    fixture->drive("full129_undo_first_block", 4);
    check(controller.redoEditChecked().wasOk() &&
              audio.rebuild(p, fixture->mode(), p.patterns.front().id).wasOk(),
          "Actual full native-universe reorder Redo");
    fixture->drive("full129_redo_first_block", 4);
    auto tooLarge = p;
    tooLarge.mixer.emplace_back(129, "Explicit over-capacity candidate", .125f);
    const auto before = audio.projectionDiagnostics();
    const auto canonical = p.state().toXmlString();
    const auto revision = controller.revision;
    const auto refused = audio.rebuild(tooLarge, fixture->mode(), p.patterns.front().id);
    std::unique_ptr<AudioEngine::PreparedChannelRouting> candidate;
    const auto route = audio.prepareChannelRouting(tooLarge, candidate);
    const auto after = audio.projectionDiagnostics();
    check(folder.getChildFile("capacity-refusal.txt")
              .replaceWithText(
                  "actualNativeInsertTracks=" + juce::String(int(fixture->nativeBindings().size())) +
                  "\nrequestedMixerRows=" + juce::String(int(tooLarge.mixer.size())) + "\nrebuild=" +
                  refused.getErrorMessage() + "\nroutingStatus=" + juce::String(int(route.status)) + "\n"),
          "Retain real native-capacity admission outcome");
    check(refused.failed() && route.status == AudioEngine::ChannelRoutingStatus::invalidProject &&
              !candidate && p.state().toXmlString() == canonical && controller.revision == revision &&
              nativeTracks(audio, p) == fixture->nativeBindings() && after.edit == before.edit &&
              after.playbackContext == before.playbackContext && after.headSeconds == before.headSeconds &&
              after.replacements == before.replacements && after.samplerCreations == before.samplerCreations,
          "Actual over-capacity native entry points refuse before replacing the held full graph or canonical "
          "model");
    fixture->drive("full129_immediately_after_capacity_refusal", 4);
    fixture->requireAllStripsProcessed("after-refusal");
    fixture->finish();
}
} // namespace vibera::test::mixer_native_identity_extended
