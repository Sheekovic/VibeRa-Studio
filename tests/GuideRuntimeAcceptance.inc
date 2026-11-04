// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/NoInline.h"
// Include INSIDE namespace vibera immediately after the actual GuidePlugin.
// Original component tests, not native graph publication/timing qualification.
namespace test
{
namespace guideRuntimeDetail
{
static void check(bool ok, const juce::String &why)
{
    if (!ok)
        throw std::runtime_error(why.toStdString());
}
struct Tick
{
    int frame;
    bool accent;
};
// Independent analytic sound2 oracle. No GuideDsp instance/table/cursor is
// copied or called to produce expected samples. Float table rounding matches
// the specified original table format, followed by independent interpolation.
static double clickAt(int frame, double rate, const std::vector<Tick> &ticks)
{
    const Tick *last = nullptr;
    for (const auto &tick : ticks)
        if (tick.frame <= frame)
            last = &tick;
        else
            break;
    if (!last)
        return 0;
    const double position = (frame - last->frame) * (48000. / rate);
    if (position >= 959)
        return 0;
    const int index = int(std::floor(position));
    const auto tableSample = [accent = last->accent](int k)
    {
        const double seconds = k / 48000.;
        const double frequency = accent ? 1700. : 1150.;
        constexpr double pi = 3.141592653589793238462643383279502884;
        return float((accent ? .3 : .2) * std::sin(2 * pi * frequency * seconds) * std::min(1., k / 12.) *
                     std::exp(-170 * seconds));
    };
    const float fraction = float(position - index);
    const float left = tableSample(index), right = tableSample(index + 1);
    return float((left + fraction * (right - left)) * .5f);
}
struct Measurement
{
    unsigned blocks = 0;
    std::uint64_t samples = 0;
    double maximumError = 0;
};
struct Fixture
{
    AudioEngine audio{false};
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<GuideControl> control = std::make_shared<GuideControl>();
    std::shared_ptr<LiveTimeline> timeline = std::make_shared<LiveTimeline>();
    std::shared_ptr<LiveTimeline> candidateTimeline = std::make_shared<LiveTimeline>();
    te::Plugin::Ptr source, candidate;
    GuidePlugin *incumbent = nullptr, *replacement = nullptr;
    juce::AudioBuffer<float> buffer{2, 268};
    Measurement measurement;
    double rate = 48000;
    int position = 0;
    static constexpr int block = 256, prefix = 5;
    static constexpr float sentinel = .125f;
    static void publish(const std::shared_ptr<LiveTimeline> &scope, double tempo = 120, int numerator = 4)
    {
        auto document = std::make_unique<LiveDocument>();
        document->generation = 1;
        document->tempo = tempo;
        document->numerator = numerator;
        document->denominator = 4;
        check(scope->publish(document), "Independent immutable Guide document publication");
    }
    Fixture()
    {
        auto project = Project::empty();
        juce::String why;
        edit = audio.buildEdit(project, false, project.patterns.front().id, te::Edit::forRendering, why);
        check(bool(edit), "Actual native Edit for Guide lifecycle: " + why);
        publish(timeline);
        publish(candidateTimeline);
        control->enabled = true;
        control->sound = 2;
        control->gain = .5f;
        source = edit->getPluginCache().createNewPlugin(GuidePlugin::xmlTypeName, {});
        incumbent = dynamic_cast<GuidePlugin *>(source.get());
        check(incumbent != nullptr, "Actual registered original GuidePlugin");
        incumbent->configure(control, timeline);
        source->baseClassInitialise({{}, rate, block});
        check(incumbent->runtimeAttachmentsControl() == 1, "One initialized Guide owner");
    }
    static void retire(te::Plugin::Ptr &plugin)
    {
        if (plugin && !plugin->baseClassNeedsInitialising())
            plugin->baseClassDeinitialise();
        plugin = nullptr;
    }
    ~Fixture()
    {
        retire(candidate);
        retire(source);
    }
    te::Plugin::Ptr fresh(const std::shared_ptr<LiveTimeline> &scope,
                          const std::shared_ptr<GuideControl> &controls)
    {
        auto result = GuidePlugin::createDetachedGraph(*edit, source->state.createCopy());
        dynamic_cast<GuidePlugin *>(result.get())->configure(controls, scope);
        return result;
    }
    void prepare()
    {
        check(!candidate, "One privately owned candidate per component scenario");
        candidate = fresh(candidateTimeline, control);
        replacement = dynamic_cast<GuidePlugin *>(candidate.get());
        check(replacement->shareCompatibleRuntimeFrom(*incumbent).wasOk(),
              "Compatible Guide runtime admitted");
        candidate->attachFixedGraphInitialisation({{}, rate, block});
        check(incumbent->runtimeIdentity() == replacement->runtimeIdentity() &&
                  incumbent->runtimeEpochControl() == replacement->runtimeEpochControl() &&
                  incumbent->runtimeAttachmentsControl() == 2,
              "Private initialized shell shares exact owner, epoch and two attachments");
    }
    void discard()
    {
        retire(candidate);
        replacement = nullptr;
        check(incumbent->runtimeAttachmentsControl() == 1, "Discard retires only candidate attachment");
    }
    void adopt()
    {
        check(bool(candidate), "Prepared candidate exists before sequential component switch");
        retire(source);
        check(replacement->runtimeAttachmentsControl() == 1,
              "Old shell retirement preserves replacement owner");
    }
    void process(GuidePlugin &shell, int frames, const std::vector<Tick> &ticks, bool rendering = false)
    {
        check(frames > 0 && frames <= block, "Bounded component block size");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.setSample(c, n, sentinel);
        const float input[2] = {.03125f, -.0625f};
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < frames; ++n)
                buffer.setSample(c, prefix + n, input[c]);
        te::PluginRenderContext context(&buffer, prefix, frames, nullptr, 0,
                                        {te::TimePosition::fromSeconds(position / rate),
                                         te::TimePosition::fromSeconds((position + frames) / rate)},
                                        true, false, rendering, false);
        shell.applyToBuffer(context); // Sole DSP owner; no alternate GuideDsp oracle.
        ++measurement.blocks;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                const bool inside = n >= prefix && n < prefix + frames;
                const double click = inside && !rendering ? clickAt(position + n - prefix, rate, ticks) : 0;
                const float expected = inside ? input[c] + float(click) : sentinel;
                const float observed = buffer.getSample(c, n);
                const double error = std::abs(double(observed) - expected);
                measurement.maximumError = !std::isfinite(observed) || !std::isfinite(expected)
                                               ? std::numeric_limits<double>::infinity()
                                               : std::max(measurement.maximumError, error);
                check(std::isfinite(observed) && error <= 2.e-7,
                      "Independent Guide PCM/offset oracle at frame " + juce::String(position + n - prefix) +
                          ";channel=" + juce::String(c) + ";observed=" + juce::String(observed, 12) +
                          ";expected=" + juce::String(expected, 12));
            }
        measurement.samples += unsigned(frames);
        position += frames;
    }
    void through(GuidePlugin &shell, int end, const std::vector<Tick> &ticks)
    {
        while (position < end)
            process(shell, std::min(block, end - position), ticks);
    }
};
template <typename Operation> static void refused(Operation &&operation, const char *prefix)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::exception &failure)
    {
        rejected = juce::String(failure.what()).startsWith(prefix);
    }
    check(rejected, "Explicit preparation failure prefix: " + juce::String(prefix));
}
VIBERA_NOINLINE static Measurement discardPartial()
{
    auto f = std::make_unique<Fixture>();
    const std::vector<Tick> ticks{{0, true}};
    f->process(*f->incumbent, 93, ticks);
    const auto epoch = f->incumbent->runtimeEpochControl(), hits = f->control->hits.load();
    f->prepare();
    check(f->control->hits == hits, "Candidate preparation emits no click/control hit");
    f->process(*f->incumbent, 163, ticks);
    f->discard();
    check(f->incumbent->runtimeEpochControl() == epoch, "Discard leaves DSP preparation epoch unchanged");
    f->through(*f->incumbent, 1200, ticks);
    check(f->control->hits == 1, "Discard never retriggers the partial click");
    return f->measurement;
}
VIBERA_NOINLINE static Measurement adoptPartial()
{
    auto f = std::make_unique<Fixture>();
    const std::vector<Tick> ticks{{0, true}};
    f->process(*f->incumbent, 93, ticks);
    f->prepare();
    // Reinitialize the same discarded shell: own candidate fallback is repinned.
    f->candidate->baseClassDeinitialise();
    f->candidate->attachFixedGraphInitialisation({{}, f->rate, Fixture::block});
    check(f->replacement->runtimeAttachmentsControl() == 2,
          "Retained shell reinitialization balances ownership");
    f->adopt();
    f->process(*f->replacement, 163, ticks);
    f->through(*f->replacement, 1200, ticks);
    check(f->control->hits == 1, "Adopted click ends without reset or duplicate hit");
    return f->measurement;
}
VIBERA_NOINLINE static Measurement differentDocument()
{
    auto f = std::make_unique<Fixture>();
    const std::vector<Tick> ticks{{0, true}, {24000, false}, {48000, false}, {96000, false}, {144000, true}};
    Fixture::publish(f->candidateTimeline, 60, 3);
    f->process(*f->incumbent, 93, ticks);
    f->prepare();
    f->through(*f->incumbent, 24256, ticks); // Old120BPM tick is real, after candidate preparation.
    check(f->control->hits == 2, "Incumbent still reads its own120BPM scope during private prepare");
    f->adopt();
    f->through(*f->replacement, 145200, ticks);
    check(f->control->hits == 5, "Adopted60BPM3/4 scope supplies exactly next three ticks/accents");
    return f->measurement;
}
VIBERA_NOINLINE static Measurement leadIn()
{
    auto f = std::make_unique<Fixture>();
    f->control->enabled = false;
    f->control->begin(0, 1);
    f->control->ready = true;
    const auto leadEpoch = f->control->epoch.load();
    const std::vector<Tick> ticks{{0, true}, {24000, false}};
    f->process(*f->incumbent, 93, ticks);
    const auto remaining = f->control->remaining.load();
    f->prepare();
    check(f->control->epoch == leadEpoch && f->control->leadStart == 0 && f->control->leadEnd == 1 &&
              f->control->remaining == remaining && f->control->hits == 1,
          "Private preparation does not alter original count-in control state");
    f->adopt();
    f->through(*f->replacement, 48256, ticks);
    check(f->control->hits == 2 && f->control->remaining == 0 && f->control->epoch == leadEpoch,
          "Count-in has exactly two ticks and ends at original absolute boundary");
    auto third = GuidePlugin::createDetachedGraph(*f->edit, f->candidate->state.createCopy());
    struct ThirdOwner
    {
        te::Plugin::Ptr plugin;
        ~ThirdOwner()
        {
            Fixture::retire(plugin);
        }
    } thirdOwner{third};
    auto *thirdGuide = dynamic_cast<GuidePlugin *>(third.get());
    thirdGuide->configure(f->control, f->candidateTimeline);
    check(thirdGuide->shareCompatibleRuntimeFrom(*f->replacement).wasOk(),
          "Post-count-in private shell admitted");
    third->attachFixedGraphInitialisation({{}, f->rate, Fixture::block});
    Fixture::retire(thirdOwner.plugin);
    third = nullptr;
    f->through(*f->replacement, 72000, ticks);
    check(f->control->hits == 2 && f->control->remaining == 0, "Discard cannot restart completed lead gate");
    return f->measurement;
}
VIBERA_NOINLINE static Measurement lateFailure()
{
    auto f = std::make_unique<Fixture>();
    const std::vector<Tick> ticks{{0, true}};
    f->process(*f->incumbent, 93, ticks);
    const auto identity = f->incumbent->runtimeIdentity();
    const auto epoch = f->incumbent->runtimeEpochControl();
    bool failed = false;
    try
    {
        f->prepare();
        throw std::runtime_error("guide.fixture.lateFactoryFailure");
    }
    catch (const std::runtime_error &why)
    {
        failed = juce::String(why.what()) == "guide.fixture.lateFactoryFailure";
        f->discard(); // Mirrors private node preparation unwind, after no graph processing.
    }
    check(failed && f->incumbent->runtimeIdentity() == identity &&
              f->incumbent->runtimeEpochControl() == epoch && f->control->hits == 1,
          "Late candidate failure preserves incumbent owner/epoch/hit");
    f->through(*f->incumbent, 1200, ticks);
    return f->measurement;
}
VIBERA_NOINLINE static Measurement refusalPaths()
{
    auto f = std::make_unique<Fixture>();
    const std::vector<Tick> ticks{{0, true}};
    f->process(*f->incumbent, 93, ticks);
    const auto identity = f->incumbent->runtimeIdentity();
    const auto epoch = f->incumbent->runtimeEpochControl();
    auto changedControl = f->fresh(f->candidateTimeline, std::make_shared<GuideControl>());
    check(dynamic_cast<GuidePlugin *>(changedControl.get())
              ->shareCompatibleRuntimeFrom(*f->incumbent)
              .getErrorMessage()
              .startsWith("guide.runtime.incompatibleSource:"),
          "Different GuideControl identity returns typed incompatibleSource");
    auto other = f->edit->getPluginCache().createNewPlugin(GuidePlugin::xmlTypeName, {});
    auto changedId = GuidePlugin::createDetachedGraph(*f->edit, other->state.createCopy());
    auto *changedIdGuide = dynamic_cast<GuidePlugin *>(changedId.get());
    changedIdGuide->configure(f->control, f->candidateTimeline);
    check(changedIdGuide->shareCompatibleRuntimeFrom(*f->incumbent)
              .getErrorMessage()
              .startsWith("guide.runtime.invalidCandidate:"),
          "Different native itemID returns typed invalidCandidate");
    auto project = Project::empty();
    juce::String why;
    auto otherEdit =
        f->audio.buildEdit(project, false, project.patterns.front().id, te::Edit::forRendering, why);
    check(bool(otherEdit), "Second actual Edit for cross-Edit refusal");
    auto crossEdit = GuidePlugin::createDetachedGraph(*otherEdit, f->source->state.createCopy());
    auto *crossGuide = dynamic_cast<GuidePlugin *>(crossEdit.get());
    crossGuide->configure(f->control, f->candidateTimeline);
    check(crossGuide->shareCompatibleRuntimeFrom(*f->incumbent)
              .getErrorMessage()
              .startsWith("guide.runtime.invalidCandidate:"),
          "Different Edit returns typed invalidCandidate");
    crossEdit = nullptr; // Private shell dies before its Edit.
    auto inactive = f->edit->getPluginCache().createNewPlugin(GuidePlugin::xmlTypeName, {});
    auto *inactiveGuide = dynamic_cast<GuidePlugin *>(inactive.get());
    inactiveGuide->configure(f->control, f->timeline);
    auto inactiveCandidate = GuidePlugin::createDetachedGraph(*f->edit, inactive->state.createCopy());
    auto *inactiveShell = dynamic_cast<GuidePlugin *>(inactiveCandidate.get());
    inactiveShell->configure(f->control, f->candidateTimeline);
    check(inactiveShell->shareCompatibleRuntimeFrom(*inactiveGuide)
              .getErrorMessage()
              .startsWith("guide.runtime.inactiveSource:"),
          "Uninitialized source returns typed inactiveSource");
    for (const auto device : {std::pair{44100., 256}, std::pair{48000., 128}})
    {
        auto candidate = f->fresh(f->candidateTimeline, f->control);
        auto *shell = dynamic_cast<GuidePlugin *>(candidate.get());
        check(shell->shareCompatibleRuntimeFrom(*f->incumbent).wasOk(),
              "Same source admitted before device check");
        refused([&] { candidate->attachFixedGraphInitialisation({{}, device.first, device.second}); },
                "controlRuntime.incompatibleSource:");
        check(candidate->baseClassNeedsInitialising(), "Rejected private native first-init count unwound");
    }
    auto empty = std::make_shared<LiveTimeline>();
    auto missingDocument = f->fresh(empty, f->control);
    auto *missingGuide = dynamic_cast<GuidePlugin *>(missingDocument.get());
    check(missingGuide->shareCompatibleRuntimeFrom(*f->incumbent).wasOk(),
          "Document validation occurs during actual preparation");
    refused([&] { missingDocument->attachFixedGraphInitialisation({{}, f->rate, Fixture::block}); },
            "guide.runtime.documentUnavailable:");
    check(missingDocument->baseClassNeedsInitialising() && f->incumbent->runtimeIdentity() == identity &&
              f->incumbent->runtimeEpochControl() == epoch && f->incumbent->runtimeAttachmentsControl() == 1,
          "Every refusal leaves source unchanged and count balanced");
    f->through(*f->incumbent, 1200, ticks); // Actual uninterrupted incumbent PCM after all refusals.
    auto delayed = f->fresh(f->candidateTimeline, f->control);
    auto *delayedGuide = dynamic_cast<GuidePlugin *>(delayed.get());
    check(delayedGuide->shareCompatibleRuntimeFrom(*f->incumbent).wasOk(),
          "Private pending source epoch admitted");
    f->source->baseClassDeinitialise();
    f->source->baseClassInitialise({{}, f->rate, Fixture::block});
    refused([&] { delayed->attachFixedGraphInitialisation({{}, f->rate, Fixture::block}); },
            "controlRuntime.staleSource:");
    check(delayed->baseClassNeedsInitialising() && f->incumbent->runtimeAttachmentsControl() == 1 &&
              f->incumbent->runtimeEpochControl() != epoch,
          "Source reprepare invalidates pending admission without attaching it");
    f->through(*f->incumbent, 1500, {}); // Genuine reset means old tail cannot resume.
    return f->measurement;
}
VIBERA_NOINLINE static Measurement genuineDeviceLifecycle()
{
    auto f = std::make_unique<Fixture>();
    f->process(*f->incumbent, 93, {{0, true}});
    const auto epoch = f->incumbent->runtimeEpochControl();
    f->source->baseClassDeinitialise();
    check(f->incumbent->runtimeAttachmentsControl() == 0, "Final genuine lifecycle retires last owner");
    f->source->baseClassInitialise({{}, f->rate, Fixture::block});
    check(f->incumbent->runtimeEpochControl() != epoch, "Ordinary reprepare advances source epoch");
    f->through(*f->incumbent, 1200, {}); // At t93, a fresh metronome does not replay already missed tick0.
    f->source->baseClassDeinitialise();
    f->rate = 44100;
    f->position = 0;
    f->control->hits = 0;
    f->source->baseClassInitialise({{}, f->rate, Fixture::block});
    const std::vector<Tick> ticks{{0, true}, {22050, false}};
    f->process(*f->incumbent, 93, ticks);
    f->prepare();
    f->adopt();
    f->through(*f->replacement, 23250, ticks);
    check(f->control->hits == 2, "44.1k partial click interpolation and next exact half-second tick");
    return f->measurement;
}
VIBERA_NOINLINE static Measurement offlineOmission()
{
    auto f = std::make_unique<Fixture>();
    f->prepare();
    const auto identity = f->incumbent->runtimeIdentity();
    f->process(*f->incumbent, 256, {}, true);
    f->process(*f->replacement, 256, {}, true);
    check(f->control->hits == 0 && f->incumbent->runtimeIdentity() == identity,
          "Neither source nor private candidate renders guide offline or mutates hit count");
    f->discard();
    f->position = 0; // Test time origin only; DSP was not entered by offline calls.
    f->through(*f->incumbent, 1200, {{0, true}});
    check(f->control->hits == 1, "Offline omission leaves normal subsequent click behavior intact");
    return f->measurement;
}
} // namespace guideRuntimeDetail
VIBERA_NOINLINE void guideRuntimeTests(const juce::File &folder)
{
    using namespace guideRuntimeDetail;
    check(juce::File::isAbsolutePath(folder.getFullPathName()) && folder.createDirectory().wasOk(),
          "Owned absolute Guide acceptance folder");
    struct Scenario
    {
        const char *name;
        Measurement (*run)();
    };
    const Scenario cases[] = {{"discard_partial_48k", discardPartial},
                              {"adopt_partial_48k", adoptPartial},
                              {"different_document_tempo_signature", differentDocument},
                              {"lead_in", leadIn},
                              {"late_candidate_failure", lateFailure},
                              {"admission_refusals", refusalPaths},
                              {"genuine_device_44k1", genuineDeviceLifecycle},
                              {"offline_omission", offlineOmission}};
    juce::String report;
    for (const auto &scenario : cases)
    {
        try
        {
            const auto measured = scenario.run();
            report += juce::String(scenario.name) + ";PASS;blocks=" + juce::String(measured.blocks) +
                      ";samples=" + juce::String(juce::int64(measured.samples)) +
                      ";maximumError=" + juce::String(measured.maximumError, 12) + "\n";
        }
        catch (const std::exception &error)
        {
            report += juce::String(scenario.name) + ";FAIL;" + error.what() + "\n";
            folder.getChildFile("guide-runtime-measurements.txt").replaceWithText(report);
            throw;
        }
        check(folder.getChildFile("guide-runtime-measurements.txt").replaceWithText(report),
              "Record exact Guide component measurements");
    }
    check(
        folder.getChildFile("guide-runtime-result.txt")
            .replaceWithText(
                "PASS8 actual original GuidePlugin component/native-lifecycle groups. Independent analytic "
                "click/table-interpolation PCM and additive offset guards; private prepare/discard, "
                "sequential "
                "switch/retire, different document, partial count-in, actual admission/late failure, genuine "
                "44.1k reprepare and offline omission. No actual graph publication, concurrent callback, "
                "callback heap instrumentation, native desktop, hardware or listening qualification.\n"),
        "Retain actual Guide component result with explicit qualification limits");
}
} // namespace test
