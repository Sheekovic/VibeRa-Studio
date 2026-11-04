// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/NoInline.h"
// Include INSIDE namespace vibera after actual FixedSampler. No production DSP
// members made public; all observations use existing lifetime/source/readback APIs.
namespace test
{
namespace dormantSamplerDetail
{
static void check(bool ok, const juce::String &message)
{
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
struct Measurement
{
    std::uint64_t blocks = 0, frames = 0;
    double maximumError = 0;
};
// Independent original source: exact binary signed prime251 PCM. At root60,
// velocity1 and explicit instant unity envelope, the oracle is the literal
// source sequence. No VoiceBank instance/cursor is copied or used as expected DSP.
static float wave(int position, int channel, int phase)
{
    const float left = float(((position + phase) % 251) - 125) / 512.f;
    return channel == 0 ? left : -.5f * left;
}
static std::shared_ptr<const Sample> makeSample(double rate, int phase)
{
    auto sample = std::make_shared<Sample>();
    sample->sampleRate = rate;
    sample->rootNote = 60;
    sample->oneShot = false;
    sample->data.setSize(2, 8193);
    for (int channel = 0; channel < 2; ++channel)
        for (int n = 0; n < sample->data.getNumSamples(); ++n)
            sample->data.setSample(channel, n, wave(n, channel, phase));
    sample->prepareNormalization();
    return sample;
}
static void publish(const std::shared_ptr<LiveTimeline> &scope, Id channel, double start = 0)
{
    auto document = std::make_unique<LiveDocument>();
    document->generation = 1;
    LiveChannel lane;
    lane.id = channel;
    SamplerSettings settings;
    settings.envelope = true;
    settings.attack = settings.decay = settings.release = 0;
    settings.sustain = 1;
    settings.start = start;
    lane.sampler = PreparedSamplerSettings(settings);
    document->channels.push_back(std::move(lane));
    check(scope->publish(document), "Actual independent immutable sample document published");
}
static void retire(te::Plugin::Ptr &plugin)
{
    if (plugin && !plugin->baseClassNeedsInitialising())
        plugin->baseClassDeinitialise();
    plugin = nullptr;
}
struct Owner
{
    te::Plugin::Ptr plugin;
    explicit Owner(te::Plugin::Ptr value) : plugin(std::move(value)) {}
    ~Owner()
    {
        retire(plugin);
    }
    FixedSampler &sampler() const
    {
        auto *value = dynamic_cast<FixedSampler *>(plugin.get());
        check(value != nullptr, "Actual FixedSampler type required");
        return *value;
    }
};
struct LaneSnapshot
{
    const MidiLane *lane;
    Id channel;
    const SamplerCutSignal *signal;
    const SamplerCutClock *clock;
    unsigned read, write, epoch, panic, overflow;
    bool enabled;
    bool operator==(const LaneSnapshot &) const = default;
};
static LaneSnapshot snapshot(const MidiLane &lane)
{
    return {&lane,
            lane.channel,
            lane.cutSignal.get(),
            lane.cutSignal->clock.get(),
            lane.read.load(),
            lane.write.load(),
            lane.epoch.load(),
            lane.panicEpoch.load(),
            lane.overflows.load(),
            lane.enabled.load()};
}
template <class Operation> static bool refused(Operation operation)
{
    try
    {
        operation();
    }
    catch (const std::exception &error)
    {
        return juce::String(error.what()).startsWith("sampler.runtime.");
    }
    return false;
}
struct Fixture
{
    AudioEngine audio{false};
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<const Sample> sample, bedSample;
    std::shared_ptr<LiveTimeline> timeline = std::make_shared<LiveTimeline>();
    std::shared_ptr<LiveTimeline> nextTimeline = std::make_shared<LiveTimeline>();
    std::shared_ptr<LiveTimeline> bedTimeline = std::make_shared<LiveTimeline>();
    std::shared_ptr<LiveTimeline> nextBedTimeline = std::make_shared<LiveTimeline>();
    std::shared_ptr<SamplerCutClock> cut = std::make_shared<SamplerCutClock>(0);
    std::shared_ptr<GuideControl> guide = std::make_shared<GuideControl>();
    std::shared_ptr<MidiLane> input = std::make_shared<MidiLane>();
    std::shared_ptr<MidiLane> typing = std::make_shared<MidiLane>();
    std::shared_ptr<MidiLane> bedInput = std::make_shared<MidiLane>();
    te::Plugin::Ptr source, candidate, bed, nextBed;
    juce::AudioBuffer<float> buffer{2, 270};
    double rate;
    Id channel = 2, bedChannel = 3;
    int bedPosition = 0;
    Measurement measurement;
    static constexpr int block = 256, prefix = 5;
    static constexpr float sentinel = .875f;

    explicit Fixture(double sampleRate = 48000) : rate(sampleRate)
    {
        auto project = Project::empty();
        juce::String error;
        edit = audio.buildEdit(project, false, project.patterns.front().id, te::Edit::forRendering, error);
        check(bool(edit), "Actual disposable native Edit: " + error);
        sample = makeSample(rate, 37);
        bedSample = makeSample(rate, 0);
        publish(timeline, channel);
        publish(nextTimeline, channel);
        publish(bedTimeline, bedChannel);
        publish(nextBedTimeline, bedChannel);
        source = edit->getPluginCache().createNewPlugin(FixedSampler::xmlTypeName, {});
        auto &sampler = dormant();
        sampler.setPreparedSource(sample);
        sampler.setTimeline(timeline, channel);
        sampler.setCutClock(cut);
        sampler.setGuide(guide);
        sampler.setInput(input);
        sampler.setTypingInput(typing);
        sampler.preparePreviewInput();
        input->enabled = typing->enabled = true;
        check(source->baseClassNeedsInitialising() && sampler.runtimeAttachmentsControl() == 0,
              "Exact genuine dormant sample starts uninitialized with zero runtime attachments");
    }
    ~Fixture()
    {
        retire(nextBed);
        retire(bed);
        retire(candidate);
        retire(source);
    }
    FixedSampler &dormant() const
    {
        return *dynamic_cast<FixedSampler *>(source.get());
    }
    te::Plugin::Ptr fresh(const std::shared_ptr<LiveTimeline> &scope = {})
    {
        auto value = FixedSampler::createDetachedGraph(*edit, source->state.createCopy());
        dynamic_cast<FixedSampler *>(value.get())->setTimeline(scope ? scope : nextTimeline, channel);
        return value;
    }
    static void noteOn(MidiLane &lane)
    {
        const unsigned char bytes[3]{0x90, 60, 127};
        lane.push(bytes, 3);
    }
    void identities(FixedSampler &value) const
    {
        check(value.preparedSource() == sample && value.inputLane() == input.get() &&
                  value.typingLane() == typing.get() && value.previewLane() == dormant().previewLane() &&
                  value.runtimeIdentity() == dormant().runtimeIdentity(),
              "Dormant lease pins exact immutable PCM, input/typing/preview lanes and nonmoving owner");
    }
    void process(FixedSampler &shell, int frames, int sourcePosition, int phase, double clock)
    {
        check(frames > 0 && frames <= block && sourcePosition + frames < 8192,
              "Independent component PCM span is within original source/capacity");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.setSample(c, n, sentinel);
        te::PluginRenderContext context(
            &buffer, prefix, frames, nullptr, 0,
            {te::TimePosition::fromSeconds(clock), te::TimePosition::fromSeconds(clock + frames / rate)},
            true, false, false, false);
        shell.applyToBuffer(context); // Actual DSP. One processing owner for this Runtime; no graph mock.
        ++measurement.blocks;
        measurement.frames += unsigned(frames);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                const bool inside = n >= prefix && n < prefix + frames;
                const float expected = inside ? wave(sourcePosition + n - prefix, c, phase) : sentinel;
                const float observed = buffer.getSample(c, n);
                const double error = std::isfinite(observed) ? std::abs(double(observed) - expected)
                                                             : std::numeric_limits<double>::infinity();
                measurement.maximumError = std::max(measurement.maximumError, error);
                check(observed == expected,
                      "Exact original PCM/sentinel;sourceFrame=" + juce::String(sourcePosition + n - prefix) +
                          ";channel=" + juce::String(c) + ";observed=" + juce::String(observed, 12) +
                          ";expected=" + juce::String(expected, 12));
            }
    }
    void startBed()
    {
        bed = edit->getPluginCache().createNewPlugin(FixedSampler::xmlTypeName, {});
        auto *sampler = dynamic_cast<FixedSampler *>(bed.get());
        sampler->setPreparedSource(bedSample);
        sampler->setTimeline(bedTimeline, bedChannel);
        sampler->setInput(bedInput);
        bedInput->enabled = true;
        bed->baseClassInitialise({{}, rate, block});
        noteOn(*bedInput); // Exactly one original NoteOn. Never replayed for candidate preparation/adoption.
    }
    void bedFrames(FixedSampler &shell, int frames)
    {
        process(shell, frames, bedPosition, 0, bedPosition / rate);
        bedPosition += frames;
        check(shell.heldInputKeys() == 1 && bedInput->read == 1 && bedInput->write == 1 &&
                  bedInput->overflows == 0,
              "Original held bed consumed exactly one NoteOn, no retrigger/overflow");
    }
};

VIBERA_NOINLINE static Measurement discardRetry()
{
    auto f = std::make_unique<Fixture>();
    Fixture::noteOn(*f->input); // Queue is already owned before either private preparation.
    const auto lane = snapshot(*f->input), typing = snapshot(*f->typing);
    const auto runtime = f->dormant().runtimeIdentity();
    {
        Owner discarded(f->fresh());
        check(discarded.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
              "Dormant lease admitted");
        f->identities(discarded.sampler());
        check(discarded.plugin->baseClassNeedsInitialising() &&
                  discarded.sampler().runtimeAttachmentsControl() == 0,
              "Lease without initialization never manufactures a DSP attachment");
    }
    check(f->dormant().runtimeIdentity() == runtime && f->dormant().runtimeAttachmentsControl() == 0 &&
              snapshot(*f->input) == lane && snapshot(*f->typing) == typing,
          "Discard preserves source owner and exact pending queue/lane metadata");
    Owner retry(f->fresh());
    check(retry.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
          "Fresh retry of discarded dormant candidate");
    retry.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block});
    f->identities(retry.sampler());
    check(retry.sampler().runtimeAttachmentsControl() == 1 && snapshot(*f->input) == lane,
          "Actual first initialization attaches exactly once without consuming pending MIDI");
    f->process(retry.sampler(), 93, 0, 37, 0);
    check(retry.sampler().heldInputKeys() == 1 && f->input->read == 1 && f->input->write == 1,
          "Actual DSP consumes pending source NoteOn exactly once after first initialization");
    f->process(retry.sampler(), 163, 93, 37, 93 / f->rate);
    return f->measurement;
}

