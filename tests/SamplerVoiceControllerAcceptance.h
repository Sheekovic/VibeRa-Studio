// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Studio.h"
#include "../src/McpSchema.h"
#include "../src/synth/SynthRegistry.h"
#include <stdexcept>

// Invoke only inside the existing timer-stopped, hardware-disabled controller fixture.
// Actual CONTROL ingress/Studio canonical sink; no PCM/hardware/native-input claim.
namespace vibera::test::sampler_voice_controller
{
inline juce::Result verify(Studio &studio, const juce::File &folder)
{
    if (!studio.controllerTestMode || studio.midiController.bindingChangesPersisted() ||
        studio.audio.playing() || folder.exists())
        return juce::Result::fail(
            "Voice mapping acceptance requires a fresh owned isolated stopped controller fixture");
    if (auto result = folder.createDirectory(); result.failed())
        return result;
    const auto savedProject = studio.controller.project;
    const auto savedProfile = juce::JSON::parse(juce::JSON::toString(studio.midiController.state()));
    const auto savedChannel = studio.selectedChannel, savedPattern = studio.selectedPattern;
    juce::String failure;
    juce::Array<juce::var> checks;
    const auto check = [&](bool condition, const char *why)
    {
        if (!condition)
            throw std::runtime_error(why);
        checks.add(why);
    };
    const auto object = [](std::initializer_list<std::pair<const char *, juce::var>> fields)
    {
        auto value = juce::var(new juce::DynamicObject);
        for (const auto &field : fields)
            value.getDynamicObject()->setProperty(field.first, field.second);
        return value;
    };
    const auto call = [&](const char *tool, const juce::var &args)
    { return studio.mcpCall(tool, args)["structuredContent"]; };
    const auto command = [&](const char *id, juce::var args)
    { return call("vibera_command", object({{"command", id}, {"args", args}})); };
    try
    {
        auto seed = Project::empty();
        const auto first = seed.channels[0].id, second = seed.channels[1].id;
        studio.controller.replace(seed);
        studio.selectedChannel = first;
        studio.selectedPattern = seed.patterns.front().id;
        studio.refresh();
        const char *actions[]{"channel.sampler.mono", "channel.sampler.portamento",
                              "channel.sampler.slideTime", "channel.sampler.gateBeats",
                              "channel.sampler.fullPortamento"};
        std::vector<ControllerBinding> bindings;
        for (int i = 0; i < 5; ++i)
        {
            const bool flag = i == 0 || i == 1 || i == 4;
            ControllerBinding b;
            b.action = actions[i];
            b.label = "Original voice parameter";
            b.port = 1;
            b.channel = 1;
            b.number = 41 + i;
            b.mode = 1;
            b.minimum = 0;
            b.maximum = flag ? 1 : i == 2 ? 10 : 64;
            b.pickup = false;
            check(MidiController::parameterActions().contains(b.action) && MidiController::valid(b),
                  "All five canonical controls are genuine continuous global mapping actions");
            auto invalid = b;
            invalid.minimum = -.01;
            check(!MidiController::valid(invalid), "Mapping refuses negative physical minimum");
            invalid = b;
            invalid.maximum += 1;
            check(!MidiController::valid(invalid),
                  "Mapping refuses physical maximum beyond canonical domain");
            invalid = b;
            invalid.mode = 0;
            check(MidiController::valid(invalid) == flag, "Only boolean controls admit safe press toggles");
            invalid = b;
            invalid.mode = 2;
            check(MidiController::valid(invalid) != flag,
                  "Relative physical controls remain numeric; boolean relative ambiguity refuses");
            bindings.push_back(b);
        }
        const auto configure = [&]
        {
            auto profile = juce::JSON::parse(juce::JSON::toString(studio.midiController.state()));
            auto *p = profile.getDynamicObject();
            p->setProperty("input", "");
            p->setProperty("output", "");
            juce::Array<juce::var> rows;
            for (const auto &b : bindings)
                rows.add(object({{"action", b.action},
                                 {"label", b.label},
                                 {"port", b.port},
                                 {"channel", b.channel},
                                 {"kind", b.kind},
                                 {"number", b.number},
                                 {"layer", b.layer},
                                 {"mode", b.mode},
                                 {"mixer", b.mixer},
                                 {"minimum", b.minimum},
                                 {"maximum", b.maximum},
                                 {"pickup", b.pickup},
                                 {"banked", b.banked},
                                 {"enabled", b.enabled},
                                 {"nativeArea", b.nativeArea}}));
            p->setProperty("bindings", rows);
            call("vibera_controller", object({{"action", "configure"}, {"profile", profile}}));
        };
        const auto inject = [&](int cc, int value)
        {
            return call("vibera_controller", object({{"action", "inject"},
                                                     {"port", 1},
                                                     {"channel", 1},
                                                     {"kind", 0},
                                                     {"number", cc},
                                                     {"value", value}}));
        };
        const auto sample = [&](Id id) -> const SamplerSettings &
        { return studio.controller.project.channel(id)->sampler; };
        configure();
        for (int i : {0, 1, 4})
        {
            const auto read = [&]
            {
                return i == 0   ? sample(first).mono
                       : i == 1 ? sample(first).portamento
                                : sample(first).fullPortamento;
            };
            inject(41 + i, 63);
            check(!read(), "Boolean absolute CC63 is below threshold");
            inject(41 + i, 64);
            check(read(), "Boolean absolute CC64 is above threshold");
            inject(41 + i, 0);
            check(!read(), "Boolean absolute CC0 clears through canonical command");
        }
        const auto beforeSlide = studio.controller.project.state();
        inject(43, 64);
        check(std::abs(sample(first).slideTime - 10. * 64 / 127.) < 1e-12,
              "Actual ingress and canonical sink map literal physical Slide seconds");
        const auto afterSlide = studio.controller.project.state();
        command("edit.undo", object({}));
        check(studio.controller.project.state().isEquivalentTo(beforeSlide),
              "Mapped physical edit has exact Undo");
        command("edit.redo", object({}));
        check(studio.controller.project.state().isEquivalentTo(afterSlide),
              "Mapped physical edit has exact Redo");
        inject(44, 127);
        check(sample(first).gateBeats == 64, "Gate maps CC127 to literal64 quarter-note beats");
        inject(44, 0);
        check(sample(first).gateBeats == 0, "Gate CC0 restores disabled endpoint");
        command("channel.sampler.slideTime", object({{"channel", juce::int64(first)}, {"value", 2.5}}));
        bindings[2].pickup = true;
        configure();
        const auto revision = studio.controller.revision;
        inject(43, 0);
        check(sample(first).slideTime == 2.5 && studio.controller.revision == revision,
              "Pickup reads actual physical current value and refuses an uncaught extreme without history");
        inject(43, 64);
        check(std::abs(sample(first).slideTime - 10. * 64 / 127.) < 1e-12,
              "Crossing pickup catches actual seconds");
        inject(43, 127);
        check(sample(first).slideTime == 10, "Caught pickup retains its physical endpoint");
        bindings[0].pickup = true;
        configure();
        inject(41, 0);
        inject(41, 64);
        inject(41, 0);
        check(!sample(first).mono,
              "Boolean pickup records canonical quantized state, so subsequent false does not get stranded");
        call("vibera_select",
             object({{"channel", juce::int64(second)}, {"pattern", juce::int64(seed.patterns.front().id)}}));
        bindings[2].pickup = false;
        configure();
        inject(43, 64);
        check(sample(first).slideTime == 10 && std::abs(sample(second).slideTime - 10. * 64 / 127.) < 1e-12,
              "Global mappings deliberately follow selected Channel; no raw fixed Project IDs enter global "
              "profile");
        bindings = {bindings.front()};
        bindings[0].mode = 0;
        bindings[0].pickup = false;
        configure();
        inject(41, 127);
        check(sample(second).mono, "Actual press edge toggles Mono on");
        const auto pressedRevision = studio.controller.revision;
        inject(41, 127);
        check(sample(second).mono && studio.controller.revision == pressedRevision,
              "Repeated held button cannot toggle twice");
        inject(41, 0);
        inject(41, 127);
        check(!sample(second).mono, "Release rearms exact next press toggle");
        inject(41, 0);
        const auto beforeBusy = studio.controller.project.state();
        const juce::String pointerOwner = "original-voice-controller-pointer";
        check(studio.controller.beginGesture(pointerOwner),
              "Own an independent pointer edit before mapped input");
        bool busyRefused = false;
        try
        {
            inject(41, 127);
        }
        catch (const McpFailure &error)
        {
            busyRefused = error.code == "controller.editBusy";
        }
        check(busyRefused && studio.controller.hasGesture() &&
                  studio.controller.project.state().isEquivalentTo(beforeBusy) &&
                  studio.controller.cancelGestureChecked(pointerOwner).wasOk(),
              "Mapped toggle refuses instead of stealing a foreign edit gesture");
        inject(41, 0);
        auto inbox = std::make_unique<MidiControlInput>();
        auto reloaded = std::make_unique<MidiController>(*inbox, false);
        const auto profile = studio.midiController.state();
        check(reloaded->importState(profile).wasOk(),
              "Existing profile1 imports known voice actions without migration");
        const auto mappingFile = folder.getChildFile("original-voice-controller-profile.json");
        juce::String identity;
        check(reloaded->bindingProfileIdentity(mappingFile, identity).wasOk() &&
                  reloaded->setBinding(0, bindings[0], identity, mappingFile).wasOk() &&
                  mappingFile.existsAsFile(),
              "Owned actual profile save uses canonical atomic binding persistence");
        auto secondInbox = std::make_unique<MidiControlInput>();
        auto fileReload = std::make_unique<MidiController>(*secondInbox, false);
        check(fileReload->importState(juce::JSON::parse(mappingFile.loadFileAsString())).wasOk() &&
                  juce::JSON::toString(fileReload->state()) == juce::JSON::toString(reloaded->state()),
              "Real profile file preserves action/range/pickup without ports or project target fields");
        // Bounded original valid media: unavailable means unsupported role, not a missing file.
        const auto originalAudio = folder.getChildFile("original-audio-role.wav");
        juce::MemoryOutputStream wav;
        wav.write("RIFF", 4);
        wav.writeInt(36 + 128);
        wav.write("WAVEfmt ", 8);
        wav.writeInt(16);
        wav.writeShort(1);
        wav.writeShort(1);
        wav.writeInt(48000);
        wav.writeInt(96000);
        wav.writeShort(2);
        wav.writeShort(16);
        wav.write("data", 4);
        wav.writeInt(128);
        for (int i = 0; i < 64; ++i)
            wav.writeShort(short((i % 2) ? 4000 : -3000));
        check(originalAudio.replaceWithData(wav.getData(), wav.getDataSize()),
              "Write owned original valid Audio Clip role source");
        for (bool clip : {true, false})
        {
            auto unavailable = seed;
            auto *target = unavailable.channel(first);
            if (clip)
            {
                target->audioClip = true;
                target->audioDuration = 64. / 48000.;
                target->source = originalAudio.getFullPathName();
            }
            else
            {
                const auto description = synth::nativeDescription();
                target->instrument = PluginSpec{
                    unavailable.allocate(), description.name, description.createXml()->toString(), {}, true};
            }
            studio.controller.replace(unavailable);
            studio.selectedChannel = first;
            studio.selectedPattern = seed.patterns.front().id;
            bindings[0].mode = 1;
            configure();
            const auto before = studio.controller.project.state();
            const auto oldRevision = studio.controller.revision;
            bool refused = false;
            try
            {
                inject(41, 127);
            }
            catch (const McpFailure &error)
            {
                refused = error.code == "samplerVoice.unsupportedTarget";
            }
            check(refused && studio.controller.project.state().isEquivalentTo(before) &&
                      studio.controller.revision == oldRevision,
                  "Mapped parameter has actual typed canonical unsupported-target refusal and no mutation");
            bindings[0].mode = 0;
            configure();
            refused = false;
            try
            {
                inject(41, 127);
            }
            catch (const McpFailure &error)
            {
                refused = error.code == "samplerVoice.unsupportedTarget";
            }
            check(refused && studio.controller.project.state().isEquivalentTo(before),
                  "Mapped toggle also refuses unsupported role via canonical command");
        }
    }
    catch (const std::exception &error)
    {
        failure = error.what();
    }
    // This is a disposable controller fixture, never a user-session restore API.
    const auto restoredProfile = studio.midiController.importState(savedProfile);
    studio.controller.replace(savedProject);
    studio.selectedChannel = savedChannel;
    studio.selectedPattern = savedPattern;
    studio.refresh();
    if (restoredProfile.failed())
        failure += "; fixture profile restore: " + restoredProfile.getErrorMessage();
    auto report = object({{"passed", failure.isEmpty()},
                          {"error", failure},
                          {"checks", checks},
                          {"hardware", false},
                          {"nativeInput", false},
                          {"listening", false},
                          {"pcmQualified", false}});
    if (!folder.getChildFile("sampler-voice-controller.json")
             .replaceWithText(juce::JSON::toString(report, true) + "\n"))
        return juce::Result::fail("Could not retain original voice controller result: " + failure);
    return failure.isEmpty() ? juce::Result::ok() : juce::Result::fail(failure);
}
} // namespace vibera::test::sampler_voice_controller
