// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Sampler.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <cmath>

// Additive musical ordering reference, NOT an implemented Automation Clip consumer test.
// Include after existing require() and CallbackAllocationProbe.h.
namespace sampler_automation_event_reference_acceptance
{
using namespace vibera;
inline void tests(const juce::File &folder)
{
    Sample source;
    source.sampleRate = 48000;
    source.oneShot = true;
    source.data.setSize(2, 48001);
    for (int frame = 0; frame < source.data.getNumSamples(); ++frame)
    {
        source.data.setSample(0, frame, .125f);
        source.data.setSample(1, frame, -.0625f);
    }
    source.prepareNormalization();
    auto correct = std::make_unique<VoiceBank>(), wrongOrder = std::make_unique<VoiceBank>();
    juce::AudioBuffer<float> actual(2, 4096), negative(2, 4096);
    const auto realtime = [&](auto &&action)
    {
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const auto prior = callbackAllocationProbe;
        callbackAllocationProbe = true;
        action();
        callbackAllocationProbe = prior;
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Musical event-order reference configures/releases/renders without callback C++ heap work");
    };
    const auto render = [&](VoiceBank &bank, juce::AudioBuffer<float> &pcm, int count)
    {
        pcm.clear();
        realtime([&]() noexcept { bank.render(pcm, 0, count); });
    };
    SamplerSettings enabled;
    enabled.filterType = 0;
    enabled.modulation[0].enabled = true;
    enabled.modulation[0].amount = 1;
    enabled.modulation[0].attack = enabled.modulation[0].decay = 0;
    enabled.modulation[0].sustain = 1;
    enabled.modulation[0].release = .02;
    auto disabled = enabled;
    disabled.modulation[0].enabled = false;
    const PreparedSamplerSettings on(enabled), off(disabled);
    const auto start =
        [&](VoiceBank &bank, const PreparedSamplerSettings &settings, juce::AudioBuffer<float> &pcm)
    {
        bank.prepare(source, 48000); // CONTROL only, before probe.
        bank.configure(settings);
        bank.noteOn(60, 1, 701, 17);
        render(bank, pcm, 2048);
    };
    const auto constant = [](const juce::AudioBuffer<float> &pcm, int count)
    {
        for (int frame = 0; frame < count; ++frame)
            if (pcm.getSample(0, frame) != .125f || pcm.getSample(1, frame) != -.0625f)
                return false;
        return true;
    };

    // A curve disables Volume on the exact score off frame. Select before that off.
    start(*correct, on, actual);
    start(*wrongOrder, on, negative);
    realtime(
        [&]() noexcept
        {
            correct->configure(off);
            correct->noteOff(60);
            wrongOrder->noteOff(60);
            wrongOrder->configure(off);
        });
    render(*correct, actual, 1024);
    render(*wrongOrder, negative, 1024);
    bool retained = false;
    correct->visitActive(
        [&](const auto &voice) noexcept
        {
            retained = voice.note == 701 && voice.placement == 17 && voice.triggerSerial == 1 &&
                       voice.noteReleased && !voice.releasing && voice.oneShot;
        });
    require(retained && constant(actual, 1024) && correct->activeVoices() == 1 &&
                wrongOrder->activeVoices() == 0 && negative.getMagnitude(0, 896, 128) == 0,
            "Exact off-frame effective Volume selection preserves original one-shot; render-only late "
            "selection cannot undo amplitude release");

    // New document manual Volume=true but effective automated frame0=false.
    // Never temporarily reconcile the manual-only gate before the effective frame.
    start(*correct, off, actual);
    start(*wrongOrder, off, negative);
    realtime(
        [&]() noexcept
        {
            correct->noteOff(60);
            wrongOrder->noteOff(60);
        });
    render(*correct, actual, 1024);
    render(*wrongOrder, negative, 1024);
    realtime(
        [&]() noexcept
        {
            correct->configure(off); // Atomic final-effective reference.
            wrongOrder->configure(on);
            wrongOrder->configure(off);
        });
    render(*correct, actual, 1024);
    render(*wrongOrder, negative, 1024);
    require(correct->activeVoices() == 1 && constant(actual, 1024) && wrongOrder->activeVoices() == 0,
            "Base-plus-effective frame0 must be atomic: temporary manual enable wrongly retires an already "
            "released key");

    // Actual current level at the exact off frame controls release capture.
    start(*correct, on, actual);
    start(*wrongOrder, on, negative);
    auto quarter = enabled;
    quarter.modulation[0].sustain = .25;
    const PreparedSamplerSettings quarterPrepared(quarter);
    realtime(
        [&]() noexcept
        {
            correct->configure(quarterPrepared);
            correct->noteOff(60);
            wrongOrder->noteOff(60);
            wrongOrder->configure(quarterPrepared);
        });
    render(*correct, actual, 1);
    render(*wrongOrder, negative, 1);
    // Independent first5ms-ramp gain:1+(.25-1)/240=.996875.
    require(std::abs(actual.getSample(0, 0) - .124609375f) < 1.e-8f &&
                std::abs(actual.getSample(1, 0) + .0623046875f) < 1.e-8f && negative.getSample(0, 0) == .125f,
            "Source off captures actual cursor sustain.25, not the preceding frame's sustain1");
    render(*correct, actual, 2048);
    render(*wrongOrder, negative, 2048);
    require(correct->activeVoices() == 0 && wrongOrder->activeVoices() == 0,
            "Both reference orders remain bounded; different capture is a musical error rather than a leaked "
            "voice");

    // MIDI/typing/preview noteOn must see the selected frame0 before applyInputs.
    auto silentOn = enabled;
    silentOn.modulation[0].sustain = 0;
    correct->prepare(source, 48000);
    wrongOrder->prepare(source, 48000);
    wrongOrder->configure(PreparedSamplerSettings(silentOn));
    realtime(
        [&]() noexcept
        {
            correct->configure(off);
            correct->noteOn(60, 1, 702);
            wrongOrder->noteOn(60, 1, 702);
            wrongOrder->configure(off);
        });
    render(*correct, actual, 256);
    render(*wrongOrder, negative, 256);
    // Source2ms protection finished at frame191; bad initial0 creates an extra5ms ramp.
    require(
        actual.getSample(0, 191) == .125f && std::abs(negative.getSample(0, 191) - .1f) < 1.e-7f &&
            correct->activeVoices() == 1 && wrongOrder->activeVoices() == 1,
        "Input frame0 selection precedes voice initialization and avoids an invented extra modulation fade");

    require(folder.createDirectory().wasOk() &&
                folder.getChildFile("sampler-automation-event-reference.txt")
                    .replaceWithText("PASS original VoiceBank manual-order musical references with "
                                     "intentional negative controls. "
                                     "No Automation Clip consumer, "
                                     "application/provider/native/hardware/listening qualification.\n"),
            "Retain the actually executed reference scope without implying product automation completion");
}
} // namespace sampler_automation_event_reference_acceptance