VIBERA_NOINLINE static Measurement nativeAdmissionRetry()
{
    auto f = std::make_unique<Fixture>();
    Fixture::noteOn(*f->input);
    const auto before = snapshot(*f->input);
    Owner candidate(f->fresh());
    check(candidate.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
          "Lease before invalid native config");
    bool rejected = false;
    try
    {
        candidate.plugin->attachFixedGraphInitialisation({{}, f->rate, 0});
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    check(rejected && candidate.plugin->baseClassNeedsInitialising() && !candidate.plugin->isInitialising() &&
              candidate.sampler().runtimeAttachmentsControl() == 0 && snapshot(*f->input) == before,
          "Invalid fixed native config refuses BEFORE virtual initialise, no attachment/queue damage");
    candidate.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block});
    f->process(candidate.sampler(), 71, 0, 37, 0);
    check(candidate.sampler().heldInputKeys() == 1 && candidate.sampler().runtimeAttachmentsControl() == 1,
          "Same candidate retries genuine first initialization and renders its one pending note");
    return f->measurement;
}

VIBERA_NOINLINE static Measurement refusedSources()
{
    auto f = std::make_unique<Fixture>();
    {
        Owner ordinary(f->edit->getPluginCache().createNewPlugin(FixedSampler::xmlTypeName, {}));
        check(ordinary.sampler().shareDormantSampleRuntimeFrom(f->dormant()).failed(),
              "Ordinary/non-detached candidate refused");
        auto otherState = ordinary.plugin->state.createCopy();
        Owner wrongId(FixedSampler::createDetachedGraph(*f->edit, otherState));
        check(wrongId.sampler().shareDormantSampleRuntimeFrom(f->dormant()).failed(),
              "Different native ID refused");
    }
    {
        auto other = std::make_unique<Fixture>();
        Owner wrongEdit(FixedSampler::createDetachedGraph(*other->edit, f->source->state.createCopy()));
        check(wrongEdit.sampler().shareDormantSampleRuntimeFrom(f->dormant()).failed(),
              "Different actual Edit refused");
    }
    {
        Owner external(f->fresh());
        external.sampler().setRemote({}, false,
                                     1001); // Genuine null-worker external placeholder, not mock hosting.
        check(external.sampler().shareDormantSampleRuntimeFrom(f->dormant()).failed(),
              "External candidate refused even without a worker");
    }
    f->dormant().setRemote({}, false, 1002);
    {
        Owner candidate(f->fresh());
        check(candidate.sampler().shareDormantSampleRuntimeFrom(f->dormant()).failed(),
              "External source placeholder cannot enter sample-only dormant sharing");
    }
    auto active = std::make_unique<Fixture>();
    active->startBed();
    auto *bed = dynamic_cast<FixedSampler *>(active->bed.get());
    active->bedFrames(*bed, 93);
    Owner candidate(FixedSampler::createDetachedGraph(*active->edit, active->bed->state.createCopy()));
    check(candidate.sampler().shareDormantSampleRuntimeFrom(*bed).failed() &&
              bed->runtimeAttachmentsControl() == 1,
          "Active initialized sample must use compatible active path, never dormant path");
    active->bedFrames(*bed, 163);
    return active->measurement;
}

