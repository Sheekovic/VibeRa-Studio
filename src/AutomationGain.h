// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include "GraphControlRuntime.h"
#include "AudioLogger.h"
#include "MonitorVolume.h"
#include "MixerMeter.h"
#include "PreparedBalance.h"
#include "PreparedStereo.h"
#include "MixerEq.h"
#include <tracktion_engine/tracktion_engine.h>

namespace vibera
{
// Retained graph stage. Model/curve compilation and lifetime ownership remain on the control thread.
class AutomationGain final : public tracktion::Plugin
{
    std::shared_ptr<LiveTimeline> timeline;
    LiveTimeline::Lease document;
    int insert = 0;
    struct Runtime
    {
        detail::GraphControlRuntimeState lifecycle;
        juce::SmoothedValue<float> manual, mute;
        std::atomic<float> reportedGain{1};
        MixerMeter meter;
        dsp::PreparedBalance balance;
        dsp::PreparedStereo stereo;
        MixerEqProcessor eq;
        std::vector<float> delayLine; // 2 x capacity, allocated in initialise only.
        int delayCapacity = 0, delayWrite = 0;
        float follow = 0; // Envelope follower state (DSP writer only).
        double activeRate = 0;
        int insert = 0;
        tracktion::PanLaw law = tracktion::PanLawLinear;
        Runtime() = default;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;
        Runtime(Runtime &&) = delete;
        Runtime &operator=(Runtime &&) = delete;
    };
    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    detail::GraphControlRuntimeAttachment attachment;
    std::atomic<std::uint64_t> initialisations{0}; // Per-shell construction diagnostic.
    tracktion::PanLaw panLaw = tracktion::PanLawLinear;
    float initialPan = 0;
    dsp::PreparedStereo::Target initialStereo;
    MixerEqSettings initialEq;
    bool configured = false;
    void detachRuntime() noexcept
    {
        document = {}; // Shell-local only, after this graph's readers quiesce.
        if (attachment.detach(runtime->lifecycle))
            runtime->activeRate = 0;
    }

