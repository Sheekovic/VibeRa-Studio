// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/LiveTimeline.h"
#include <memory>

// Original actual AutomationProgram -> pinned LiveDocument -> four-bank consumer tests.
// Include after require() and CallbackAllocationProbe.h. Source proposal: UNCOMPILED/UNRUN.
namespace sampler_modulation_automation_acceptance
{
using namespace vibera;
inline void tests(const juce::File &folder)
{
    const auto realtime = [](auto &&run)
    {
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const bool previous = callbackAllocationProbe;
        callbackAllocationProbe = true;
        const bool ok = run();
        callbackAllocationProbe = previous;
        require(ok, "Actual sampler automation callback operation admits/completes");
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Actual consumer has no callback C++ allocation/destruction");
    };
    Sample sample;
    sample.oneShot = true;
    sample.sampleRate = 48000;
    sample.data.setSize(2, 48001);
    for (int i = 0; i < sample.data.getNumSamples(); ++i)
    {
        sample.data.setSample(0, i, .125f);
        sample.data.setSample(1, i, -.0625f);
    }
    sample.prepareNormalization();
    struct Banks
    {
        std::array<VoiceBank, 4> bank;
        SamplerVoiceBudget budget{bank[0], bank[1], bank[2], bank[3]};
        void prepare(const Sample &source, double rate, int capacity)
        {
            for (auto &b : bank)
                b.prepare(source, rate);
            require(budget.prepareModulationAutomation(capacity),
                    "Prepare actual callback capacity on CONTROL");
        }
    };
    auto banks = std::make_unique<Banks>();
    juce::AudioBuffer<float> output(2, 4096);
    for (bool disable : {true, false})
    {
        auto project = Project::empty();
        project.tempo = 120;
        const auto owner = project.channels.front().id;
        auto &settings = project.channel(owner)->sampler;
        settings.filterType = 0;
        settings.modulation[0].enabled = true;
        settings.modulation[0].amount = 1;
        settings.modulation[0].attack = settings.modulation[0].decay = 0;
        settings.modulation[0].sustain = 1;
        settings.modulation[0].release = .02;
        auto &pattern = project.patterns.front();
        pattern.autoLength = false;
        pattern.length = 4;
        pattern.notes.clear();
        const double boundary = 1792. / 24000. + 256. / 24000.;
        const auto note = project.allocate();
        pattern.notes.push_back({note, owner, 0, boundary, 60, 1});
        project.placements.clear();
        project.placements.push_back({project.allocate(), pattern.id, 0, 0, 4, 0});
        AutomationSource source;
        source.id = project.allocate();
        source.length = 4;
        source.points = {{project.allocate(), 0, 1},
                         {project.allocate(), boundary, disable ? 0.f : .25f, AutomationShape::hold},
                         {project.allocate(), 4, disable ? 0.f : .25f, AutomationShape::hold}};
        source.links = {{{AutomationParameter::samplerModulation, owner, disable ? 0u : 6u}, 0, 1}};
        project.automation.sources = {source};
        project.automation.placements = {{project.allocate(), source.id, 0, 4, 0}};
        auto document = compileLiveDocument(project, false, pattern.id, 1);
        require(document && document->find(owner) && document->find(owner)->modulationLanes.size() == 1,
                "Real compiler binds genuine local sampler lanes inside pinned document");
        auto patternDocument = compileLiveDocument(project, true, pattern.id, 2);
        require(patternDocument && patternDocument->find(owner)->modulationLanes.empty(),
                "Existing Pattern automation policy is preserved");
        const auto *channel = document->find(owner);
        banks = std::make_unique<Banks>();
        banks->prepare(sample, 48000, 2048);
        LiveSequencer sequencer;
        output.clear();
        std::array<VoiceBank *, 3> extra{&banks->bank[1], &banks->bank[2], &banks->bank[3]};
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginAutomatedModulationBlock(channel->sampler, channel->modulationLanes,
                                                                 0, 1. / 24000., 0, 1792, true))
                    return false;
                sequencer.process(*document, owner, 0, 1792, 48000, true, banks->bank[0], output, 0, extra);
                return banks->budget.endModulationBlock();
            });
        output.clear();
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginAutomatedModulationBlock(channel->sampler, channel->modulationLanes,
                                                                 1792. / 24000., 1. / 24000., 0, 1024, false))
                    return false;
                sequencer.process(*document, owner, 1792. / 48000., 1024, 48000, true, banks->bank[0], output,
                                  0, extra);
                return banks->budget.endModulationBlock();
            });
        if (disable)
        {
            bool correct = false;
            banks->bank[0].visitActive(
                [&](const auto &v)
                {
                    correct =
                        v.note == note && v.noteReleased && !v.releasing && v.oneShot && v.triggerSerial == 1;
                });
            bool pcm = true;
            for (int frame = 0; frame < 1024; ++frame)
                pcm &= output.getSample(0, frame) == .125f && output.getSample(1, frame) == -.0625f;
            require(
                correct && pcm,
                "Effective Volume disabled on exact score off frame preserves same one-shot/source/voice");
        }
        else
            require(std::abs(output.getSample(0, 256) - .124609375f) < 1.e-7f &&
                        std::abs(output.getSample(1, 256) + .0623046875f) < 1.e-7f,
                    "Exact score off captures automated sustain .25 before release, not stale sustain1");

        // Native/AudioClip roles retain canonical lanes but never bind/apply sampler automation.
        project.channel(owner)->audioClip = true;
        auto dormant = compileLiveDocument(project, false, pattern.id, 3);
        require(dormant && dormant->find(owner)->modulationLanes.empty(),
                "No dormant-role curve applied to AudioClip");
    }

    // Fresh frame-zero input and generation-change collision: never install temporary manual Volume.
    {
        AutomationDocument automation;
        automation.initialValues = {{{AutomationParameter::samplerModulation, 7, 0}, 0}};
        AutomationProgram program;
        std::string error;
        require(AutomationProgram::compile(automation, true, program, error),
                "Compile real initialized Volume disable");
        std::vector<SamplerModulationAutomationLane> bindings;
        require(prepareSamplerModulationAutomation(program, 7, true, bindings),
                "Prepare exact frame-zero initialization");
        std::vector<SamplerModulationAutomationLane> native;
        require(prepareSamplerModulationAutomation(program, 7, false, native) && native.empty(),
                "Same stored automation stays dormant for native role");
        SamplerSettings off;
        off.filterType = 0;
        auto on = off;
        on.modulation[0].enabled = true;
        on.modulation[0].amount = 1;
        on.modulation[0].attack = on.modulation[0].decay = 0;
        const PreparedSamplerSettings disabled(off), enabled(on);
        banks = std::make_unique<Banks>();
        banks->prepare(sample, 48000, 2048);
        output.clear();
        for (auto &b : banks->bank)
            b.configure(disabled);
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginModulationBlock(0, 2048))
                    return false;
                banks->bank[1].noteOn(60, 1);
                banks->bank[1].render(output, 0, 2048);
                banks->bank[1].noteOff(60); // One-shot remembered key-off, source remains sounding.
                return banks->budget.endModulationBlock();
            });
        output.clear();
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginAutomatedModulationBlock(enabled, bindings, 1, 1. / 24000., 0, 256,
                                                                 true))
                    return false;
                banks->bank[2].noteOn(60, 1); // Actual input-style note-on AFTER frame-zero selection.
                banks->bank[1].render(output, 0, 256);
                return banks->budget.endModulationBlock();
            });
        bool oldGate = false, newGate = false;
        banks->bank[1].visitActive([&](const auto &v)
                                   { oldGate = v.noteReleased && !v.releasing && v.oneShot; });
        banks->bank[2].visitActive([&](const auto &v)
                                   { newGate = !v.noteReleased && !v.releasing && v.oneShot; });
        require(oldGate && newGate && output.getSample(0, 191) == .125f,
                "Atomic manual+effective frame0 preserves ignored prior off and selects new input gate");
    }

    // Real compiler physical ramp, quantized tempo flags, split/reversed independent bank order.
    for (double rate : {48000., 44100.})
        for (bool global : {true, false})
        {
            AutomationDocument automation;
            AutomationSource source;
            source.id = 1;
            source.length = 4;
            const double span = 512. * 2. / rate;
            source.points = {{2, 0, 0}, {3, span, 1}, {5, 4, 1}};
            source.links = {{{AutomationParameter::samplerModulation, 7, 49}, .01f, .04f},
                            {{AutomationParameter::samplerModulation, 7, 45}, 0, 1}};
            automation.sources = {source};
            automation.initialValues = {{{AutomationParameter::samplerModulation, 7, 49}, .01f},
                                        {{AutomationParameter::samplerModulation, 7, 45}, 0}};
            automation.placements = {{4, 1, 0, 4, 0}};
            AutomationProgram program;
            std::string error;
            require(AutomationProgram::compile(automation, true, program, error),
                    "Compile actual ramp period and tempo flag lanes");
            std::vector<SamplerModulationAutomationLane> bindings;
            require(prepareSamplerModulationAutomation(program, 7, true, bindings) && bindings.size() == 2,
                    "Prepare immutable exact two-row consumer");
            SamplerSettings values;
            values.filterType = 0;
            auto &pan = values.modulation[1];
            pan.lfoEnabled = true;
            pan.lfoGlobal = global;
            pan.lfoShape = 1;
            pan.lfoAmount = .5;
            const PreparedSamplerSettings prepared(values, 120);
            auto full = std::make_unique<Banks>(), split = std::make_unique<Banks>(),
                 reference = std::make_unique<Banks>();
            full->prepare(sample, rate, 512);
            split->prepare(sample, rate, 256);
            reference->prepare(sample, rate, 512);
            juce::AudioBuffer<float> actual(2, 512), partitioned(2, 512), expected(2, 512);
            actual.clear();
            partitioned.clear();
            expected.clear();
            realtime(
                [&]() noexcept
                {
                    if (!full->budget.beginAutomatedModulationBlock(prepared, bindings, 0, 2. / rate, 0, 512,
                                                                    true))
                        return false;
                    for (auto &bank : full->bank)
                        bank.noteOn(60, 1);
                    for (int bank = 3; bank >= 0; --bank)
                        full->bank[size_t(bank)].render(actual, 0, 512);
                    return full->budget.endModulationBlock();
                });
            for (int block = 0; block < 2; ++block)
                realtime(
                    [&]() noexcept
                    {
                        const int offset = block * 256;
                        if (!split->budget.beginAutomatedModulationBlock(
                                prepared, bindings, offset * 2. / rate, 2. / rate, offset, 256, block == 0))
                            return false;
                        if (block == 0)
                            for (auto &bank : split->bank)
                                bank.noteOn(60, 1);
                        for (int bank = 0; bank < 4; ++bank)
                        {
                            split->bank[size_t(bank)].render(partitioned, offset, 128);
                            split->bank[size_t(bank)].render(partitioned, offset + 128, 128);
                        }
                        return split->budget.endModulationBlock();
                    });
            // Independent manual control/reference integration. The actual path above consumes compiled
            // lanes.
            double phase = 0;
            for (int frame = 0; frame < 512; ++frame)
            {
                const double position = frame / 512.;
                const float stored = float(double(.01f) + (double(.04f) - .01f) * position);
                const double period = frame == 0 ? .01 : double(stored);
                auto selected = values;
                selected.modulation[1].lfoPeriod = period;
                selected.modulation[1].lfoTempo = position >= .5;
                const PreparedSamplerSettings setting(selected, 120);
                SamplerModulationPhases phases{};
                phases[1] = phase;
                for (auto &bank : reference->bank)
                {
                    bank.configure(setting);
                    bank.beginModulationBlock(phases, frame);
                    if (frame == 0)
                        bank.noteOn(60, 1);
                    bank.render(expected, frame, 1);
                    bank.endModulationBlock();
                }
                phase = std::fmod(phase + (position >= .5 ? 2. : 1.) / (rate * period), 1.);
            }
            double maximumError = 0;
            for (int channel = 0; channel < 2; ++channel)
                for (int frame = 0; frame < 512; ++frame)
                {
                    maximumError =
                        std::max(maximumError, std::abs(double(actual.getSample(channel, frame) -
                                                               expected.getSample(channel, frame))));
                    require(std::abs(actual.getSample(channel, frame) -
                                     partitioned.getSample(channel, frame)) < 2.e-6f,
                            "Split blocks/reversed bank order preserve actual consumer PCM");
                }
            require(maximumError < 2.e-6 &&
                        std::abs(full->budget.committedModulationPhases()[1] - phase) < 2.e-10,
                    "Actual program global/local LFO PCM agrees with independent physical float ramp and "
                    "integrated clock");
            const auto prior = full->budget.committedModulationPhases();
            for (auto &bank : full->bank)
                bank.reset();
            realtime(
                [&]() noexcept
                {
                    if (!full->budget.beginAutomatedModulationBlock(prepared, bindings, span, 2. / rate, 0,
                                                                    128, false))
                        return false;
                    for (auto &bank : full->bank)
                        bank.render(actual, 0, 128);
                    return full->budget.endModulationBlock();
                });
            require(full->budget.committedModulationPhases() != prior,
                    "Common clock advances through no-active-voice spans");
            realtime(
                [&]() noexcept
                {
                    if (!full->budget.beginAutomatedModulationBlock(prepared, bindings, span + 256. / rate,
                                                                    2. / rate, 0, 128, false))
                        return false;
                    full->bank[2].noteOn(60, 1);
                    full->bank[2].render(actual, 0, 128);
                    return full->budget.endModulationBlock();
                });
            require(full->bank[2].activeVoices() == 1, "Real retrigger follows silent common-clock span");
        }

    // Stop and binding exit restore manual controls once, even with the SAME document generation.
    for (bool stopped : {true, false})
    {
        AutomationDocument automation;
        automation.initialValues = {{{AutomationParameter::samplerModulation, 7, 0}, 1},
                                    {{AutomationParameter::samplerModulation, 7, 11}, 1},
                                    {{AutomationParameter::samplerModulation, 7, 45}, 1},
                                    {{AutomationParameter::samplerModulation, 7, 49}, .02f},
                                    {{AutomationParameter::samplerModulation, 7, 50}, .75f}};
        AutomationProgram program;
        std::string error;
        require(AutomationProgram::compile(automation, true, program, error),
                "Compile actual held Song controls for Stop/binding-exit");
        std::vector<SamplerModulationAutomationLane> bindings;
        require(prepareSamplerModulationAutomation(program, 7, true, bindings) && bindings.size() == 5,
                "Bind actual held Song controls");
        SamplerSettings values;
        values.filterType = 0;
        values.modulation[0].amount = .5;
        values.modulation[0].attack = values.modulation[0].decay = 0;
        auto &pan = values.modulation[1];
        pan.lfoEnabled = pan.lfoGlobal = true;
        pan.lfoShape = 1;
        pan.lfoAmount = .25;
        pan.lfoPeriod = .5;
        const PreparedSamplerSettings manual(values, 120);
        banks = std::make_unique<Banks>();
        banks->prepare(sample, 48000, 256);
        output.clear();
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginAutomatedModulationBlock(manual, bindings, 0, 1. / 24000., 0, 256,
                                                                 true))
                    return false;
                banks->bank[1].noteOn(60, 1, 500, 600); // Held live-MIDI owner, never score-reset on Stop.
                for (auto &bank : banks->bank)
                    bank.render(output, 0, 256);
                return banks->budget.endModulationBlock();
            });
        double age = 0;
        bool automatedGate = false;
        banks->bank[1].visitActive(
            [&](const auto &v)
            {
                age = v.modulation.ageSeconds();
                automatedGate = !v.oneShot && !v.releasing;
            });
        require(automatedGate && banks->bank[1].preparedModulation().settings[1].lfoAmount == .75,
                "Actual automation differs from manual controls before exit");
        const auto priorPhase = banks->budget.committedModulationPhases();
        bool restored = false;
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginAutomatedModulationBlock(manual, {}, 1, stopped ? 0. : 1. / 24000., 0,
                                                                 256, false))
                    return false;
                restored = true;
                for (auto &bank : banks->bank)
                    restored &= bank.preparedModulation().settings == manual.values.modulation;
                // Input application belongs here, AFTER base restore; original held voice remains untouched.
                for (auto &bank : banks->bank)
                    bank.render(output, 0, 256);
                return banks->budget.endModulationBlock();
            });
        bool sameVoice = false;
        banks->bank[1].visitActive(
            [&](const auto &v)
            {
                sameVoice = v.note == 500 && v.placement == 600 && v.triggerSerial == 1 &&
                            v.position == 512 && v.oneShot && !v.noteReleased && !v.releasing &&
                            v.modulation.ageSeconds() > age;
            });
        const auto expected = PreparedSamplerModulation::wrap(priorPhase[1] + 256 * (1. / 48000.) / .5);
        require(restored && sameVoice && banks->budget.committedModulationPhases()[1] == expected,
                "Stop/binding exit restores full manual values with same live voice/source and continuous "
                "manual phase");
        const auto phaseAfterExit = banks->budget.committedModulationPhases();
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginAutomatedModulationBlock(manual, {}, 1, stopped ? 0. : 1. / 24000., 0,
                                                                 256, false))
                    return false;
                banks->bank[1].noteOff(
                    60); // Restored manual one-shot ignores source release while remembering key-off.
                banks->bank[1].render(output, 0, 256);
                return banks->budget.endModulationBlock();
            });
        sameVoice = false;
        banks->bank[1].visitActive(
            [&](const auto &v)
            {
                sameVoice = v.note == 500 && v.placement == 600 && v.triggerSerial == 1 &&
                            v.position == 768 && v.oneShot && v.noteReleased && !v.releasing;
            });
        require(sameVoice &&
                    banks->budget.committedModulationPhases()[1] ==
                        PreparedSamplerModulation::wrap(phaseAfterExit[1] + 256 * (1. / 48000.) / .5),
                "Steady manual held-input processing/key-off needs no graph/voice reset after exit");
        require(
            program.valueAt({AutomationParameter::samplerModulation, 7, 50}, 1) == .75f,
            "Stop/manual fallback never mutates canonical automation or its held native-independent lane");
    }

    // Empty-bank shortcut keeps only its endpoint; cursor selection still precedes a later real score on/off.
    {
        auto project = Project::empty();
        project.tempo = 120;
        const auto owner = project.channels.front().id;
        auto &settings = project.channel(owner)->sampler;
        settings.filterType = 0;
        settings.modulation[0].enabled = true;
        settings.modulation[0].amount = 1;
        settings.modulation[0].attack = settings.modulation[0].decay = 0;
        auto &part = project.patterns.front();
        part.autoLength = false;
        part.length = 4;
        part.notes.clear();
        const auto note = project.allocate();
        part.notes.push_back({note, owner, 128. / 24000., 256. / 24000., 60, 1});
        project.placements.clear();
        project.placements.push_back({project.allocate(), part.id, 0, 0, 4, 0});
        AutomationSource source;
        source.id = project.allocate();
        source.length = 4;
        source.points = {{project.allocate(), 0, 1},
                         {project.allocate(), 128. / 24000., 0, AutomationShape::hold},
                         {project.allocate(), 4, 0, AutomationShape::hold}};
        source.links = {{{AutomationParameter::samplerModulation, owner, 0}, 0, 1}};
        project.automation.sources = {source};
        project.automation.placements = {{project.allocate(), source.id, 0, 4, 0}};
        auto document = compileLiveDocument(project, false, part.id, 20);
        require(document && document->find(owner),
                "Prepare genuine silent-prefix score with automated on-frame Volume disable");
        const auto *channel = document->find(owner);
        banks = std::make_unique<Banks>();
        banks->prepare(sample, 48000, 1024);
        output.clear();
        LiveSequencer sequencer;
        std::array<VoiceBank *, 3> extra{&banks->bank[1], &banks->bank[2], &banks->bank[3]};
        realtime(
            [&]() noexcept
            {
                if (!banks->budget.beginAutomatedModulationBlock(channel->sampler, channel->modulationLanes,
                                                                 0, 1. / 24000., 0, 1024, true))
                    return false;
                sequencer.process(*document, owner, 0, 1024, 48000, true, banks->bank[0], output, 0, extra);
                return banks->budget.endModulationBlock();
            });
        bool retained = false;
        banks->bank[0].visitActive(
            [&](const auto &v)
            {
                retained = v.note == note && v.triggerSerial == 1 && v.position == 896 && v.noteReleased &&
                           v.oneShot && !v.releasing;
            });
        require(retained && output.getMagnitude(0, 0, 128) == 0 && output.getSample(0, 600) == .125f &&
                    output.getSample(1, 600) == -.0625f,
                "Real later note after silent-bank endpoint selection uses exact on/off controls and retains "
                "source PCM");
        const auto end = banks->budget.committedModulationPhases();
        bool clocks = true;
        for (double phase : end)
            clocks &= phase == 1024 * (1. / 48000.);
        require(clocks && banks->bank[1].activeVoices() == 0 && banks->bank[2].activeVoices() == 0 &&
                    banks->bank[3].activeVoices() == 0,
                "Silent extra banks do not own/duplicate common clock advancement");
    }

    // Every one of the88 actual registry-backed fields is consumed, no unsupported row silently skipped.
    {
        AutomationDocument automation;
        for (std::uint32_t target = 0; target < 5; ++target)
            for (std::uint32_t field = 0; field < 20; ++field)
                if (const auto parameter = lookupSamplerAutomationParameter(target * 32 + field))
                    automation.initialValues.push_back(
                        {{AutomationParameter::samplerModulation, 7, parameter->code},
                         float(parameter->minimum)});
        AutomationProgram program;
        std::string error;
        require(AutomationProgram::compile(automation, true, program, error),
                "Compile all88 genuine initialized sampler rows");
        std::vector<SamplerModulationAutomationLane> bindings;
        require(prepareSamplerModulationAutomation(program, 7, true, bindings) && bindings.size() == 88,
                "CONTROL prepares the exact complete supported registry, not an arbitrary cap");
        banks = std::make_unique<Banks>();
        banks->prepare(sample, 48000, 256);
        const PreparedSamplerSettings base;
        realtime(
            [&]() noexcept
            {
                return banks->budget.beginAutomatedModulationBlock(base, bindings, 0, 1. / 24000., 0, 256,
                                                                   true) &&
                       banks->budget.endModulationBlock();
            });
        bool exact = true;
        for (const auto &row : bindings)
            exact &= samplerModulationValue(
                         banks->bank[3].preparedModulation().settings[size_t(row.parameter.registryTarget)],
                         *row.parameter.registryParameter) == row.parameter.minimum;
        require(exact,
                "Effective frame0 assigns all numbers/flags/shape at exact float-adapted canonical minima");
    }

    PreparedSamplerModulation manual;
    require(manual.prepare(48000, 120, {}), "Prepare manual clock");
    SamplerLfoPhaseBlock allocationFree;
    realtime(
        [&]() noexcept
        {
            if (!allocationFree.beginManual(manual, 8192))
                return false;
            SamplerModulationPhases value{};
            return allocationFree.view().read(8192, value) && allocationFree.commit();
        });
    require(!allocationFree.begin(manual, 1), "Original prepared begin still refuses absent capacity");
    auto badManual = manual;
    badManual.settings[0].lfoPeriod = double(.01f);
    const auto manualBefore = allocationFree.phases();
    realtime(
        [&]() noexcept
        {
            return !allocationFree.beginManual(badManual, 1) && !allocationFree.beginManual(manual, 0) &&
                   allocationFree.phases() == manualBefore;
        });
    badManual.settings[0].lfoPeriod = .01;
    badManual.globalIncrements[0] = badManual.secondsPerSample / .01;
    realtime([&]() noexcept { return allocationFree.beginManual(badManual, 1) && allocationFree.commit(); });
    require(allocationFree.phases() != manualBefore,
            "Manual path preserves exact canonical period bound; promoted .01f is not a new tolerance");
    SamplerModulationAutomationBlock invalid;
    require(invalid.prepare(256), "Prepare invalid-input owner");
    AutomationProgram::Lane bad;
    bad.key = {AutomationParameter::samplerModulation, 1, 17};
    bad.initial = std::numeric_limits<float>::quiet_NaN();
    const SamplerModulationAutomationLane row{&bad, *lookupSamplerAutomationParameter(17)};
    const auto before = invalid.committedPhases();
    realtime(
        [&]() noexcept
        {
            return !invalid.begin(manual, {&row, 1}, 0, 1. / 24000., 256) && !invalid.pending() &&
                   invalid.committedPhases() == before;
        });
    require(invalid.status() == SamplerModulationAutomationBlock::Status::invalidCurve,
            "Invalid curve retains typed failure, phase/voices/events untouched before admission");
    banks = std::make_unique<Banks>();
    banks->prepare(sample, 48000, 256);
    const PreparedSamplerSettings untouched;
    for (auto &bank : banks->bank)
        bank.configure(untouched);
    output.clear();
    realtime(
        [&]() noexcept
        {
            if (!banks->budget.beginModulationBlock(0, 256))
                return false;
            banks->bank[1].noteOn(60, 1, 500, 600);
            banks->bank[1].render(output, 0, 256);
            return banks->budget.endModulationBlock();
        });
    double positionBefore = 0, positionAfter = 0;
    banks->bank[1].visitActive([&](const auto &voice) { positionBefore = voice.position; });
    const auto bankPhaseBefore = banks->budget.committedModulationPhases();
    realtime(
        [&]() noexcept
        {
            return !banks->budget.beginAutomatedModulationBlock(untouched, {&row, 1}, 0, 1. / 24000., 0, 256,
                                                                true);
        });
    banks->bank[1].visitActive([&](const auto &voice) { positionAfter = voice.position; });
    require(positionBefore == positionAfter && banks->budget.committedModulationPhases() == bankPhaseBefore,
            "Rejected actual bank admission preserves old voice position and common phase");
    bad.initial = .02f;
    realtime(
        [&]() noexcept
        {
            if (!banks->budget.beginAutomatedModulationBlock(untouched, {&row, 1}, 0, 1. / 24000., 0, 256,
                                                             true))
                return false;
            banks->bank[1].render(output, 0, 256);
            return banks->budget.endModulationBlock();
        });
    banks->bank[1].visitActive([&](const auto &voice) { positionAfter = voice.position; });
    require(positionAfter == positionBefore + 256 && banks->bank[1].activeVoices() == 1,
            "Next valid actual bank callback automatically resumes same voice/source after refusal");
    realtime([&]() noexcept
             { return invalid.begin(manual, {&row, 1}, 0, 1. / 24000., 256) && invalid.finish(true); });
    require(invalid.status() == SamplerModulationAutomationBlock::Status::idle,
            "Valid block after rejected prevalidation resumes automatically without CONTROL reset");
    SamplerModulationPhases admittedEnd{};
    auto selected = manual;
    auto selectedValues = manual.settings;
    realtime(
        [&]() noexcept
        {
            if (!invalid.begin(manual, {&row, 1}, 0, 1. / 24000., 256) ||
                !invalid.readPhases(256, admittedEnd))
                return false;
            // Fault injection AFTER admission: never relabel partial source work successful.
            return !invalid.select(256, selected, selectedValues) && !invalid.finish(false) &&
                   invalid.committedPhases() == admittedEnd;
        });
    require(invalid.status() == SamplerModulationAutomationBlock::Status::staleView,
            "Unexpected late failure reports exact fault and retires full admitted clock, no origin replay");
    realtime([&]() noexcept { return invalid.beginManual(manual, 128) && invalid.finish(true); });
    require(invalid.status() == SamplerModulationAutomationBlock::Status::idle,
            "Valid manual callback resumes after explicit partial failure without persistent silence");
    require(folder.getChildFile("sampler-automation-consumer-cases.txt")
                .replaceWithText(
                    "Real AutomationProgram + LiveDocument + score off + four banks; 48k/44.1k "
                    "split/order/global/local/invalid/manual; automatic core only, no hardware/listening.\n"),
            "Retain actual completed consumer cases only after assertions");
}
} // namespace sampler_modulation_automation_acceptance