VIBERA_NOINLINE static Measurement immutableLease()
{
    auto f = std::make_unique<Fixture>();
    const auto before = snapshot(*f->input);
    const auto expectMismatch = [&](auto configure)
    {
        Owner candidate(f->fresh());
        configure(candidate.sampler());
        check(candidate.sampler().shareDormantSampleRuntimeFrom(f->dormant()).failed() &&
                  snapshot(*f->input) == before && f->dormant().runtimeAttachmentsControl() == 0,
              "Explicit incompatible candidate configuration refuses without overwriting source identity");
    };
    expectMismatch([&](auto &candidate) { candidate.setPreparedSource(f->bedSample); });
    expectMismatch([&](auto &candidate) { candidate.setTimeline(f->nextTimeline, f->channel + 50); });
    expectMismatch([&](auto &candidate) { candidate.setCutClock(std::make_shared<SamplerCutClock>(0)); });
    expectMismatch([&](auto &candidate) { candidate.setGuide(std::make_shared<GuideControl>()); });
    expectMismatch([&](auto &candidate) { candidate.setInput(std::make_shared<MidiLane>()); });
    Owner candidate(f->fresh());
    check(candidate.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
          "Compatible dormant lease");
    const auto replacementInput = std::make_shared<MidiLane>();
    check(refused([&] { candidate.sampler().setPreparedSource(f->sample); }) &&
              refused([&] { candidate.sampler().setTimeline(f->nextTimeline, f->channel); }) &&
              refused([&] { candidate.sampler().setInput(replacementInput); }) &&
              refused([&] { candidate.sampler().setGuide(f->guide); }) && snapshot(*f->input) == before,
          "Candidate setters seal immediately after alias, before any native initialization");
    check(refused([&] { f->dormant().setInput(f->input); }) &&
              refused([&] { f->dormant().setPreparedSource(f->sample); }) && snapshot(*f->input) == before,
          "Shared dormant lease seals source setters too; equal-value writes are not safe exceptions");
    f->identities(candidate.sampler());
    retire(candidate.plugin);
    f->dormant().setInput(f->input); // CONTROL, no DSP owner and final private lease has now retired.
    f->dormant().setPreparedSource(f->sample);
    check(snapshot(*f->input) == before && f->dormant().runtimeAttachmentsControl() == 0,
          "Final uninitialized lease discard restores genuine private source configuration");
    return f->measurement;
}