  public:
    static constexpr const char *xmlTypeName = "vibera_automation_gain";
    explicit AutomationGain(tracktion::PluginCreationInfo info) : Plugin(info) {}
    ~AutomationGain() override
    {
        detachRuntime();
        notifyListenersOfDeletion();
    }
    // Native per-adapter CPU telemetry is unused; overall callback health is independent.
    bool shouldMeasureCpuUsage() const noexcept override
    {
        return false;
    }
    juce::String getName() const override
    {
        return "VibeRa Mixer Volume";
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool isSynth() override
    {
        return false;
    }
    bool takesAudioInput() override
    {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return false;
    }
    int getNumOutputChannelsGivenInputs(int channels) override
    {
        return channels;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    static tracktion::Plugin::Ptr createDetachedGraph(tracktion::Edit &edit, const juce::ValueTree &state)
    {
        if (state[tracktion::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new AutomationGain({edit, state, false, tracktion::PluginCreationInfo::Mode::detachedGraph});
    }
    juce::Result shareCompatibleRuntimeFrom(const AutomationGain &source)
    {
        if (!isDetachedGraph() || &source == this || &source.edit != &edit || itemID != source.itemID ||
            !configured || !timeline || !attachment.privateConfiguration(runtime->lifecycle))
            return juce::Result::fail(
                "automationGain.runtime.invalidCandidate: Fresh configured same-ID detached shell required");
        if (!source.attachment.isAttachedControl() || !source.runtime->lifecycle.canRetain())
            return juce::Result::fail("automationGain.runtime.inactiveSource: Initialized source required");
        if (insert != source.insert || panLaw != source.panLaw || insert != source.runtime->insert ||
            panLaw != source.runtime->law)
            return juce::Result::fail(
                "automationGain.runtime.incompatibleSource: Insert and pan law must match");
        runtime = source.runtime;
        attachment.retain(runtime->lifecycle);
        return juce::Result::ok();
    }
    // CONTROL only. Keeps no PluginPtr/Edit alive past graph retirement.
    std::shared_ptr<const void> runtimeOwnerControl() const { return runtime; }
    std::uintptr_t runtimeIdentity() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(runtime.get());
    }
    unsigned runtimeAttachmentsControl() const noexcept
    {
        return runtime->lifecycle.shells;
    }
    std::uint64_t runtimeEpochControl() const noexcept
    {
        return runtime->lifecycle.epoch;
    }
    void configure(std::shared_ptr<LiveTimeline> value, const MixerInsert &owner, tracktion::PanLaw law)
    {
        attachment.requirePrivateConfiguration(runtime->lifecycle);
        timeline = std::move(value);
        insert = owner.index;
        panLaw = law;
        initialPan = owner.pan;
        initialStereo = {owner.stereoSeparation, owner.phaseInverted, owner.stereoSwapped};
        initialEq = owner.eq;
        runtime->manual.setCurrentAndTargetValue(owner.gain);
        runtime->mute.setCurrentAndTargetValue(owner.muted ? 0.f : 1.f);
        runtime->reportedGain = owner.gain;
        configured = true;
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override {}
    void initialise(const tracktion::PluginInitialisationInfo &info) override
    {
        const auto mode = attachment.preflight(runtime->lifecycle, info.sampleRate, info.blockSizeSamples,
                                               runtime->insert == insert && runtime->law == panLaw);
        if (mode == detail::GraphControlRuntimeAttachment::Preparation::retained)
        {
            // Pre-own THIS graph's immutable fallback before attachment. Never
            // copy the incumbent Lease or inspect/reset any shared DSP owner.
            auto ownDocument = timeline ? timeline->acquire() : LiveTimeline::Lease{};
            if (!ownDocument.document ||
                std::none_of(ownDocument.document->mixer.begin(), ownDocument.document->mixer.end(),
                             [this](const auto &owner) { return owner.index == insert; }))
                throw std::logic_error(
                    "automationGain.runtime.documentUnavailable: Candidate insert document required");
            document = std::move(ownDocument);
            attachment.complete(runtime->lifecycle, info.sampleRate, info.blockSizeSamples, mode);
            ++initialisations;
            return;
        }
        ++initialisations;
        // Base lifecycle uses initialiseWithoutStopping for unchanged live
        // graphs. Fresh activation/rate/buffer changes start at current admitted
        // controls, including edits not yet consumed by a callback.
        float pan = initialPan;
        auto stereo = initialStereo;
        auto eqSettings = initialEq;
        if (auto next = timeline->acquire(); next.document)
        {
            const auto &controls = next.document->mixer;
            const auto owner = std::lower_bound(controls.begin(), controls.end(), insert,
                                                [](const auto &m, int id) { return m.index < id; });
            if (owner != controls.end() && owner->index == insert)
            {
                runtime->manual.setCurrentAndTargetValue(owner->gain);
                runtime->mute.setCurrentAndTargetValue(owner->muted ? 0.f : 1.f);
                runtime->reportedGain = owner->gain;
                pan = owner->pan;
                stereo = {owner->stereoSeparation, owner->phaseInverted, owner->stereoSwapped};
                eqSettings = owner->eq;
            }
        }
        runtime->eq.prepare(info.sampleRate, eqSettings);
        runtime->delayCapacity = int(info.sampleRate * .5) + 2;
        runtime->delayLine.assign(size_t(runtime->delayCapacity) * 2, 0.f);
        runtime->delayWrite = 0;
        if (!runtime->stereo.prepare(info.sampleRate, stereo))
            throw std::logic_error("automationGain.runtime.invalidStereo: Valid stereo preparation required");
        runtime->meter.reset();
        document = {};
        runtime->manual.reset(info.sampleRate, .015);
        runtime->mute.reset(info.sampleRate, .015);
        const auto ready =
            runtime->balance.prepare(info.sampleRate, dsp::PreparedBalance::Mode::panOnly, panLaw, 1, pan);
        jassert(ready == dsp::PreparedBalance::Result::ok);
        if (ready != dsp::PreparedBalance::Result::ok)
            throw std::logic_error(
                "automationGain.runtime.invalidBalance: Valid balance preparation required");
        runtime->activeRate = info.sampleRate;
        runtime->insert = insert;
        runtime->law = panLaw;
        attachment.complete(runtime->lifecycle, info.sampleRate, info.blockSizeSamples, mode);
    }
    void deinitialise() override
    {
        detachRuntime();
    }
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    float displayGain() const noexcept
    {
        return runtime->reportedGain.load(std::memory_order_relaxed);
    }
    MixerMeterReading meterReading() const noexcept
    {
        return runtime->meter.read();
    }
    std::uint64_t clearOverload() noexcept
    {
        return runtime->meter.clearOverload();
    } // Control thread only.
    std::uint64_t graphInitialisations() const noexcept
    {
        return initialisations.load();
    }
    void applyToBuffer(const tracktion::PluginRenderContext &fc) override
    {
        if (!fc.destBuffer || !timeline || runtime->activeRate <= 0 || fc.destBuffer->getNumChannels() <= 0)
            return;
        auto next = timeline->acquire();
        if (next.document)
            document = std::move(next);
        if (!document.document)
            return;
        const auto &doc = *document.document;
        const auto owner = std::lower_bound(doc.mixer.begin(), doc.mixer.end(), insert,
                                            [](const auto &m, int id) { return m.index < id; });
        if (owner == doc.mixer.end() || owner->index != insert)
            return;
        runtime->manual.setTargetValue(owner->gain);
        runtime->mute.setTargetValue(owner->muted ? 0.f : 1.f);
        runtime->balance.setManualTarget(1, owner->pan, fc.destBuffer->getNumChannels());
        runtime->stereo.setTarget({owner->stereoSeparation, owner->phaseInverted, owner->stereoSwapped});
        runtime->eq.setTarget(owner->eq);
        {
            // Integrated EQ: post-effects, before the stereo image, pan and fader stages.
            std::array<float *, 8> channels{};
            const int count = std::min(fc.destBuffer->getNumChannels(), 8);
            for (int ch = 0; ch < count; ++ch)
                channels[size_t(ch)] = fc.destBuffer->getWritePointer(ch, fc.bufferStartSample);
            runtime->eq.process(channels.data(), count, fc.bufferNumSamples);
            // Track delay: read `delay` samples behind the write head (fixed prepared line).
            const int capacity = runtime->delayCapacity;
            const int delay = juce::jlimit(0, std::max(0, capacity - 2),
                                           int(std::lround(owner->delayMs * .001 * runtime->activeRate)));
            if (capacity > 0)
                for (int frame = 0; frame < fc.bufferNumSamples; ++frame)
                {
                    const int read = (runtime->delayWrite - delay + capacity) % capacity;
                    for (int ch = 0; ch < std::min(count, 2); ++ch)
                    {
                        float *line = runtime->delayLine.data() + size_t(ch) * size_t(capacity);
                        line[runtime->delayWrite] = channels[size_t(ch)][frame];
                        channels[size_t(ch)][frame] = line[read];
                    }
                    runtime->delayWrite = (runtime->delayWrite + 1) % capacity;
                }
        }
        const auto *curve = (fc.isPlaying || fc.isRendering)
                                ? doc.automation.find({AutomationParameter::mixerGain, insert, 0})
                                : nullptr;
        const auto *panCurve = (fc.isPlaying || fc.isRendering)
            ? doc.automation.find({AutomationParameter::mixerPan, insert, 0}) : nullptr;
        const auto *stereoCurve = (fc.isPlaying || fc.isRendering)
            ? doc.automation.find({AutomationParameter::mixerStereoSeparation, insert, 0}) : nullptr;
        const double beat = doc.tempoMap.beatAt(fc.editTime.getStart().inSeconds());
        const double step = doc.tempoMap.bpmAt(beat) / (60. * sampleRate);
        // Immutable lane lookup and polynomial arithmetic only. No model traversal or resource deletion.
        float displayed = runtime->manual.getCurrentValue();
        for (int frame = 0; frame < fc.bufferNumSamples; ++frame)
        {
            const float manualGain = runtime->manual.getNextValue();
            displayed = curve ? curve->valueAt(beat + frame * step) : manualGain;
            const float gain = displayed * runtime->mute.getNextValue();
            dsp::PreparedBalance::Coefficients pan;
            runtime->balance.next(fc.destBuffer->getNumChannels(), pan);
            if (panCurve)
                runtime->balance.automated(1, panCurve->valueAt(beat + frame * step), pan);
            // Stereo image stage is post-effects and pre-pan/fader/meter. Both
            // originals are read before either channel changes; no graph edit.
            if (fc.destBuffer->getNumChannels() >= 2)
            {
                auto &left = fc.destBuffer->getWritePointer(0, fc.bufferStartSample)[frame];
                auto &right = fc.destBuffer->getWritePointer(1, fc.bufferStartSample)[frame];
                if (stereoCurve)
                    runtime->stereo.processAutomated(left, right, stereoCurve->valueAt(beat + frame * step));
                else
                    runtime->stereo.process(left, right);
            }
            for (int ch = 0; ch < fc.destBuffer->getNumChannels(); ++ch)
                fc.destBuffer->getWritePointer(ch, fc.bufferStartSample)[frame] *=
                    gain * (ch == 0   ? pan.left
                            : ch == 1 ? pan.right
                                      : pan.extra);
        }
        runtime->reportedGain.store(displayed, std::memory_order_relaxed);
        runtime->meter.publish(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, sampleRate);
        if (runtime->insert == 0 && !fc.isRendering) // Master post-fader into the audio logger (live only).
            AudioLogger::instance().write(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, sampleRate);
        {
            // Envelope follower source: block peak through 10 ms attack / 150 ms release.
            float peak = 0;
            for (int ch = 0; ch < fc.destBuffer->getNumChannels(); ++ch)
                peak = std::max(peak, fc.destBuffer->getMagnitude(ch, fc.bufferStartSample, fc.bufferNumSamples));
            const double seconds = fc.bufferNumSamples / std::max(1., sampleRate);
            const float coefficient = float(1 - std::exp(-seconds / (peak > runtime->follow ? .01 : .15)));
            runtime->follow += (std::min(1.f, peak) - runtime->follow) * coefficient;
            InsertLevels::instance().publish(runtime->insert, runtime->follow);
        }
        if (runtime->insert == 0 && !fc.isRendering) // Monitor volume: last, live Master output only.
            MonitorVolume::instance().apply(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples);
    }
};
} // namespace vibera
