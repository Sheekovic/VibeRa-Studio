// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ChannelPreset.h"
#include "Commands.h"
#include "synth/SynthRegistry.h"

// Model/file/history tests for portable sampler Channel presets. No audio claim.
static void channelPresetModelTests(const juce::File &folder)
{
    using namespace vibera;
    auto project = Project::empty();
    auto &source = project.channels[0];
    auto &target = project.channels[1];
    source.name = "PUNCHY KICK";
    source.gain = .55f;
    source.pan = -.25f;
    source.sampler.root = 48;
    source.sampler.cents = 35;
    source.sampler.reverse = true;
    source.sampler.mono = true;
    source.sampler.slideTime = .2;
    source.swingMix = .5;
    source.icon = "none";
    source.mixer = 3;
    target.mixer = 5;
    target.muted = true;
    const auto targetId = target.id;
    const auto targetMixer = target.mixer;
    auto &pattern = project.patterns.front();
    pattern.notes.push_back({project.allocate(), targetId, 0, .25, 60, .8f, true});
    require(project.validate().wasOk(), "Preset fixture is valid");

    juce::String xml;
    require(encodeChannelPreset(project, source.id, xml).wasOk(), "Encode sampler Channel preset");
    const auto file = folder.getChildFile("punchy.vibch");
    require(file.replaceWithText(xml) && file.loadFileAsString() == xml, "Preset file round trip");
    Channel decoded;
    require(decodeChannelPreset(file.loadFileAsString(), decoded).wasOk(), "Decode sampler Channel preset");
    require(decoded.id == 0 && decoded.mixer == 0 && !decoded.muted && decoded.name == "PUNCHY KICK" &&
                decoded.source == source.source && decoded.gain == .55f && decoded.pan == -.25f &&
                decoded.sampler.root == 48 && decoded.sampler.cents == 35 && decoded.sampler.reverse &&
                decoded.sampler.mono && decoded.sampler.slideTime == .2 && decoded.swingMix == .5,
            "Decoded preset retains sound settings and drops routing/mute");

    Controller controller;
    controller.replace(project);
    const auto baseline = controller.project.state();
    CommandArgs args;
    args.channel = targetId;
    args.text = xml;
    require(controller.execute("channel.preset", args).wasOk(), "Apply preset through canonical command");
    const auto *applied = controller.project.channel(targetId);
    require(applied && applied->name == "PUNCHY KICK" && applied->sampler.root == 48 &&
                applied->sampler.reverse && applied->gain == .55f && applied->mixer == targetMixer &&
                applied->muted && controller.project.patterns.front().notes.size() == 1,
            "Preset applies sound settings, preserves routing/mute/score");
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
            "Preset apply is one exact Undo step");
    require(controller.redoEditChecked().wasOk() &&
                controller.project.channel(targetId)->sampler.root == 48,
            "Preset apply Redo");

    const auto revision = controller.revision;
    args.text = "<VIBERA_CHANNEL_PRESET format=\"99\"/>";
    require(controller.execute("channel.preset", args).failed() && controller.revision == revision,
            "Unsupported preset format refuses without mutation");
    args.text = "not xml";
    require(controller.execute("channel.preset", args).failed() && controller.revision == revision,
            "Malformed preset refuses without mutation");
    args.text = xml;
    args.channel = 999999;
    require(controller.execute("channel.preset", args).failed() && controller.revision == revision,
            "Missing target refuses without mutation");

    auto clip = project;
    clip.channels[2].audioClip = true;
    clip.channels[2].audioDuration = 1;
    juce::String refused;
    require(encodeChannelPreset(clip, clip.channels[2].id, refused).failed(),
            "Audio Clip Channel is not a sampler preset source");

    // Instrument presets carry the captured native state with a fresh plugin ID.
    {
        auto withSynth = Project::empty();
        auto &synthChannel = withSynth.channels[0];
        PluginSpec spec;
        spec.id = withSynth.allocate();
        spec.name = "VibeRa Synthesizer";
        spec.descriptor = synth::nativeDescription().createXml()->toString();
        spec.state = juce::MemoryBlock("state", 5).toBase64Encoding(); // Opaque bytes, kept exactly.
        spec.instrument = true;
        synthChannel.instrument = spec;
        synthChannel.name = "PAD";
        require(withSynth.validate().wasOk(), "Instrument fixture valid: " + withSynth.validate().getErrorMessage().toStdString());
        juce::String instrumentXml;
        require(encodeChannelPreset(withSynth, synthChannel.id, instrumentXml).wasOk(),
                "Encode instrument Channel preset");
        Channel decodedInstrument;
        require(decodeChannelPreset(instrumentXml, decodedInstrument).wasOk() && decodedInstrument.instrument &&
                    decodedInstrument.instrument->state == spec.state &&
                    decodedInstrument.instrument->descriptor == spec.descriptor,
                "Decoded instrument preset keeps descriptor and exact state");
        Controller apply;
        apply.replace(withSynth);
        const auto target = withSynth.channels[1].id;
        const auto before = apply.project.state();
        CommandArgs instrumentArgs;
        instrumentArgs.channel = target;
        instrumentArgs.text = instrumentXml;
        require(apply.execute("channel.preset", instrumentArgs).wasOk(), "Apply instrument preset to sampler");
        const auto *loaded = apply.project.channel(target);
        require(loaded && loaded->instrument && loaded->instrument->state == spec.state &&
                    loaded->instrument->id != spec.id && loaded->name == "PAD",
                "Target gets the instrument with a fresh plugin ID");
        require(apply.undoEditChecked().wasOk() && apply.project.state().isEquivalentTo(before),
                "Instrument preset apply is one Undo step");
        // A sampler preset replaces an instrument Channel's plugin with the sampler sound.
        CommandArgs back;
        back.channel = synthChannel.id;
        back.text = xml;
        require(apply.execute("channel.preset", back).wasOk() && !apply.project.channel(synthChannel.id)->instrument,
                "Sampler preset turns an instrument Channel back into a sampler");
    }
    folder.getChildFile("channel-preset-verification.txt")
        .replaceWithText("Channel preset encode/decode/apply/undo/redo/refusals passed. Model only.\n");
}