VIBERA_NOINLINE static Measurement overlappingStale()
{
    auto f = std::make_unique<Fixture>();
    Fixture::noteOn(*f->input);
    Owner first(f->fresh()), stale(f->fresh());
    check(first.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk() &&
              stale.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
          "Two private dormant epoch witnesses");
    first.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block});
    f->process(first.sampler(), 93, 0, 37, 0);
    check(
        refused([&] { stale.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block}); }) &&
            stale.plugin->baseClassNeedsInitialising() && !stale.plugin->isInitialising() &&
            first.sampler().runtimeAttachmentsControl() == 1,
        "Other candidate first-init invalidates dormant witness; failed native attach balances bookkeeping");
    f->process(first.sampler(), 163, 93, 37, 93 / f->rate);
    retire(
        first.plugin); // Genuine last initialized owner ends; runtime epoch must still reject stale witness.
    check(refused([&] { stale.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block}); }) &&
              stale.plugin->baseClassNeedsInitialising() && f->dormant().runtimeAttachmentsControl() == 0,
          "Last owner retirement cannot ABA-reactivate an old dormant epoch witness");
    retire(stale.plugin);
    Owner fresh(f->fresh());
    check(fresh.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
          "Fresh post-retirement epoch retry admitted");
    fresh.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block});
    Fixture::noteOn(
        *f->input); // New genuine lifecycle; deliberately separate original event, not held-voice replay.
    f->process(fresh.sampler(), 71, 0, 37, 256 / f->rate);
    return f->measurement;
}

