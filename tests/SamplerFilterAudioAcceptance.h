// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/LiveTimeline.h"
#include <array>

// Original multitone/stereo/voice-state oracles. No FL transfer-function claim.
// Include after require and existing callback allocation/destruction probes.
namespace sampler_filter_audio_acceptance
{
using namespace vibera;
constexpr std::array<double, 5> tones{128, 500, 2000, 4000, 8000};
inline Sample stimulus(int rate, bool silentRight = false)
{
    Sample sample;
    sample.sampleRate = rate;
    sample.oneShot = false;
    sample.data.setSize(2, rate + 1);
    for (int i = 0; i < sample.data.getNumSamples(); ++i)
    {
        double value = 0;
        for (double hz : tones)
            value += .03 * std::sin(2 * juce::MathConstants<double>::pi * hz * i / rate);
        sample.data.setSample(0, i, float(value));
        sample.data.setSample(1, i, silentRight ? 0.f : float(-value * .375));
    }
    sample.prepareNormalization();
    return sample;
}
inline SamplerSettings settings(int type, double cutoff, double resonance)
{
    SamplerSettings s;
    s.envelope = true;
    s.attack = s.decay = 0;
    s.sustain = 1;
    s.filterType = type;
    s.filterCutoff = cutoff;
    s.filterResonance = resonance;
    return s;
}
inline void render(VoiceBank &bank, juce::AudioBuffer<float> &audio, int frames)
{
    audio.clear();
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    const bool previous = callbackAllocationProbe;
    callbackAllocationProbe = true;
    bank.render(audio, 0, frames);
    callbackAllocationProbe = previous;
    require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
            "Sampler filter VoiceBank rendering allocates/destroys no measured C++ objects");
}
inline double projection(const juce::AudioBuffer<float> &audio, int side, double hz, int rate)
{
    // Last quarter-second contains integral cycles of every original tone.
    // This measures actual signal amplitude, without inspecting coefficients.
    const int count = rate / 4, start = audio.getNumSamples() - count;
    double real = 0, imaginary = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto angle = 2 * juce::MathConstants<double>::pi * hz * i / rate;
        const auto value = audio.getSample(side, start + i);
        real += value * std::cos(angle);
        imaginary += value * std::sin(angle);
    }
    return 2 * std::hypot(real, imaginary) / count;
}
struct VoiceState
{
    bool found = false;
    double position = 0;
    float envelope = 0;
    std::uint64_t serial = 0, live = 0;
    Id note = 0, placement = 0;
};
inline VoiceState state(VoiceBank &bank)
{
    VoiceState s;
    bank.visitActive(
        [&](const auto &v) noexcept
        { s = {true, v.position, v.envelope, v.triggerSerial, v.liveSequence, v.note, v.placement}; });
    return s;
}
inline void tests()
{
    // Input position only: the public logarithmic20..20000Hz knob places2000Hz
    // here. Expected pass/stop bands below do not use the filter implementation.
    const double cutoff2000 = std::log(100.) / std::log(1000.);
    for (int rate : {44100, 48000, 96000})
    {
        auto sample = stimulus(rate);
        const auto *source = sample.data.getReadPointer(0);
        const juce::AudioBuffer<float> original(sample.data);
        juce::AudioBuffer<float> audio(2, rate / 2);
        auto bank = std::make_unique<VoiceBank>();
        bank->prepare(sample, rate);
        for (int type : {0, 1})
        {
            bank->reset();
            const auto s = settings(type, type == 0 ? .25 : 1., type == 0 ? .9 : 0.);
            bank->configure(PreparedSamplerSettings(s));
            bank->noteOn(60, .75f);
            render(*bank, audio, audio.getNumSamples());
            bool exact = true;
            for (int side = 0; side < 2; ++side)
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    exact = exact &&
                            audio.getSample(side, i) == float(double(sample.data.getSample(side, i)) * .75);
            require(exact, "Default and explicit Off preserve literal legacy signed stereo PCM bit exactly");
        }
        std::array<std::array<double, tones.size()>, 6> gains{};
        for (int type = 1; type <= 5; ++type)
        {
            bank->reset();
            bank->configure(PreparedSamplerSettings(settings(type, cutoff2000, 0)));
            bank->noteOn(60, 1);
            render(*bank, audio, audio.getNumSamples());
            for (size_t tone = 0; tone < tones.size(); ++tone)
                gains[size_t(type)][tone] = projection(audio, 0, tones[tone], rate) / .03;
            bool finite = true;
            for (int side = 0; side < 2; ++side)
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    finite = finite && std::isfinite(audio.getSample(side, i));
            require(finite, "Every original filter mode renders finite real stereo multitone PCM");
            require(std::abs(projection(audio, 1, 500, rate) / projection(audio, 0, 500, rate) - .375) < .002,
                    "Filtering preserves independent stereo-source amplitude ratio");
        }
        require(gains[1][0] > .95 && gains[1][1] > .90 && gains[1][4] < .12,
                "LP12 passes low tones and rejects8000Hz above the2000Hz cutoff");
        require(gains[3][0] < .02 && gains[3][4] > .85,
                "HP12 rejects128Hz and passes8000Hz with the same cutoff");
        require(gains[4][2] > .90 && gains[4][2] < 1.10 && gains[4][0] < .15 && gains[4][4] < .50,
                "BP12 isolates the cutoff band and rejects both distant sides");
        require(gains[5][2] < .03 && gains[5][0] > .90 && gains[5][4] > .85,
                "BS12 rejects the cutoff tone while retaining both distant bands");
        const auto slope12 = 20 * std::log10(gains[1][3] / gains[1][4]);
        const auto slope24 = 20 * std::log10(gains[2][3] / gains[2][4]);
        require(slope12 > 9 && slope12 < 18 && slope24 > 18 && slope24 < 36 && slope24 > slope12 + 8 &&
                    gains[2][4] < gains[1][4] * .2,
                "LP24 has materially steeper octave rejection than LP12 on actual PCM");
        bank->reset();
        bank->configure(PreparedSamplerSettings(settings(1, cutoff2000, 1)));
        bank->noteOn(60, 1);
        render(*bank, audio, audio.getNumSamples());
        require(projection(audio, 0, 2000, rate) / .03 > 5 * gains[1][2],
                "Resonance produces an measurable cutoff peak rather than stored metadata only");

        auto isolated = stimulus(rate, true);
        bank->prepare(isolated, rate);
        for (int type = 1; type <= 5; ++type)
        {
            bank->reset();
            bank->configure(PreparedSamplerSettings(settings(type, cutoff2000, .8)));
            bank->noteOn(60, 1);
            render(*bank, audio, audio.getNumSamples());
            require(audio.getMagnitude(0, audio.getNumSamples()) > .001f &&
                        audio.getMagnitude(1, 0, audio.getNumSamples()) == 0,
                    "A driven left filter cannot write into a silent right filter history");
        }
        // Original alternating near-Nyquist content, DC and an impulse probe
        // finite/stable endpoints without copying any transfer equation.
        Sample edge;
        edge.sampleRate = rate;
        edge.oneShot = false;
        edge.data.setSize(2, 4096);
        for (int i = 0; i < 4096; ++i)
        {
            edge.data.setSample(0, i, (i == 0 ? .3f : .05f) + (i % 2 ? -.1f : .1f));
            edge.data.setSample(1, i, i % 3 ? -.07f : .11f);
        }
        bank->prepare(edge, rate);
        for (int type = 0; type <= 5; ++type)
            for (double cutoff : {0., .5, 1.})
                for (double resonance : {0., 1.})
                {
                    bank->reset();
                    bank->configure(PreparedSamplerSettings(settings(type, cutoff, resonance)));
                    bank->noteOn(60, 1);
                    render(*bank, audio, 2048);
                    bool stable = true;
                    for (int side = 0; side < 2; ++side)
                        for (int i = 0; i < 2048; ++i)
                            stable = stable && std::isfinite(audio.getSample(side, i)) &&
                                     std::abs(audio.getSample(side, i)) < 128;
                    require(stable,
                            "Filter endpoint/resonance/near-Nyquist fixture remains finite and bounded");
                }
        bool unchanged = sample.data.getReadPointer(0) == source;
        for (int side = 0; side < 2; ++side)
            unchanged =
                unchanged && std::memcmp(sample.data.getReadPointer(side), original.getReadPointer(side),
                                         size_t(sample.data.getNumSamples()) * sizeof(float)) == 0;
        require(unchanged,
                "Filter preparation and all voice processing leave immutable original PCM ownership intact");
    }
    {
        auto sample = stimulus(48000);
        auto both = std::make_unique<VoiceBank>(), first = std::make_unique<VoiceBank>(),
             second = std::make_unique<VoiceBank>();
        const auto prepared = PreparedSamplerSettings(settings(2, cutoff2000, .7));
        for (auto *bank : {both.get(), first.get(), second.get()})
        {
            bank->prepare(sample, 48000);
            bank->configure(prepared);
        }
        both->noteOn(60, .5f, 10);
        first->noteOn(60, .5f, 10);
        juce::AudioBuffer<float> sum(2, 2048), a(2, 2048), b(2, 2048);
        render(*both, sum, 256);
        render(*first, a, 256);
        both->noteOn(67, .25f, 11);
        second->noteOn(67, .25f, 11);
        render(*both, sum, 2048);
        render(*first, a, 2048);
        render(*second, b, 2048);
        double error = 0;
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < 2048; ++i)
                error = std::max(error, std::abs(double(sum.getSample(side, i)) -
                                                 float(a.getSample(side, i) + b.getSample(side, i))));
        require(error < 2.e-6,
                "A new voice neither resets nor shares another voice's stereo filter memories");
    }
    for (bool patternMode : {true, false})
    {
        auto sample = stimulus(48000);
        auto project = Project::empty();
        const auto channel = project.channels.front().id, pattern = project.patterns.front().id;
        const auto source = project.allocate();
        project.patterns.front().autoLength = false;
        project.patterns.front().length = 4;
        project.patterns.front().notes = {{source, channel, 0, 3, 60, .25f, true}};
        project.placements = {{project.allocate(), pattern, 0, 0, 4, 0}};
        const auto baseline = settings(1, 1, 0);
        project.channels.front().sampler = baseline;
        LiveTimeline timeline;
        std::uint64_t generation = 0;
        auto score = std::make_unique<VoiceBank>(), input = std::make_unique<VoiceBank>();
        score->prepare(sample, 48000);
        input->prepare(sample, 48000);
        score->configure(PreparedSamplerSettings(baseline));
        input->configure(PreparedSamplerSettings(baseline));
        input->noteOn(60, .25f, 0, 0, 0, 93);
        std::array<VoiceBank *, 1> extra{input.get()};
        LiveSequencer sequence;
        juce::AudioBuffer<float> pcm(2, 4096);
        const auto publish = [&]()
        {
            auto doc = compileLiveDocument(project, patternMode, pattern, ++generation);
            require(timeline.publish(doc) && !doc, "Real filter generation publishes outside callback");
        };
        const auto drive = [&](int begin, int count)
        {
            pcm.clear();
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            callbackAllocationProbe = true;
            {
                auto lease = timeline.acquire();
                if (lease.document)
                {
                    const auto *lane = lease.document->find(channel);
                    // Same prepared configure seam used by AudioEngine for
                    // retained score and input banks; no table preparation here.
                    score->configure(lane->sampler);
                    input->configure(lane->sampler);
                    sequence.process(*lease.document, channel, begin / 48000., count, 48000, true, *score,
                                     pcm, 0, extra);
                }
            }
            callbackAllocationProbe = false;
            require(
                callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Filter publication/configure/reconciliation/held render allocate/destroy no C++ objects");
        };
        publish();
        drive(0, 1024);
        const auto oldScore = state(*score), oldInput = state(*input);
        project.channels.front().sampler = settings(3, cutoff2000, .3);
        publish();
        drive(1024, 256);
        const auto editedScore = state(*score), editedInput = state(*input);
        require(editedScore.found && editedInput.found && editedScore.serial == oldScore.serial &&
                    editedInput.serial == oldInput.serial && editedScore.note == source &&
                    editedInput.live == 93 && editedScore.position == oldScore.position + 256 &&
                    editedInput.position == oldInput.position + 256 && editedScore.envelope == 1 &&
                    editedInput.envelope == 1,
                "Ordinary held filter edit retains source phase, envelope, trigger and input ownership");
        // Prepared baseline restoration represents the audio side of canonical
        // cancel/Undo. Model/application history is qualified by its own tests.
        project.channels.front().sampler = baseline;
        publish();
        drive(1280, 4096);
        bool exactDry = true;
        for (int side = 0; side < 2; ++side)
            for (int i = 3840; i < 4096; ++i)
            {
                const auto inputSample = double(sample.data.getSample(side, 1280 + i));
                // The score follows its existing MIDI-quantized velocity32/127;
                // this direct preview keeps.25. Each voice rounds before summing.
                const auto expected = float(inputSample * (32.f / 127.f)) + float(inputSample * .25f);
                exactDry = exactDry && pcm.getSample(side, i) == expected;
            }
        const auto restoredScore = state(*score), restoredInput = state(*input);
        require(exactDry && restoredScore.serial == oldScore.serial &&
                    restoredInput.serial == oldInput.serial && restoredScore.position == 5376 &&
                    restoredInput.position == 5376 && project.patterns.front().notes.front().id == source &&
                    project.patterns.front().notes.front().beat == 0 &&
                    project.patterns.front().notes.front().length == 3,
                "Restored default settles to exact dry PCM without restarting held/source occurrences");
    }
}
} // namespace sampler_filter_audio_acceptance
inline void samplerFilterAudioTests()
{
    sampler_filter_audio_acceptance::tests();
}

