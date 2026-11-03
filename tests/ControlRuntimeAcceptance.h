// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AudioEngine.h"
#include "AutomationGain.h"
#include "ChannelBalance.h"
#include "MixerRoutePlugins.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace vibera::test::controlRuntimeDetail
{
inline void check(bool ok, const juce::String &message)
{
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
struct Measurement
{
    unsigned blocks = 0;
    std::uint64_t frames = 0;
    double maximumError = 0;
};
inline void sameMeter(const MixerMeterReading &a, const MixerMeterReading &b)
{
    check(a.available && b.available && a.blocks == b.blocks &&
              a.clearedThroughBlock == b.clearedThroughBlock,
          "Candidate preparation/retirement preserves actual meter publication and clear fence");
    for (int c = 0; c < 2; ++c)
        check(a.peak[c] == b.peak[c] && a.rms[c] == b.rms[c] && a.display[c] == b.display[c] &&
                  a.lastClippedBlock[c] == b.lastClippedBlock[c] && a.overload[c] == b.overload[c],
              "Candidate lifecycle neither resets meter envelope nor invents clip publication");
}
template <typename Operation> void refused(Operation &&operation, const char *prefix)
{
    bool found = false;
    try
    {
        operation();
    }
    catch (const std::exception &error)
    {
        found = juce::String(error.what()).startsWith(prefix);
    }
    check(found, "Explicit refused preparation prefix: " + juce::String(prefix));
}
template <typename Stage> struct Fixture
{
    AudioEngine audio{false};
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<LiveTimeline> timeline = std::make_shared<LiveTimeline>();
    std::shared_ptr<LiveTimeline> candidateTimeline = std::make_shared<LiveTimeline>();
    te::Plugin::Ptr source, candidate;
    Stage *incumbent = nullptr, *replacement = nullptr;
    Id channel = 0;
    std::uint64_t generation = 0, expectedMeterBlocks = 0;
    juce::AudioBuffer<float> pcm{2, 268};
    int position = 0;
    Measurement measurement;
    static constexpr double rate = 48000;
    static constexpr int block = 256, prefix = 5;
    void publish(const std::shared_ptr<LiveTimeline> &scope, float gain, float pan = 0, bool muted = false)
    {
        auto document = std::make_unique<LiveDocument>();
        document->generation = ++generation;
        LiveChannel lane;
        lane.id = channel;
        lane.gain = gain;
        lane.pan = pan;
        document->channels.push_back(std::move(lane));
        document->mixer.push_back({1, gain, muted, pan});
        document->mixerSends.push_back({1, 0, gain});
        check(scope->publish(document), "Independent immutable control document publication");
    }
    void configure(Stage &plugin, const std::shared_ptr<LiveTimeline> &scope, float gain, float pan = 0,
                   bool muted = false, int bindingDelta = 0, te::PanLaw law = te::PanLawLinear)
    {
        if constexpr (std::is_same_v<Stage, ChannelBalance>)
        {
            Channel owner;
            owner.id = channel + bindingDelta;
            owner.gain = gain;
            owner.pan = pan;
            owner.muted = muted;
            plugin.configure(scope, owner, law);
        }
        else if constexpr (std::is_same_v<Stage, AutomationGain>)
        {
            MixerInsert owner{1 + bindingDelta, "Actual control runtime fixture", gain, pan, muted};
            plugin.configure(scope, owner, law);
        }
        else
            plugin.configure(scope, 1 + bindingDelta, gain);
    }
    explicit Fixture(float initialGain = .25f, float initialPan = 0)
    {
        auto project = Project::empty();
        channel = project.channels.front().id;
        juce::String error;
        edit = audio.buildEdit(project, true, project.patterns.front().id, te::Edit::forRendering, error);
        check(bool(edit), "Actual native Edit for control stage: " + error);
        publish(timeline, initialGain, initialPan);
        publish(candidateTimeline, initialGain, initialPan);
        source = edit->getPluginCache().createNewPlugin(Stage::xmlTypeName, {});
        incumbent = dynamic_cast<Stage *>(source.get());
        check(incumbent != nullptr, "Actual registered original control adapter type");
        configure(*incumbent, timeline, initialGain, initialPan);
        source->baseClassInitialise({{}, rate, block});
        check(incumbent->runtimeAttachmentsControl() == 1, "Only incumbent initially attached");
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
    te::Plugin::Ptr fresh(const std::shared_ptr<LiveTimeline> &scope, float gain, float pan = 0,
                          bool muted = false, int delta = 0, te::PanLaw law = te::PanLawLinear)
    {
        auto value = Stage::createDetachedGraph(*edit, source->state.createCopy());
        configure(*dynamic_cast<Stage *>(value.get()), scope, gain, pan, muted, delta, law);
        return value;
    }
    void prepare(float gain, float pan = 0, bool muted = false)
    {
        check(!candidate, "One privately owned candidate");
        candidate = fresh(candidateTimeline, gain, pan, muted);
        replacement = dynamic_cast<Stage *>(candidate.get());
        check(replacement->shareCompatibleRuntimeFrom(*incumbent).wasOk(), "Exact stage owner admitted");
        candidate->attachFixedGraphInitialisation({{}, rate, block});
        check(incumbent->runtimeIdentity() == replacement->runtimeIdentity() &&
                  incumbent->runtimeEpochControl() == replacement->runtimeEpochControl() &&
                  incumbent->runtimeAttachmentsControl() == 2,
              "Same nonmoving owner/epoch, two initialized private graph shells");
    }
    void discard()
    {
        retire(candidate);
        replacement = nullptr;
        check(incumbent->runtimeAttachmentsControl() == 1, "Discard removes only candidate attachment");
    }
    void adopt()
    {
        retire(source);
        check(replacement->runtimeAttachmentsControl() == 1,
              "Incumbent retirement leaves adopted DSP owner initialized");
    }
    template <typename Oracle>
    void process(Stage &stage, int frames, Oracle &&oracle, double tolerance = 0, float leftInput = 1,
                 float rightInput = -.5f)
    {
        check(frames > 0 && frames <= block, "Bounded component block");
        constexpr float sentinel = .125f;
        const float input[2] = {leftInput, rightInput};
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < pcm.getNumSamples(); ++n)
                pcm.setSample(c, n, n >= prefix && n < prefix + frames ? input[c] : sentinel);
        const te::PluginRenderContext context(&pcm, prefix, frames, nullptr, 0,
                                              {te::TimePosition::fromSeconds(position / rate),
                                               te::TimePosition::fromSeconds((position + frames) / rate)},
                                              true, false, false, false);
        stage.applyToBuffer(context); // Exactly one original component is processed.
        std::array<float, 2> expectedPeak{};
        std::array<double, 2> expectedSquares{};
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < pcm.getNumSamples(); ++n)
            {
                const bool inside = n >= prefix && n < prefix + frames;
                const float expected = inside ? input[c] * float(oracle(position + n - prefix, c)) : sentinel;
                const float observed = pcm.getSample(c, n);
                const double error = std::abs(double(observed) - expected);
                measurement.maximumError = !std::isfinite(observed) || !std::isfinite(expected)
                                               ? std::numeric_limits<double>::infinity()
                                               : std::max(measurement.maximumError, error);
                check(std::isfinite(observed) && error <= (inside ? tolerance : 0),
                      "Independent control PCM oracle at frame " + juce::String(position + n - prefix) +
                          ";channel=" + juce::String(c) + ";observed=" + juce::String(observed, 12) +
                          ";expected=" + juce::String(expected, 12));
                if (inside)
                {
                    expectedPeak[c] = std::max(expectedPeak[c], std::abs(expected));
                    expectedSquares[c] += double(expected) * expected;
                }
            }
        ++measurement.blocks;
        if constexpr (std::is_same_v<Stage, AutomationGain>)
        {
            const auto reading = stage.meterReading();
            check(reading.available && !reading.stale && reading.blocks == ++expectedMeterBlocks,
                  "Actual shared meter advances once per actual component block");
            for (int c = 0; c < 2; ++c)
                check(std::isfinite(reading.peak[c]) && std::isfinite(reading.rms[c]) &&
                          std::abs(reading.peak[c] - expectedPeak[c]) <= std::max(2.e-6, tolerance) &&
                          std::abs(reading.rms[c] - std::sqrt(expectedSquares[c] / frames)) <=
                              std::max(2.e-6, tolerance),
                      "Actual peak/RMS follow independent emitted PCM oracle");
        }
        measurement.frames += unsigned(frames);
        position += frames;
    }
    template <typename Oracle> void through(Stage &stage, int end, Oracle &&oracle, double tolerance = 0)
    {
        while (position < end)
            process(stage, std::min(block, end - position), oracle, tolerance);
    }
};
__declspec(noinline) inline Measurement gainRamp(bool adopt)
{
    auto f = std::make_unique<Fixture<AutomationGain>>();
    f->publish(f->timeline, .953125f);              // .25 +720/1024, exactly represented.
    f->publish(f->candidateTimeline, .6083984375f); // (.25+7/1024)+720/2048.
    const auto original = [](int frame, int) { return .25 + std::min(frame + 1, 720) / 1024.; };
    const auto changed = [](int frame, int)
    { return frame < 7 ? .25 + (frame + 1) / 1024. : .2568359375 + std::min(frame - 6, 720) / 2048.; };
    f->process(*f->incumbent, 7, original);
    const auto meter = f->incumbent->meterReading();
    const auto displayed = f->incumbent->displayGain();
    const auto epoch = f->incumbent->runtimeEpochControl();
    f->prepare(.6083984375f);
    sameMeter(meter, f->replacement->meterReading());
    check(f->replacement->displayGain() == displayed && displayed == .2568359375f,
          "Candidate config does not apply its gain target before adopted DSP");
    if (adopt)
    {
        f->adopt();
        sameMeter(meter, f->replacement->meterReading());
        f->process(*f->replacement, 13, changed);
        f->through(*f->replacement, 760, changed);
        check(f->replacement->displayGain() == .6083984375f, "Adopted exact binary gain endpoint");
    }
    else
    {
        f->process(*f->incumbent, 13, original); // Prepared candidate must not change incumbent target.
        const auto beforeDiscard = f->incumbent->meterReading();
        f->discard();
        sameMeter(beforeDiscard, f->incumbent->meterReading());
        f->through(*f->incumbent, 760, original);
        check(f->incumbent->displayGain() == .953125f && f->incumbent->runtimeEpochControl() == epoch,
              "Discarded target never reaches incumbent manual gain");
    }
    return f->measurement;
}
__declspec(noinline) inline Measurement panRamp(bool adopt)
{
    auto f = std::make_unique<Fixture<ChannelBalance>>(1);
    f->publish(f->timeline, 1, .703125f);
    f->publish(f->candidateTimeline, 1, .3583984375f);
    const auto original = [](int frame, int c)
    {
        const auto pan = std::min(frame + 1, 720) / 1024.;
        return c == 0 ? 1 - pan : 1 + pan;
    };
    const auto changed = [](int frame, int c)
    {
        const auto pan = frame < 7 ? (frame + 1) / 1024. : .0068359375 + std::min(frame - 6, 720) / 2048.;
        return c == 0 ? 1 - pan : 1 + pan;
    };
    f->process(*f->incumbent, 7, original);
    f->prepare(1, .3583984375f);
    if (adopt)
    {
        f->adopt();
        f->process(*f->replacement, 13, changed);
        f->through(*f->replacement, 760, changed);
    }
    else
    {
        f->process(*f->incumbent, 13, original);
        f->discard();
        f->through(*f->incumbent, 760, original);
    }
    return f->measurement;
}
__declspec(noinline) inline Measurement masterRamp(bool adopt)
{
    auto f = std::make_unique<Fixture<MixerMasterGate>>();
    f->publish(f->timeline, .71875f);              // .25 +240/512.
    f->publish(f->candidateTimeline, .498046875f); // (.25+7/512)+240/1024.
    const auto original = [](int frame, int) { return .25 + std::min(frame + 1, 240) / 512.; };
    const auto changed = [](int frame, int)
    { return frame < 7 ? .25 + (frame + 1) / 512. : .263671875 + std::min(frame - 6, 240) / 1024.; };
    f->process(*f->incumbent, 7, original);
    f->prepare(.498046875f);
    if (adopt)
    {
        f->adopt();
        f->process(*f->replacement, 13, changed);
        f->through(*f->replacement, 280, changed);
    }
    else
    {
        f->process(*f->incumbent, 13, original);
        f->discard();
        f->through(*f->incumbent, 280, original);
    }
    return f->measurement;
}
__declspec(noinline) inline Measurement meterContinuity()
{
    auto f = std::make_unique<Fixture<AutomationGain>>();
    const auto constant = [](int, int) { return .25; };
    f->process(*f->incumbent, 7, constant, 0, 8, -4);
    const auto clipped = f->incumbent->meterReading();
    check(clipped.blocks == 1 && clipped.overload[0] && clipped.overload[1] &&
              clipped.lastClippedBlock[0] == 1 && clipped.lastClippedBlock[1] == 1 && clipped.peak[0] == 2 &&
              clipped.peak[1] == 1,
          "Known independent loud input creates actual two-channel clipping publication");
    f->prepare(.25f);
    sameMeter(clipped, f->replacement->meterReading());
    check(f->replacement->clearOverload() == 1,
          "Candidate control readback clears exact shared completed fence");
    const auto cleared = f->incumbent->meterReading();
    check(cleared.blocks == 1 && cleared.clearedThroughBlock == 1 && !cleared.overload[0] &&
              !cleared.overload[1] && cleared.lastClippedBlock[0] == 1 && cleared.lastClippedBlock[1] == 1,
          "Shared clear does not reset actual clip payload or block count");
    f->process(*f->incumbent, 61, constant);
    const auto quiet = f->incumbent->meterReading();
    const auto release61 = float(std::exp(-61. / (.3 * Fixture<AutomationGain>::rate)));
    check(std::abs(quiet.display[0] - 2 * release61) <= 2.e-6 &&
              std::abs(quiet.display[1] - release61) <= 2.e-6,
          "Original meter envelope decays from actual earlier clip, not candidate reset");
    f->adopt();
    sameMeter(quiet, f->replacement->meterReading());
    f->process(*f->replacement, 17, constant);
    const auto adopted = f->replacement->meterReading();
    const auto release17 = float(std::exp(-17. / (.3 * Fixture<AutomationGain>::rate)));
    check(adopted.blocks == 3 && adopted.clearedThroughBlock == 1 && !adopted.overload[0] &&
              !adopted.overload[1] && std::abs(adopted.display[0] - 2 * release61 * release17) <= 2.e-6,
          "Old retirement preserves actual display envelope and overload fence");
    check(f->replacement->clearOverload() == 3, "Next explicit clear advances actual fence to third block");
    f->process(*f->replacement, 9, constant, 0, 8, -4);
    const auto relatch = f->replacement->meterReading();
    check(relatch.blocks == 4 && relatch.clearedThroughBlock == 3 && relatch.overload[0] &&
              relatch.overload[1] && relatch.lastClippedBlock[0] == 4 && relatch.lastClippedBlock[1] == 4,
          "New actual clipping after adoption re-latches with exact newer identity");
    return f->measurement;
}
__declspec(noinline) inline Measurement channelEndpoints()
{
    auto f = std::make_unique<Fixture<ChannelBalance>>(.25f, 1);
    // Independent native linear law: .25*(1-pan), .25*(1+pan).
    // Fixed2e-6 already used by PreparedBalanceAcceptance, no production helper.
    f->process(*f->incumbent, 17, [](int, int c) { return c == 0 ? 0. : .5; }, 2.e-6);
    f->source->baseClassDeinitialise();
    f->publish(f->timeline, .25f, -1);
    f->source->baseClassInitialise({{}, Fixture<ChannelBalance>::rate, Fixture<ChannelBalance>::block});
    f->process(*f->incumbent, 19, [](int, int c) { return c == 0 ? .5 : 0.; }, 2.e-6);
    return f->measurement;
}
template <typename Stage> __declspec(noinline) Measurement admissionRefusals()
{
    auto f = std::make_unique<Fixture<Stage>>(1);
    const auto constant = [](int, int) { return 1.; };
    f->process(*f->incumbent, 7, constant);
    const auto identity = f->incumbent->runtimeIdentity(), epoch = f->incumbent->runtimeEpochControl();
    auto wrongBinding = f->fresh(f->candidateTimeline, 1, 0, false, 1);
    const auto family = std::is_same_v<Stage, AutomationGain>   ? "automationGain.runtime."
                        : std::is_same_v<Stage, ChannelBalance> ? "channelBalance.runtime."
                                                                : "masterGate.runtime.";
    check(dynamic_cast<Stage *>(wrongBinding.get())
              ->shareCompatibleRuntimeFrom(*f->incumbent)
              .getErrorMessage()
              .startsWith(juce::String(family) + "incompatibleSource:"),
          "Changed channel/insert/source identity returns typed incompatibleSource");
    if constexpr (!std::is_same_v<Stage, MixerMasterGate>)
    {
        auto wrongLaw = f->fresh(f->candidateTimeline, 1, 0, false, 0, te::PanLaw3dBCenter);
        check(dynamic_cast<Stage *>(wrongLaw.get())
                  ->shareCompatibleRuntimeFrom(*f->incumbent)
                  .getErrorMessage()
                  .startsWith(juce::String(family) + "incompatibleSource:"),
              "Changed immutable pan law returns typed incompatibleSource");
    }
    for (const auto device : {std::pair{44100., 256}, std::pair{48000., 128}})
    {
        auto candidate = f->fresh(f->candidateTimeline, 1);
        auto *stage = dynamic_cast<Stage *>(candidate.get());
        check(stage->shareCompatibleRuntimeFrom(*f->incumbent).wasOk(),
              "Same owner admitted before exact device validation");
        refused([&] { candidate->attachFixedGraphInitialisation({{}, device.first, device.second}); },
                "controlRuntime.incompatibleSource:");
        check(candidate->baseClassNeedsInitialising(), "Rejected private first init is natively balanced");
    }
    if constexpr (std::is_same_v<Stage, AutomationGain>)
    {
        auto emptyScope = std::make_shared<LiveTimeline>();
        auto missing = f->fresh(emptyScope, 1);
        auto *stage = dynamic_cast<Stage *>(missing.get());
        check(stage->shareCompatibleRuntimeFrom(*f->incumbent).wasOk(),
              "Candidate document checked during actual native first init");
        refused(
            [&]
            { missing->attachFixedGraphInitialisation({{}, Fixture<Stage>::rate, Fixture<Stage>::block}); },
            "automationGain.runtime.documentUnavailable:");
        check(missing->baseClassNeedsInitialising(),
              "Missing candidate doc owns no native initialized count");
    }
    check(f->incumbent->runtimeIdentity() == identity && f->incumbent->runtimeEpochControl() == epoch &&
              f->incumbent->runtimeAttachmentsControl() == 1,
          "Refusal does not mutate live DSP owner, epoch or attachment census");
    f->through(*f->incumbent, 40, constant);
    auto delayed = f->fresh(f->candidateTimeline, 1);
    auto *delayedStage = dynamic_cast<Stage *>(delayed.get());
    check(delayedStage->shareCompatibleRuntimeFrom(*f->incumbent).wasOk(),
          "Pending epoch admitted before ordinary source reprepare");
    f->source->baseClassDeinitialise();
    f->source->baseClassInitialise({{}, Fixture<Stage>::rate, Fixture<Stage>::block});
    f->expectedMeterBlocks = 0; // Genuine first/device meter reset is intentionally retained.
    refused([&]
            { delayed->attachFixedGraphInitialisation({{}, Fixture<Stage>::rate, Fixture<Stage>::block}); },
            "controlRuntime.staleSource:");
    check(delayed->baseClassNeedsInitialising() && f->incumbent->runtimeAttachmentsControl() == 1 &&
              f->incumbent->runtimeEpochControl() != epoch,
          "Stale refused candidate never joins new source epoch");
    f->through(*f->incumbent, 71, constant);
    return f->measurement;
}
} // namespace vibera::test::controlRuntimeDetail
namespace vibera::test
{
__declspec(noinline) inline void controlRuntimeTests(const juce::File &folder)
{
    using namespace controlRuntimeDetail;
    check(juce::File::isAbsolutePath(folder.getFullPathName()) && folder.createDirectory().wasOk(),
          "Owned absolute control evidence folder");
    juce::String report;
    unsigned passed = 0;
    const auto run = [&](const char *name, auto &&operation)
    {
        try
        {
            const auto result = operation();
            report += juce::String(name) + ";PASS;blocks=" + juce::String(result.blocks) +
                      ";frames=" + juce::String(juce::int64(result.frames)) +
                      ";maximumError=" + juce::String(result.maximumError, 12) + "\n";
            ++passed;
        }
        catch (const std::exception &error)
        {
            report += juce::String(name) + ";FAIL;" + error.what() + "\n";
            folder.getChildFile("control-runtime-measurements.txt").replaceWithText(report);
            throw;
        }
        check(folder.getChildFile("control-runtime-measurements.txt").replaceWithText(report),
              "Retain exact component evidence");
    };
    for (bool adopt : {false, true})
    {
        run(adopt ? "gain_adopt" : "gain_discard", [&] { return gainRamp(adopt); });
        run(adopt ? "channel_pan_adopt" : "channel_pan_discard", [&] { return panRamp(adopt); });
        run(adopt ? "master_adopt" : "master_discard", [&] { return masterRamp(adopt); });
    }
    run("actual_meter_envelope_overload", [] { return meterContinuity(); });
    run("native_channel_gain_pan_endpoints", [] { return channelEndpoints(); });
    run("gain_admission_refusals", [] { return admissionRefusals<AutomationGain>(); });
    run("channel_admission_refusals", [] { return admissionRefusals<ChannelBalance>(); });
    run("master_admission_refusals", [] { return admissionRefusals<MixerMasterGate>(); });
    check(passed == 11, "All eleven actual component groups executed");
    check(
        folder.getChildFile("control-runtime-result.txt")
            .replaceWithText(
                "PASS11 original actual native-lifecycle component groups; exact dyadic15ms gain/pan and5ms "
                "master ramp PCM, prepare/discard/sequential switch/retire, independent actual meter "
                "peak/RMS/"
                "envelope/block/overload fence, native endpoint conversion, device/binding/stale/document "
                "refusal. "
                "No native graph publication, concurrent processing, mute/automation-curve adoption, send "
                "smoothing, callback heap/timing, desktop, installed providers, hardware or listening "
                "qualification.\n"),
        "Record honest actual component qualification limit");
}
} // namespace vibera::test