VIBERA_NOINLINE static Measurement privateDocument()
{
    auto f = std::make_unique<Fixture>();
    auto scope = std::make_shared<LiveTimeline>();
    publish(scope, f->channel, .25); // 8193-frame source gives exact independent start2048.
    Owner candidate(f->fresh(scope));
    check(candidate.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk() &&
              candidate.sampler().timelineReference() == scope &&
              f->dormant().timelineReference() == f->timeline,
          "Dormant candidate retains private immutable document without rebinding incumbent scope");
    candidate.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block});
    Fixture::noteOn(*f->input);
    f->process(candidate.sampler(), 93, 2048, 37, 0);
    return f->measurement;
}

VIBERA_NOINLINE static Measurement sourceFirstWins()
{
    auto f = std::make_unique<Fixture>();
    Fixture::noteOn(*f->input);
    Owner candidate(f->fresh());
    check(candidate.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
          "Private dormant witness exists before genuine source initialization");
    // Ordinary CONTROL initialization is legal while no DSP bank is owned. It
    // wins this epoch; the pending candidate must not reset the now-active bank.
    f->source->baseClassInitialise({{}, f->rate, Fixture::block});
    f->process(f->dormant(), 93, 0, 37, 0);
    check(refused([&] { candidate.plugin->attachFixedGraphInitialisation({{}, f->rate, Fixture::block}); }) &&
              candidate.plugin->baseClassNeedsInitialising() && !candidate.plugin->isInitialising() &&
              f->dormant().runtimeAttachmentsControl() == 1 && f->dormant().heldInputKeys() == 1,
          "Source-first epoch invalidates candidate before work; source remains the only held DSP owner");
    retire(candidate.plugin);
    f->process(f->dormant(), 163, 93, 37, 93 / f->rate);
    check(f->input->read == 1 && f->input->write == 1 && f->dormant().heldInputKeys() == 1,
          "Discard of stale dormant witness never clears/replays original source MIDI or voice");
    return f->measurement;
}