// Actual AudioEngine/SyntheticDevice/provider branch adapted from our existing
// samplerNormalizeLiveTests. Include after its helpers; no new worker protocol.
static void samplerFilterLiveTests(const juce::File &folder, const PluginSpec *instrument = nullptr)
{
    juce::String report;
    for (const bool patternMode : {true, false})
    {
        auto owner = std::make_unique<AudioEngine>(false, juce::File{}, 256);
        auto &audio = *owner;
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
        require(device != nullptr, "Filter virtual audio device");
        Controller c;
        c.project.channels.resize(2);
        c.project.tempo = 240;
        const auto pattern = c.project.patterns.front().id, first = c.project.channels[0].id,
                   second = c.project.channels[1].id;
        for (auto &ch : c.project.channels)
        {
            ch.source = "builtin:bass";
            ch.sampler.loop = true;
        }
        c.project.channels[0].pan = -1;
        c.project.channels[1].pan = 1;
        if (instrument)
        {
            c.project.channels[1].instrument = *instrument;
            c.project.channels[1].instrument->id = c.project.allocate();
        }
        c.project.patterns.front().length = .5;
        c.project.patterns.front().autoLength = false; // Filter edits preserve exact half-beat loop.
        c.project.placements.push_back({c.project.allocate(), pattern, 0, 0, .5, 0});
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Filter live projection");
        audio.setMidiTarget(instrument ? second : first);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, silentLeft = 0, silentRight = 0, loops = 0;
        double maximum = 0, previous = 0;
        bool monitoring = false;
        struct Observation
        {
            SyntheticCallbackSpan span;
            double threadCpuMs = -1;
            juce::uint64 threadCycles = 0;
        };
        std::array<Observation, 512> observations{};
        juce::String trace = "callback,wallMs,left,right,headSeconds\n";
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
#if JUCE_WINDOWS
                FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
                ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                const bool cpuClock =
                    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelBefore, &userBefore) != 0;
                const bool cycleClock = QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore) != 0;
