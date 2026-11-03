// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerMonoPortamentoAcceptance.h"
#include "SampleCache.h"

// Sampler source fade in/out: PCM, reverse, persistence and command oracles.
namespace sampler_fade_acceptance
{
using namespace vibera;
using sampler_mono_portamento_acceptance::Banks;
using sampler_mono_portamento_acceptance::callback;
using sampler_mono_portamento_acceptance::settings;
using sampler_mono_portamento_acceptance::stimulus;

// Renders a constant .25 source at root pitch; returns the left channel.
inline std::vector<float> render(int rate, const SamplerSettings &value, int frames)
{
    auto sample = stimulus(rate);
    auto banks = std::make_unique<Banks>(sample, rate, value);
    juce::AudioBuffer<float> pcm(2, frames);
    callback(
        [&]() noexcept
        {
            banks->bank[0].noteOn(60, 1.f, 1);
            banks->render(pcm, frames);
        });
    return {pcm.getReadPointer(0), pcm.getReadPointer(0) + frames};
}
inline void tests(const juce::File &folder)
{
    const int rate = 48000;
    auto neutral = settings();
    const auto plain = render(rate, neutral, rate);
    require(std::abs(plain[rate / 2] - .25f) < 1e-3f, "Neutral sampler plays the constant source");

    auto faded = settings();
    faded.fadeIn = .5;
    const auto in = render(rate, faded, rate);
    require(std::abs(in[0]) < 1e-3f && std::abs(in[rate / 4] - .125f) < 2e-3f &&
                std::abs(in[rate / 2 + 100] - .25f) < 1e-3f,
            "Fade in ramps linearly over .5 s of source time");

    // stimulus is 2 s; with end at 50% the region ends at ~1 s.
    auto out = settings();
    out.end = .5;
    out.fadeOut = .25;
    const auto tail = render(rate, out, rate);
    require(std::abs(tail[rate / 2] - .25f) < 1e-3f && std::abs(tail[rate - rate / 8 - 10] - .125f) < 5e-3f,
            "Fade out reaches half level halfway through the final .25 s");

    auto reversed = settings();
    reversed.reverse = true;
    reversed.fadeIn = .5;
    const auto back = render(rate, reversed, rate);
    require(std::abs(back[0]) < 1e-3f && std::abs(back[rate / 4] - .125f) < 2e-3f,
            "Reverse playback fades in from its own playback start");

    // Loop crossfade on a rising ramp (value = frame / (8 * rate)), loop .5 s .. 1 s.
    {
        auto ramp = stimulus(rate, false);
        auto loopSettings = [&](double crossfade)
        {
            auto value = settings();
            value.loop = true;
            value.loopStart = .25;
            value.loopEnd = .5;
            value.loopCrossfade = crossfade;
            return value;
        };
        auto play = [&](double crossfade)
        {
            auto banks = std::make_unique<Banks>(ramp, rate, loopSettings(crossfade));
            juce::AudioBuffer<float> pcm(2, rate + rate / 4);
            callback(
                [&]() noexcept
                {
                    banks->bank[0].noteOn(60, 1.f, 1);
                    banks->render(pcm, pcm.getNumSamples());
                });
            return std::vector<float>(pcm.getReadPointer(0), pcm.getReadPointer(0) + pcm.getNumSamples());
        };
        const auto hard = play(0), soft = play(.1);
        double hardJump = 0, softJump = 0;
        for (int i = rate - 8; i < rate + 8; ++i)
        {
            hardJump = std::max(hardJump, double(std::abs(hard[size_t(i + 1)] - hard[size_t(i)])));
            softJump = std::max(softJump, double(std::abs(soft[size_t(i + 1)] - soft[size_t(i)])));
        }
        require(hardJump > .05 && softJump < .001,
                "Loop crossfade removes the wrap discontinuity (hard " + std::to_string(hardJump) + ", soft " +
                    std::to_string(softJump) + ")");
        require(std::abs(soft[size_t(rate / 4)] - hard[size_t(rate / 4)]) < 1e-6,
                "Crossfade leaves audio outside the crossfade zone unchanged");
    }

    // Pitch-preserving tempo fit: prepared stretched PCM, unchanged playback pitch.
    {
        const auto wav = folder.getChildFile("sine440.wav");
        {
            juce::AudioBuffer<float> tone(1, rate * 2);
            for (int i = 0; i < tone.getNumSamples(); ++i)
                tone.setSample(0, i, float(.5 * std::sin(2 * juce::MathConstants<double>::pi * 440 * i / rate)));
            wav.deleteFile();
            std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.get(), rate, 1, 24, {}, 0));
            require(writer != nullptr, "Create stretch fixture WAV");
            stream.release();
            require(writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples()), "Write stretch fixture");
        }
        auto project = Project::empty();
        project.tempo = 120;
        auto &channel = project.channels[0];
        channel.source = wav.getFullPathName();
        channel.sampler.sourceTempo = 60;
        channel.sampler.fitTempo = true;
        require(SampleCache::preparationKey(project, channel) == channel.source,
                "Resample fit keeps the plain source key");
        require(std::abs(PreparedSamplerSettings(channel.sampler, 120).tuning - 2) < 1e-9,
                "Resample fit speeds playback (pitch changes)");
        channel.sampler.stretch = true;
        const auto key = SampleCache::preparationKey(project, channel);
        require(key.contains(SampleCache::stretchMarker) &&
                    std::abs(PreparedSamplerSettings(channel.sampler, 120).tuning - 1) < 1e-9,
                "Keep pitch uses a stretched key and neutral playback rate");
        SampleCache cache;
        auto settle = [&](const juce::String &which, std::shared_ptr<const Sample> &out)
        {
            // The stretch runs on an owned worker; prepare reports pending until it lands.
            for (int attempt = 0; attempt < 2000; ++attempt)
            {
                const auto ready = cache.prepare(which, out);
                if (ready.wasOk())
                    return true;
                if (ready.getErrorMessage() != SampleCache::stretchPending)
                    return false;
                juce::Thread::sleep(5);
            }
            return false;
        };
        std::shared_ptr<const Sample> base, fitted, again;
        require(cache.prepare(channel.source, base).wasOk(), "Base source prepares synchronously");
        require(cache.prepare(key, fitted).getErrorMessage() == SampleCache::stretchPending && !fitted,
                "Stretch starts as an owned background job and reports pending");
        require(settle(key, fitted) && cache.prepare(key, again).wasOk() && fitted == again && fitted != base,
                "Stretched copy is prepared once and cached");
        require(std::abs(fitted->data.getNumSamples() - base->data.getNumSamples() / 2) <= 1,
                "Doubling tempo halves the stretched length");
        auto frequency = [](const Sample &s)
        {
            const int from = s.data.getNumSamples() / 4, to = 3 * s.data.getNumSamples() / 4;
            int crossings = 0;
            for (int i = from + 1; i < to; ++i)
                crossings += (s.data.getSample(0, i - 1) < 0) != (s.data.getSample(0, i) < 0);
            return crossings / 2. / (double(to - from) / s.sampleRate);
        };
        const double baseHz = frequency(*base), fittedHz = frequency(*fitted);
        require(std::abs(baseHz - 440) < 5 && std::abs(fittedHz - 440) < 10,
                "Stretch keeps the 440 Hz pitch (got " + std::to_string(fittedHz) + " Hz)");
        auto other = project;
        other.tempo = 90;
        std::shared_ptr<const Sample> slower;
        require(settle(SampleCache::preparationKey(other, other.channels[0]), slower) &&
                    slower != fitted &&
                    std::abs(slower->data.getNumSamples() - base->data.getNumSamples() * 60 / 90) <= 1,
                "A tempo change prepares a new stretch ratio");
        Project reopened;
        const auto file = folder.getChildFile("stretch.vibra");
        require(saveProject(project, file).wasOk() && loadProject(file, reopened).wasOk() &&
                    reopened.channels[0].sampler.stretch,
                "Keep pitch persists in project42");
    }

    // Embedded WAV smpl loop points are read and applied atomically.
    {
        const auto looped = folder.getChildFile("looped.wav");
        looped.deleteFile();
        juce::AudioBuffer<float> tone(1, 48000);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample(0, i, float(.25 * std::sin(i * .05)));
        juce::StringPairArray metadata;
        metadata.set("MidiUnityNote", "60");
        metadata.set("NumSampleLoops", "1");
        metadata.set("Loop0Identifier", "0");
        metadata.set("Loop0Type", "0");
        metadata.set("Loop0Start", "12000");
        metadata.set("Loop0End", "35999");
        metadata.set("Loop0Fraction", "0");
        metadata.set("Loop0PlayCount", "0");
        {
            std::unique_ptr<juce::FileOutputStream> stream(looped.createOutputStream());
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.get(), 48000, 1, 16, metadata, 0));
            require(writer != nullptr, "Create looped WAV fixture");
            stream.release();
            require(writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples()), "Write looped WAV");
        }
        SampleCache loopCache;
        std::shared_ptr<const Sample> withLoop;
        require(loopCache.prepare(looped.getFullPathName(), withLoop).wasOk(), "Prepare looped WAV");
        const double last = 47999.;
        require(std::abs(withLoop->fileLoopStart - 12000 / last) < 1e-9 &&
                    std::abs(withLoop->fileLoopEnd - 36000 / last) < 1e-9,
                "smpl loop (inclusive end) maps to exclusive loop fractions");
        auto loopProject = Project::empty();
        const auto loopChannel = loopProject.channels[0].id;
        Controller loops;
        loops.replace(loopProject);
        const auto loopBaseline = loops.project.state();
        CommandArgs loopArgs;
        loopArgs.channel = loopChannel;
        loopArgs.beat = withLoop->fileLoopStart;
        loopArgs.length = withLoop->fileLoopEnd;
        loopArgs.enabled = true;
        require(loops.execute("channel.sampler.loopPoints", loopArgs).wasOk(), "Apply file loop points");
        const auto &applied = loops.project.channel(loopChannel)->sampler;
        require(applied.loop && applied.loopStart == loopArgs.beat && applied.loopEnd == loopArgs.length,
                "loopPoints sets range and Use loop points together");
        require(loops.undoEditChecked().wasOk() && loops.project.state().isEquivalentTo(loopBaseline),
                "File loop application is one Undo step");
        std::shared_ptr<const Sample> plain;
        require(loopCache.prepare(folder.getChildFile("sine440.wav").getFullPathName(), plain).wasOk() && plain->fileLoopStart < 0,
                "Files without smpl loops report none");
    }

    // HQ (Hermite) interpolation tracks a band-limited sine better than linear at a fractional rate.
    {
        Sample sine;
        sine.sampleRate = rate;
        sine.rootNote = 60;
        sine.oneShot = false;
        const double cycle = 20.; // 2400 Hz at 48 kHz: interpolation error is clearly measurable.
        sine.data.setSize(2, rate * 2);
        for (int i = 0; i < sine.data.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                sine.data.setSample(c, i, float(.5 * std::sin(2 * juce::MathConstants<double>::pi * i / cycle)));
        auto error = [&](bool hqMode)
        {
            auto value = settings();
            value.hqInterpolation = hqMode;
            value.cents = 50; // Quarter-tone up: every output frame reads between source frames.
            auto banks = std::make_unique<Banks>(sine, rate, value);
            juce::AudioBuffer<float> pcm(2, rate / 4);
            callback(
                [&]() noexcept
                {
                    banks->bank[0].noteOn(60, 1.f, 1);
                    banks->render(pcm, pcm.getNumSamples());
                });
            const double ratio = std::pow(2., 50. / 1200.);
            double worst = 0;
            for (int i = 1000; i < pcm.getNumSamples(); ++i)
            {
                const double ideal = .5 * std::sin(2 * juce::MathConstants<double>::pi * i * ratio / cycle);
                worst = std::max(worst, std::abs(pcm.getSample(0, i) - ideal));
            }
            return worst;
        };
        const double linear = error(false), hermite = error(true);
        require(hermite < linear * .5,
                "HQ interpolation error " + std::to_string(hermite) + " beats linear " + std::to_string(linear));
    }

    SamplerSettings bad;
    bad.fadeIn = 11;
    require(!bad.valid(), "Fades above 10 s are invalid");

    // Command, history and project42 persistence.
    auto project = Project::empty();
    const auto channel = project.channels[0].id;
    Controller controller;
    controller.replace(project);
    const auto baseline = controller.project.state();
    CommandArgs args;
    args.channel = channel;
    args.value = .3;
    require(controller.execute("channel.sampler.fadeIn", args).wasOk() &&
                controller.project.channel(channel)->sampler.fadeIn == .3,
            "channel.sampler.fadeIn command");
    args.value = 12;
    const auto revision = controller.revision;
    require(controller.execute("channel.sampler.fadeOut", args).failed() && controller.revision == revision,
            "Out-of-range fade refuses without mutation");
    args.value = .2;
    require(controller.execute("channel.sampler.fadeOut", args).wasOk(), "channel.sampler.fadeOut command");
    const auto file = folder.getChildFile("fades.vibra");
    Project reopened;
    require(saveProject(controller.project, file).wasOk() && loadProject(file, reopened).wasOk() &&
                reopened.channel(channel)->sampler.fadeIn == .3 &&
                reopened.channel(channel)->sampler.fadeOut == .2 && reopened.version == Project::currentVersion,
            "Fades persist in project42");
    auto legacy = controller.project.state();
    legacy.setProperty("version", 41, nullptr);
    Project refused;
    require(Project::parse(legacy, refused).failed(), "Fade fields refuse in an older-version document");
    require(!baseline.getChildWithName("CHANNELS").getChild(0).getChildWithName("SAMPLER").hasProperty("fadeIn"),
            "Neutral fades are not written (sparse)");
    require(controller.undoEditChecked().wasOk() && controller.undoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(baseline),
            "Fade edits undo exactly");
    folder.getChildFile("sampler-fade-verification.txt")
        .replaceWithText("Fade in/out PCM, reverse, validation, command/history and project42 passed.\n");
}
} // namespace sampler_fade_acceptance