VIBERA_NOINLINE static Measurement overlappingBed(double rate)
{
    auto f = std::make_unique<Fixture>(rate);
    f->startBed();
    auto *incumbent = dynamic_cast<FixedSampler *>(f->bed.get());
    const auto bedOwner = incumbent->runtimeIdentity();
    f->bedFrames(*incumbent, 93);
    {
        Owner discarded(f->fresh());
        check(discarded.sampler().shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
              "Independent dormant candidate admitted during held bed");
        discarded.plugin->attachFixedGraphInitialisation({{}, rate, Fixture::block});
        f->bedFrames(*incumbent, 163);
    }
    f->bedFrames(*incumbent, 71);
    f->candidate = f->fresh();
    auto *replacement = dynamic_cast<FixedSampler *>(f->candidate.get());
    check(replacement->shareDormantSampleRuntimeFrom(f->dormant()).wasOk(),
          "Retry dormant preparation after initialized candidate discard");
    f->candidate->attachFixedGraphInitialisation({{}, rate, Fixture::block});
    f->nextBed = FixedSampler::createDetachedGraph(*f->edit, f->bed->state.createCopy());
    auto *nextBed = dynamic_cast<FixedSampler *>(f->nextBed.get());
    nextBed->setTimeline(f->nextBedTimeline, f->bedChannel);
    check(nextBed->shareCompatibleRuntimeFrom(*incumbent).wasOk(),
          "Actual held bed uses existing compatible active sharing");
    f->nextBed->attachFixedGraphInitialisation({{}, rate, Fixture::block});
    check(nextBed->runtimeIdentity() == bedOwner && nextBed->runtimeAttachmentsControl() == 2 &&
              replacement->runtimeIdentity() != bedOwner,
          "Independent dormant owner never aliases held bed DSP");
    f->bedFrames(*incumbent, 47);
    f->bedFrames(*nextBed, 17); // Sequential component selection, not native graph publication.
    retire(f->bed);
    check(nextBed->runtimeAttachmentsControl() == 1 && nextBed->heldInputKeys() == 1,
          "Old bed shell retirement preserves exact held voice in selected component owner");
    Fixture::noteOn(*f->input);
    int dormantPosition = 0;
    const auto renderBoth = [&](int frames)
    {
        const int periodStart = f->bedPosition;
        f->bedFrames(*nextBed, frames);
        f->process(*replacement, frames, dormantPosition, 37, periodStart / rate);
        dormantPosition += frames;
    };
    renderBoth(93);
    retire(f->source); // Source never initialized; candidate owns sample/lanes/runtime independently.
    renderBoth(163);
    renderBoth(71);
    check(nextBed->runtimeIdentity() == bedOwner && nextBed->heldInputKeys() == 1 &&
              replacement->heldInputKeys() == 1 && replacement->inputLane() == f->input.get() &&
              replacement->preparedSource() == f->sample,
          "Both independent held owners/sample/lanes survive preparation and source retirement");
    return f->measurement;
}
} // namespace dormantSamplerDetail

VIBERA_NOINLINE void dormantSamplerRuntimeTests(const juce::File &folder)
{
    using namespace dormantSamplerDetail;
    check(folder.isDirectory(), "Caller supplies existing strict owned acceptance output");
    const std::array<std::pair<const char *, Measurement (*)()>, 7> cases{
        {{"discard-retry", discardRetry},
         {"native-admission-retry", nativeAdmissionRetry},
         {"strict-source-refusal", refusedSources},
         {"immutable-dormant-lease", immutableLease},
         {"overlapping-stale-epoch", overlappingStale},
         {"private-document", privateDocument},
         {"source-first-wins", sourceFirstWins}}};
    juce::Array<juce::var> rows;
    unsigned failures = 0;
    const auto run = [&](const char *name, auto operation)
    {
        auto *row = new juce::DynamicObject;
        row->setProperty("case", name);
        const auto began = juce::Time::getMillisecondCounterHiRes();
        try
        {
            const auto measured = operation();
            row->setProperty("passed", true);
            row->setProperty("blocks", juce::int64(measured.blocks));
            row->setProperty("frames", juce::int64(measured.frames));
            row->setProperty("maximumError", measured.maximumError);
        }
        catch (const std::exception &error)
        {
            ++failures;
            row->setProperty("passed", false);
            row->setProperty("error", juce::String(error.what()));
        }
        catch (...)
        {
            ++failures;
            row->setProperty("passed", false);
            row->setProperty("error", "Unknown component test exception");
        }
        row->setProperty("milliseconds", juce::Time::getMillisecondCounterHiRes() - began);
        rows.add(juce::var(row));
        check(folder.getChildFile("dormant-sampler-cases.json").replaceWithText(juce::JSON::toString(rows)),
              "Retain each real case outcome before aggregate assertion");
    };
    for (const auto &[name, operation] : cases)
        run(name, operation);
    run("held-bed-48000", [] { return overlappingBed(48000); });
    run("held-bed-44100", [] { return overlappingBed(44100); });
    check(rows.size() == 9 && failures == 0, "All9 actual dormant sampler/native lifecycle/component PCM "
                                             "cases must pass; see retained cases JSON");
    check(folder.getChildFile("dormant-sampler-result.txt")
              .replaceWithText(
                  "PASS9 actual FixedSampler component/native lifecycle cases, original exact signed "
                  "prime251 PCM, "
                  "dormant discard/retry/config admission, immutable identity/lane retention, strict "
                  "active/external refusal, "
                  "overlapping stale epochs, graph-local document and independent held bed48k/44.1k. "
                  "No sampler virtual-init fault hook, actual graph publication, callback timing/heap, real "
                  "plugin provider, "
                  "native desktop, physical MIDI, listening or hardware qualification.\n"),
          "Write bounded completion receipt");
}
} // namespace test