#endif
                callbackAllocationProbe = monitoring;
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ended = juce::Time::getHighResolutionTicks();
                const auto ms = juce::Time::highResolutionTicksToSeconds(ended - began) * 1000;
                callbackAllocationProbe = false;
                double threadCpuMs = -1;
                juce::uint64 threadCycles = 0;
#if JUCE_WINDOWS
                if (cycleClock && QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter) &&
                    cyclesAfter >= cyclesBefore)
                    threadCycles = cyclesAfter - cyclesBefore;
                if (cpuClock &&
                    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelAfter, &userAfter))
                {
                    const auto ticks = [](FILETIME value)
                    { return (juce::uint64(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
                    threadCpuMs = double(ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) -
                                         ticks(userBefore)) /
                                  10000.;
                }
#endif
                require(callbacks < int(observations.size()), "Filter observation capacity");
                require(device->lastTiming().containedBy(began, ended), "Filter timing spans nested");
                observations[size_t(callbacks)] = {
                    {device->lastTiming(), began, ended}, threadCpuMs, threadCycles};
                maximum = std::max(maximum, ms);
                overruns += ms > 256000. / 48000.;
                ++callbacks;
                const auto head = audio.projectionDiagnostics().headSeconds;
                loops += head + .02 < previous;
                previous = head;
                silentLeft += monitoring && block.getMagnitude(0, 0, 256) == 0;
                silentRight += monitoring && block.getMagnitude(1, 0, 256) == 0;
                trace += juce::String(callbacks) + "," + juce::String(ms, 6) + "," +
                         juce::String(block.getMagnitude(0, 0, 256), 9) + "," +
                         juce::String(block.getMagnitude(1, 0, 256), 9) + "," + juce::String(head, 9) + "\n";
                dispatchMessages(1);
            }
        };
        tick(16);
        audio.midiInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(
            nullptr, juce::MidiMessage::noteOn(1, instrument ? 72 : 48, juce::uint8(100)));
        require(audio.previewChannelNote(instrument ? first : second, instrument ? 48 : 60, true),
                "Independent held preview beside filtered sampler");
        tick(8);
        monitoring = true;
        const auto before = audio.projectionDiagnostics();
        const auto workers = audio.pluginDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        CommandArgs args;
        args.channel = first;
        args.value = .5;
        if (instrument)
        {
            auto unsupported = args;
            unsupported.channel = second;
            const auto rev = c.revision;
            require(c.execute("channel.sampler.filterCutoff", unsupported).failed() && c.revision == rev,
                    "Actual hosted instrument refuses sampler Filter atomically");
        }
        for (int edit = 0; edit < 8; ++edit)
        {
            const auto baseline = c.project.state();
            const auto head = audio.projectionDiagnostics().headSeconds;
            const auto command =
                juce::StringArray{"channel.sampler.filterType", "channel.sampler.filterCutoff",
                                  "channel.sampler.filterResonance"}[edit % 3];
            args.value = edit % 3 == 0   ? std::array<int, 3>{2, 3, 5}[size_t(edit / 3)]
                         : edit % 3 == 1 ? .3 + .05 * edit
                                         : .5;
            require(c.execute(command, args).wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live Filter edit publication");
            require(std::abs(audio.projectionDiagnostics().headSeconds - head) < 1. / 48000.,
                    "Filter preserves actual playhead before another callback");
            tick(4);
            c.undoEdit();
            require(c.project.state().isEquivalentTo(baseline) &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live Filter exact Undo");
            tick(2);
            c.redoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Live Filter Redo");
            tick(2);
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live baseline publication");
            tick(2);
            require(audio.midiInput.capture.active &&
                        audio.projectionDiagnostics().channels[instrument ? 1 : 0].heldInputKeys == 1,
                    "Held key and real capture remain owned through edits/history");
        }
        // Required envelope/LFO controls use the same live publication beside
        // held input/preview and, when supplied, the actual installed instrument.
        for (const auto &entry : std::array<std::pair<const char *, double>, 4>{
                 {{"enabled", 1}, {"amount", -.25}, {"lfoEnabled", 1}, {"lfoAmount", .05}}})
        {
            args.text = "cutoff";
            args.value = entry.second;
            args.enabled = entry.second != 0;
            require(c.execute("channel.sampler.modulation." + juce::String(entry.first), args).wasOk(),
                    "Live modulation setup uses canonical validation");
        }
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "Live modulation setup publication");
        tick(4);
        const std::array<std::pair<const char *, double>, 12> edits{{{"delay", .01},
                                                                     {"attack", .03},
                                                                     {"hold", .01},
                                                                     {"decay", .04},
                                                                     {"sustain", .5},
                                                                     {"release", .05},
                                                                     {"attackTension", -.5},
                                                                     {"amount", -.4},
                                                                     {"lfoPeriod", .25},
                                                                     {"lfoTempo", 1},
                                                                     {"lfoGlobal", 1},
                                                                     {"lfoShape", 1}}};
        for (const auto &entry : edits)
        {
            const auto baseline = c.project.state();
            const auto head = audio.projectionDiagnostics().headSeconds;
            args.text = "cutoff";
            args.value = entry.second;
            args.enabled = entry.second != 0;
            require(c.execute("channel.sampler.modulation." + juce::String(entry.first), args).wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live envelope/LFO publication");
            require(std::abs(audio.projectionDiagnostics().headSeconds - head) < 1. / 48000.,
                    "Modulation preserves playhead");
            tick(4);
            require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(baseline) &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live envelope/LFO exact Undo");
            tick(2);
            require(c.redoEditChecked().wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live envelope/LFO Redo");
            tick(2);
            require(c.undoEditChecked().wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live envelope/LFO baseline restoration");
            tick(2);
            require(audio.midiInput.capture.active &&
                        audio.projectionDiagnostics().channels[instrument ? 1 : 0].heldInputKeys == 1,
                    "Envelope/LFO history retains captured MIDI and native ownership");
        }
        // Actual sampler Automation Clip publication/history, with held input/capture
        // and the unrelated installed instrument. Pattern uses its manual fallback;
        // Song traverses the same half-beat curve on every loop.
        const auto beforeCurves = c.project.state();
        CommandArgs curve;
        curve.channel = first;
        curve.samplerAutomationParameter = samplerAutomationParameterCode("cutoff", "lfoPeriod");
        curve.length = .5;
        curve.lane = 1;
        require(curve.samplerAutomationParameter && c.execute("automation.sampler.create", curve).wasOk() &&
                    awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "Actual sampler curve creation during held playback");
        tick(8);
        curve.target = c.project.automation.sources.back().id;
        for (int edit = 0; edit < 6; ++edit)
        {
            const auto baseline = c.project.state();
            const auto head = audio.projectionDiagnostics().headSeconds;
            curve.beat = .125 + edit * .01;
            curve.length = .5 - edit * .025;
            require(c.execute("automation.sampler.range", curve).wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Actual sampler curve range edit publication");
            require(std::abs(audio.projectionDiagnostics().headSeconds - head) < 1. / 48000.,
                    "Sampler curve edit retains actual playhead");
            tick(4);
            require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(baseline) &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Actual sampler curve exact Undo");
            tick(2);
            require(c.redoEditChecked().wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Actual sampler curve Redo");
            tick(2);
            require(c.undoEditChecked().wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Actual sampler curve baseline restoration");
            tick(2);
            require(audio.midiInput.capture.active &&
                        audio.projectionDiagnostics().channels[instrument ? 1 : 0].heldInputKeys == 1,
                    "Sampler curve history retains held ownership and MIDI capture");
        }
        require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeCurves) &&
                    awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "Removing the actual curve via Undo restores complete manual project");
        tick(4);
        if (instrument)
        {
            args.channel = second;
            const auto baseline = c.project.state();
            require(c.execute("channel.sampler.modulation.lfoPeriod", args).failed() &&
                        c.project.state().isEquivalentTo(baseline),
                    "Actual hosted plugin refuses sampler modulation without state changes");
            args.channel = first;
        }
        const auto after = audio.projectionDiagnostics();
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "Filter retains graph/context/device/prepared cache");
        for (size_t i = 0; i < before.channels.size(); ++i)
            require(before.channels[i].sample == after.channels[i].sample &&
                        before.channels[i].sampler == after.channels[i].sampler &&
                        before.channels[i].graphInitialisations == after.channels[i].graphInitialisations &&
                        after.channels[i].samplerAutomationFailures == 0,
                    "Filter retains every prepared Channel and voice-bank identity");
        if (instrument)
        {
            const auto current = audio.pluginDiagnostics();
            require(current.size() == workers.size() && !current.empty(), "Hosted worker retained");
            for (size_t i = 0; i < current.size(); ++i)
                require(current[i].alive && current[i].processId == workers[i].processId &&
                            current[i].packets.missed == workers[i].packets.missed &&
                            current[i].packets.dropped == workers[i].packets.dropped &&
                            current[i].packets.midiOverflow == workers[i].packets.midiOverflow,
                        "Filter does not restart or miss/drop the unrelated real plugin worker");
        }
        const auto mode = juce::String(patternMode ? "pattern" : "song");
        folder.getChildFile("filter-" + mode + "-callbacks.csv").replaceWithText(trace);
        juce::String phases = "callback,outerMs,threadCpuMs,threadCycles," +
                              juce::String(SyntheticCallbackTiming::csvHeader) + "\n";
        for (int i = 0; i < callbacks; ++i)
        {
            const auto &observation = observations[size_t(i)];
            const auto &span = observation.span;
            phases += juce::String(i + 1) + "," +
                      juce::String(SyntheticCallbackTiming::milliseconds(span.began, span.ended), 6) + "," +
                      juce::String(observation.threadCpuMs, 6) + "," +
                      juce::String(observation.threadCycles) + "," + span.timing.csv(span.began, span.ended) +
                      "\n";
        }
        require(folder.getChildFile("filter-" + mode + "-phases.csv").replaceWithText(phases),
                "Filter observations retained after processing");
        report += mode + " callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
                  " overruns=" + juce::String(overruns) + " silentLeft=" + juce::String(silentLeft) +
                  " silentRight=" + juce::String(silentRight) + " loops=" + juce::String(loops) +
                  " allocations=" + juce::String(callbackAllocations.load() - allocations) +
                  " destructions=" + juce::String(callbackDestructions.load() - destructions) +
                  " graphReplacements=0 deviceStarts=0 deviceStops=0\n";
        folder.getChildFile("filter-live.txt").replaceWithText(report);
        require(overruns == 0 && silentLeft == 0 && silentRight == 0 && loops >= 2,
                "Actual Pattern/Song held PCM survives eight filter edits plus history/loops within callback "
                "deadline");
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Filter processing introduces no instrumented callback allocation/destruction");
        monitoring = false;
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOff(1, instrument ? 72 : 48));
        audio.stopChannelPreview(instrument ? first : second);
        tick(4);
        require(audio.projectionDiagnostics().channels[instrument ? 1 : 0].heldInputKeys == 0,
                "Filtered held note releases without stuck ownership");
        audio.midiInput.capture.active = false;
        audio.stop();
    }
}
