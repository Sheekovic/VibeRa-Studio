// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../src/EffectChain.h"
#include "../src/PluginHost.h"
#include "../src/effects/NativeEffectsRegistry.h"
#include "../src/synth/SynthRegistry.h"
#include <array>
#include <set>
#include <stdexcept>
#include <limits>

namespace vibera::acceptance::nativeEffectModel
{
// Existing model/portable-chain cases retain opaque test bytes. The additional
// worker-meter case below uses a real original built-in effect and actual Host
// processing; it does not qualify native OS input, listening or physical output.
inline void verifyMeter(const std::function<void(bool, const char *)> &check)
{
    PluginNativeMeterTelemetry telemetry;
    check(telemetry.read().status == PluginNativeMeterStatus::unsupported,
          "Absent native meter capability is unsupported, not a fabricated zero measurement");
    telemetry.publish(PluginNativeMeterStatus::notProcessed);
    check(telemetry.read().status == PluginNativeMeterStatus::notProcessed,
          "Native capability without actual processing has explicit notProcessed state");
    check(juce::String(pluginNativeMeterStatusName(PluginNativeMeterStatus::unsupported)) == "unsupported" &&
              juce::String(pluginNativeMeterStatusName(PluginNativeMeterStatus::notProcessed)) ==
                  "notProcessed" &&
              juce::String(pluginNativeMeterStatusName(PluginNativeMeterStatus::ready)) == "ready" &&
              juce::String(pluginNativeMeterStatusName(PluginNativeMeterStatus::invalid)) == "invalid" &&
              juce::String(pluginNativeMeterStatusName(PluginNativeMeterStatus::unavailable)) ==
                  "unavailable",
          "Semantic meter status names are stable and distinguish availability from silence");
    telemetry.publish(PluginNativeMeterStatus::ready, .125f, .5f);
    check(telemetry.peaks.load() == 0x3f0000003e000000ULL && telemetry.read().leftPeak == .125f &&
              telemetry.read().rightPeak == .5f,
          "Stereo meter transport retains independently specified exact float pair bits");
    telemetry.publish(PluginNativeMeterStatus::ready, 2.f, .25f);
    check(telemetry.read().status == PluginNativeMeterStatus::ready && telemetry.read().leftPeak == 2.f,
          "Real over-unity sample peak remains available rather than being silently clipped");
    for (const float invalid :
         {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        telemetry.publish(PluginNativeMeterStatus::ready, invalid, .5f);
        check(telemetry.read().status == PluginNativeMeterStatus::invalid,
              "Invalid native peak is explicit and never a successful zero meter");
        telemetry.publish(PluginNativeMeterStatus::ready, .125f, invalid);
        check(telemetry.read().status == PluginNativeMeterStatus::invalid,
              "Both native output channels reject malformed peak values");
    }
    telemetry.status.store(static_cast<PluginNativeMeterStatus>(999));
    check(telemetry.read().status == PluginNativeMeterStatus::invalid,
          "Unknown meter status refuses rather than becoming a ready measurement");
    PluginHost host;
    check(host.nativeOutputMeter().status == PluginNativeMeterStatus::unavailable,
          "Unstarted Host meter is unavailable");
    PluginSpec balance;
    balance.id = 1;
    balance.name = effects::nativeDescription(effects::EffectId::balance).name;
    balance.descriptor = effects::nativeDescription(effects::EffectId::balance).createXml()->toString();
    check(host.start(balance, 48000, 256, true).wasOk(), "Actual original Balance worker starts offline");
    check(host.nativeOutputMeter().status == PluginNativeMeterStatus::notProcessed &&
              host.statistics().processed == 0 && !host.editorWindowState().exists,
          "Fresh worker meter has no audio work and requires no native editor");
    juce::AudioBuffer<float> pcm(2, 3 * 256);
    for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
    {
        pcm.setSample(0, frame, .125f);
        pcm.setSample(1, frame, .5f);
    }
    for (int offset = 0; offset < pcm.getNumSamples(); offset += 256)
        host.process(pcm, offset, 256, nullptr, 0, double(offset) / 48000., 120, true, false);
    for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
        check(pcm.getSample(0, frame) == (frame < 256 ? 0.f : .125f) &&
                  pcm.getSample(1, frame) == (frame < 256 ? 0.f : .5f),
              "Actual Balance Host preserves literal one-quantum startup and known asymmetric PCM");
    const auto began = juce::Time::getMillisecondCounterHiRes();
    auto meter = host.nativeOutputMeter();
    while (meter.status != PluginNativeMeterStatus::ready &&
           juce::Time::getMillisecondCounterHiRes() - began < 2000.0)
    {
        juce::Thread::sleep(5); // CONTROL-only telemetry wait; no callback or worker DSP waits added.
        meter = host.nativeOutputMeter();
    }
    check(meter.status == PluginNativeMeterStatus::ready && meter.leftPeak == .125f &&
              meter.rightPeak == .5f && host.statistics().processed >= 2 && !host.editorWindowState().exists,
          "Real worker CONTROL telemetry reports actual individual native output with editor closed");
    check(!host.deliveryFailed() && host.statistics().missed == 0 && host.statistics().dropped == 0,
          "Original offline meter fixture has no lost audio packets");
    PluginHost refused;
    PluginBusRequest unsupported;
    unsupported.mainInputBus = 1;
    check(refused.start(balance, 48000, 256, true, {}, unsupported).failed() &&
              refused.nativeOutputMeter().status == PluginNativeMeterStatus::unavailable,
          "Refused native bus preparation cannot expose a healthy output meter");
}

inline juce::Result verify()
{
    try
    {
        const auto check = [](bool condition, const char *message)
        {
            if (!condition)
                throw std::runtime_error(message);
        };
        const auto descriptions = effects::nativeDescriptions();
        check(descriptions.size() == 25, "All 25 original native effects are individually described");
        std::set<juce::String> identifiers;
        constexpr std::array<unsigned char, 5> opaque{0, 0x5a, 0xff, 1, 0x3f};
        const auto state = juce::MemoryBlock(opaque.data(), opaque.size()).toBase64Encoding();
        const auto specFor = [&](Project &project, const juce::PluginDescription &description)
        {
            PluginSpec spec;
            spec.id = project.allocate();
            spec.name = description.name;
            spec.descriptor = description.createXml()->toString();
            spec.state = state;
            spec.bypass = true;
            spec.wet = 0.375f;
            return spec;
        };
        for (const auto &description : descriptions)
        {
            check(identifiers.insert(description.createIdentifierString()).second &&
                      effects::isNativeDescription(description),
                  "Each native effect has an independent validated persistent identity");
            auto project = Project::empty();
            project.mixer[1].effects.push_back(specFor(project, description));
            check(project.validate().wasOk(), "Canonical native effect is accepted in a Mixer slot");
            const auto saved = project.state();
            Project reopened;
            check(Project::parse(saved, reopened).wasOk() && reopened.state().isEquivalentTo(saved) &&
                      reopened.mixer[1].effects.front() == project.mixer[1].effects.front(),
                  "Native project roundtrip preserves identity, opaque state, bypass and host wet");

            effect_chain::Chain chain{
                description.name,
                true,
                {{description.name, description.createXml()->toString(), state, true, 0.375f}}};
            std::unique_ptr<juce::MemoryBlock> bytes;
            check(effect_chain::encode(chain, bytes).wasOk(), "Native chain encodes without installation");
            std::unique_ptr<effect_chain::Chain> decoded;
            check(effect_chain::decode({static_cast<const std::byte *>(bytes->getData()), bytes->getSize()},
                                       decoded)
                          .wasOk() &&
                      *decoded == chain,
                  "Native chain roundtrip retains exact descriptor/state and mixing controls");
        }

        auto mixedProject = Project::demo();
        juce::PluginDescription external;
        external.name = "Original absent VST3 model fixture";
        external.pluginFormatName = "VST3";
        external.fileOrIdentifier = "original-model-fixture-not-installed.vst3";
        external.numInputChannels = external.numOutputChannels = 2;
        mixedProject.mixer[1].effects.push_back(specFor(mixedProject, descriptions.front()));
        mixedProject.mixer[1].effects.push_back(specFor(mixedProject, external));
        mixedProject.mixer[1].effects.push_back(specFor(mixedProject, descriptions.back()));
        external.isInstrument = true;
        auto instrument = specFor(mixedProject, external);
        instrument.instrument = true;
        instrument.wet = 1.0f;
        mixedProject.channels.front().instrument = instrument;
        check(mixedProject.validate().wasOk(), "VST3 instruments and mixed native/VST3 effects remain valid");
        Project mixedReopened;
        check(Project::parse(mixedProject.state(), mixedReopened).wasOk() &&
                  mixedReopened.state().isEquivalentTo(mixedProject.state()),
              "Mixed native/VST3 project retains compatibility without any installed external plugin");

        effect_chain::Chain mixedChain{"Mixed original effects", false, {}};
        for (const auto &spec : mixedProject.mixer[1].effects)
            mixedChain.effects.push_back({spec.name, spec.descriptor, spec.state, spec.bypass, spec.wet});
        std::unique_ptr<juce::MemoryBlock> mixedBytes;
        check(effect_chain::encode(mixedChain, mixedBytes).wasOk(),
              "Mixed native/VST3 portable chain encodes");
        std::unique_ptr<effect_chain::Chain> mixedDecoded;
        check(
            effect_chain::decode(
                {static_cast<const std::byte *>(mixedBytes->getData()), mixedBytes->getSize()}, mixedDecoded)
                    .wasOk() &&
                *mixedDecoded == mixedChain,
            "Mixed portable chain preserves exact native/external order and data");

        auto nativeInstrument = mixedProject;
        nativeInstrument.channels.front().instrument->descriptor =
            descriptions.front().createXml()->toString();
        check(nativeInstrument.validate().failed(), "Native effects cannot be repurposed as instruments");

        auto nativeSynthProject = Project::empty();
        auto nativeSynthSpec = specFor(nativeSynthProject, synth::nativeDescription());
        nativeSynthSpec.instrument = true;
        nativeSynthSpec.bypass = false;
        nativeSynthSpec.wet = 1.0f;
        nativeSynthSpec.state.clear(); // A newly created instrument uses its native defaults.
        nativeSynthProject.channels.front().instrument = nativeSynthSpec;
        check(nativeSynthProject.validate().wasOk(),
              "Canonical native Synth instrument is accepted in a Channel");
        const auto nativeSynthSaved = nativeSynthProject.state();
        Project nativeSynthReopened;
        check(Project::parse(nativeSynthSaved, nativeSynthReopened).wasOk() &&
                  nativeSynthReopened.state().isEquivalentTo(nativeSynthSaved) &&
                  *nativeSynthReopened.channels.front().instrument == nativeSynthSpec,
              "Native Synth Channel roundtrip retains its distinct descriptor and instrument role");

        auto synthAsEffect = mixedProject;
        synthAsEffect.mixer[1].effects.front().descriptor =
            synth::nativeDescription().createXml()->toString();
        check(synthAsEffect.validate().failed() &&
                  Project::parse(synthAsEffect.state(), nativeSynthReopened).failed() &&
                  nativeSynthReopened.state().isEquivalentTo(nativeSynthSaved),
              "Native Synth cannot occupy a Mixer effect slot or replace a valid parsed project");

        auto unknownNative = synth::nativeDescription();
        unknownNative.fileOrIdentifier = "vibera.instrument.unknown";
        auto unknownInstrument = nativeSynthProject;
        unknownInstrument.channels.front().instrument->descriptor = unknownNative.createXml()->toString();
        check(unknownInstrument.validate().failed() &&
                  Project::parse(unknownInstrument.state(), nativeSynthReopened).failed() &&
                  nativeSynthReopened.state().isEquivalentTo(nativeSynthSaved),
              "Unknown native instrument identity refuses atomically without a format fallback");

        const auto unchangedProject = mixedReopened.state();
        const auto *unchangedBytes = mixedBytes.get();
        for (int variant = 0; variant < 14; ++variant)
        {
            auto spoof = descriptions.front();
            switch (variant)
            {
            case 0:
                spoof.name += " altered";
                break;
            case 1:
                spoof.descriptiveName += " altered";
                break;
            case 2:
                spoof.category += " altered";
                break;
            case 3:
                spoof.manufacturerName = "Another publisher";
                break;
            case 4:
                spoof.version = "unrecognized";
                break;
            case 5:
                spoof.fileOrIdentifier = "vibera.effect.unknown";
                break;
            case 6:
                ++spoof.uniqueId;
                break;
            case 7:
                ++spoof.deprecatedUid;
                break;
            case 8:
                spoof.isInstrument = true;
                break;
            case 9:
                spoof.numInputChannels = 1;
                break;
            case 10:
                spoof.numOutputChannels = 1;
                break;
            case 11:
                spoof.hasSharedContainer = true;
                break;
            case 12:
                spoof.hasARAExtension = true;
                break;
            case 13:
                spoof.pluginFormatName = "Unsupported original format";
                break;
            }
            check(!effects::isNativeDescription(spoof),
                  "Altered native metadata is rejected by the registry");
            auto invalid = mixedProject;
            invalid.mixer[1].effects.front().descriptor = spoof.createXml()->toString();
            check(invalid.validate().failed() && Project::parse(invalid.state(), mixedReopened).failed() &&
                      mixedReopened.state().isEquivalentTo(unchangedProject),
                  "Invalid native descriptor is rejected without replacing an existing parsed project");
            auto invalidChain = mixedChain;
            invalidChain.effects.front().descriptor = spoof.createXml()->toString();
            check(effect_chain::validate(invalidChain).failed() &&
                      effect_chain::encode(invalidChain, mixedBytes).failed() &&
                      mixedBytes.get() == unchangedBytes,
                  "Invalid native descriptor cannot enter a chain or replace an existing encoded output");
        }
        verifyMeter(check);
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::nativeEffectModel
