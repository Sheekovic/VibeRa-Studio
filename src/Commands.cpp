// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Performance.h"
#include "synth/SynthRegistry.h"
#include "NativeInstruments.h"
#include "NativeInstruments.h"
#include "Commands.h"
#include "ScoreTools.h"
#include "Arrangements.h"
#include "StepActivationCommand.h"
#include "effects/NativeEffectsRegistry.h"
#include "NoteScript.h"
#include "MixerAutomationTake.h"
#include "PluginParameterState.h"
#include "MixerEffectOrder.h"
#include "EffectChain.h"
#include "MixerInsertOrder.h"
#include "MixerInsertLifecycle.h"
#include "MixerChainControls.h"
#include "MixerRouting.h"
#include "StoppedEffectChangePlan.h"
#include "MixerControls.h"
#include "MixerGroups.h"
#include "ClipGroups.h"
#include "AutomationCommands.h"
#include "StepSequence.h"
#include "PatternChannelLoops.h"
#include "PatternExtent.h"
#include "ChannelSolo.h"
#include "ChannelManagement.h"
#include "ChannelPreset.h"
#include "PatternManagement.h"
#include "ChannelAppearanceCommands.h"
#include "StepVelocity.h"
#include "StepPitch.h"
#include "StepExpression.h"
#include "StepShift.h"
#include "StepRepeat.h"
#include "StepRelease.h"
#include "SwingCommands.h"
#include "Quantizer.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <array>
#include <map>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vibera
{
juce::Result validateNotePropertiesPatch(const NotePropertiesPatch &patch)
{
    const auto in = [](const std::optional<double> &value, double low, double high)
    { return !value || (std::isfinite(*value) && *value >= low && *value <= high); };
    if (patch.empty() || (patch.colourGroup && (*patch.colourGroup < 0 || *patch.colourGroup > 15)) ||
        (patch.pitch && (*patch.pitch < 0 || *patch.pitch > 127)) || !in(patch.velocity, 0, 1) ||
        !in(patch.pan, -1, 1) || !in(patch.fineCents, -100, 100) || !in(patch.modX, 0, 1) ||
        !in(patch.modY, 0, 1) || !in(patch.releaseVelocity, 0, 1) || !in(patch.beat, 0, 1000000) ||
        !in(patch.length, 0, 1000000) || (patch.length && *patch.length <= 0))
        return juce::Result::fail(
            "noteMode.invalidProperties: Nonempty, finite, exact-domain Note Properties required");
    return juce::Result::ok();
}
juce::var notePropertiesState(const NotePropertiesPatch &patch)
{
    auto *out = new juce::DynamicObject;
    const auto put = [&](const char *name, const auto &value)
    {
        if (value)
            out->setProperty(name, *value);
    };
    put("slide", patch.slide);
    put("portamento", patch.portamento);
    put("muted", patch.muted);
    put("colourGroup", patch.colourGroup);
    put("pitch", patch.pitch);
    put("velocity", patch.velocity);
    put("pan", patch.pan);
    put("fineCents", patch.fineCents);
    put("modX", patch.modX);
    put("modY", patch.modY);
    put("releaseVelocity", patch.releaseVelocity);
    put("beat", patch.beat);
    put("length", patch.length);
    return juce::var(out);
}
juce::Result decodeNotePropertiesPatch(const juce::var &value, NotePropertiesPatch &output)
{
    const auto *object = value.getDynamicObject();
    if (!object || object->getProperties().size() == 0)
        return juce::Result::fail("noteMode.invalidProperties: Nonempty Note Properties object required");
    NotePropertiesPatch parsed;
    for (const auto &field : object->getProperties())
    {
        const auto name = field.name.toString();
        const auto &v = field.value;
        if (name == "slide" || name == "portamento" || name == "muted")
        {
            if (!v.isBool())
                return juce::Result::fail("noteMode.invalidProperties: Exact boolean required");
            if (name == "slide")
                parsed.slide = bool(v);
            else if (name == "portamento")
                parsed.portamento = bool(v);
            else
                parsed.muted = bool(v);
        }
        else
        {
            if ((!v.isInt() && !v.isInt64() && !v.isDouble()) || !std::isfinite(double(v)))
                return juce::Result::fail("noteMode.invalidProperties: Finite numeric property required");
            const double number = double(v);
            if (name == "colourGroup" || name == "pitch")
            {
                const double maximum = name == "pitch" ? 127 : 15;
                if (number < 0 || number > maximum || std::floor(number) != number)
                    return juce::Result::fail("noteMode.invalidProperties: Exact integer property required");
                if (name == "pitch")
                    parsed.pitch = int(number);
                else
                    parsed.colourGroup = int(number);
            }
            else if (name == "velocity")
                parsed.velocity = number;
            else if (name == "pan")
                parsed.pan = number;
            else if (name == "fineCents")
                parsed.fineCents = number;
            else if (name == "modX")
                parsed.modX = number;
            else if (name == "modY")
                parsed.modY = number;
            else if (name == "releaseVelocity")
                parsed.releaseVelocity = number;
            else if (name == "beat")
                parsed.beat = number;
            else if (name == "length")
                parsed.length = number;
            else
                return juce::Result::fail("noteMode.invalidProperties: Unknown Note property");
        }
    }
    if (auto valid = validateNotePropertiesPatch(parsed); valid.failed())
        return valid;
    output = std::move(parsed);
    return juce::Result::ok();
}
namespace
{
juce::Result applyChannelMixerAssignment(Project &project, const CommandArgs &args)
{
    auto *channel = project.channel(args.channel);
    if (!channel)
        return juce::Result::fail("Command target is unavailable");
    channel->mixer = args.mixer;
    return juce::Result::ok();
}
bool isSamplerControllerCommand(const juce::String &id)
{
    return id == "channel.sampler.controller.create" || id == "channel.sampler.controller.update" ||
           id == "channel.sampler.controller.remove";
}
juce::Result applySamplerControllerCommand(Project &project, const juce::String &id, const CommandArgs &args)
{
    auto &links = project.samplerControllerLinks;
    const auto existing =
        std::find_if(links.begin(), links.end(), [&](const auto &link) { return link.id == args.target; });
    if (id == "channel.sampler.controller.remove")
    {
        if (args.target <= 0 || existing == links.end() || args.samplerControllerLink ||
            !args.samplerControllerReplacements.empty() ||
            (args.channel != 0 && args.channel != existing->channel))
            return juce::Result::fail(
                "samplerController.invalidArguments: Existing link ID without payload required");
        links.erase(existing);
        return juce::Result::ok();
    }
    if (!args.samplerControllerLink)
        return juce::Result::fail("samplerController.invalidArguments: Complete typed link required");
    auto candidate = *args.samplerControllerLink;
    const bool create = id == "channel.sampler.controller.create";
    if ((create && (args.target != 0 || candidate.id != 0)) ||
        (!create && (args.target <= 0 || existing == links.end() || candidate.id != args.target)) ||
        (args.channel != 0 && args.channel != candidate.channel))
        return juce::Result::fail(
            "samplerController.invalidArguments: Exact create/update identity required");
    if (auto valid = validateSamplerControllerLink(candidate); valid.failed())
        return valid;
    const auto *owner = project.channel(candidate.channel);
    if (!owner)
        return juce::Result::fail("samplerController.missingChannel: Existing Channel required");
    if ((owner->instrument || owner->audioClip) &&
        (create || existing->channel != candidate.channel || existing->parameter != candidate.parameter))
        return juce::Result::fail("samplerController.unavailable: New target requires local sampler");
    std::set<Id> conflicts, replacements;
    for (const auto &link : links)
        if ((create || link.id != args.target) && samplerControllerSourceOverlaps(candidate, link))
            conflicts.insert(link.id);
    for (auto replacement : args.samplerControllerReplacements)
        if (replacement <= 0 || !replacements.insert(replacement).second)
            return juce::Result::fail(
                "samplerController.invalidReplacement: Distinct positive project link IDs required");
    if (conflicts != replacements)
        return juce::Result::fail(
            "samplerController.sourceConflict: Explicit exact displaced project link IDs required");
    const auto finalSize = links.size() - replacements.size() + (create ? 1 : 0);
    if (finalSize > samplerControllerLinkLimit ||
        (create && (project.nextId < 1 || project.nextId > 1000000000)))
        return juce::Result::fail("samplerController.limit: Project link capacity/allocator exhausted");
    // All refusals above precede any candidate mutation. Controller still validates
    // and owns this entire model/history transaction before publishing it.
    std::erase_if(links, [&](const auto &link) { return replacements.contains(link.id); });
    if (create)
    {
        candidate.id = project.allocate();
        links.push_back(std::move(candidate));
    }
    else
        *std::find_if(links.begin(), links.end(), [&](const auto &link) { return link.id == args.target; }) =
            std::move(candidate);
    return juce::Result::ok();
}
} // namespace
juce::Result validateRackGroupName(const Project &project, const juce::String &name, Id except, bool reuse)
{
    if (name.isEmpty() || name.length() > 128 || name != name.trim() || name.containsAnyOf("\r\n\t") ||
        name.equalsIgnoreCase("All") || name.equalsIgnoreCase("Unsorted"))
        return juce::Result::fail(
            "Use 1..128 characters without outer spaces; All and Unsorted are reserved");
    if (!reuse)
        for (const auto &group : project.channelGroups)
            if (group.id != except && group.name.equalsIgnoreCase(name))
                return juce::Result::fail("A Channel group with that name already exists");
    return juce::Result::ok();
}
const std::vector<CommandInfo> &commandRegistry()
{
    static const std::vector<CommandInfo> commands{
        {"workspace.tileBalanced", "Automatically tile editors", Scope::global, false, true, "action"},
        {"workspace.tileFocused", "Tile around active editor", Scope::global, false, true, "action"},
        {"workspace.free", "Free window movement", Scope::global, false, true, "action"},
        {"workspace.tileDetached", "Tile detached editors on their monitors", Scope::global, false, true,
         "action"},
        {"workspace.docked", "Restore docked workspace", Scope::global, false, true, "action"},
        {"sampler.modulation.volume", "Sampler Volume envelope/LFO page", Scope::sampler, false, true,
         "action"},
        {"sampler.modulation.pan", "Sampler Pan LFO page", Scope::sampler, false, true, "action"},
        {"sampler.modulation.cutoff", "Sampler Mod X envelope/LFO page", Scope::sampler, false, true,
         "action"},
        {"sampler.modulation.resonance", "Sampler Mod Y envelope/LFO page", Scope::sampler, false, true,
         "action"},
        {"sampler.modulation.pitch", "Sampler Pitch envelope/LFO page", Scope::sampler, false, true,
         "action"},
        {"sampler.view.zoomIn", "Sampler waveform zoom in", Scope::sampler, false, true, "action"},
        {"sampler.page.sample", "Sampler Channel window: Sample page", Scope::sampler, false, true, "action"},
        {"sampler.page.envelope", "Sampler Channel window: Envelope page", Scope::sampler, false, true, "action"},
        {"sampler.page.misc", "Sampler Channel window: Miscellaneous page", Scope::sampler, false, true, "action"},
        {"channel.previewNote", "Preview a note through the Channel's settings", Scope::sampler, false, false, "vibera_transport"},
        {"channel.sampler.tempoDetect", "Detect a sample's tempo", Scope::sampler, false, false, "vibera_samplerTempo"},
        {"sampler.view.zoomOut", "Sampler waveform zoom out", Scope::sampler, false, true, "action"},
        {"sampler.view.all", "Sampler show full sample", Scope::sampler, false, true, "action"},
        {"sampler.view.range", "Sampler zoom to source range", Scope::sampler, false, true, "action"},
        {"piano.zoomIn", "Piano Roll zoom in", Scope::piano, false, true, "action"},
        {"piano.zoomOut", "Piano Roll zoom out", Scope::piano, false, true, "action"},
        {"piano.fit", "Piano Roll fit score", Scope::piano, false, true, "action"},
        {"piano.selectionZoom", "Piano Roll zoom to timeline selection", Scope::piano, false, true, "action"},
        {"piano.preset1", "Piano Roll zoom preset 1", Scope::piano, false, true, "action"},
        {"piano.preset2", "Piano Roll zoom preset 2", Scope::piano, false, true, "action"},
        {"piano.preset3", "Piano Roll zoom preset 3", Scope::piano, false, true, "action"},
        {"piano.ghosts", "Ghost Channels", Scope::piano, false, true, "action"},
        {"piano.editGhosts", "Editable ghosts", Scope::piano, false, true, "action"},
        {"piano.insertSlide", "Toggle Slide insertion", Scope::piano, false, true, "action"},
        {"piano.insertPortamento", "Toggle Portamento insertion", Scope::piano, false, true, "action"},
        {"piano.changeColour", "Change selected note colour", Scope::piano, false, true, "action"},
        {"piano.selectColour", "Select current note colour", Scope::piano, false, true, "action"},
        {"piano.noteProperties", "Note Properties", Scope::piano, false, true, "action"},
        {"project.new", "New project", Scope::global, false, true, "vibera_project"},
        {"project.saveTemplate", "Save as template (File > New from template)", Scope::global, false, true, "action"},
        {"project.diagnostics", "Export diagnostics (Tools menu)", Scope::global, false, true, "vibera_project"},
        {"project.loudness", "Analyze loudness (LUFS / true peak) of an audio file", Scope::global, false, true, "vibera_project"},
        {"project.tapTempo", "Tap tempo (average of the last taps)", Scope::global, false, true, "action"},
        {"project.demo", "Load demonstration project", Scope::global, false, true, "vibera_project"},
        {"project.open", "Open project", Scope::global, false, true, "vibera_project"},
        {"project.save", "Save project", Scope::global, false, true, "vibera_project"},
        {"project.export", "Export WAV", Scope::global, false, true, "vibera_project"},
        {"transport.play", "Play / stop", Scope::global, false, true, "action"},
        {"transport.loop", "Loop current Pattern / Song", Scope::global, false, true, "action"},
        {"transport.stop", "Stop transport", Scope::global, false, true, "action"},
        {"channel.openInstrument", "Open Channel instrument", Scope::global, false, true, "action"},
        {"channel.pluginControls", "Open Channel plugin controls", Scope::global, false, true, "action"},
        {"channel.pluginRecover", "Recover Channel plugin saved state", Scope::global, false, true,
         "vibera_plugin"},
        {"synth.discover", "Discover VibeRa Synthesizer sounds", Scope::global, false, false, "vibera_synth"},
        {"synth.browser.state", "Read Synthesizer browser", Scope::global, false, false, "vibera_synth"},
        {"synth.browser.filter", "Filter Synthesizer presets", Scope::global, false, false, "vibera_synth"},
        {"synth.browser.favorite", "Set Synthesizer favorite", Scope::global, false, false, "vibera_synth"},
        {"synth.editor.navigate", "Navigate Synthesizer editor", Scope::global, false, false, "vibera_synth"},
        {"synth.preset.load", "Load Synthesizer factory patch", Scope::global, true, false, "vibera_synth"},
        {"synth.preset.previous", "Previous filtered Synthesizer patch", Scope::global, true, false,
         "vibera_synth"},
        {"synth.preset.next", "Next filtered Synthesizer patch", Scope::global, true, false, "vibera_synth"},
        {"synth.user.list", "List Synthesizer user presets", Scope::global, false, false, "vibera_synth"},
        {"synth.user.save", "Save Synthesizer user preset", Scope::global, false, false, "vibera_synth"},
        {"synth.user.load", "Load Synthesizer user preset", Scope::global, true, false, "vibera_synth"},
        {"synth.parameters", "Read Synthesizer parameters", Scope::global, false, false, "vibera_synth"},
        {"synth.parameter.set", "Change Synthesizer parameter", Scope::global, true, false, "vibera_synth"},
        {"synth.preview.on", "Preview Synthesizer note on", Scope::global, false, false, "vibera_synth"},
        {"synth.preview.off", "Preview Synthesizer note off", Scope::global, false, false, "vibera_synth"},
        {"synth.preview.panic", "Release Synthesizer preview notes", Scope::global, false, false,
         "vibera_synth"},
        {"synth.editor.open", "Open Synthesizer native editor", Scope::global, false, false, "vibera_synth"},
        {"synth.editor.close", "Close Synthesizer native editor", Scope::global, false, false,
         "vibera_synth"},
        {"plugin.catalogue", "Read native plugin parameter catalogue", Scope::global, false, false,
         "vibera_plugin"},
        {"plugin.refresh", "Refresh native plugin parameter catalogue", Scope::global, false, false,
         "vibera_plugin"},
        {"plugin.notifications", "Configure native plugin edit notifications", Scope::global, false, false,
         "vibera_plugin"},
        {"plugin.prepareAutomation", "Prepare canonical native automation action", Scope::playlist, false,
         false, "vibera_plugin"},
        {"plugins.discover", "Read installed VST3 catalogue", Scope::global, false, false,
         "vibera_catalogue"},
        {"plugins.scan", "Scan VST3 locations", Scope::global, false, false, "vibera_catalogue"},
        {"channel.select", "Select Channel and Pattern (shared selection rules)", Scope::global, false, false, "vibera_select"},
        {"plugin.parameter.set", "Set native plugin parameter with acknowledged history", Scope::global, true, false, "vibera_plugin"},
        {"plugin.editor.open", "Open or close a loaded plugin's native editor", Scope::global, false, false, "vibera_plugin"},
        {"plugin.wrapper.view", "Show a plugin window's Plugin / Settings / Parameters view", Scope::global, false, false, "vibera_plugin"},
        {"plugin.parameters.refresh", "Refresh a plugin's parameter list", Scope::global, false, false, "vibera_plugin"},
        {"plugins.cancelScan", "Cancel VST3 discovery", Scope::global, false, false, "vibera_catalogue"},
        {"tools.lastTweaked.controls", "Open last tweaked plugin control", Scope::global, false, true,
         "action"},
        {"tools.lastTweaked.create", "Create automation for last tweaked plugin control", Scope::global,
         false, true, "action"},
        {"tools.lastTweaked.init", "Initialize last tweaked plugin control", Scope::global, false, true,
         "action"},
        {"tools.lastTweaked.removeInit", "Delete last tweaked plugin initialization", Scope::global, false,
         true, "action"},
        {"controller.bankPrevious", "Previous controller Mixer bank", Scope::global, false, true, "action"},
        {"controller.bankNext", "Next controller Mixer bank", Scope::global, false, true, "action"},
        {"mixer.pan", "Mixer pan", Scope::mixer, true, true, "command"},
        {"mixer.stereoSeparation", "Mixer stereo separation", Scope::mixer, true, true, "command"},
        {"mixer.eq", "Mixer EQ band (gain / frequency)", Scope::mixer, true, true, "command"},
        {"mixer.eqReset", "Reset Mixer EQ", Scope::mixer, true, true, "command"},
        {"mixer.delay", "Mixer track delay (ms)", Scope::mixer, true, true, "command"},
        {"mixer.insert.fromChainPreset", "Add insert from chain preset (file chooser)", Scope::mixer, false, true, "action"},
        {"mixer.dock", "Dock Mixer track left / middle / right", Scope::mixer, true, true, "command"},
        {"mixer.reset", "Reset Mixer track controls, name and colour (keeps effects and routing)", Scope::mixer, true, true, "command"},
        {"channel.multiOutputs0", "Multi-output: main output only", Scope::rack, false, true, "action"},
        {"channel.multiOutputs1", "Multi-output: route 1 extra output to free Mixer tracks", Scope::rack, false, true, "action"},
        {"channel.multiOutputs2", "Multi-output: route 2 extra outputs to free Mixer tracks", Scope::rack, false, true, "action"},
        {"channel.multiOutputs3", "Multi-output: route 3 extra outputs to free Mixer tracks", Scope::rack, false, true, "action"},
        {"channel.multiOutputs4", "Multi-output: route 4 extra outputs to free Mixer tracks", Scope::rack, false, true, "action"},
        {"channel.multiOutputs5", "Multi-output: route 5 extra outputs to free Mixer tracks", Scope::rack, false, true, "action"},
        {"channel.multiOutputs6", "Multi-output: route 6 extra outputs to free Mixer tracks", Scope::rack, false, true, "action"},
        {"channel.multiOutputs7", "Multi-output: route 7 extra outputs to free Mixer tracks", Scope::rack, false, true, "action"},
        {"channel.outputRoutes", "Route a multi-output instrument's extra outputs to Mixer tracks", Scope::rack, true, true, "command"},
        {"channel.automateVolume", "Create automation clip: selected Channel volume", Scope::rack, false, true, "action"},
        {"channel.automatePan", "Create automation clip: selected Channel pan", Scope::rack, false, true, "action"},
        {"mixer.routeSelectedHere", "Route the selected Mixer tracks to the active track", Scope::mixer, false, true, "action"},
        {"mixer.routeSelectedHereOnly", "Route the selected Mixer tracks only to the active track", Scope::mixer, false, true, "action"},
        {"mixer.createSubmix", "Create a submix track fed only by the selected Mixer tracks", Scope::mixer, false, true, "action"},
        {"mixer.trackControls", "Apply Mixer track controls from a track state (JSON text)", Scope::mixer, true, false, "command"},
        {"mixer.trimUnused", "Trim unused Mixer tracks (no Channel, route, effect, automation or group)", Scope::mixer, false, true, "action"},
        {"mixer.selectChannelsRouted", "Select the Channels routed to the selected Mixer track", Scope::mixer, false, true, "action"},
        {"mixer.selectRoutedHere", "Select the Mixer tracks routed to the selected track", Scope::mixer, false, true, "action"},
        {"mixer.selectRoutedFrom", "Select the Mixer tracks the selected track is routed to", Scope::mixer, false, true, "action"},
        {"mixer.meterWave", "Meter-wave view (level waveforms instead of peak meters)", Scope::mixer, false, true, "action"},
        {"mixer.phaseInvert", "Invert Mixer polarity", Scope::mixer, true, true, "command"},
        {"mixer.stereoSwap", "Swap Mixer stereo channels", Scope::mixer, true, true, "command"},
        {"mixer.chain.copy", "Copy selected Mixer effect chain", Scope::mixer, false, true,
         "vibera_mixerchain"},
        {"mixer.chain.paste", "Paste effect chain onto selected Mixer insert", Scope::mixer, true, true,
         "vibera_mixerchain"},
        {"mixer.chain.loadApply", "Load and apply Mixer chain preset", Scope::mixer, true, true,
         "vibera_mixerchain"},
        {"mixer.chain.save", "Save selected Mixer chain preset", Scope::mixer, false, true,
         "vibera_mixerchain"},
        {"mixer.chain.loadClipboard", "Load chain preset to clipboard (no Paste)", Scope::mixer, false, true,
         "vibera_mixerchain"},
        {"mixer.chain.directory", "Configure chain preset directory", Scope::mixer, false, true,
         "vibera_mixerchain"},
        {"mixer.chain.cancel", "Cancel active chain operation", Scope::mixer, false, true,
         "vibera_mixerchain"},
        {"mixer.effects.browse", "Browse effects for selected Mixer insert", Scope::mixer, false, true,
         "vibera_mixer"},
        {"mixer.effects.scroll", "Scroll ordered Mixer effect slots", Scope::mixer, false, false,
         "vibera_mixer"},
        {"mixer.meterReset", "Clear sample overload indication", Scope::mixer, false, true, "vibera_mixer"},
        {"mixer.move", "Move Mixer insert to display row", Scope::mixer, true, true, "command"},
        {"mixer.moveLeft", "Move Mixer insert left", Scope::mixer, true, true, "command"},
        {"mixer.moveRight", "Move Mixer insert right", Scope::mixer, true, true, "command"},
        {"mixer.rename", "Rename Mixer insert", Scope::mixer, true, true, "command"},
        {"mixer.selection.rename", "Rename selected Mixer inserts", Scope::mixer, true, true, "command"},
        {"mixer.selection.colour", "Colour selected Mixer inserts", Scope::mixer, true, true, "command"},
        {"mixer.selectAll", "Select all Mixer inserts", Scope::mixer, false, true, "vibera_mixer"},
        {"mixer.organisation.menu", "Mixer selection and groups", Scope::mixer, false, true, "vibera_mixer"},
        {"mixer.group.create", "Group selected Mixer inserts", Scope::mixer, true, true, "command"},
        {"mixer.group.assign", "Assign Mixer group members", Scope::mixer, true, true, "command"},
        {"mixer.group.rename", "Rename Mixer display group", Scope::mixer, true, true, "command"},
        {"mixer.group.colour", "Mixer display group colour", Scope::mixer, true, true, "command"},
        {"mixer.group.delete", "Dissolve Mixer display group", Scope::mixer, true, true, "command"},
        {"mixer.group.colourMembers", "Apply member colour or theme defaults to Mixer group", Scope::mixer,
         true, true, "command"},
        {"mixer.group.mute", "Mute Mixer group", Scope::mixer, true, true, "command"},
        {"mixer.group.solo", "Solo Mixer group", Scope::mixer, true, true, "command"},
        {"mixer.group.soloConnected", "Solo Mixer group with inputs", Scope::mixer, true, true, "command"},
        {"mixer.group.unsolo", "Restore Mixer group solo", Scope::mixer, true, true, "command"},
        {"mixer.colour", "Mixer insert colour", Scope::mixer, true, true, "command"},
        {"mixer.solo", "Solo Mixer insert", Scope::mixer, true, true, "command"},
        {"mixer.soloConnected", "Solo connected Mixer tracks", Scope::mixer, true, true, "command"},
        {"mixer.unsolo", "Restore Mixer solo", Scope::mixer, true, true, "command"},
        {"mixer.unmuteAll", "Unmute all Mixer tracks", Scope::mixer, true, true, "command"},
        {"mixer.mute", "Mixer mute", Scope::mixer, true, true, "command"},
        {"transport.mode", "Pattern / Song", Scope::global, false, true, "action"},
        {"transport.record", "Arm recording (MIDI notes; audio input when enabled)", Scope::global, false, true, "action"},
        {"recording.finish", "Finish MIDI take", Scope::global, false, true, "vibera_recording"},
        {"recording.retry", "Retry retained MIDI take", Scope::global, false, true, "vibera_recording"},
        {"recording.cancel", "Cancel unapplied MIDI take", Scope::global, false, true, "vibera_recording"},
        {"transport.metronome", "Metronome", Scope::global, false, true, "action"},
        {"score.dump", "Dump score log to selected pattern", Scope::global, false, true, "action"},
        {"score.clear", "Clear score log", Scope::global, false, true, "action"},
        {"midi.typing", "Typing keyboard to piano", Scope::global, false, true, "action"},
        {"transport.countIn", "Recording count-in", Scope::global, false, true, "action"},
        {"notes.record", "Record MIDI take", Scope::global, true, true, "command"},
        {"notes.import", "Import MIDI score", Scope::piano, true, true, "command"},
        {"notes.importTracks", "Import MIDI tracks as Channels", Scope::piano, true, true, "command"},
        {"media.relink", "Find missing samples", Scope::global, true, true, "vibera_project"},
        {"coproducer.accept", "Accept CoProducer part", Scope::global, true, true, "command"},
        {"coproducer.replace", "Replace CoProducer destination region", Scope::global, true, true, "command"},
        {"edit.undo", "Undo", Scope::global, false, true, "action"},
        {"edit.redo", "Redo", Scope::global, false, true, "action"},
        {"view.playlist", "Show Playlist", Scope::global, false, true, "action"},
        {"view.rack", "Show Channel Rack", Scope::global, false, true, "action"},
        {"view.piano", "Show Piano roll", Scope::global, false, true, "action"},
        {"view.mixer", "Show Mixer", Scope::global, false, true, "action"},
        {"view.settings", "Settings", Scope::global, false, true, "action"},
        {"view.drumPads", "Drum pads", Scope::global, false, true, "action"},
        {"view.browser", "Show / hide the Browser", Scope::global, false, true, "action"},
        {"view.coProducer", "Show / hide the CoProducer panel", Scope::global, false, true, "action"},
        {"view.snap", "Set the Piano roll / Playlist snap", Scope::global, false, true, "vibera_editor"},
        {"view.chordProgression", "Create a chord progression", Scope::global, false, true, "action"},
        {"transport.stepRecord", "Step record", Scope::global, false, true, "action"},
        {"transport.stepRest", "Step record rest", Scope::global, false, true, "action"},
        {"view.plugins", "VST3 plugin browser", Scope::global, false, true, "action"},
        {"browser.back", "Browser previous folder", Scope::browser, false, true, "action"},
        {"browser.forward", "Browser next folder", Scope::browser, false, true, "action"},
        {"browser.up", "Browser parent folder", Scope::browser, false, true, "action"},
        {"browser.clearSearch", "Browser clear filter", Scope::browser, false, true, "action"},
        {"browser.find", "Browser search saved folders", Scope::browser, false, true, "action"},
        {"browser.nextMatch", "Browser next search match", Scope::browser, false, true, "action"},
        {"browser.previousMatch", "Browser previous search match", Scope::browser, false, true, "action"},
        {"browser.reindex", "Browser refresh folders and index", Scope::browser, false, true, "action"},
        {"browser.showAll", "Browser All tab", Scope::browser, false, true, "action"},
        {"browser.showStarred", "Browser Starred tab", Scope::browser, false, true, "action"},
        {"browser.toggleFavorite", "Browser toggle selected file in Starred", Scope::browser, false, true, "action"},
        {"browser.collectionsMenu", "Browser search tabs and favorites menu", Scope::browser, false, true, "action"},
        {"browser.showTags", "Browser show/hide tag filters", Scope::browser, false, true, "action"},
        {"browser.editTags", "Browser edit selected file tags", Scope::browser, false, true, "action"},
        {"browser.addTags", "Browser add selected file tags", Scope::browser, false, true, "action"},
        {"browser.matchAllTags", "Browser toggle ANY/ALL tag matching", Scope::browser, false, true, "action"},
        {"browser.clearTagFilters", "Browser clear selected tag filters", Scope::browser, false, true, "action"},
        {"browser.playPreview", "Browser play sample to end", Scope::browser, false, true, "action"},
        {"browser.stopPreview", "Browser stop sample preview", Scope::browser, false, true, "action"},
        {"browser.fullPreview", "Browser full sample preview", Scope::browser, false, true, "action"},
        {"browser.previewLouder", "Browser preview volume up", Scope::browser, false, true, "action"},
        {"browser.previewSofter", "Browser preview volume down", Scope::browser, false, true, "action"},
        {"midi.panic", "MIDI All Notes Off", Scope::global, false, true, "action"},
        {"midi.input", "MIDI input channel and recording quantize", Scope::global, false, true, "vibera_transport"},
        {"transport.guide", "Metronome and count-in settings", Scope::global, false, true, "vibera_transport"},
        {"controller.binding", "Edit a controller mapping", Scope::global, false, true, "vibera_controller"},
        {"controller.binding.remove", "Remove a controller mapping", Scope::global, false, true, "vibera_controller"},
        {"shortcut.bind", "Assign a keyboard shortcut", Scope::global, false, true, "vibera_macros"},
        {"shortcut.unbind", "Remove a keyboard shortcut", Scope::global, false, true, "vibera_macros"},
        {"shortcut.reset", "Restore the default keyboard shortcuts", Scope::global, false, true, "vibera_macros"},
        {"userMacro.save", "Save a macro", Scope::global, false, true, "vibera_macros"},
        {"userMacro.run", "Run a saved macro", Scope::global, false, true, "vibera_macros"},
        {"userMacro.delete", "Delete a saved macro", Scope::global, false, true, "vibera_macros"},
        {"plugin.recover", "Recover a plugin from its saved state", Scope::global, false, false, "vibera_plugin"},
        {"audio.vocalPitch", "Retune a vocal Audio Clip (pitch, formant, timing)", Scope::playlist, true, true, "vibera_project"},
        {"selection.all", "Select all", Scope::global, false, true, "action"},
        {"selection.none", "Deselect", Scope::global, false, true, "action"},
        {"selection.next", "Select next note or clip", Scope::global, false, true, "action"},
        {"selection.previous", "Select previous note or clip", Scope::global, false, true, "action"},
        {"selection.cancel", "Cancel edit", Scope::global, false, true, "action"},
        {"selection.set", "Set typed note or clip selection", Scope::global, false, false,
         "vibera_selection"},
        {"edit.gesture", "Own a continuous edit transaction", Scope::global, false, false, "vibera_gesture"},
        {"tool.draw", "Draw tool", Scope::global, false, true, "action"},
        {"tool.paint", "Paint tool", Scope::global, false, true, "action"},
        {"tool.slice", "Slice tool", Scope::playlist, false, true, "action"},
        {"tool.slip", "Slip tool", Scope::playlist, false, true, "action"},
        {"tool.mute", "Mute clips tool", Scope::playlist, false, true, "action"},
        {"tool.select", "Select clips tool", Scope::playlist, false, true, "action"},
        {"view.restoreLayout", "Restore docked workspace", Scope::global, false, true, "action"},
        {"tool.delete", "Delete clips tool", Scope::playlist, false, true, "action"},
        {"tool.zoom", "Zoom region tool", Scope::playlist, false, true, "action"},
        {"playlist.zoomIn", "Playlist zoom in", Scope::playlist, false, true, "action"},
        {"playlist.follow", "Auto-scroll the Playlist with the playhead", Scope::playlist, false, true, "action"},
        {"playlist.stretchMode", "Stretch audio clips by dragging their right edge", Scope::playlist, false, true, "action"},
        {"playlist.zoomOut", "Playlist zoom out", Scope::playlist, false, true, "action"},
        {"playlist.zoom1", "Playlist quick zoom 1", Scope::playlist, false, true, "action"},
        {"playlist.zoom2", "Playlist quick zoom 2", Scope::playlist, false, true, "action"},
        {"playlist.zoom3", "Playlist quick zoom 3", Scope::playlist, false, true, "action"},
        {"playlist.fit", "Playlist zoom out / restore", Scope::playlist, false, true, "action"},
        {"playlist.zoomSelection", "Playlist zoom selected clips", Scope::playlist, false, true, "action"},
        {"clipboard.copy", "Copy Channel sequence", Scope::rack, false, true, "action"},
        {"clipboard.paste", "Paste Channel sequence", Scope::rack, false, true, "action"},
        {"notes.nudgeLeft", "Nudge notes left", Scope::piano, false, true, "action"},
        {"notes.nudgeRight", "Nudge notes right", Scope::piano, false, true, "action"},
        {"clip.nudgeLeft", "Nudge clips left", Scope::playlist, false, true, "action"},
        {"clip.trackUp", "Move clips up one track", Scope::playlist, false, true, "action"},
        {"clip.trackDown", "Move clips down one track", Scope::playlist, false, true, "action"},
        {"transport.home", "Playhead to the start", Scope::global, false, true, "action"},
        {"marker.addAtPlayhead", "Add time marker at the playhead", Scope::playlist, false, true, "action"},
        {"transport.end", "Playhead to the end of the Song", Scope::global, false, true, "action"},
        {"clip.nudgeRight", "Nudge clips right", Scope::playlist, false, true, "action"},
        {"notes.transposeUp", "Transpose up", Scope::piano, false, true, "action"},
        {"notes.transposeDown", "Transpose down", Scope::piano, false, true, "action"},
        {"notes.octaveUp", "Transpose octave up", Scope::piano, false, true, "action"},
        {"notes.octaveDown", "Transpose octave down", Scope::piano, false, true, "action"},
        {"project.tempo", "Tempo", Scope::global, true, true, "command"},
        {"project.swing", "Global Swing", Scope::rack, true, true, "command"},
        {"channel.swingMix", "Channel Swing Mix", Scope::rack, true, true, "command"},
        {"channel.truncateSwing", "Truncate swung notes", Scope::rack, true, false, "command"},
        {"rack.swingMix", "Swing Mix for selected Channels", Scope::rack, true, false, "command"},
        {"rack.truncateSwing", "Truncate swung notes for selected Channels", Scope::rack, true, false,
         "command"},
        {"project.meter", "Project time signature", Scope::global, true, true, "command"},
        {"channel.add", "Add Channel", Scope::rack, true, true, "command"},
        {"channel.clone", "Clone selected Channels", Scope::rack, true, true, "vibera_rack"},
        {"channel.delete", "Delete selected Channels", Scope::rack, true, true, "vibera_rack"},
        {"channel.moveUp", "Move selected Channels up", Scope::rack, true, true, "command"},
        {"channel.moveDown", "Move selected Channels down", Scope::rack, true, true, "command"},
        {"plugin.instrument.create", "New native / VST3 instrument Channel", Scope::rack, true, true,
         "command"},
        {"plugin.instrument.replace", "Replace Channel instrument", Scope::rack, true, true, "command"},
        {"mixer.insert.create", "Create Mixer insert", Scope::mixer, true, true, "command"},
        {"mixer.insert.delete", "Delete Mixer insert", Scope::mixer, true, true, "command"},
        {"plugin.effect.insert", "Insert effect", Scope::mixer, true, true, "command"},
        {"plugin.effect.replace", "Replace selected effect", Scope::mixer, true, true, "command"},
        {"plugin.io", "Select plugin main I/O", Scope::mixer, true, true, "command"},
        {"plugin.effect.sidechain", "Set effect detector input", Scope::mixer, true, true, "command"},
        {"plugin.effect.move", "Reorder effect", Scope::mixer, true, true, "command"},
        {"plugin.effect.mix", "Effect host wet/dry", Scope::mixer, true, true, "command"},
        {"mixer.effects.bypass", "Bypass Mixer effect chain", Scope::mixer, true, true, "command"},
        {"plugin.remove", "Remove plugin", Scope::global, true, true, "command"},
        {"plugin.bypass", "Bypass plugin", Scope::global, true, true, "command"},
        {"plugin.binding.declare", "Store plugin automation binding", Scope::global, true, false, "command"},
        {"plugin.binding.remove", "Remove plugin automation binding", Scope::global, true, false, "command"},
        {"channel.source", "Replace sample", Scope::rack, true, true, "command"},
        {"channel.preset", "Apply Channel preset", Scope::rack, true, false, "command"},
        {"channel.properties", "Channel name / colour / icon", Scope::rack, true, true, "command"},
        {"rack.properties", "Selected Channel colour / icon", Scope::rack, true, true, "command"},
        {"rack.sort", "Sort selected Channels", Scope::rack, true, true, "command"},
        {"rack.transpose", "Transpose selected Channels (notes in every Pattern)", Scope::rack, true, true, "command"},
        {"rack.assignFreeInserts", "Assign selected Channels to free Mixer inserts (one each)", Scope::rack, false, true, "action"},
        {"rack.zip", "Zip selected Channels (compact rows)", Scope::rack, false, true, "action"},
        {"rack.linkSequential", "Link selected Channels to consecutive Mixer inserts", Scope::global, false, true, "action"},
        {"rack.unzipAll", "Unzip all Channels", Scope::rack, false, true, "action"},
        {"rack.colourSpread", "Colour selected Channels (random / gradient)", Scope::rack, true, true, "command"},
        {"channel.mixer", "Assign Mixer insert", Scope::rack, true, true, "command"},
        {"channel.gain", "Channel volume", Scope::rack, true, true, "command"},
        {"channel.pitch", "Channel pitch (plugin pitch bend -1..1)", Scope::rack, true, true, "command"},
        {"channel.spareStore", "Store in spare state", Scope::rack, false, true, "action"},
        {"channel.spareFlip", "Flip with spare state", Scope::rack, false, true, "action"},
        {"mixer.spareStore", "Store the selected insert's effect chain in spare state", Scope::mixer, false, true, "action"},
        {"mixer.spareFlip", "Flip the selected insert's effect chain with spare state", Scope::mixer, false, true, "action"},
        {"channel.pitchRange", "Channel pitch range (semitones)", Scope::rack, true, true, "command"},
        {"channel.keyLabel", "Rename a Piano roll key (drum map label)", Scope::piano, true, true, "command"},
        {"channel.pan", "Channel pan", Scope::rack, true, true, "command"},
        {"channel.sampler.sourceTempo", "Set sample tempo", Scope::rack, true, true, "command"},
        {"channel.sampler.fitTempo", "Fit sample to project tempo", Scope::rack, true, true, "command"},
        {"channel.sampler.stretch", "Keep pitch when fitting tempo (time stretch)", Scope::rack, true, true,
         "command"},
        {"channel.sampler.stretchPitch", "Sampler time stretching PITCH (cents)", Scope::rack, true, true, "command"},
        {"channel.sampler.timeMul", "Sampler time stretching MUL (length multiplier)", Scope::rack, true, true, "command"},
        {"channel.sampler.timeBeats", "Sampler time stretching TIME (beats, 0 = off)", Scope::rack, true, true, "command"},
        {"channel.sampler.hqInterpolation", "Sampler HQ (Hermite) interpolation", Scope::rack, true, true,
         "command"},
        {"channel.sampler.tempo", "Set and fit sample tempo", Scope::sampler, true, true, "command"},
        {"channel.sampler.root", "Sampler root note", Scope::rack, true, true, "command"},
        {"channel.sampler.cents", "Sampler pitch (cents)", Scope::rack, true, true, "command"},
        {"channel.sampler.start", "Sampler source start", Scope::rack, true, true, "command"},
        {"channel.sampler.end", "Sampler source end", Scope::rack, true, true, "command"},
        {"channel.sampler.range", "Sampler source range", Scope::rack, true, true, "command"},
        {"channel.sampler.reverse", "Reverse sampler playback", Scope::rack, true, true, "command"},
        {"channel.sampler.normalize", "Normalize sample peak", Scope::rack, true, true, "command"},
        {"channel.sampler.loop", "Use sampler loop points", Scope::rack, true, true, "command"},
        {"channel.sampler.pingPong", "Sampler ping pong loop", Scope::rack, true, true, "command"},
        {"channel.sampler.fadeIn", "Sampler fade in (seconds)", Scope::rack, true, true, "command"},
        {"channel.sampler.fadeOut", "Sampler fade out (seconds)", Scope::rack, true, true, "command"},
        {"channel.sampler.loopCrossfade", "Sampler loop crossfade (seconds)", Scope::rack, true, true,
         "command"},
        {"channel.sampler.loopStart", "Sampler loop start", Scope::rack, true, true, "command"},
        {"channel.sampler.loopEnd", "Sampler loop end", Scope::rack, true, true, "command"},
        {"channel.sampler.loopRange", "Sampler loop range", Scope::rack, true, true, "command"},
        {"channel.sampler.loopPoints", "Sampler loop range and Use loop points", Scope::rack, true, false,
         "command"},
        {"channel.sampler.mono", "Sampler Mono", Scope::sampler, true, true, "command"},
        {"channel.sampler.portamento", "Sampler Portamento", Scope::sampler, true, true, "command"},
        {"channel.sampler.slideTime", "Sampler Slide time", Scope::sampler, true, true, "command"},
        {"channel.sampler.gateBeats", "Sampler Gate", Scope::sampler, true, true, "command"},
        {"channel.sampler.fullPortamento", "Sampler Full portamento", Scope::sampler, true, true, "command"},
        {"channel.sampler.filterType", "Sampler filter type", Scope::rack, true, true, "command"},
        {"channel.sampler.filterCutoff", "Sampler filter cutoff", Scope::rack, true, true, "command"},
        {"channel.sampler.filterResonance", "Sampler filter resonance", Scope::rack, true, true, "command"},
        {"channel.sampler.controller.create", "Link sampler parameter to project MIDI CC", Scope::sampler,
         true, false, "command"},
        {"channel.sampler.controller.update", "Edit project sampler MIDI CC link", Scope::sampler, true,
         false, "command"},
        {"channel.sampler.controller.remove", "Remove project sampler MIDI CC link", Scope::sampler, true,
         false, "command"},
        {"channel.sampler.modulation.enabled", "Sampler modulation enabled", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.tempo", "Sampler modulation tempo", Scope::rack, true, true, "command"},
        {"channel.sampler.modulation.delay", "Sampler modulation delay", Scope::rack, true, true, "command"},
        {"channel.sampler.modulation.attack", "Sampler modulation attack", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.hold", "Sampler modulation hold", Scope::rack, true, true, "command"},
        {"channel.sampler.modulation.decay", "Sampler modulation decay", Scope::rack, true, true, "command"},
        {"channel.sampler.modulation.sustain", "Sampler modulation sustain", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.release", "Sampler modulation release", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.attackTension", "Sampler modulation attackTension", Scope::rack, true,
         true, "command"},
        {"channel.sampler.modulation.decayTension", "Sampler modulation decayTension", Scope::rack, true,
         true, "command"},
        {"channel.sampler.modulation.releaseTension", "Sampler modulation releaseTension", Scope::rack, true,
         true, "command"},
        {"channel.sampler.modulation.amount", "Sampler modulation amount", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoEnabled", "Sampler modulation lfoEnabled", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoTempo", "Sampler modulation lfoTempo", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoGlobal", "Sampler modulation lfoGlobal", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoDelay", "Sampler modulation lfoDelay", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoAttack", "Sampler modulation lfoAttack", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoPeriod", "Sampler modulation lfoPeriod", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoAmount", "Sampler modulation lfoAmount", Scope::rack, true, true,
         "command"},
        {"channel.sampler.modulation.lfoShape", "Sampler modulation lfoShape", Scope::rack, true, true,
         "command"},
        {"channel.sampler.envelope", "Sampler volume envelope", Scope::rack, true, true, "command"},
        {"channel.sampler.attack", "Sampler volume attack", Scope::rack, true, true, "command"},
        {"channel.sampler.decay", "Sampler volume decay", Scope::rack, true, true, "command"},
        {"channel.sampler.sustain", "Sampler volume sustain", Scope::rack, true, true, "command"},
        {"channel.sampler.release", "Sampler volume release", Scope::rack, true, true, "command"},
        {"channel.sampler.cut", "Sampler Cut group", Scope::rack, true, true, "command"},
        {"channel.sampler.cutBy", "Sampler Cut-by group", Scope::rack, true, true, "command"},
        {"channel.sampler.cutItself", "Sampler Cut itself", Scope::rack, true, true, "command"},
        {"channel.sampler.maxPoly", "Sampler maximum polyphony", Scope::rack, true, true, "command"},
        {"channel.mute", "Mute Channel", Scope::rack, true, true, "command"},
        {"channel.solo", "Solo / unsolo Channel", Scope::rack, true, true, "command"},
        {"channel.muteLock", "Lock / unlock Channel mute state", Scope::rack, true, true, "command"},
        {"rack.muteLock", "Lock / unlock selected Channel mute states", Scope::rack, true, true, "command"},
        {"channel.unsolo", "Unsolo Channels", Scope::rack, true, true, "command"},
        {"rack.muteSelected", "Mute selected Channels", Scope::rack, true, true, "command"},
        {"rack.unmuteSelected", "Unmute selected Channels", Scope::rack, true, true, "command"},
        {"rack.soloRestore", "Restore previous mute state after solo", Scope::global, false, true,
         "vibera_rack"},
        {"rack.graph", "Show step Graph Editor", Scope::rack, false, true, "vibera_rack"},
        {"rack.graphProperty", "Step Graph property", Scope::rack, false, false, "vibera_rack"},
        {"steps.velocity", "Step velocity graph edit", Scope::rack, true, false, "command"},
        {"steps.pitch", "Step pitch graph edit", Scope::rack, true, false, "command"},
        {"steps.pan", "Sampler step pan graph edit", Scope::rack, true, false, "command"},
        {"steps.fine", "Sampler step Fine Pitch graph edit", Scope::rack, true, false, "command"},
        {"steps.modX", "Sampler step Mod X cutoff graph edit", Scope::rack, true, false, "command"},
        {"steps.modY", "Sampler step Mod Y resonance graph edit", Scope::rack, true, false, "command"},
        {"steps.shift", "Step Shift timing graph edit", Scope::rack, true, false, "command"},
        {"steps.repeat", "Step Repeat rate graph edit", Scope::rack, true, false, "command"},
        {"steps.release", "Step Release velocity graph edit", Scope::rack, true, false, "command"},
        {"steps.set", "Activate / deactivate step", Scope::rack, true, true, "command"},
        {"steps.fill", "Fill Channel steps at an interval", Scope::rack, true, false, "command"},
        {"steps.fill2", "Fill each 2 steps", Scope::rack, true, true, "command"},
        {"steps.rotate", "Rotate Channel steps (wrap within the Pattern)", Scope::rack, true, false, "command"},
        {"steps.clear", "Clear Channel steps", Scope::rack, true, true, "command"},
        {"steps.rotateLeft", "Rotate steps left", Scope::rack, false, true, "action"},
        {"steps.rotateRight", "Rotate steps right", Scope::rack, false, true, "action"},
        {"steps.fill4", "Fill each 4 steps", Scope::rack, true, true, "command"},
        {"steps.fill8", "Fill each 8 steps", Scope::rack, true, true, "command"},
        {"steps.compose", "Apply Advanced Fill recipes", Scope::rack, true, false, "command"},
        {"steps.composeDialog", "Advanced Fill...", Scope::rack, false, true, "action"},
        {"pattern.add", "New Pattern", Scope::rack, true, true, "command"},
        {"pattern.clone", "Clone Pattern", Scope::rack, true, true, "command"},
        {"pattern.splitChannels", "Split Pattern by channel", Scope::rack, true, true, "command"},
        {"pattern.transpose", "Transpose Pattern Channels", Scope::rack, true, true, "command"},
        {"pattern.group", "Group selected Patterns", Scope::patterns, true, true, "command"},
        {"pattern.launchQuantum", "Set Pattern launch quantum", Scope::patterns, true, true, "command"},
        {"pattern.followAction", "Set Pattern Follow Action (performance)", Scope::patterns, true, true, "command"},
        {"performance.stopAll", "Performance: stop all launched Patterns", Scope::global, false, true, "action"},
        {"performance.saveScene", "Save launched Patterns as a performance scene", Scope::global, true, true, "command"},
        {"performance.assignPad", "Assign a Pattern to a performance pad", Scope::global, true, true, "command"},
        {"performance.scene1", "Performance scene 1", Scope::global, false, true, "action"},
        {"performance.scene2", "Performance scene 2", Scope::global, false, true, "action"},
        {"performance.scene3", "Performance scene 3", Scope::global, false, true, "action"},
        {"performance.scene4", "Performance scene 4", Scope::global, false, true, "action"},
        {"performance.scene5", "Performance scene 5", Scope::global, false, true, "action"},
        {"performance.scene6", "Performance scene 6", Scope::global, false, true, "action"},
        {"performance.scene7", "Performance scene 7", Scope::global, false, true, "action"},
        {"performance.scene8", "Performance scene 8", Scope::global, false, true, "action"},
        {"performance.pad1", "Performance pad 1", Scope::global, false, true, "action"},
        {"performance.pad2", "Performance pad 2", Scope::global, false, true, "action"},
        {"performance.pad3", "Performance pad 3", Scope::global, false, true, "action"},
        {"performance.pad4", "Performance pad 4", Scope::global, false, true, "action"},
        {"performance.pad5", "Performance pad 5", Scope::global, false, true, "action"},
        {"performance.pad6", "Performance pad 6", Scope::global, false, true, "action"},
        {"performance.pad7", "Performance pad 7", Scope::global, false, true, "action"},
        {"performance.pad8", "Performance pad 8", Scope::global, false, true, "action"},
        {"performance.pad9", "Performance pad 9", Scope::global, false, true, "action"},
        {"performance.pad10", "Performance pad 10", Scope::global, false, true, "action"},
        {"performance.pad11", "Performance pad 11", Scope::global, false, true, "action"},
        {"performance.pad12", "Performance pad 12", Scope::global, false, true, "action"},
        {"performance.pad13", "Performance pad 13", Scope::global, false, true, "action"},
        {"performance.pad14", "Performance pad 14", Scope::global, false, true, "action"},
        {"performance.pad15", "Performance pad 15", Scope::global, false, true, "action"},
        {"performance.pad16", "Performance pad 16", Scope::global, false, true, "action"},
        {"picker.place", "Place selected sources", Scope::playlist, true, true, "command"},
        {"picker.type", "Picker source type", Scope::playlist, false, true, "vibera_picker"},
        {"picker.filter", "Picker source text filter", Scope::playlist, false, true, "vibera_picker"},
        {"picker.groupFilter", "Picker Pattern group filter", Scope::playlist, false, true, "vibera_picker"},
        {"picker.select", "Picker source selection", Scope::playlist, false, true, "vibera_picker"},
        {"picker.width", "Resize Picker Panel", Scope::playlist, false, true, "vibera_picker"},
        {"picker.scroll", "Scroll Picker sources", Scope::playlist, false, true, "vibera_picker"},
        {"picker.visible", "Show/hide Picker Panel", Scope::playlist, false, true, "vibera_picker"},
        {"playlist.picker", "Show/hide Picker Panel", Scope::playlist, false, true, "action"},
        {"pattern.length", "Pattern length", Scope::rack, true, true, "command"},
        {"pattern.extent", "Automatic / manual Pattern length", Scope::rack, true, true, "command"},
        {"pattern.meter", "Pattern time signature", Scope::rack, true, true, "command"},
        {"pattern.channelLoop", "Channel loop in current Pattern", Scope::rack, true, true, "command"},
        {"pattern.burnChannelLoop", "Burn Channel loop to Pattern score", Scope::rack, true, true, "command"},
        {"pattern.rename", "Rename Pattern", Scope::rack, true, true, "command"},
        {"pattern.properties", "Rename / recolour Pattern", Scope::rack, true, true, "command"},
        {"pattern.insert", "Insert Pattern before selected", Scope::rack, true, true, "command"},
        {"pattern.delete", "Delete Pattern and its placements", Scope::rack, true, true, "command"},
        {"pattern.cloneSelected", "Clone selected Patterns", Scope::patterns, true, false, "command"},
        {"pattern.deleteSelected", "Delete selected Patterns and their placements", Scope::patterns, true, false,
         "command"},
        {"pattern.deleteEmpty", "Delete empty unused Patterns", Scope::patterns, true, true, "command"},
        {"pattern.deleteUnplaced", "Delete Patterns not used in the Playlist", Scope::patterns, true, true,
         "command"},
        {"pattern.move", "Reorder Pattern", Scope::rack, true, true, "command"},
        {"pattern.moveUp", "Move Pattern up", Scope::rack, false, true, "action"},
        {"pattern.moveDown", "Move Pattern down", Scope::rack, false, true, "action"},
        {"pattern.findEmpty", "Find next empty Pattern", Scope::global, false, true, "action"},
        {"pattern.firstEmpty", "Find first empty Pattern", Scope::global, false, true, "action"},
        {"pattern.renameDialog", "Rename selected Pattern", Scope::global, false, true, "action"},
        {"channel.paste", "Paste Channel sequence", Scope::rack, true, true, "command"},
        {"notes.add", "Draw note", Scope::piano, true, true, "command"},
        {"notes.properties", "Apply Note Properties", Scope::piano, true, false, "command"},
        {"notes.slide", "Set sampler Slide notes", Scope::piano, true, false, "command"},
        {"notes.portamento", "Set sampler Portamento notes", Scope::piano, true, false, "command"},
        {"notes.colourGroup", "Set note colour voice group", Scope::piano, true, false, "command"},
        {"notes.move", "Move / resize notes", Scope::piano, true, true, "command"},
        {"notes.delete", "Delete notes", Scope::piano, true, true, "command"},
        {"notes.copy", "Copy notes", Scope::piano, false, true, "action"},
        {"notes.copyMidiClipboard", "Copy to MIDI clipboard (a .mid file other apps can paste)", Scope::piano, false, true, "action"},
        {"notes.exportScore", "Export as score sheet (PDF)", Scope::piano, false, true, "action"},
        {"notes.pasteMidiClipboard", "Paste from MIDI clipboard (a copied .mid file)", Scope::piano, false, true, "action"},
        {"notes.cut", "Cut notes", Scope::piano, false, true, "action"},
        {"notes.paste", "Paste notes", Scope::piano, true, true, "command"},
        {"notes.flip", "Flip notes horizontally / vertically", Scope::piano, true, true, "command"},
        {"notes.controller.lfo", "LFO tool: write an LFO-shaped controller lane", Scope::piano, true, true, "command"},
        {"notes.randomize", "Randomize: generate notes in a scale", Scope::piano, true, true, "command"},
        {"notes.claw", "Claw machine: slice notes into steps and keep a rhythm", Scope::piano, true, true, "command"},
        {"notes.riff", "Riff machine: generate a stepwise riff in a scale", Scope::piano, true, true, "command"},
        {"notes.rotate", "Rotate notes (wrap within their bars)", Scope::piano, true, true, "command"},
        {"notes.articulate", "Articulate: note lengths as a share of the gap to the next note", Scope::piano, true, true, "command"},
        {"notes.scaleLevels", "Scale levels: multiply / offset / invert velocities", Scope::piano, true, true, "command"},
        {"notes.insertSpace", "Insert space in the Channel part", Scope::piano, true, true, "command"},
        {"notes.deleteSpace", "Delete space in the Channel part", Scope::piano, true, true, "command"},
        {"notes.insertSpaceAction", "Insert space (time selection)", Scope::piano, false, true, "action"},
        {"notes.insertSpaceSliced", "Slice and insert space (time selection)", Scope::piano, false, true, "action"},
        {"notes.deleteSpaceAction", "Delete space (time selection)", Scope::piano, false, true, "action"},
        {"notes.trim", "Trim the Channel part to a time range", Scope::piano, true, true, "command"},
        {"notes.trimAction", "Trim selection (keep only the time selection)", Scope::piano, false, true, "action"},
        {"notes.discardLengths", "Discard note lengths", Scope::piano, true, true, "command"},
        {"notes.limit", "Limit notes to a key range", Scope::piano, true, true, "command"},
        {"notes.flam", "Flam notes", Scope::piano, true, true, "command"},
        {"notes.rotateLeft", "Rotate notes left", Scope::piano, false, true, "action"},
        {"notes.rotateRight", "Rotate notes right", Scope::piano, false, true, "action"},
        {"notes.discardLengthsAction", "Discard note lengths (snap)", Scope::piano, false, true, "action"},
        {"selection.random", "Select one more note at random", Scope::piano, false, true, "action"},
        {"piano.resizeLeft", "Allow resizing notes from the left", Scope::piano, false, true, "action"},
        {"tool.drumPaint", "Paint drum sequencer tool", Scope::piano, false, true, "action"},
        {"tool.play", "Play selected tool (hold to audition)", Scope::global, false, true, "action"},
        {"view.noteGridHighlights", "Note grid highlights (shade black-key rows)", Scope::piano, false, true, "action"},
        {"view.snapIncomingMidi", "Snap incoming MIDI to the scale guide", Scope::piano, false, true, "action"},
        {"view.centerPlayhead", "Center the view on the playhead", Scope::global, false, true, "action"},
        {"view.pianoFollow", "Auto-scroll the Piano roll with the playhead", Scope::piano, false, true, "action"},
        {"view.pianoBackgroundWaveform", "Piano roll: show Playlist audio clips as a background waveform", Scope::piano, false, true, "action"},
        {"view.previewDuringPlayback", "Preview notes during playback (Piano roll, Channel Rack)", Scope::piano, false, true, "action"},
        {"mixer.linkAll", "Link all parameters of the selected Mixer track to MIDI CCs", Scope::mixer, false, true, "action"},
        {"mixer.renderArm", "Arm/disarm the selected Mixer track for rendering", Scope::mixer, false, true, "action"},
        {"mixer.renderArmed", "Render armed Mixer tracks to WAV files...", Scope::mixer, false, true, "action"},
        {"view.rackFocusSelected", "Channel Rack: focus (scroll to) the selected Channel automatically", Scope::rack, false, true, "action"},
        {"view.rackScrollToSelected", "Channel Rack: scroll to the selected Channel", Scope::rack, false, true, "action"},
        {"view.rackButtonsNarrow", "Channel Rack channel button width: narrow", Scope::rack, false, true, "action"},
        {"view.rackButtonsNormal", "Channel Rack channel button width: normal", Scope::rack, false, true, "action"},
        {"view.rackButtonsWide", "Channel Rack channel button width: wide", Scope::rack, false, true, "action"},
        {"view.followCenter", "Auto-scroll keeps the playhead centred", Scope::global, false, true, "action"},
        {"view.keyLabelsNone", "Piano roll keyboard labels: none", Scope::piano, false, true, "action"},
        {"view.keyLabelsRoot", "Piano roll keyboard labels: root notes (C)", Scope::piano, false, true, "action"},
        {"view.keyLabelsWhite", "Piano roll keyboard labels: all white notes", Scope::piano, false, true, "action"},
        {"selection.timeAround", "Select time around the selected notes", Scope::piano, false, true, "action"},
        {"selection.timePrevious", "Select previous time (shift the time selection left)", Scope::piano, false, true, "action"},
        {"selection.timeNext", "Select next time (shift the time selection right)", Scope::piano, false, true, "action"},
        {"selection.odd", "Select odd (off-beat) notes", Scope::piano, false, true, "action"},
        {"selection.muted", "Select muted notes", Scope::piano, false, true, "action"},
        {"selection.overlapping", "Select overlapping notes", Scope::piano, false, true, "action"},
        {"selection.stacked", "Select stacked notes", Scope::piano, false, true, "action"},
        {"selection.invert", "Invert selection", Scope::global, false, true, "action"},
        {"notes.duplicate", "Duplicate notes", Scope::piano, true, true, "command"},
        {"notes.velocity", "Note velocity", Scope::piano, true, true, "command"},
        {"notes.slice", "Slice notes", Scope::piano, true, true, "command"},
        {"notes.stretch", "Stretch selected notes", Scope::piano, true, true, "command"},
        {"piano.toolSlice", "Slice notes tool", Scope::piano, false, true, "action"},
        {"notes.mute", "Mute notes", Scope::piano, true, true, "command"},
        {"notes.unmute", "Unmute notes", Scope::piano, true, true, "command"},
        {"piano.toolMute", "Mute notes tool", Scope::piano, false, true, "action"},
        {"piano.exportMidi", "Export Piano Roll as MIDI file", Scope::piano, false, true, "vibera_project"},
        {"notes.quantize", "Quantizer...", Scope::piano, true, true, "command"},
        {"quantizer.groove.capture", "Capture score groove", Scope::piano, false, false, "vibera_groove"},
        {"quantizer.groove.presets", "Original groove presets", Scope::piano, false, false, "vibera_groove"},
        {"quantizer.groove.validate", "Validate groove template", Scope::piano, false, false,
         "vibera_groove"},
        {"quantizer.groove.load", "Load groove template", Scope::piano, false, false, "vibera_groove"},
        {"quantizer.groove.save", "Save groove template", Scope::piano, false, false, "vibera_groove"},
        {"quantizer.groove.preview", "Read active groove guides", Scope::piano, false, false,
         "vibera_groove"},
        {"notes.quickQuantize", "Quick quantize", Scope::piano, true, true, "command"},
        {"notes.quantizeStarts", "Quick quantize start times", Scope::piano, true, true, "command"},
        {"notes.legato", "Quick legato", Scope::piano, true, true, "command"},
        {"notes.chop", "Quick chop", Scope::piano, true, true, "command"},
        {"notes.glue", "Glue", Scope::piano, true, true, "command"},
        {"notes.group", "Group selected notes (click one selects all)", Scope::piano, true, true, "command"},
        {"notes.toAutomation", "Turn a controller lane into an automation clip", Scope::piano, true, true, "command"},
        {"notes.script", "Run a Piano roll script (JavaScript in text) on the Channel part", Scope::piano, true, true, "command"},
        {"notes.scriptAgain", "Run the last Piano roll script again", Scope::piano, false, true, "action"},
        {"notes.insertControllerValue", "Insert current controller value at the playhead (last edited lane)", Scope::piano, false, true, "action"},
        {"notes.ungroup", "Ungroup selected notes", Scope::piano, true, true, "command"},
        {"notes.chordify", "Chordify selection", Scope::piano, true, true, "command"},
        {"notes.clearControllers", "Clear recorded controllers", Scope::piano, true, true, "command"},
        {"notes.controller.add", "Add a controller event", Scope::piano, true, true, "command"},
        {"notes.controller.set", "Set a controller event", Scope::piano, true, true, "command"},
        {"notes.controller.delete", "Delete controller events", Scope::piano, true, true, "command"},
        {"notes.controller.interpolate", "Interpolate controller events in a time range", Scope::piano, true, true, "command"},
        {"channel.tuning", "Set Channel tuning", Scope::rack, true, true, "command"},
        {"channel.scale", "Set Channel keyboard-mapped scale", Scope::rack, true, true, "command"},
        {"channel.mpe", "Play Channel tuning through MPE", Scope::rack, true, true, "command"},
        {"channel.mtsEsp", "Broadcast Channel tuning via MTS-ESP", Scope::rack, true, true, "command"},
        {"channel.sliceToChannels", "Slice sample to Channels", Scope::rack, true, true, "command"},
        {"notes.arpeggiate", "Arpeggiate chords", Scope::piano, true, true, "command"},
        {"notes.strum", "Strum chords", Scope::piano, true, true, "command"},
        {"notes.humanize", "Humanize notes", Scope::piano, true, true, "command"},
        {"notes.scaleSnap", "Snap notes to scale", Scope::piano, true, true, "command"},
        {"notes.maqam", "Snap notes to maqam (quarter tones)", Scope::piano, true, true, "command"},
        {"notes.followChords", "Follow the Chord Track (notes move to the nearest chord tone)", Scope::piano, true, true, "command"},
        {"chord.add", "Add or replace a Chord Track chord", Scope::playlist, true, true, "command"},
        {"chord.set", "Change a Chord Track chord", Scope::playlist, true, true, "command"},
        {"chord.delete", "Delete Chord Track chords", Scope::playlist, true, true, "command"},
        {"chord.fill", "Write a chord progression to the Chord Track (one chord per bar)", Scope::playlist, true, true, "command"},
        {"marker.action", "Set a marker's playback action (start, loop, skip, pause)", Scope::playlist, true, true, "command"},
        {"channel.articulations", "Set the Channel expression map (articulations and keyswitches)", Scope::rack, true, true, "command"},
        {"channel.receiveMidi", "Receive notes from a MIDI channel (multitimbral live input)", Scope::rack, true, true, "command"},
        {"pattern.marker.add", "Add a Pattern marker (Piano roll ruler)", Scope::piano, true, true, "command"},
        {"pattern.marker.rename", "Rename a Pattern marker", Scope::piano, true, true, "command"},
        {"pattern.marker.delete", "Delete a Pattern marker", Scope::piano, true, true, "command"},
        {"notes.articulation", "Set the articulation of notes (expression map)", Scope::piano, true, true, "command"},
        {"rack.group.create", "Add Channel filter group", Scope::rack, true, true, "command"},
        {"rack.group.rename", "Rename Channel filter group", Scope::rack, true, true, "command"},
        {"rack.group.delete", "Delete Channel filter group", Scope::rack, true, true, "command"},
        {"rack.group.assign", "Group selected Channels", Scope::rack, true, true, "command"},
        {"rack.group.dialog", "Group selected Channels...", Scope::rack, false, true, "vibera_rack"},
        {"rack.group.previous", "Previous Channel filter", Scope::rack, false, true, "action"},
        {"rack.group.next", "Next Channel filter", Scope::rack, false, true, "action"},
        {"audio.add", "Place audio source", Scope::playlist, true, true, "command"},
        {"audio.trim", "Trim audio source window", Scope::playlist, true, true, "command"},
        {"audio.takes", "Place recorded takes", Scope::playlist, true, true, "command"},
        {"audio.comp", "Use this take here (comp)", Scope::playlist, true, true, "command"},
        {"audio.flattenComp", "Flatten comp (remove unused takes)", Scope::playlist, true, true, "command"},
        {"audio.alignToGrid", "Align first transient to the grid", Scope::playlist, false, true, "vibera_project"},
        {"audio.fade", "Set Audio Clip fades", Scope::playlist, true, true, "command"},
        {"audio.stretchApply", "Use a stretched source for an Audio Clip", Scope::playlist, true, true, "command"},
        {"clip.pitchDown", "Pitch selected Audio Clip one semitone down (new file)", Scope::playlist, false, true, "action"},
        {"clip.pitchUp", "Pitch selected Audio Clip one semitone up (new file)", Scope::playlist, false, true, "action"},
        {"clip.reverse", "Reverse selected Audio Clip (new file)", Scope::playlist, false, true, "action"},
        {"arrangement.add", "New arrangement", Scope::playlist, true, true, "command"},
        {"arrangement.clone", "Clone arrangement", Scope::playlist, true, true, "command"},
        {"arrangement.select", "Switch arrangement", Scope::playlist, true, true, "command"},
        {"arrangement.rename", "Rename arrangement", Scope::playlist, true, true, "command"},
        {"arrangement.merge", "Merge another arrangement into this one", Scope::playlist, true, true, "command"},
        {"arrangement.delete", "Delete arrangement", Scope::playlist, true, true, "command"},
        {"tempo.change.add", "Add tempo change", Scope::playlist, true, true, "command"},
        {"tempo.change.set", "Edit tempo change", Scope::playlist, true, true, "command"},
        {"tempo.change.delete", "Delete tempo change", Scope::playlist, true, true, "command"},
        {"tempo.curve", "Draw tempo curve", Scope::playlist, true, true, "command"},
        {"tempo.fromAutomation", "Use automation clip as tempo", Scope::playlist, true, true, "command"},
        {"marker.add", "Add Playlist marker", Scope::playlist, true, true, "command"},
        {"marker.addEvery", "Add a marker every N bars (value 1..64)", Scope::playlist, true, true, "command"},
        {"marker.addPair", "Add markers at the start and end of a time range", Scope::playlist, true, true, "command"},
        {"marker.rename", "Rename Playlist marker", Scope::playlist, true, true, "command"},
        {"marker.move", "Move Playlist marker", Scope::playlist, true, true, "command"},
        {"marker.delete", "Delete Playlist marker", Scope::playlist, true, true, "command"},
        {"meter.change.add", "Add time-signature change", Scope::playlist, true, true, "command"},
        {"meter.change.set", "Edit time-signature change", Scope::playlist, true, true, "command"},
        {"meter.change.delete", "Delete time-signature change", Scope::playlist, true, true, "command"},
        {"track.rename", "Rename Playlist track", Scope::playlist, true, true, "command"},
        {"track.colour", "Set Playlist track colour", Scope::playlist, true, true, "command"},
        {"track.mute", "Mute Playlist track", Scope::playlist, true, true, "command"},
        {"track.solo", "Solo Playlist track", Scope::playlist, true, true, "command"},
        {"track.group", "Group Playlist track with the track above", Scope::playlist, true, true, "command"},
        {"track.collapse", "Collapse Playlist track group", Scope::playlist, true, true, "command"},
        {"track.lock", "Lock Playlist track", Scope::playlist, true, true, "command"},
        {"track.height", "Set Playlist track height", Scope::playlist, true, true, "command"},
        {"track.instrument", "Make instrument track", Scope::playlist, true, true, "command"},
        {"track.instrumentClip", "New Pattern on instrument track", Scope::playlist, true, true, "command"},
        {"track.audio", "Make audio track", Scope::playlist, true, true, "command"},
        {"track.insert", "Insert Playlist track", Scope::playlist, true, true, "command"},
        {"track.delete", "Delete Playlist track", Scope::playlist, true, true, "command"},
        {"track.move", "Move Playlist track", Scope::playlist, true, true, "command"},
        {"track.clone", "Clone Playlist track (with its clips)", Scope::playlist, true, true, "command"},
        {"track.selectPrevious", "Select the track above", Scope::playlist, false, true, "action"},
        {"track.selectNext", "Select the track below", Scope::playlist, false, true, "action"},
        {"track.extendPrevious", "Extend the track selection up", Scope::playlist, false, true, "action"},
        {"track.extendNext", "Extend the track selection down", Scope::playlist, false, true, "action"},
        {"track.cloneSelected", "Clone selected Playlist tracks", Scope::playlist, false, true, "action"},
        {"track.autoName", "Auto name Playlist track from its first clip", Scope::playlist, true, true, "command"},
        {"track.reset", "Reset Playlist track name and colour", Scope::playlist, true, true, "command"},
        {"marker.next", "Jump to next marker", Scope::playlist, false, true, "action"},
        {"marker.previous", "Jump to previous marker", Scope::playlist, false, true, "action"},
        {"marker.selectSection", "Select section between markers", Scope::playlist, false, true, "action"},
        {"clip.group", "Group Clips", Scope::playlist, true, true, "command"},
        {"clip.ungroup", "Ungroup Clips", Scope::playlist, true, true, "command"},
        {"playlist.grouping", "Enable Clip grouping", Scope::playlist, false, true, "action"},
        {"clip.add", "Place Pattern", Scope::playlist, true, true, "command"},
        {"clip.move", "Move / resize clip", Scope::playlist, true, true, "command"},
        {"clip.delete", "Delete clips", Scope::playlist, true, true, "command"},
        {"clip.duplicate", "Duplicate clips", Scope::playlist, true, true, "command"},
        {"clip.unique", "Make Unique", Scope::playlist, true, true, "command"},
        {"clip.splitChannels", "Split Pattern clip by channel", Scope::playlist, true, true, "command"},
        {"clip.copy", "Copy clips", Scope::playlist, false, true, "action"},
        {"clip.cut", "Cut clips", Scope::playlist, false, true, "action"},
        {"clip.paste", "Paste clips at the playhead", Scope::playlist, true, true, "command"},
        {"clip.chop", "Chop clips into bars or beats", Scope::playlist, true, true, "command"},
        {"clip.trimStart", "Trim Pattern clip start", Scope::playlist, true, true, "command"},
        {"clip.render", "Render selected clips to audio", Scope::playlist, false, true, "action"},
        {"clip.quantize", "Quick quantize clip start times", Scope::playlist, true, true, "command"},
        {"time.insert", "Insert space (time selection)", Scope::playlist, true, true, "command"},
        {"time.delete", "Delete space (time selection)", Scope::playlist, true, true, "command"},
        {"time.insertSliced", "Insert space and slice clips (time selection)", Scope::playlist, false, true, "action"},
        {"clip.mergeSimilar", "Merge similar Pattern clips (the selected stack and its repeats)", Scope::playlist, true, true, "command"},
        {"clip.merge", "Merge Pattern clips", Scope::playlist, true, true, "command"},
        {"clip.slice", "Slice clips at beat", Scope::playlist, true, true, "command"},
        {"clip.slip", "Slip source content", Scope::playlist, true, true, "command"},
        {"clip.mute", "Mute clips", Scope::playlist, true, true, "command"},
        {"mixer.gain", "Mixer volume", Scope::mixer, true, true, "command"},
        {"mixer.route", "Enable Mixer send", Scope::mixer, true, true, "command"},
        {"mixer.routeOnly", "Route to this insert only", Scope::mixer, true, true, "command"},
        {"mixer.send", "Mixer send level", Scope::mixer, true, true, "command"},
        {"automation.record.arm", "Record Song Mixer automation from MIDI", Scope::mixer, false, true,
         "vibera_automationRecording"},
        {"automation.record.finish", "Keep recorded Mixer automation", Scope::mixer, false, true,
         "vibera_automationRecording"},
        {"automation.record.cancel", "Cancel recorded Mixer automation", Scope::mixer, false, true,
         "vibera_automationRecording"},
        {"automation.create", "Create automation clip", Scope::playlist, true, true, "command"},
        {"automation.sampler.create", "Create sampler automation clip", Scope::sampler, true, false,
         "command"},
        {"automation.sampler.init", "Initialize sampler modulation parameter", Scope::sampler, true, false,
         "command"},
        {"automation.sampler.init.remove", "Remove sampler modulation initialization", Scope::sampler, true,
         false, "command"},
        {"automation.sampler.link", "Link sampler modulation parameter", Scope::sampler, true, false,
         "command"},
        {"automation.sampler.unlink", "Unlink sampler modulation parameter", Scope::sampler, true, false,
         "command"},
        {"automation.sampler.range", "Set sampler modulation automation range", Scope::sampler, true, false,
         "command"},
        {"automation.plugin.create", "Create native plugin automation clip", Scope::playlist, true, false,
         "command"},
        {"automation.plugin.init", "Initialize native plugin parameter", Scope::playlist, true, false,
         "command"},
        {"automation.plugin.init.remove", "Delete initialized native plugin parameter", Scope::playlist, true,
         false, "command"},
        {"automation.place", "Place automation clip", Scope::playlist, true, true, "command"},
        {"automation.move", "Move / resize automation clip", Scope::playlist, true, true, "command"},
        {"automation.delete", "Delete automation placement", Scope::playlist, true, true, "command"},
        {"automation.duplicate", "Duplicate linked automation clip", Scope::playlist, true, true, "command"},
        {"automation.unique", "Make automation unique", Scope::playlist, true, true, "command"},
        {"automation.mute", "Mute automation clip", Scope::playlist, true, true, "command"},
        {"automation.point.add", "Add automation point", Scope::playlist, true, true, "command"},
        {"automation.point.move", "Move automation point", Scope::playlist, true, true, "command"},
        {"automation.point.delete", "Delete automation point", Scope::playlist, true, true, "command"},
        {"automation.point.shape", "Automation point curve shape", Scope::playlist, true, true, "command"},
        {"automation.source.rename", "Rename automation source", Scope::playlist, true, true, "command"},
        {"automation.source.clone", "Clone automation source / with links", Scope::playlist, true, true,
         "command"},
        {"automation.source.delete", "Delete automation source", Scope::playlist, true, true, "command"},
        {"automation.link", "Link automation target", Scope::playlist, true, true, "command"},
        {"automation.unlink", "Unlink automation target", Scope::playlist, true, true, "command"},
        {"automation.range", "Automation target range", Scope::playlist, true, true, "command"},
        {"automation.init", "Init song with this position", Scope::mixer, true, true, "command"},
        {"automation.init.remove", "Delete initialized control", Scope::mixer, true, true, "command"},
        {"automation.source.flip", "Flip automation curve vertically", Scope::playlist, true, true, "command"},
        {"automation.source.normalize", "Normalize automation curve levels", Scope::playlist, true, true, "command"},
        {"automation.source.smooth", "Turn all automation points smooth", Scope::playlist, true, true, "command"},
        {"automation.source.scale", "Scale automation curve levels (value x, offset)", Scope::playlist, true, true, "command"},
        {"automation.merge", "Merge Automation Clips", Scope::playlist, true, true, "command"},
        {"clip.rippleDelete", "Ripple delete clips (close the gap on their track)", Scope::playlist, true, true, "command"},
        {"automation.modulator", "Add a modulator (LFO / random) to a Mixer or Channel control", Scope::mixer, true, true,
         "command"},
        {"automation.plugin.modulator", "Add a modulator (LFO / random) to a plugin parameter", Scope::playlist, true,
         false, "command"},
        {"automation.modulator.set", "Change a modulator's shape, rate, depth and phase", Scope::mixer, true, true,
         "command"},
        {"automation.modulator.enable", "Turn a modulator on or off", Scope::mixer, true, true, "command"},
        {"automation.modulator.delete", "Remove a modulator", Scope::mixer, true, true, "command"},
        {"automation.macro.add", "Add a macro knob", Scope::mixer, true, true, "command"},
        {"automation.macro.set", "Turn a macro knob (0..1)", Scope::mixer, true, true, "command"},
        {"automation.macro.rename", "Rename a macro knob", Scope::mixer, true, true, "command"},
        {"automation.macro.delete", "Delete a macro knob", Scope::mixer, true, true, "command"},
        {"automation.macro.link", "Link a Mixer or Channel control to a macro knob", Scope::mixer, true, true, "command"},
        {"automation.plugin.macro.link", "Link a plugin parameter to a macro knob", Scope::playlist, true, false, "command"},
        {"automation.macro.unlink", "Unlink a macro knob target", Scope::mixer, true, true, "command"}};
    return commands;
}
namespace
{
juce::Result validateNativeScalarSnapshots(const Project &, const Project &,
                                           std::span<const NativeScalarEdit>);
}
class StateAction final : public juce::UndoableAction
{
    Controller &owner;
    Project before, after;
    const int cachedUnits;
    const std::vector<NativeScalarEdit> scalarEdits;
    const bool scalarProof;

  public:
    const std::vector<NativeScalarEdit> &nativeScalars() const noexcept { return scalarEdits; }
    bool isNativeScalarAction() const noexcept
    {
        return scalarProof && !scalarEdits.empty();
    }
    static int checkedUnits(const Project &before, const Project &after, std::size_t scalars = 0)
    {
        const auto beforeUnits = before.state().toXmlString().length();
        const auto afterUnits = after.state().toXmlString().length();
        const auto total = std::int64_t(beforeUnits) + std::int64_t(afterUnits) +
                           std::int64_t(scalars) * std::int64_t(sizeof(NativeScalarEdit));
        if (beforeUnits < 0 || afterUnits < 0 || total > std::numeric_limits<int>::max())
            throw std::length_error("history.snapshotCostOverflow");
        return int(total);
    }
    StateAction(Controller &c, Project b, Project a, std::span<const NativeScalarEdit> scalars = {},
                bool provenScalars = false)
        : owner(c), before(std::move(b)), after(std::move(a)),
          cachedUnits(checkedUnits(before, after, scalars.size())),
          scalarEdits(scalars.begin(), scalars.end()), scalarProof(provenScalars)
    {
        if (!scalarEdits.empty() && !scalarProof)
            throw std::invalid_argument("parameterHistory.unprovenAction");
    }
    int cachedSize() const noexcept
    {
        return cachedUnits;
    }
    bool perform() override
    {
        owner.project = after;
        owner.notify();
        return true;
    }
    bool undo() override
    {
        owner.project = before;
        owner.notify();
        return true;
    }
    bool belongsTo(const Controller &controller) const noexcept
    {
        return &owner == &controller;
    }
    const Project &beforeSnapshot() const noexcept
    {
        return before;
    }
    const Project &afterSnapshot() const noexcept
    {
        return after;
    }
    int getSizeInUnits() override
    {
        return cachedUnits;
    }
};
// Controller::markExternalChange documents native state as outside Project
// snapshot Undo. Save/capture can refresh opaque state without a history action.
// Preserve that CURRENT capture only across independently proven pure routing
// actions; immutable snapshots and all other state/descriptor semantics remain.
static void retainCapturedStateForRoutingHistory(const Controller& owner,
    const juce::Array<const juce::UndoableAction*>& actions, Project& destination)
{
    if (actions.isEmpty()) return;
    for (const auto* action : actions)
    {
        const auto* snapshot = dynamic_cast<const StateAction*>(action);
        if (!snapshot || !snapshot->belongsTo(owner)) return;
        const auto& before = snapshot->beforeSnapshot();
        auto normalized = snapshot->afterSnapshot();
        if (before.channels.size() != normalized.channels.size()) return;
        bool changed = false;
        for (auto& channel : normalized.channels)
        {
            const auto* source = before.channel(channel.id);
            if (!source) return;
            changed |= channel.mixer != source->mixer;
            channel.mixer = source->mixer;
        }
        // Positive proof against ORIGINAL action snapshots, not the current
        // destination. State/mixed/descriptor changes get no exception.
        if (!changed || !normalized.state().isEquivalentTo(before.state())) return;
    }
    const auto preserve = [&](PluginSpec& plugin)
    {
        const auto* current = owner.project.plugin(plugin.id);
        if (current && current->descriptor == plugin.descriptor)
            plugin.state = current->state;
    };
    for (auto& channel : destination.channels)
        if (channel.instrument) preserve(*channel.instrument);
    for (auto& insert : destination.mixer)
        for (auto& effect : insert.effects) preserve(effect);
}
juce::Result Controller::previewHistoryDestination(HistoryDirection direction,
                                                   HistoryDestination &destination) const
{
    if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
        return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
    if (hasGesture() || undo.isPerformingUndoRedo())
        return juce::Result::fail("history.busy: Finish the current edit before preparing history");
    juce::Array<const juce::UndoableAction *> actions;
    if (direction == HistoryDirection::undo)
        undo.getActionsInUndoTransaction(actions);
    else
        undo.getActionsInRedoTransaction(actions);
    if (actions.isEmpty())
        return juce::Result::fail("history.unavailable: No transaction in this direction");
    for (const auto *action : actions)
    {
        const auto *snapshot = dynamic_cast<const StateAction *>(action);
        if (!snapshot || !snapshot->belongsTo(*this))
            return juce::Result::fail("history.unsupportedTransaction: Project snapshot actions required");
    }
    // Undo executes reverse order, so the earliest before snapshot is final.
    // Redo executes forward order, so the latest after snapshot is final.
    const auto *edge = static_cast<const StateAction *>(
        direction == HistoryDirection::undo ? actions.getFirst() : actions.getLast());
    HistoryDestination next;
    next.controller = this;
    next.sourceProject = project;
    next.destinationProject =
        direction == HistoryDirection::undo ? edge->beforeSnapshot() : edge->afterSnapshot();
    retainCapturedStateForRoutingHistory(*this, actions, next.destinationProject);
    if (auto valid = next.destinationProject.validate(); valid.failed())
        return juce::Result::fail("history.invalidDestination: " + valid.getErrorMessage());
    next.identity = projectIdentity;
    next.sourceRevision = revision;
    next.requestedDirection = direction;
    next.name = direction == HistoryDirection::undo ? undo.getUndoDescription() : undo.getRedoDescription();
    next.actions.reserve(size_t(actions.size()));
    for (const auto *action : actions)
        next.actions.push_back(reinterpret_cast<std::uintptr_t>(action));
    destination = std::move(next);
    return juce::Result::ok();
}
juce::Result Controller::validateHistoryDestination(const HistoryDestination &destination) const
{
    if (destination.controller != this || destination.identity != projectIdentity ||
        destination.sourceRevision != revision ||
        !destination.sourceProject.state().isEquivalentTo(project.state()))
        return juce::Result::fail("history.staleDestination: Project changed during preparation");
    HistoryDestination current;
    if (auto result = previewHistoryDestination(destination.requestedDirection, current); result.failed())
        return result;
    if (current.actions != destination.actions || current.name != destination.name ||
        !current.destinationProject.state().isEquivalentTo(destination.destinationProject.state()))
        return juce::Result::fail("history.staleDestination: History changed during preparation");
    return juce::Result::ok();
}
static_assert(std::is_nothrow_swappable_v<Project>, "Canonical Project publication must not throw");
static_assert(std::is_nothrow_move_constructible_v<Project>, "Prepared ownership must move without throwing");
namespace
{
bool admittedSnapshotUnits(const juce::UndoableAction &action, int &units, void *context) noexcept
{
    const auto *snapshot = dynamic_cast<const StateAction *>(&action);
    if (!snapshot || !snapshot->belongsTo(*static_cast<const Controller *>(context)))
        return false;
    units = snapshot->cachedSize();
    return units >= 0;
}
} // namespace
juce::Result Controller::prepareSnapshotChange(Project next, const Project &historyBefore,
                                               const juce::String &name, bool recordHistory,
                                               bool finishGesture, bool destinationDirty,
                                               std::unique_ptr<PreparedControllerChange> &output,
                                               std::span<const NativeScalarEdit> nativeScalars)
{
    try
    {
        if (revision == std::numeric_limits<std::uint64_t>::max() || undo.isPerformingUndoRedo())
            return juce::Result::fail("history.busy: Cannot prepare canonical mutation");
        for (auto &pattern : next.patterns) // A deleted automation source leaves no Pattern window behind.
            std::erase_if(pattern.automation, [&](const auto &w)
                          { return std::none_of(next.automation.sources.begin(), next.automation.sources.end(),
                                                [&](const auto &s) { return s.id == w.source; }); });
        bool unbound = false; // A deleted Channel turns its instrument track back into a free track.
        for (auto &track : next.tracks)
            if (track.channel != 0 && !next.channel(track.channel))
            {
                track.channel = 0;
                unbound = true;
            }
        if (unbound)
            std::erase_if(next.tracks, [](const auto &t)
                          { return t.name.isEmpty() && t.colour == 0 && !t.muted && !t.groupedAbove && !t.collapsed &&
                                   !t.locked && t.height == 1 && t.channel == 0 && !t.audio; });
        if (auto valid = next.validate(); valid.failed())
            return valid;
        if (recordHistory)
            if (auto valid = historyBefore.validate(); valid.failed())
                return valid;
        auto prepared = std::make_unique<PreparedControllerChange>();
        prepared->controller = this;
        prepared->sourceProject = project;
        prepared->nextProject = std::move(next);
        prepared->identity = projectIdentity;
        prepared->owner = editOwner;
        prepared->sourceRevision = revision;
        prepared->sourceGestureSerial = editSerial;
        prepared->sourceDirty = dirty;
        prepared->sourceGestureDirty = gestureDirty;
        prepared->sourceHasGesture = bool(gestureStart);
        prepared->clearGesture = finishGesture;
        prepared->clearQuantizer = finishGesture;
        prepared->destinationDirty = destinationDirty;
        if (!nativeScalars.empty() && !recordHistory)
            return juce::Result::fail("parameterHistory.invalidTransaction");
        if (recordHistory)
        {
            if (!nativeScalars.empty())
                if (auto valid =
                        validateNativeScalarSnapshots(historyBefore, prepared->nextProject, nativeScalars);
                    valid.failed())
                    return valid;
            auto action = std::make_unique<StateAction>(*this, historyBefore, prepared->nextProject,
                                                        nativeScalars, !nativeScalars.empty());
            const auto *beforeSnapshot = &action->beforeSnapshot();
            const auto units = action->cachedSize();
            prepared->history =
                undo.prepareNewTransaction(std::move(action), name, units, admittedSnapshotUnits, this);
            if (!prepared->history)
                return juce::Result::fail("history.unsupportedTransaction: Snapshot admission refused");
            prepared->historyBeforeSnapshot = beforeSnapshot; // Same action is now owned by history.
        }
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.preparationFailed: Canonical source and history unchanged");
    }
}
juce::Result Controller::prepareNativeStateRefresh(Project next,
    std::unique_ptr<PreparedControllerChange>& output)
{
    try
    {
        if (hasGesture()) return juce::Result::fail("device.activeGesture: Opaque refresh cannot replace a preview");
        if (auto valid = next.validate(); valid.failed()) return valid;
        auto normalized = next;
        const auto normalize = [&](PluginSpec& value)
        {
            const auto* source = project.plugin(value.id);
            if (!source || source->descriptor != value.descriptor) return false;
            value.state = source->state; return true;
        };
        for (auto& channel : normalized.channels)
            if (channel.instrument && !normalize(*channel.instrument))
                return juce::Result::fail("device.invalidCapture: Native identity changed");
        for (auto& insert : normalized.mixer)
            for (auto& effect : insert.effects)
                if (!normalize(effect)) return juce::Result::fail("device.invalidCapture: Native identity changed");
        if (!normalized.state().isEquivalentTo(project.state()))
            return juce::Result::fail("device.invalidCapture: Only current opaque native state may refresh");
        const bool changed = !next.state().isEquivalentTo(project.state());
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(std::move(next), project, {}, false, false,
                                                dirty || changed, prepared); result.failed()) return result;
        prepared->incrementRevision = false;
        prepared->clearQuantizer = false;
        prepared->commandNoOp = !changed;
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...) { return juce::Result::fail("device.capturePreparationFailed: Source/history retained"); }
}
juce::Result Controller::prepareHistoryChange(HistoryDirection direction,
                                              std::unique_ptr<PreparedControllerChange> &output)
{
    if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
        return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
    try
    {
        if (revision == std::numeric_limits<std::uint64_t>::max() || undo.isPerformingUndoRedo())
            return juce::Result::fail("history.busy: Cannot prepare history");
        auto history =
            direction == HistoryDirection::undo ? undo.prepareUndoMutation() : undo.prepareRedoMutation();
        juce::Array<const juce::UndoableAction *> actions;
        if (history && !undo.getPreparedMutationActions(*history, actions))
            return juce::Result::fail("history.stalePreparedChange: History changed during preparation");
        if (!history && (direction == HistoryDirection::undo ? undo.canUndo() : undo.canRedo()))
            return juce::Result::fail("history.busy: History admission refused");
        if (!history && !gestureStart)
            return juce::Result::fail("history.unavailable: No transaction in this direction");
        if (history && actions.isEmpty())
            return juce::Result::fail("history.unsupportedTransaction: Empty action set");
        for (const auto *action : actions)
        {
            int units = -1;
            if (!admittedSnapshotUnits(*action, units, this))
                return juce::Result::fail(
                    "history.unsupportedTransaction: Project snapshot actions required");
            if (static_cast<const StateAction *>(action)->isNativeScalarAction())
                return juce::Result::fail(
                    "parameterHistory.captureRequired: Fresh current native state required");
        }
        Project destination;
        if (history)
        {
            const auto *edge = static_cast<const StateAction *>(
                direction == HistoryDirection::undo ? actions.getFirst() : actions.getLast());
            destination =
                direction == HistoryDirection::undo ? edge->beforeSnapshot() : edge->afterSnapshot();
        }
        else
            destination = *gestureStart;
        if (history) retainCapturedStateForRoutingHistory(*this, actions, destination);
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(std::move(destination), project, {}, false,
                                                bool(gestureStart), history ? true : gestureDirty, prepared);
            result.failed())
            return result;
        prepared->clearQuantizer = true;
        prepared->history = std::move(history);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.preparationFailed: Canonical source and history unchanged");
    }
}

juce::Result Controller::prepareStoppedEffectHistory(HistoryDirection direction, int insert,
    const Project& capturedSource, std::unique_ptr<PreparedControllerChange>& output)
{
    try
    {
        if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
            return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
        if (hasGesture() || undo.isPerformingUndoRedo() || revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("history.busy: Finish the current edit before native inverse preparation");
        if (auto valid = capturedSource.validate(); valid.failed()) return valid;
        const auto copyPresentNativeStates = [] (Project& target, const Project& current)
        {
            const auto copy = [&] (PluginSpec& plugin)
            {
                if (const auto* present = current.plugin(plugin.id); present && present->descriptor == plugin.descriptor)
                    plugin.state = present->state;
            };
            for (auto& channel : target.channels) if (channel.instrument) copy(*channel.instrument);
            for (auto& row : target.mixer) for (auto& plugin : row.effects) copy(plugin);
        };
        auto canonicalCapture = capturedSource;
        copyPresentNativeStates(canonicalCapture, project);
        if (! canonicalCapture.state().isEquivalentTo(project.state()))
            return juce::Result::fail("history.invalidCapture: Only same-ID/descriptor opaque states may differ");
        juce::Array<const juce::UndoableAction*> actions;
        if (direction == HistoryDirection::undo) undo.getActionsInUndoTransaction(actions);
        else undo.getActionsInRedoTransaction(actions);
        if (actions.size() != 1)
            return juce::Result::fail("history.unsupportedTransaction: One stopped effect snapshot action required");
        const auto* action = dynamic_cast<const StateAction*>(actions[0]);
        if (! action || !action->belongsTo(*this))
            return juce::Result::fail("history.unsupportedTransaction: Owned canonical snapshot required");
        StoppedEffectChangePlan delta;
        if (auto valid = planStoppedEffectChange(action->beforeSnapshot(), action->afterSnapshot(), insert, delta);
            valid.failed())
            return juce::Result::fail("history.unsupportedEffectDelta: " + valid.getErrorMessage());
        auto before = action->beforeSnapshot(), after = action->afterSnapshot();
        const auto& historicalSource = direction == HistoryDirection::undo ? after : before;
        auto source = historicalSource;
        copyPresentNativeStates(source, project);
        if (! source.state().isEquivalentTo(project.state()))
            return juce::Result::fail("history.staleNativeSource: Musical source no longer matches actual history edge");
        // Positive original pure-chain proof above; absent reinserted plugins
        // retain their exact prior captured history bytes. No generic state/mixed
        // action exemption, and no native operation runs from this primitive.
        copyPresentNativeStates(before, capturedSource);
        copyPresentNativeStates(after, capturedSource);
        const auto& capturedEdge = direction == HistoryDirection::undo ? after : before;
        if (! capturedEdge.state().isEquivalentTo(capturedSource.state()))
            return juce::Result::fail("history.invalidCapture: Captured source must be the exact rebased history edge");
        const auto& destination = direction == HistoryDirection::undo ? before : after;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(destination, project, {}, false, false, true, prepared); result.failed())
            return result;
        auto replacement = std::make_unique<StateAction>(*this, std::move(before), std::move(after));
        const auto* capturedPointer = direction == HistoryDirection::undo ? &replacement->afterSnapshot()
                                                                         : &replacement->beforeSnapshot();
        const auto units = replacement->cachedSize();
        auto history = undo.prepareHistoryReplacement(direction == HistoryDirection::undo,
            std::move(replacement), units, admittedSnapshotUnits, this);
        if (!history)
            return juce::Result::fail("history.inverseAdmissionRefused: Stale, unsupported or over-budget history retained");
        prepared->history = std::move(history);
        prepared->nativeEffectHistorySource = capturedPointer;
        prepared->nativeEffectHistoryInsert = insert;
        prepared->nativeEffectHistoryDirection = direction;
        prepared->nativeEffectHistoryIsTraversal = true;
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.inversePreparationFailed: Source and actual history unchanged");
    }
}

bool Controller::isPreparedChangeCurrent(const PreparedControllerChange &prepared) const noexcept
{
    return prepared.controller == this && !prepared.committed && prepared.sourceRevision == revision &&
           prepared.identity == projectIdentity && prepared.sourceGestureSerial == editSerial &&
           prepared.sourceDirty == dirty && prepared.sourceGestureDirty == gestureDirty &&
           prepared.sourceHasGesture == bool(gestureStart) && prepared.owner == editOwner &&
           revision != std::numeric_limits<std::uint64_t>::max() &&
           (!prepared.history || undo.isPreparedMutationCurrent(*prepared.history));
}
juce::Result Controller::validatePreparedChange(PreparedControllerChange &prepared) const
{
    prepared.validated = false;
    try
    {
        if (!isPreparedChangeCurrent(prepared))
            return juce::Result::fail(
                "history.stalePreparedChange: Canonical source changed during preparation");
        if (auto valid = prepared.nextProject.validate(); valid.failed())
            return valid;
        if (applicationChangeAdmission)
            if (auto admitted = applicationChangeAdmission(prepared.nextProject); admitted.failed())
                return admitted;
        if (!isPreparedChangeCurrent(prepared) ||
            !prepared.sourceProject.state().isEquivalentTo(project.state()))
            return juce::Result::fail(
                "history.stalePreparedChange: Canonical source changed during admission");
        // No callback, message pump or user code may intervene after this exact check.
        // All subsequent checks and the canonical+manager commit are noexcept.
        if (!isPreparedChangeCurrent(prepared))
            return juce::Result::fail("history.stalePreparedChange: Admission changed during validation");
        prepared.validated = true;
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail(
            "history.preparationFailed: Canonical validation failed before publication");
    }
}
void Controller::applyPreparedChange(void *context) noexcept
{
    auto &prepared = *static_cast<PreparedControllerChange *>(context);
    auto &owner = *prepared.controller;
    if (prepared.commandNoOp)
    {
        prepared.committed = true;
        return; // No model swap, history movement, notification or dirty/revision write.
    }
    using std::swap;
    swap(owner.project, prepared.nextProject);
    if (prepared.clearGesture)
    {
        owner.gestureStart.swap(prepared.retiredGesture);
        owner.editOwner.swapWith(prepared.retiredOwner);
    }
    if (prepared.clearQuantizer)
        owner.quantizerStart.swap(prepared.retiredQuantizer);
    if (prepared.incrementRevision)
        ++owner.revision;
    owner.dirty = prepared.destinationDirty;
    owner.pendingControllerNotification = owner.pendingControllerNotification || prepared.incrementRevision;
    owner.pendingUndoNotification = owner.pendingUndoNotification || bool(prepared.history);
    prepared.committed = true;
}
bool Controller::commitPreparedChange(PreparedControllerChange &prepared) noexcept
{
    if (!prepared.validated || !isPreparedChangeCurrent(prepared))
        return false;
    if (prepared.history)
        return undo.commitPreparedMutation(*prepared.history, applyPreparedChange, &prepared);
    applyPreparedChange(&prepared);
    return true;
}
bool Controller::deliverPendingNotifications() noexcept
{
    if (deliveringNotifications)
        return false;
    deliveringNotifications = true;
    if (pendingUndoNotification)
    {
        pendingUndoNotification = false;
        try
        {
            undo.sendChangeMessage();
        }
        catch (...)
        {
            pendingUndoNotification = true;
        }
    }
    if (pendingControllerNotification)
    {
        pendingControllerNotification = false;
        try
        {
            if (changed)
                changed();
        }
        catch (...)
        {
            pendingControllerNotification = true;
        }
    }
    deliveringNotifications = false;
    return !pendingControllerNotification && !pendingUndoNotification;
}
void Controller::notify()
{
    // Legacy callers retain intrinsic dirty/revision semantics. Observer failure
    // is post-mutation pending work and cannot turn a real change into rollback.
    if (revision != std::numeric_limits<std::uint64_t>::max())
        ++revision;
    dirty = true;
    pendingControllerNotification = true;
    deliverPendingNotifications();
}
void Controller::replace(Project next, bool deferNotifications)
{
    gestureStart.reset();
    quantizerStart.reset();
    editOwner.clear();
    projectIdentity = juce::Uuid().toString();
    project = std::move(next);
    mixerChainNextId = project.nextId; // A new projectIdentity starts a new reservation session.
    mixerInsertNextId = project.nextId;
    undo.clearUndoHistory();
    dirty = false;
    if (revision != std::numeric_limits<std::uint64_t>::max())
        ++revision;
    pendingControllerNotification = true;
    if (!deferNotifications)
        deliverPendingNotifications();
}
void Controller::markExternalChange()
{
    if (externalChangeVersion != std::numeric_limits<std::uint64_t>::max())
        ++externalChangeVersion;
    if (gestureStart && editOwner.isNotEmpty())
        cancelGesture();
    dirty = true;
    if (gestureStart)
        gestureDirty = true;
}
bool Controller::beginGesture(const juce::String &owner)
{
    if (applicationGestureAdmission)
        try { if (applicationGestureAdmission().failed()) return false; }
        catch (...) { return false; }
    const bool localTakeover = gestureStart && editOwner.isNotEmpty() && owner.isEmpty();
    if ((gestureStart && !localTakeover) || editSerial == std::numeric_limits<std::uint64_t>::max())
        return false;
    // Prepare all new owners before cancellation/ownership metadata can change.
    auto baseline = std::make_unique<Project>(localTakeover ? *gestureStart : project);
    auto preparedOwner = owner;
    std::unique_ptr<Project> retiredPreview, retiredQuantizer;
    juce::String retiredOwner;
    // BEGIN intrinsic takeover: all owners already prepared, no user callback,
    // allocation or resource destruction until the new gesture is fully visible.
    if (localTakeover)
    {
        using std::swap;
        swap(project, *gestureStart);
        retiredPreview.swap(gestureStart);
        retiredOwner.swapWith(editOwner);
        if (revision != std::numeric_limits<std::uint64_t>::max())
            ++revision;
        dirty = gestureDirty;
        pendingControllerNotification = true;
    }
    retiredQuantizer.swap(quantizerStart);
    gestureStart.swap(baseline);
    ++editSerial;
    editOwner.swapWith(preparedOwner);
    gestureDirty = dirty;
    // END intrinsic takeover. Reentrant observers now see the complete local
    // gesture and may edit/end it or acquire another owner; never overwrite them.
    if (localTakeover)
        deliverPendingNotifications();
    return true;
}
juce::Result Controller::endGestureChecked(const juce::String &name, const juce::String &owner)
{
    if (!gestureStart)
        return juce::Result::ok();
    if (editOwner != owner)
    {
        if (owner.isEmpty())
            return cancelGestureChecked();
        return juce::Result::fail("history.gestureOwnership: Edit gesture ownership expired");
    }
    std::unique_ptr<PreparedControllerChange> prepared;
    try
    {
        const bool noChange = gestureStart->state().isEquivalentTo(project.state());
        if (auto result = prepareSnapshotChange(project, *gestureStart, name, !noChange, true,
                                                noChange ? gestureDirty : true, prepared);
            result.failed())
            return result; // Ownership/baseline remain available for retry or exact cancel.
        prepared->incrementRevision = !noChange;
        if (auto result = validatePreparedChange(*prepared); result.failed())
            return result;
    }
    catch (...)
    {
        return juce::Result::fail("history.preparationFailed: Gesture preparation failed before publication");
    }
    if (!commitPreparedChange(*prepared))
        return juce::Result::fail("history.stalePreparedChange: Gesture completion refused");
    deliverPendingNotifications();
    return juce::Result::ok();
}
juce::Result Controller::cancelGestureChecked(const juce::String &owner)
{
    if (!gestureStart)
        return juce::Result::ok();
    if (owner.isNotEmpty() && editOwner != owner)
        return juce::Result::fail("history.gestureOwnership: Edit gesture ownership expired");
    // Baseline is ALREADY prepared and owned. Escape must remain usable after
    // allocation failure in completion/admission. These empty owners need no heap.
    std::unique_ptr<Project> retiredPreview, retiredQuantizer;
    juce::String retiredOwner;
    // BEGIN intrinsic no-throw/no-allocation/no-delete cancellation publication.
    using std::swap;
    swap(project, *gestureStart);
    retiredPreview.swap(gestureStart);
    retiredQuantizer.swap(quantizerStart);
    retiredOwner.swapWith(editOwner);
    if (revision != std::numeric_limits<std::uint64_t>::max())
        ++revision;
    dirty = gestureDirty;
    pendingControllerNotification = true;
    // END publication. Old preview/quantizer/string retirement is on CONTROL
    // after publication; no history movement and no copy or allocation required.
    deliverPendingNotifications();
    return juce::Result::ok();
}
void Controller::endGesture(const juce::String &name, const juce::String &owner)
{
    (void)endGestureChecked(name, owner);
}
void Controller::cancelGesture(const juce::String &owner)
{
    (void)cancelGestureChecked(owner);
}
juce::Result Controller::undoEditChecked()
{
    if (applicationHistory)
        try
        {
            if (auto handled = applicationHistory(HistoryDirection::undo)) return *handled;
        }
        catch (...) { return juce::Result::fail("application.historyAdmissionFailed: No history delegated"); }

    if (gestureStart && !undo.canUndo())
        return cancelGestureChecked();
    std::unique_ptr<PreparedControllerChange> prepared;
    try
    {
        if (auto result = prepareHistoryChange(HistoryDirection::undo, prepared); result.failed())
            return result;
        if (auto result = validatePreparedChange(*prepared); result.failed())
            return result;
    }
    catch (...)
    {
        return juce::Result::fail("history.preparationFailed: History preparation failed before publication");
    }
    if (!commitPreparedChange(*prepared))
        return juce::Result::fail("history.stalePreparedChange: Undo publication refused");
    deliverPendingNotifications();
    return juce::Result::ok();
}
juce::Result Controller::redoEditChecked()
{
    if (applicationHistory)
        try
        {
            if (auto handled = applicationHistory(HistoryDirection::redo)) return *handled;
        }
        catch (...) { return juce::Result::fail("application.historyAdmissionFailed: No history delegated"); }

    if (gestureStart && !undo.canRedo())
        return cancelGestureChecked();
    std::unique_ptr<PreparedControllerChange> prepared;
    try
    {
        if (auto result = prepareHistoryChange(HistoryDirection::redo, prepared); result.failed())
            return result;
        if (auto result = validatePreparedChange(*prepared); result.failed())
            return result;
    }
    catch (...)
    {
        return juce::Result::fail("history.preparationFailed: History preparation failed before publication");
    }
    if (!commitPreparedChange(*prepared))
        return juce::Result::fail("history.stalePreparedChange: Redo publication refused");
    deliverPendingNotifications();
    return juce::Result::ok();
}
void Controller::undoEdit()
{
    (void)undoEditChecked();
}
void Controller::redoEdit()
{
    (void)redoEditChecked();
}

juce::Result Controller::execute(const juce::String &id, const CommandArgs &a, const juce::String &owner)
{
    if (applicationCommand)
        try
        {
            if (auto handled = applicationCommand(id, a, owner)) return *handled;
        }
        catch (...) { return juce::Result::fail("application.commandAdmissionFailed: No command delegated"); }
    if (isMixerGroupCommand(id))
        try { return executeInternal(id, a, owner, nullptr); }
        catch (...) { return juce::Result::fail("mixer.group.preparationFailed: Canonical group/history unchanged"); }
    return executeInternal(id, a, owner, nullptr);
}
juce::Result Controller::prepareStructuralCommand(const juce::String &id, const CommandArgs &a,
                                                  std::unique_ptr<PreparedControllerChange> &output,
                                                  const juce::String &owner)
{
    try
    {
        if (id != "channel.mixer" && id != "mixer.route" && id != "mixer.routeOnly" && id != "mixer.send")
            return juce::Result::fail(
                "command.unsupportedPreparation: This internal path accepts Mixer routing commands only");
        if (gestureStart)
            return juce::Result::fail(
                editOwner == owner
                    ? "command.activeGesture: Finish/cancel the owned gesture through its checked owner "
                      "before preparing routing"
                    : "command.foreignGesture: Active gesture ownership prevents routing preparation");
        return executeInternal(id, a, owner, &output);
    }
    catch (...)
    {
        return juce::Result::fail(
            "command.preparationFailed: Canonical model, gesture and history unchanged");
    }
}
juce::Result Controller::prepareStoppedEffectCommand(const juce::String &id, const CommandArgs &args,
                                                     const Project &capturedSource,
                                                     std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (id != "plugin.effect.insert" && id != "plugin.remove")
            return juce::Result::fail(
                "command.unsupportedPreparation: Stopped effect insert/remove command required");
        if (hasGesture() || undo.isPerformingUndoRedo())
            return juce::Result::fail(
                "command.activeGesture: Finish the current edit before stopped effect preparation");
        if (!args.capturedInstruments.empty())
            return juce::Result::fail(
                "effects.invalidCapture: Separate Channel capture arguments are not effect preparation");
        if (auto valid = capturedSource.validate(); valid.failed())
            return valid;
        auto normalized = capturedSource;
        const auto normalize = [&](PluginSpec &plugin)
        {
            const auto *current = project.plugin(plugin.id);
            if (!current || current->descriptor != plugin.descriptor)
                return false;
            plugin.state = current->state;
            return true;
        };
        for (auto &channel : normalized.channels)
            if (channel.instrument && !normalize(*channel.instrument))
                return juce::Result::fail("effects.invalidCapture: Native instrument identity changed");
        for (auto &insert : normalized.mixer)
            for (auto &effect : insert.effects)
                if (!normalize(effect))
                    return juce::Result::fail("effects.invalidCapture: Native effect identity changed");
        if (!normalized.state().isEquivalentTo(project.state()))
            return juce::Result::fail(
                "effects.invalidCapture: Only existing opaque plugin states may differ");
        int insert = args.mixer;
        if (id == "plugin.remove")
        {
            bool found = false;
            for (const auto &row : project.mixer)
                for (const auto &effect : row.effects)
                    if (effect.id == args.target)
                    {
                        insert = row.index;
                        found = true;
                    }
            if (!found)
                return juce::Result::fail("effects.unavailableTarget: Existing Mixer effect target required");
        }
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = executeInternal(id, args, {}, &prepared, &capturedSource); result.failed())
            return result;
        StoppedEffectChangePlan plan;
        if (auto result =
                planStoppedEffectChange(capturedSource, *prepared->destinationBeforeCommit(), insert, plan);
            result.failed())
            return result;
        if (!prepared->history || !prepared->historyBeforeSnapshot)
            return juce::Result::fail(
                "history.missingStoppedEffectBaseline: Actual prepared canonical history required");
        prepared->nativeEffectHistorySource = prepared->historyBeforeSnapshot;
        prepared->nativeEffectHistoryInsert = plan.insert;
        prepared->nativeEffectHistoryIsTraversal = false;
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("command.preparationFailed: Stopped effect source and history unchanged");
    }
}
namespace
{
void copyMatchingMixerHistoryStates(Project &target, const Project &current)
{
    const auto copy = [&](PluginSpec &plugin)
    {
        const auto *present = current.plugin(plugin.id);
        if (present && present->descriptor == plugin.descriptor)
            plugin.state = present->state;
    };
    for (auto &channel : target.channels)
        if (channel.instrument)
            copy(*channel.instrument);
    for (auto &row : target.mixer)
        for (auto &plugin : row.effects)
            copy(plugin);
}
juce::Result validateMixerChainCapture(const Project &canonical, const Project &captured)
{
    if (auto valid = captured.validate(); valid.failed())
        return valid;
    auto normalized = captured;
    copyMatchingMixerHistoryStates(normalized, canonical);
    if (!normalized.state().isEquivalentTo(canonical.state()))
        return juce::Result::fail(
            "effectChain.invalidCapture: Only same-ID/descriptor opaque native states may differ");
    return juce::Result::ok();
}
bool capturedMixerRoutingOnly(const Project &before, const Project &after)
{
    if (before.channels.size() != after.channels.size() || before.mixer.size() != after.mixer.size())
        return false;
    auto normalized = after;
    bool changed = false;
    for (const auto &old : before.channels)
    {
        auto *next = normalized.channel(old.id);
        if (!next)
            return false;
        changed |= next->mixer != old.mixer;
        next->mixer = old.mixer;
    }
    for (const auto &old : before.mixer)
    {
        auto next = std::find_if(normalized.mixer.begin(), normalized.mixer.end(),
                                 [&](const auto &row) { return row.index == old.index; });
        if (next == normalized.mixer.end())
            return false;
        changed |= next->routes.size() != old.routes.size();
        if (next->routes.size() == old.routes.size())
            for (std::size_t i = 0; i < old.routes.size(); ++i)
                changed |= next->routes[i].target != old.routes[i].target;
        next->routes = old.routes;
    }
    copyMatchingMixerHistoryStates(normalized, before);
    // No nextId, note, control, detector, order or membership normalization.
    return changed && normalized.state().isEquivalentTo(before.state());
}
bool pureMixerEffectStructuralHistory(const Project &before, const Project &after)
{
    auto normalized = after;
    bool structural = false;
    for (const auto &old : before.mixer)
    {
        auto next = std::find_if(normalized.mixer.begin(), normalized.mixer.end(),
                                 [&](const auto &row) { return row.index == old.index; });
        if (next == normalized.mixer.end())
            return false;
        structural = structural || old.effects.size() != next->effects.size();
        if (old.effects.size() == next->effects.size())
            for (std::size_t i = 0; i < old.effects.size(); ++i)
                structural = structural || old.effects[i].id != next->effects[i].id ||
                             old.effects[i].descriptor != next->effects[i].descriptor ||
                             old.effects[i].detector != next->effects[i].detector;
        next->effects = old.effects;
        next->effectsBypassed = old.effectsBypassed;
    }
    normalized.nextId = before.nextId; // Allocation metadata, never a musical/source exception.
    return structural && normalized.state().isEquivalentTo(before.state());
}
} // namespace
bool pluginNativeTopologyDifference(const Project &source, const Project &target)
{
    struct Identity
    {
        Id id = 0, owner = 0;
        bool instrument = false;
        int slot = 0;
        juce::String descriptor;
        PluginMainIo io;
        std::optional<PluginDetectorAssignment> detector;
        bool operator==(const Identity &) const = default;
    };
    const auto inventory = [](const Project &project)
    {
        std::vector<Identity> entries;
        for (const auto &channel : project.channels)
            if (channel.instrument)
            {
                const auto &plugin = *channel.instrument;
                entries.push_back({plugin.id, channel.id, true, 0, plugin.descriptor, pluginMainIoFor(plugin),
                                   plugin.detector});
            }
        for (const auto &row : project.mixer)
            for (std::size_t slot = 0; slot < row.effects.size(); ++slot)
            {
                const auto &plugin = row.effects[slot];
                entries.push_back({plugin.id, row.index, false, static_cast<int>(slot), plugin.descriptor,
                                   pluginMainIoFor(plugin), plugin.detector});
            }
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        return entries;
    };
    return inventory(source) != inventory(target);
}
#include "CommandsNativeScalarHistory.inc"
juce::Result Controller::prepareCapturedMixerCommand(const juce::String &id, const CommandArgs &args,
                                                     const Project &capturedSource,
                                                     std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        const bool routing = id == "channel.mixer" || id == "mixer.route" || id == "mixer.routeOnly";
        if (!routing && id != "mixer.send" && id != "plugin.effect.insert" && id != "plugin.remove" &&
            id != "plugin.effect.move" && id != "plugin.io")
            return juce::Result::fail(
                "command.unsupportedPreparation: Closed Mixer structural command required");
        if (hasGesture() || undo.isPerformingUndoRedo())
            return juce::Result::fail(
                "command.activeGesture: Finish the current edit before structural preparation");
        if (!args.capturedInstruments.empty())
            return juce::Result::fail("effectChain.invalidCapture: Use the one owned full native snapshot");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        if (id == "plugin.remove" || id == "plugin.effect.move")
        {
            bool effect = false;
            for (const auto &row : project.mixer)
                for (const auto &slot : row.effects)
                    effect |= slot.id == args.target;
            if (!effect)
                return juce::Result::fail("effects.unavailableTarget: Existing Mixer effect ID required");
        }
        if (id == "mixer.send")
        {
            const auto *row = mixerInsert(project, args.mixer);
            if (!row || std::any_of(row->routes.begin(), row->routes.end(),
                                    [&](const auto &route) { return Id(route.target) == args.target; }))
                return juce::Result::fail(
                    "routing.existingSend: Existing send levels use the live control path");
        }
        // No-op routing/reorder cannot acquire an Undo entry merely because the
        // fresh native snapshot contains newer opaque bytes.
        if (routing || id == "plugin.effect.move" || id == "plugin.io")
        {
            std::unique_ptr<PreparedControllerChange> probe;
            if (auto valid = executeInternal(id, args, {}, &probe); valid.failed())
                return valid;
            if (probe && probe->isNoOp())
            {
                output = std::move(probe);
                return juce::Result::ok();
            }
        }
        return executeInternal(id, args, {}, &output, &capturedSource);
    }
    catch (...)
    {
        return juce::Result::fail("command.preparationFailed: Model, capture and history retained");
    }
}
juce::Result Controller::prepareCapturedMixerRoutingHistory(HistoryDirection direction,
                                                            const Project &capturedSource,
                                                            std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
            return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
        if (hasGesture() || undo.isPerformingUndoRedo() ||
            revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("history.busy: Finish the current edit before routing history");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        juce::Array<const juce::UndoableAction *> actions;
        if (direction == HistoryDirection::undo)
            undo.getActionsInUndoTransaction(actions);
        else
            undo.getActionsInRedoTransaction(actions);
        if (actions.size() != 1)
            return juce::Result::fail("history.unsupportedTransaction: One owned routing snapshot required");
        const auto *action = dynamic_cast<const StateAction *>(actions[0]);
        if (!action || !action->belongsTo(*this) ||
            !capturedMixerRoutingOnly(action->beforeSnapshot(), action->afterSnapshot()))
            return juce::Result::fail(
                "history.unsupportedRoutingDelta: Actual pure routing history required");
        auto before = action->beforeSnapshot(), after = action->afterSnapshot();
        auto source = direction == HistoryDirection::undo ? after : before;
        copyMatchingMixerHistoryStates(source, project);
        if (!source.state().isEquivalentTo(project.state()))
            return juce::Result::fail("history.staleNativeSource: Current music differs from history edge");
        copyMatchingMixerHistoryStates(before, capturedSource);
        copyMatchingMixerHistoryStates(after, capturedSource);
        const auto &edge = direction == HistoryDirection::undo ? after : before;
        if (!edge.state().isEquivalentTo(capturedSource.state()))
            return juce::Result::fail("history.invalidCapture: Exact captured routing edge required");
        const auto &destination = direction == HistoryDirection::undo ? before : after;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(destination, project, {}, false, false, true, prepared);
            result.failed())
            return result;
        auto replacement = std::make_unique<StateAction>(*this, std::move(before), std::move(after));
        const auto units = replacement->cachedSize();
        auto history = undo.prepareHistoryReplacement(
            direction == HistoryDirection::undo, std::move(replacement), units, admittedSnapshotUnits, this);
        if (!history)
            return juce::Result::fail("history.inverseAdmissionRefused: Original history retained");
        prepared->history = std::move(history);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.inversePreparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::preparePluginIoHistory(HistoryDirection direction, const Project &capturedSource,
                                                std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
            return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
        if (hasGesture() || undo.isPerformingUndoRedo() ||
            revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("history.busy: Finish the current edit before plugin I/O history");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        juce::Array<const juce::UndoableAction *> actions;
        if (direction == HistoryDirection::undo)
            undo.getActionsInUndoTransaction(actions);
        else
            undo.getActionsInRedoTransaction(actions);
        if (actions.size() != 1)
            return juce::Result::fail(
                "history.unsupportedTransaction: One owned plugin I/O snapshot required");
        const auto *action = dynamic_cast<const StateAction *>(actions[0]);
        if (!action || !action->belongsTo(*this) ||
            !purePluginIoHistory(action->beforeSnapshot(), action->afterSnapshot()))
            return juce::Result::fail(
                "history.unsupportedPluginIoDelta: Actual pure plugin I/O history required");
        auto before = action->beforeSnapshot(), after = action->afterSnapshot();
        auto source = direction == HistoryDirection::undo ? after : before;
        copyMatchingMixerHistoryStates(source, project);
        if (!source.state().isEquivalentTo(project.state()))
            return juce::Result::fail("history.staleNativeSource: Current music differs from history edge");
        copyMatchingMixerHistoryStates(before, capturedSource);
        copyMatchingMixerHistoryStates(after, capturedSource);
        const auto &edge = direction == HistoryDirection::undo ? after : before;
        if (!edge.state().isEquivalentTo(capturedSource.state()))
            return juce::Result::fail("history.invalidCapture: Exact captured plugin I/O edge required");
        const auto &destination = direction == HistoryDirection::undo ? before : after;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(destination, project, {}, false, false, true, prepared);
            result.failed())
            return result;
        auto replacement = std::make_unique<StateAction>(*this, std::move(before), std::move(after));
        const auto units = replacement->cachedSize();
        auto history = undo.prepareHistoryReplacement(
            direction == HistoryDirection::undo, std::move(replacement), units, admittedSnapshotUnits, this);
        if (!history)
            return juce::Result::fail("history.inverseAdmissionRefused: Original history retained");
        prepared->history = std::move(history);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.inversePreparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareCapturedStructuralHistory(HistoryDirection direction,
                                                          const Project &capturedSource,
                                                          std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
            return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
        if (hasGesture() || undo.isPerformingUndoRedo() ||
            revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("history.busy: Finish the current edit before structural history");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        juce::Array<const juce::UndoableAction *> actions;
        if (direction == HistoryDirection::undo)
            undo.getActionsInUndoTransaction(actions);
        else
            undo.getActionsInRedoTransaction(actions);
        if (actions.size() != 1)
            return juce::Result::fail(
                "history.unsupportedTransaction: One owned structural snapshot required");
        const auto *action = dynamic_cast<const StateAction *>(actions[0]);
        if (!action || !action->belongsTo(*this) || action->isNativeScalarAction())
            return juce::Result::fail(
                "history.unsupportedStructuralAction: Actual owned non-scalar snapshot required");
        auto before = action->beforeSnapshot(), after = action->afterSnapshot();
        auto source = direction == HistoryDirection::undo ? after : before;
        copyMatchingMixerHistoryStates(source, project);
        if (!source.state().isEquivalentTo(project.state()))
            return juce::Result::fail("history.staleNativeSource: Current music differs from history edge");
        copyMatchingMixerHistoryStates(before, capturedSource);
        copyMatchingMixerHistoryStates(after, capturedSource);
        const auto &edge = direction == HistoryDirection::undo ? after : before;
        if (!edge.state().isEquivalentTo(capturedSource.state()))
            return juce::Result::fail("history.invalidCapture: Exact captured structural edge required");
        const auto &destination = direction == HistoryDirection::undo ? before : after;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(destination, project, {}, false, false, true, prepared);
            result.failed())
            return result;
        auto replacement = std::make_unique<StateAction>(*this, std::move(before), std::move(after));
        const auto units = replacement->cachedSize();
        auto history = undo.prepareHistoryReplacement(
            direction == HistoryDirection::undo, std::move(replacement), units, admittedSnapshotUnits, this);
        if (!history)
            return juce::Result::fail("history.inverseAdmissionRefused: Original history retained");
        prepared->history = std::move(history);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.inversePreparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareMixerChainReplacement(const Project &capturedSource, int insert,
                                                      const effect_chain::Chain &chain,
                                                      std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (hasGesture() || undo.isPerformingUndoRedo())
            return juce::Result::fail("effectChain.activeGesture: Finish the current edit first");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        std::unique_ptr<effect_chain::ReplacementPlan> plan;
        if (auto result = effect_chain::prepareReplacement(capturedSource, insert, chain,
                                                           std::max(project.nextId, mixerChainNextId), plan);
            result.failed())
            return result;
        const bool noOp = plan->destination.state().isEquivalentTo(capturedSource.state());
        const auto reservedThrough = plan->destination.nextId;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(std::move(plan->destination), capturedSource,
                                                "Replace Mixer effect chain", !noOp, false,
                                                noOp ? dirty : true, prepared);
            result.failed())
            return result;
        prepared->commandNoOp = noOp;
        prepared->incrementRevision = !noOp;
        prepared->clearQuantizer = false;
        // All fallible preparation succeeded. Consume reservations even if this
        // ticket is subsequently discarded; no publication or history move here.
        mixerChainNextId = std::max(mixerChainNextId, reservedThrough);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("effectChain.preparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareMixerEffectReplacement(const Project &capturedSource, Id target,
                                                       const juce::String &descriptor,
                                                       std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (hasGesture() || undo.isPerformingUndoRedo())
            return juce::Result::fail("effectChain.activeGesture: Finish the current edit first");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        CommandArgs args;
        args.target = target;
        args.text = descriptor;
        return executeInternal("plugin.effect.replace", args, {}, &output, &capturedSource);
    }
    catch (...)
    {
        return juce::Result::fail("effectChain.preparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareMixerEffectSidechain(const Project &capturedSource, Id target,
                                                     std::optional<PluginDetectorAssignment> assignment,
                                                     std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (hasGesture() || undo.isPerformingUndoRedo())
            return juce::Result::fail("effectChain.activeGesture: Finish the current edit first");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        CommandArgs args;
        args.target = target;
        args.detector.emplace(assignment);
        return executeInternal("plugin.effect.sidechain", args, {}, &output, &capturedSource);
    }
    catch (...)
    {
        return juce::Result::fail("sidechain.preparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareMixerInsertCommand(const juce::String &id, const CommandArgs &args,
                                                   const Project &capturedSource,
                                                   std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (!isMixerInsertLifecycleCommand(id))
            return juce::Result::fail("mixer.unsupportedPreparation: Insert create/delete required");
        if (hasGesture() || undo.isPerformingUndoRedo())
            return juce::Result::fail("mixer.activeGesture: Finish the current edit first");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        return executeInternal(id, args, {}, &output, &capturedSource);
    }
    catch (...)
    {
        return juce::Result::fail("mixer.preparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareMixerInsertHistory(HistoryDirection direction, const Project &capturedSource,
                                                   std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
            return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
        if (hasGesture() || undo.isPerformingUndoRedo() ||
            revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("history.busy: Finish the current edit before insert history");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        juce::Array<const juce::UndoableAction *> actions;
        if (direction == HistoryDirection::undo)
            undo.getActionsInUndoTransaction(actions);
        else
            undo.getActionsInRedoTransaction(actions);
        if (actions.size() != 1)
            return juce::Result::fail("history.unsupportedTransaction: One owned insert snapshot required");
        const auto *action = dynamic_cast<const StateAction *>(actions[0]);
        if (!action || !action->belongsTo(*this))
            return juce::Result::fail("history.unsupportedTransaction: Owned canonical snapshot required");
        if (!pureMixerInsertHistory(action->beforeSnapshot(), action->afterSnapshot()))
            return juce::Result::fail(
                "history.unsupportedInsertDelta: Pure insert create/delete history required");
        auto before = action->beforeSnapshot(), after = action->afterSnapshot();
        auto source = direction == HistoryDirection::undo ? after : before;
        copyMatchingMixerHistoryStates(source, project);
        if (!source.state().isEquivalentTo(project.state()))
            return juce::Result::fail(
                "history.staleNativeSource: Current music differs from actual insert history");
        copyMatchingMixerHistoryStates(before, capturedSource);
        copyMatchingMixerHistoryStates(after, capturedSource);
        const auto &capturedEdge = direction == HistoryDirection::undo ? after : before;
        if (!capturedEdge.state().isEquivalentTo(capturedSource.state()))
            return juce::Result::fail("history.invalidCapture: Actual captured insert history edge required");
        const auto &destination = direction == HistoryDirection::undo ? before : after;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(destination, project, {}, false, false, true, prepared);
            result.failed())
            return result;
        auto replacement = std::make_unique<StateAction>(*this, std::move(before), std::move(after));
        const auto units = replacement->cachedSize();
        auto history = undo.prepareHistoryReplacement(
            direction == HistoryDirection::undo, std::move(replacement), units, admittedSnapshotUnits, this);
        if (!history)
            return juce::Result::fail("history.inverseAdmissionRefused: Actual history retained");
        prepared->history = std::move(history);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.inversePreparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareMixerEffectHistory(HistoryDirection direction, const Project &capturedSource,
                                                   std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (direction != HistoryDirection::undo && direction != HistoryDirection::redo)
            return juce::Result::fail("history.invalidDirection: Expected Undo or Redo");
        if (hasGesture() || undo.isPerformingUndoRedo() ||
            revision == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("history.busy: Finish the current edit before effect history");
        if (auto valid = validateMixerChainCapture(project, capturedSource); valid.failed())
            return valid;
        juce::Array<const juce::UndoableAction *> actions;
        if (direction == HistoryDirection::undo)
            undo.getActionsInUndoTransaction(actions);
        else
            undo.getActionsInRedoTransaction(actions);
        if (actions.size() != 1)
            return juce::Result::fail(
                "history.unsupportedTransaction: One owned Mixer effect snapshot required");
        const auto *action = dynamic_cast<const StateAction *>(actions[0]);
        if (!action || !action->belongsTo(*this))
            return juce::Result::fail("history.unsupportedTransaction: Owned canonical snapshot required");
        if (!pureMixerEffectStructuralHistory(action->beforeSnapshot(), action->afterSnapshot()))
            return juce::Result::fail(
                "history.unsupportedEffectDelta: Pure Mixer effect structural history required");
        auto before = action->beforeSnapshot(), after = action->afterSnapshot();
        auto source = direction == HistoryDirection::undo ? after : before;
        copyMatchingMixerHistoryStates(source, project);
        if (!source.state().isEquivalentTo(project.state()))
            return juce::Result::fail(
                "history.staleNativeSource: Current music differs from the actual history edge");
        copyMatchingMixerHistoryStates(before, capturedSource);
        copyMatchingMixerHistoryStates(after, capturedSource);
        const auto &capturedEdge = direction == HistoryDirection::undo ? after : before;
        if (!capturedEdge.state().isEquivalentTo(capturedSource.state()))
            return juce::Result::fail("history.invalidCapture: Actual captured history edge required");
        const auto &destination = direction == HistoryDirection::undo ? before : after;
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(destination, project, {}, false, false, true, prepared);
            result.failed())
            return result;
        auto replacement = std::make_unique<StateAction>(*this, std::move(before), std::move(after));
        const auto units = replacement->cachedSize();
        auto history = undo.prepareHistoryReplacement(
            direction == HistoryDirection::undo, std::move(replacement), units, admittedSnapshotUnits, this);
        if (!history)
            return juce::Result::fail("history.inverseAdmissionRefused: Actual history retained");
        prepared->history = std::move(history);
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("history.inversePreparationFailed: Source, history and output retained");
    }
}
juce::Result Controller::prepareChannelRoutingSequence(const std::vector<CommandArgs> &assignments,
                                                       const juce::String &label,
                                                       std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (hasGesture())
            return juce::Result::fail("command.activeGesture: Finish the current gesture first");
        if (assignments.empty() || assignments.size() > 64)
            return juce::Result::fail("command.invalidSequence: Expected 1..64 channel.mixer assignments");
        Project next = project;
        for (const auto &args : assignments)
        {
            if (!args.capturedInstruments.empty())
                return juce::Result::fail(
                    "command.unsupportedSequence: Native capture is not a route assignment");
            const Project before = next;
            if (auto applied = applyChannelMixerAssignment(next, args); applied.failed())
                return applied;
            // Same canonical postmutation reconciliation/validation as execute.
            reconcileSoloChannels(before, next);
            reconcileMixerSolo(next);
            if (auto valid = next.validate(); valid.failed())
                return valid;
        }
        const bool noOp = next.state().isEquivalentTo(project.state());
        std::unique_ptr<PreparedControllerChange> prepared;
        if (auto result = prepareSnapshotChange(std::move(next), project, label, !noOp, false,
                                                noOp ? dirty : true, prepared);
            result.failed())
            return result;
        prepared->commandNoOp = noOp;
        prepared->incrementRevision = !noOp;
        prepared->clearQuantizer = false;
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("command.preparationFailed: Sequence source/history unchanged");
    }
}
juce::Result Controller::prepareAutomationTouchPreview(const MixerAutomationTake &take,
                                                       const juce::String &id, const CommandArgs &args,
                                                       std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        const auto key = MixerAutomationTake::keyForCommand(id, args.mixer, args.target);
        if (!key || !take.target(*key) || projectIdentity != take.identity || hasGesture() ||
            !take.sourceCurrent(project))
            return juce::Result::fail("automationRecording.staleSource: Exact owned Mixer source required");
        // SAME canonical mutation/validation; this private prepare-only flag never
        // reaches public execute/MCP and does not allocate a discarded history action.
        return executeInternal(id, args, {}, &output, nullptr, true);
    }
    catch (...)
    {
        return juce::Result::fail("automationRecording.preparationFailed: Preview and history retained");
    }
}
juce::Result Controller::prepareAutomationTouchFinish(const MixerAutomationTake &take, bool cancel,
                                                      std::unique_ptr<PreparedControllerChange> &output)
{
    try
    {
        if (projectIdentity != take.identity || hasGesture() || !take.sourceCurrent(project))
            return juce::Result::fail(
                "automationRecording.staleSource: Current Project differs from owned previews");
        Project next;
        if (auto result = take.destination(cancel, next); result.failed())
            return result;
        std::unique_ptr<PreparedControllerChange> prepared;
        const bool record = !cancel && !take.events.empty();
        const bool noChange = next.state().isEquivalentTo(project.state());
        const bool externalDirty = externalChangeVersion != take.externalChangesBefore ||
                                   externalChangeVersion == std::numeric_limits<std::uint64_t>::max();
        if (auto result =
                prepareSnapshotChange(std::move(next), *take.before, "Record Mixer automation", record, false,
                                      record || take.beforeDirty || externalDirty, prepared);
            result.failed())
            return result;
        if (!record && noChange)
        {
            prepared->incrementRevision = false;
            prepared->commandNoOp = true;
        }
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("automationRecording.preparationFailed: Take and history retained");
    }
}
// The file tempo map and meter from a MIDI import replace the project's (one Undo with the notes).
// Cut every clip that `pick` accepts and that strictly contains `beat` into a left part
// (the original id) and a right part (a new id, recorded in `made`). Audio fades stay on the
// outer edges. Returns whether anything was cut.
template <typename Pick>
static bool sliceClipsAt(Project &next, const Pick &pick, double beat, std::map<Id, Id> &made)
{
    std::vector<Placement> patterns;
    std::vector<AudioPlacement> audio;
    std::vector<AutomationPlacement> automation;
    for (auto &clip : next.automation.placements)
        if (pick(clip.id) && beat > clip.beat + 1.e-9 && beat < clip.beat + clip.length - 1.e-9)
        {
            auto right = clip;
            const auto left = beat - clip.beat;
            right.id = next.allocate();
            made.emplace(clip.id, right.id);
            right.beat = beat;
            right.offset += left;
            right.length -= left;
            clip.length = left;
            automation.push_back(right);
        }
    for (auto &clip : next.placements)
        if (pick(clip.id) && beat > clip.beat + 1.e-9 && beat < clip.beat + clip.length - 1.e-9)
        {
            const auto left = beat - clip.beat;
            auto right = clip;
            right.id = next.allocate();
            made.emplace(clip.id, right.id);
            right.beat = beat;
            right.offset += left;
            right.length -= left;
            clip.length = left;
            patterns.push_back(right);
        }
    for (auto &clip : next.audioPlacements)
        if (pick(clip.id) && beat > clip.beat + 1.e-9 && beat < next.audioEndBeat(clip.beat, clip.duration) - 1.e-9)
        {
            const auto left = next.songSeconds(beat) - next.songSeconds(clip.beat);
            auto right = clip;
            right.id = next.allocate();
            made.emplace(clip.id, right.id);
            right.beat = beat;
            right.offset += left;
            right.duration -= left;
            right.fadeIn = 0; // Fades stay on the clip's outer edges, never at the cut.
            clip.duration = left;
            clip.fadeOut = 0;
            audio.push_back(right);
        }
    next.placements.insert(next.placements.end(), patterns.begin(), patterns.end());
    next.audioPlacements.insert(next.audioPlacements.end(), audio.begin(), audio.end());
    next.automation.placements.insert(next.automation.placements.end(), automation.begin(), automation.end());
    return !(patterns.empty() && audio.empty() && automation.empty());
}

static juce::Result applyImportedTempo(Project &next, const CommandArgs::ImportedTempo &map)
{
    for (const auto &[beat, bpm] : map.tempos)
        if (!std::isfinite(beat) || beat < 0 || beat > 1000000 || !std::isfinite(bpm) || bpm < 10 || bpm > 999)
            return juce::Result::fail("The file tempo map is outside 10..999 BPM");
    if (map.numerator != 0 &&
        (map.numerator < 1 || map.numerator > 32 ||
         (map.denominator != 2 && map.denominator != 4 && map.denominator != 8 && map.denominator != 16)))
        return juce::Result::fail("The file time signature is outside 1..32 / 2,4,8,16");
    if (!map.tempos.empty())
    {
        next.tempo = map.tempos.front().second;
        next.tempoChanges.clear();
        for (const auto &[beat, bpm] : map.tempos)
        {
            if (beat < 1.e-9)
            {
                next.tempo = bpm; // The last change at beat zero wins.
                continue;
            }
            if (!next.tempoChanges.empty() && std::abs(next.tempoChanges.back().beat - beat) < 1.e-9)
                next.tempoChanges.back().bpm = bpm;
            else if (next.tempoChanges.size() < 4096)
                next.tempoChanges.push_back({next.allocate(), beat, bpm, false});
        }
    }
    if (map.numerator != 0)
    {
        next.numerator = map.numerator;
        next.denominator = map.denominator;
    }
    return juce::Result::ok();
}
juce::Result Controller::executeInternal(const juce::String &id, const CommandArgs &a,
                                         const juce::String &owner,
                                         std::unique_ptr<PreparedControllerChange> *preparedOutput,
                                         const Project *capturedEffectSource, bool withoutHistory)
{
    const bool samplerController = isSamplerControllerCommand(id);
    if ((!samplerController && (a.samplerControllerLink || !a.samplerControllerReplacements.empty())) ||
        (samplerController && id != "channel.sampler.controller.remove" && !a.samplerControllerLink))
        return juce::Result::fail(
            "samplerController.invalidArguments: Typed metadata belongs only to link create/update");
    if (samplerController && hasGesture())
        return juce::Result::fail(
            "samplerController.activeGesture: Finish the current edit before changing MIDI links");
    if (a.retainSteps && !isStepRetentionCommand(id))
        return juce::Result::fail(
            "stepMemory.invalidArguments: Retention is only supported by step activation/fill");
    if (a.pluginIo.has_value() != (id == "plugin.io"))
        return juce::Result::fail("pluginIo.invalidArguments: Exact typed payload belongs only to plugin.io");
    if ((a.appearance && id != "channel.properties" && id != "rack.properties") ||
        (a.channelSort && id != "rack.sort"))
        return juce::Result::fail(
            "channelAppearance.invalidArguments: Typed fields belong only to Channel properties/sort");
    if (a.soloScope && (id != "channel.solo" || (*a.soloScope != "global" && *a.soloScope != "group")))
        return juce::Result::fail("channel.solo.invalidScope: Expected global or group on channel.solo");
    if (a.detector.has_value() != (id == "plugin.effect.sidechain"))
        return juce::Result::fail("sidechain.invalidArguments: Required detector belongs only to sidechain");
    if (a.mixerDelete && id != "mixer.insert.delete")
        return juce::Result::fail("mixer.invalidDeletePolicy: Policy belongs only to insert deletion");
    if (isMixerInsertLifecycleCommand(id) && hasGesture())
        return juce::Result::fail(
            "mixer.activeGesture: Finish the current edit before insert membership changes");
    if (a.mixerGroup && !isMixerGroupCommand(id))
        return juce::Result::fail("mixer.group.invalidArguments: Group UUID belongs only to Mixer group commands");
    const bool samplerAutomation = isSamplerAutomationTargetCommand(id);
    if (samplerAutomation != a.samplerAutomationParameter.has_value())
        return juce::Result::fail(
            "automation.invalidArguments: Sampler automation requires its explicit parameter code only");
    if (samplerAutomation && (a.channel <= 0 || a.automationControl || a.automationSendTarget))
        return juce::Result::fail("automation.invalidArguments: Sampler automation requires a stable Channel "
                                  "owner and no Mixer target");
    if (a.automationSendTarget &&
        (!isMixerAutomationTargetCommand(id) ||
         a.automationControl.value_or(MixerAutomationControl::gain) != MixerAutomationControl::send))
        return juce::Result::fail(
            "automation.invalidArguments: Send destination belongs only to send automation target commands");
    if (a.automationControl && (!isMixerAutomationTargetCommand(id) ||
                               !validMixerAutomationControl(*a.automationControl)))
        return juce::Result::fail("automation.invalidArguments: Typed control belongs only to supported Mixer automation target commands");
    if (owner.isNotEmpty() && (!gestureStart || editOwner != owner))
        return juce::Result::fail("Edit gesture ownership expired");
    if ((id == "plugin.effect.replace" || id == "plugin.effect.sidechain" || id == "plugin.io") &&
        hasGesture())
        return juce::Result::fail(
            "effectChain.activeGesture: Finish the current edit before effect replacement");
    const bool localTakeover = gestureStart && editOwner.isNotEmpty() && owner.isEmpty();
    const Project &canonicalSource = localTakeover ? *gestureStart : project;
    const bool continuingGesture = gestureStart && !localTakeover;
    std::unique_ptr<Project> preparedQuantizerStart;
    auto info =
        std::find_if(commandRegistry().begin(), commandRegistry().end(), [&](auto &c) { return c.id == id; });
    if (info == commandRegistry().end() || !info->documentEdit)
        return juce::Result::fail("Unavailable document command: " + id);
    std::optional<Project> capturedBaseline;
    if (capturedEffectSource)
        capturedBaseline = *capturedEffectSource; // Internal validated opaque-only effect snapshot.
    if (!a.capturedInstruments.empty())
    {
        capturedBaseline = canonicalSource;
        auto &baseline = *capturedBaseline;
        if (a.capturedInstruments.size() > 128)
            return juce::Result::fail("Too many captured instruments");
        if (id != "channel.clone" && id != "channel.delete")
            return juce::Result::fail("Captured instrument state requires Channel clone/delete");
        std::set<Id> seen;
        for (const auto &captured : a.capturedInstruments)
        {
            auto *spec = baseline.plugin(captured.id);
            const auto channel = std::find_if(
                baseline.channels.begin(), baseline.channels.end(),
                [&](const auto &c)
                {
                    return c.instrument && c.instrument->id == captured.id &&
                           (a.ids.empty() ? c.id == a.channel
                                          : std::find(a.ids.begin(), a.ids.end(), c.id) != a.ids.end());
                });
            if (!spec || channel == baseline.channels.end() || spec->descriptor != captured.descriptor ||
                !seen.insert(captured.id).second)
                return juce::Result::fail("Captured instrument target changed");
            spec->state = captured.state;
        }
        if (auto valid = baseline.validate(); valid.failed())
            return valid;
    }
    auto next = capturedBaseline.value_or(canonicalSource);
    auto *pat = next.pattern(a.pattern);
    auto *ch = next.channel(a.channel);
    const bool clipAction = id.startsWith("clip.") && id != "clip.add" && id != "clip.unique";
    auto clipTargets = a.ids;
    if (clipAction)
    {
        const auto valid = projectClipIds(next);
        if (std::set<Id>(clipTargets.begin(), clipTargets.end()).size() != clipTargets.size())
            return juce::Result::fail("Clip targets must be distinct");
        for (auto target : clipTargets)
            if (!valid.contains(target))
                return juce::Result::fail("Clip target is unavailable");
        if (id != "clip.ungroup" && a.grouping.value_or(clipGroupingEnabled))
            clipTargets = expandedClipSelection(next, clipTargets);
    }
    auto chosen = [&](Id n)
    {
        const auto &targets = clipAction ? clipTargets : a.ids;
        return std::find(targets.begin(), targets.end(), n) != targets.end();
    };
    auto missing = []() { return juce::Result::fail("Command target is unavailable"); };
    if (id == "project.meter")
    {
        next.numerator = a.numerator;
        next.denominator = a.denominator;
    }
    if (a.noteProperties.has_value() != (id == "notes.properties"))
        return juce::Result::fail(
            "noteMode.invalidArguments: Typed properties belong only to notes.properties");
    if (a.colourGroup < 0 || a.colourGroup > 15 ||
        (id != "notes.add" && (a.slide || a.portamento || a.colourGroup != 0)))
        return juce::Result::fail("noteMode.invalidArguments: Creation fields belong only to notes.add");
    if (id.startsWith("notes.") && id != "notes.add")
    {
        if (!pat)
            return missing();
        if (id != "notes.quantize" && id != "notes.quickQuantize" && id != "notes.quantizeStarts")
            for (auto target : a.ids)
                if (std::none_of(pat->notes.begin(), pat->notes.end(),
                                 [&](auto &n) { return n.id == target; }))
                    return missing();
    }
    if (id == "clip.move" || id == "clip.delete" || id == "clip.duplicate" || id == "clip.slice" ||
        id == "clip.slip" || id == "clip.rippleDelete")
        for (auto target : a.ids)
            if (std::none_of(next.placements.begin(), next.placements.end(),
                             [&](auto &p) { return p.id == target; }) &&
                std::none_of(next.audioPlacements.begin(), next.audioPlacements.end(),
                             [&](auto &p) { return p.id == target; }) &&
                std::none_of(next.automation.placements.begin(), next.automation.placements.end(),
                             [&](auto &p) { return p.id == target; }))
                return missing();
    if (id == "plugin.effect.mix" || id == "mixer.effects.bypass")
    {
        if (auto result = setMixerChainControl(next, id, a.target, a.mixer, a.value, a.enabled);
            result.failed())
            return result;
    }
    else if (id.startsWith("rack.group."))
    {
        auto named = [&](const juce::String &name) -> ChannelGroup *
        {
            for (auto &group : next.channelGroups)
                if (group.name.equalsIgnoreCase(name))
                    return &group;
            return nullptr;
        };
        if (id == "rack.group.create")
        {
            if (auto valid = validateRackGroupName(next, a.text); valid.failed())
                return valid;
            next.channelGroups.push_back({next.allocate(), a.text});
        }
        else if (id == "rack.group.assign")
        {
            if (a.ids.empty() || a.ids.size() > size_t(Project::maxChannels))
                return juce::Result::fail("Select 1.." + juce::String(Project::maxChannels) + " Channels to group");
            for (auto channel : a.ids)
                if (!next.channel(channel))
                    return missing();
            Id group = a.target;
            if (a.text.isNotEmpty())
            {
                if (group != 0)
                    return juce::Result::fail("Use a group ID or name, not both");
                if (auto valid = validateRackGroupName(next, a.text, 0, true); valid.failed())
                    return valid;
                if (auto *existing = named(a.text))
                    group = existing->id;
                else
                {
                    group = next.allocate();
                    next.channelGroups.push_back({group, a.text});
                }
            }
            if (group < 0 || (group != 0 && !next.channelGroup(group)))
                return missing();
            for (auto channel : a.ids)
                next.channel(channel)->group = group;
        }
        else
        {
            auto *group = next.channelGroup(a.target);
            if (!group)
                return missing();
            if (id == "rack.group.rename")
            {
                if (auto valid = validateRackGroupName(next, a.text, group->id); valid.failed())
                    return valid;
                group->name = a.text;
            }
            else if (id == "rack.group.delete")
            {
                for (auto &channel : next.channels)
                    if (channel.group == a.target)
                        channel.group = 0;
                std::erase_if(next.channelGroups, [&](const auto &item) { return item.id == a.target; });
            }
        }
    }
    else if (id.startsWith("plugin."))
    {
        if (id.startsWith("plugin.binding."))
        {
            auto *plugin = next.plugin(a.target);
            if (!plugin || !a.binding)
                return missing();
            auto &bindings = plugin->automationBindings;
            const auto binding = std::find_if(bindings.begin(), bindings.end(),
                                              [&](const auto &b) { return b.id == a.binding->id; });
            if (id == "plugin.binding.declare")
            {
                if (binding != bindings.end())
                    return juce::Result::fail("Plugin automation binding already exists");
                bindings.push_back(*a.binding);
            }
            else if (id == "plugin.binding.remove")
            {
                if (binding == bindings.end())
                    return missing();
                const AutomationKey key{AutomationParameter::pluginNormalized, plugin->id, binding->id};
                for (const auto &source : next.automation.sources)
                    if (std::any_of(source.links.begin(), source.links.end(),
                                    [&](const auto &link) { return link.key == key; }))
                        return juce::Result::fail("Unlink automation before removing its binding");
                if (std::any_of(next.automation.initialValues.begin(), next.automation.initialValues.end(),
                                [&](const auto &value) { return value.key == key; }))
                    return juce::Result::fail("Remove the initialized control before its binding");
                if (std::any_of(next.automation.modulators.begin(), next.automation.modulators.end(),
                                [&](const auto &m) { return m.link.key == key; }))
                    return juce::Result::fail("Remove the modulator before its binding");
                bindings.erase(binding);
            }
            else
                return missing();
        }
        else if (id == "plugin.io")
        {
            if (auto result = setPluginMainIo(next, a.target, *a.pluginIo); result.failed())
                return result;
        }
        else if (id == "plugin.effect.sidechain")
        {
            if (auto result = setPluginDetector(next, a.target, *a.detector); result.failed())
                return result;
        }
        else if (id == "plugin.effect.replace")
        {
            if (a.text.isEmpty() || a.text.getNumBytesAsUTF8() > 65536)
                return juce::Result::fail(
                    "effects.invalidReplacementDescriptor: Bounded effect descriptor required");
            auto xml = juce::XmlDocument::parse(a.text);
            juce::PluginDescription type;
            if (!xml || !type.loadFromXml(*xml) ||
                (type.pluginFormatName != "VST3" && (!effects::isNativeDescription(type) && !isNativeInstrument(type))) || type.isInstrument)
                return juce::Result::fail("effects.invalidReplacementDescriptor: Choose an available effect");
            PluginSpec *target = nullptr;
            for (auto &row : next.mixer)
                for (auto &effect : row.effects)
                    if (effect.id == a.target)
                        target = &effect;
            if (!target)
                return juce::Result::fail("effects.unavailableTarget: Existing Mixer effect ID required");
            const auto referencesTarget = [&](const AutomationKey &key)
            { return key.kind == AutomationParameter::pluginNormalized && key.owner == target->id; };
            for (const auto &source : next.automation.sources)
                if (std::any_of(source.links.begin(), source.links.end(),
                                [&](const auto &link) { return referencesTarget(link.key); }))
                    return juce::Result::fail("effects.targetAutomationReferenced: Unlink target native "
                                              "automation before replacement");
            if (std::any_of(next.automation.initialValues.begin(), next.automation.initialValues.end(),
                            [&](const auto &value) { return referencesTarget(value.key); }))
                return juce::Result::fail(
                    "effects.targetAutomationReferenced: Remove target initialization before replacement");
            if (std::any_of(next.automation.modulators.begin(), next.automation.modulators.end(),
                            [&](const auto &m) { return referencesTarget(m.link.key); }))
                return juce::Result::fail(
                    "effects.targetAutomationReferenced: Remove the target's modulator before replacement");
            const Id reserved = std::max(next.nextId, mixerChainNextId);
            if (reserved < 1 || reserved > 1000000000)
                return juce::Result::fail("effects.idExhausted: No new replacement identity available");
            PluginSpec replacement{reserved, type.name, a.text, {}, false, target->bypass};
            replacement.wet = target->wet; // Host slot control, never a copied native parameter.
            *target = std::move(replacement);
            next.nextId = reserved + 1;
        }
        else if (id == "plugin.effect.move")
        {
            if (auto result = moveMixerEffect(next, a.target, a.lane); result.failed())
                return result;
        }
        else if (id == "plugin.instrument.create" || id == "plugin.instrument.replace" ||
                 id == "plugin.effect.insert")
        {
            auto xml = juce::XmlDocument::parse(a.text);
            juce::PluginDescription type;
            if (!xml || !type.loadFromXml(*xml) ||
                (type.pluginFormatName != "VST3" && (!effects::isNativeDescription(type) && !isNativeInstrument(type))))
                return missing();
            const bool instrument = id != "plugin.effect.insert";
            if (instrument != type.isInstrument)
                return juce::Result::fail(
                    "Choose an instrument for a Channel, or an effect for a Mixer slot");
            PluginSpec spec{next.allocate(), type.name, a.text, {}, instrument, false};
            if (id == "plugin.instrument.create")
            {
                Channel channel{next.allocate(), type.name, "builtin:bass", 0, .7f, 0, false};
                channel.instrument = std::move(spec);
                channel.group = a.target;
                next.channels.push_back(std::move(channel));
            }
            else if (instrument)
            {
                if (!ch)
                    return missing();
                ch->instrument = std::move(spec);
                ch->name = type.name;
            }
            else
            {
                auto insert = std::find_if(next.mixer.begin(), next.mixer.end(),
                                           [&](auto &m) { return m.index == a.mixer; });
                if (insert == next.mixer.end())
                    return missing();
                insert->effects.push_back(std::move(spec));
            }
        }
        else
        {
            bool found = false;
            for (auto &c : next.channels)
                if (c.instrument && c.instrument->id == a.target)
                {
                    found = true;
                    if (id == "plugin.remove")
                        c.instrument.reset();
                    else if (id == "plugin.bypass")
                        c.instrument->bypass = a.enabled;
                }
            for (auto &m : next.mixer)
                for (size_t i = 0; i < m.effects.size(); ++i)
                    if (m.effects[i].id == a.target)
                    {
                        found = true;
                        if (id == "plugin.remove")
                            m.effects.erase(m.effects.begin() + std::ptrdiff_t(i));
                        else if (id == "plugin.bypass")
                            m.effects[i].bypass = a.enabled;
                        break;
                    }
            if (!found)
                return missing();
        }
    }
    else if (id == "project.tempo")
        next.tempo = a.value;
    else if (isSwingCommand(id))
    {
        if (auto result = applySwingCommand(next, id, a); result.failed())
            return result;
    }
    else if (id == "channel.solo" || id == "channel.unsolo" || id == "channel.mute" ||
             id == "channel.muteLock" || id == "rack.muteLock" || id == "rack.muteSelected" ||
             id == "rack.unmuteSelected")
    {
        if (auto muted = applyChannelMuteCommand(next, id, a); muted.failed())
            return muted;
    }
    else if (id == "channel.clone" || id == "channel.delete" || id == "channel.moveUp" ||
             id == "channel.moveDown")
    {
        if (auto edited = applyChannelManagement(next, id, a); edited.failed())
            return edited;
    }
    else if (id == "rack.colourSpread")
    {
        // args.text "random" (args.value seeds it) or "gradient" (from the first to the last
        // selected Channel's colour; a hue sweep when they match or follow the theme).
        if (a.text != "random" && a.text != "gradient")
            return juce::Result::fail("Colour mode is random or gradient");
        std::vector<Channel *> chosenChannels;
        for (auto id2 : a.ids)
        {
            auto *channel = next.channel(id2);
            if (!channel)
                return missing();
            chosenChannels.push_back(channel);
        }
        if (chosenChannels.empty() || chosenChannels.size() > 1024)
            return juce::Result::fail("Select Channels to colour");
        const auto n = chosenChannels.size();
        if (a.text == "random")
        {
            juce::Random random(juce::int64(std::isfinite(a.value) ? a.value : 0));
            float hue = random.nextFloat();
            for (auto *channel : chosenChannels)
            {
                // Golden-ratio hue steps keep neighbours distinct; moderate saturation and
                // brightness keep every colour readable on the Rack.
                hue = std::fmod(hue + .618034f, 1.f);
                channel->colour = juce::Colour::fromHSV(hue, .45f + .2f * random.nextFloat(),
                                                        .78f + .15f * random.nextFloat(), 1.f)
                                      .getARGB();
            }
        }
        else
        {
            const auto first = juce::Colour(chosenChannels.front()->colour),
                       last = juce::Colour(chosenChannels.back()->colour);
            const bool sweep = chosenChannels.front()->colour == 0 || chosenChannels.back()->colour == 0 ||
                               first == last;
            for (size_t i = 0; i < n; ++i)
            {
                const float t = n > 1 ? float(i) / float(n - 1) : 0.f;
                chosenChannels[i]->colour =
                    (sweep ? juce::Colour::fromHSV(.02f + .75f * t, .55f, .88f, 1.f) : first.interpolatedWith(last, t))
                        .withAlpha(1.f)
                        .getARGB();
            }
        }
    }
    else if (isChannelAppearanceCommand(id))
    {
        if (auto edited = applyChannelAppearance(next, id, a); edited.failed())
            return edited;
    }
    else if (samplerController)
    {
        if (auto edited = applySamplerControllerCommand(next, id, a); edited.failed())
            return edited;
    }
    else if (id == "channel.add")
    {
        auto channelId = next.allocate();
        next.channels.push_back({channelId, sourceName(a.text).toUpperCase(), a.text, 0, .7f, 0, false});
        next.channels.back().group = a.target;
    }
    else if (isSamplerVoiceModeCommand(id))
    {
        if (!ch || ch->instrument || ch->audioClip)
            return juce::Result::fail(
                "samplerVoice.unsupportedTarget: Existing local sampler Channel required");
        auto &settings = ch->sampler;
        if (id == "channel.sampler.mono")
            settings.mono = a.enabled;
        else if (id == "channel.sampler.portamento")
            settings.portamento = a.enabled;
        else if (id == "channel.sampler.fullPortamento")
            settings.fullPortamento = a.enabled;
        else
        {
            const auto maximum = id == "channel.sampler.slideTime" ? 10. : 64.;
            if (!std::isfinite(a.value) || a.value < 0 || a.value > maximum)
                return juce::Result::fail("samplerVoice.invalidValue: Finite ranged physical value required");
            if (id == "channel.sampler.slideTime")
                settings.slideTime = a.value;
            else
                settings.gateBeats = a.value;
        }
    }
    else if (id == "channel.preset")
    {
        Channel preset;
        if (auto decoded = decodeChannelPreset(a.text, preset); decoded.failed())
            return decoded;
        if (auto applied = applyChannelPreset(next, a.channel, preset); applied.failed())
            return applied;
    }
    else if (id == "channel.outputRoutes")
    {
        // args.channel: a plugin-instrument Channel. args.ids: Mixer insert (0 Master) for each
        // extra output bus after the main one, in the plugin's bus order (empty: main output only).
        auto *target = next.channel(a.channel);
        if (!target || !target->instrument || target->audioClip)
            return juce::Result::fail("channel.outputRoutes.invalidTarget: Choose a plugin instrument Channel");
        if (a.ids.size() > size_t(PluginBusRequest::maximumExtraOutputBuses))
            return juce::Result::fail("channel.outputRoutes.tooMany: At most seven extra outputs");
        std::vector<int> routes;
        for (const auto mixer : a.ids)
        {
            if (mixer < 0 || mixer > std::numeric_limits<int>::max() || !mixerInsert(next, int(mixer)))
                return juce::Result::fail("channel.outputRoutes.invalidMixer: Each output needs an existing Mixer track");
            routes.push_back(int(mixer));
        }
        if (routes == target->instrument->extraOutputMixers)
            return juce::Result::fail("channel.outputRoutes.unchanged: The outputs already use these Mixer tracks");
        target->instrument->extraOutputMixers = std::move(routes);
    }
    else if (id.startsWith("channel.") && id != "channel.paste")
    {
        if (!ch)
            return missing();
        if (id == "channel.source")
        {
            if (ch->source != a.text)
            {
                ch->sampler.sourceTempo = 0;
                ch->sampler.fitTempo = false;
            }
            ch->source = a.text;
            ch->instrument.reset();
            ch->name = sourceName(a.text).toUpperCase();
        }
        if (id == "channel.mixer")
            if (auto applied = applyChannelMixerAssignment(next, a); applied.failed())
                return applied;
        if (id == "channel.gain")
            ch->gain = float(a.value);
        if (id == "channel.pitch")
        {
            if (!std::isfinite(a.value) || std::abs(a.value) > 1)
                return juce::Result::fail("Channel pitch is -1..1 (pitch bend)");
            ch->pitchBend = float(a.value);
        }
        if (id == "channel.keyLabel")
        {
            const auto label = a.text.trim();
            if (a.pitch < 0 || a.pitch > 127 || label.length() > 32 || label.containsAnyOf(";=\n\r"))
                return juce::Result::fail("A key label is at most 32 characters (no ; or =) on pitch 0..127");
            if (label.isEmpty())
                ch->keyLabels.erase(a.pitch);
            else
                ch->keyLabels[a.pitch] = label;
        }
        if (id == "channel.pitchRange")
        {
            if (!std::isfinite(a.value) || a.value < 1 || a.value > 48 || std::floor(a.value) != a.value)
                return juce::Result::fail("Channel pitch range is 1..48 semitones");
            ch->pitchRange = int(a.value);
        }
        if (id == "channel.pan")
            ch->pan = float(a.value);
        if (id.startsWith("channel.sampler."))
        {
            if (ch->audioClip || ch->instrument)
                return juce::Result::fail(
                    id.startsWith("channel.sampler.modulation.")
                        ? "samplerModulation.unsupportedTarget: Local sampler Channel required"
                        : "Sampler controls require a sample or original instrument Channel");
            auto &s = ch->sampler;
            if (id.startsWith("channel.sampler.modulation."))
            {
                const auto target = samplerModulationTarget(a.text.toStdString());
                const auto *parameter = samplerModulationParameter(
                    id.fromFirstOccurrenceOf("channel.sampler.modulation.", false, false).toStdString());
                if (target < 0)
                    return juce::Result::fail("samplerModulation.invalidTarget: Explicit target id required");
                if (!parameter)
                    return juce::Result::fail("samplerModulation.invalidParameter: Known parameter required");
                if (target == 1 && parameter->id.substr(0, 3) != "lfo")
                    return juce::Result::fail(
                        "samplerModulation.unsupportedTarget: Pan provides LFO parameters only");
                auto candidate = s.modulation[size_t(target)];
                if (!setSamplerModulationValue(candidate, *parameter,
                                               parameter->boolean ? (a.enabled ? 1. : 0.) : a.value))
                    return juce::Result::fail(
                        "samplerModulation.invalidValue: Finite ranged parameter required");
                if (!validSamplerModulation(candidate, size_t(target)))
                    return juce::Result::fail(
                        "samplerModulation.panEnvelopeUnsupported: Pan supports LFO only");
                s.modulation[size_t(target)] = candidate;
            }
            if (id == "channel.sampler.filterType")
            {
                if (!std::isfinite(a.value) || a.value < 0 || a.value >= samplerFilterTypeCount ||
                    std::floor(a.value) != a.value)
                    return juce::Result::fail("samplerFilter.invalidType: Exact integer type0..5 required");
                s.filterType = int(a.value);
            }
            if (id == "channel.sampler.filterCutoff" || id == "channel.sampler.filterResonance")
            {
                if (!std::isfinite(a.value) || a.value < 0 || a.value > 1)
                    return juce::Result::fail(
                        "samplerFilter.invalidValue: Finite normalized value0..1 required");
                if (id == "channel.sampler.filterCutoff")
                    s.filterCutoff = a.value;
                else
                    s.filterResonance = a.value;
            }
            if (id == "channel.sampler.cut" || id == "channel.sampler.cutBy" ||
                id == "channel.sampler.maxPoly")
            {
                if (!std::isfinite(a.value) || a.value < 0 || a.value > 99 || std::floor(a.value) != a.value)
                    return juce::Result::fail("Sampler groups/maximum polyphony require integers0..99");
                if (id == "channel.sampler.cut")
                    s.cut = int(a.value);
                if (id == "channel.sampler.cutBy")
                    s.cutBy = int(a.value);
                if (id == "channel.sampler.maxPoly")
                    s.maxPoly = int(a.value);
            }
            if (id == "channel.sampler.cutItself")
            {
                if (!a.enabled)
                    s.cut = s.cutBy = 0;
                else
                {
                    std::array<bool, 100> used{};
                    for (const auto &other : next.channels)
                        if (other.id != ch->id)
                        {
                            if (other.sampler.cut < 0 || other.sampler.cut > 99 || other.sampler.cutBy < 0 ||
                                other.sampler.cutBy > 99)
                                return juce::Result::fail("Invalid existing Cut group");
                            used[size_t(other.sampler.cut)] = true;
                            used[size_t(other.sampler.cutBy)] = true;
                        }
                    int group =
                        s.cut > 0 && s.cut <= 99 && s.cut == s.cutBy && !used[size_t(s.cut)] ? s.cut : 0;
                    if (!group)
                        for (int candidate = 1; candidate <= 99; ++candidate)
                            if (!used[size_t(candidate)])
                            {
                                group = candidate;
                                break;
                            }
                    if (!group)
                        return juce::Result::fail("No free Cut group1..99; assign an explicit shared group");
                    s.cut = s.cutBy = group;
                }
            }
            if (id == "channel.sampler.sourceTempo")
                s.sourceTempo = a.value;
            if (id == "channel.sampler.fitTempo")
                s.fitTempo = a.enabled;
            if (id == "channel.sampler.stretch")
                s.stretch = a.enabled;
            if (id == "channel.sampler.stretchPitch")
                s.stretchPitch = a.value;
            if (id == "channel.sampler.timeMul")
                s.timeMul = a.value;
            if (id == "channel.sampler.timeBeats")
                s.timeBeats = a.value;
            if (id == "channel.sampler.hqInterpolation")
                s.hqInterpolation = a.enabled;
            if (id == "channel.sampler.tempo")
            {
                if (!std::isfinite(a.value) || a.value < 20 || a.value > 400)
                    return juce::Result::fail("samplerTempo.range: Source BPM must be20-400");
                s.sourceTempo = a.value;
                s.fitTempo = a.enabled;
            }
            if (id == "channel.sampler.root")
            {
                if (!std::isfinite(a.value) || a.value < -1 || a.value > 127 ||
                    std::floor(a.value) != a.value)
                    return juce::Result::fail("Root must be Auto (-1) or a MIDI note 0-127");
                s.root = int(a.value);
            }
            if (id == "channel.sampler.cents")
                s.cents = a.value;
            if (id == "channel.sampler.start")
                s.start = a.value;
            if (id == "channel.sampler.end")
                s.end = a.value;
            if (id == "channel.sampler.range")
            {
                s.start = a.beat;
                s.end = a.length;
            }
            if (id == "channel.sampler.normalize")
                s.normalize = a.enabled;
            if (id == "channel.sampler.reverse")
                s.reverse = a.enabled;
            if (id == "channel.sampler.loop")
                s.loop = a.enabled;
            if (id == "channel.sampler.pingPong")
                s.pingPong = a.enabled;
            if (id == "channel.sampler.loopStart")
                s.loopStart = a.value;
            if (id == "channel.sampler.loopEnd")
                s.loopEnd = a.value;
            if (id == "channel.sampler.loopPoints")
            {
                // One atomic edit: loop range plus Use loop points (e.g. from embedded file loops).
                s.loopStart = a.beat;
                s.loopEnd = a.length;
                s.loop = a.enabled;
            }
            if (id == "channel.sampler.loopRange")
            {
                s.loopStart = a.beat;
                s.loopEnd = a.length;
            }
            if (id == "channel.sampler.fadeIn" || id == "channel.sampler.fadeOut" ||
                id == "channel.sampler.loopCrossfade")
            {
                if (!std::isfinite(a.value) || a.value < 0 || a.value > 10)
                    return juce::Result::fail("samplerFade.invalidValue: Fade seconds 0..10 required");
                (id == "channel.sampler.fadeIn"    ? s.fadeIn
                 : id == "channel.sampler.fadeOut" ? s.fadeOut
                                                   : s.loopCrossfade) = a.value;
            }
            if (id == "channel.sampler.envelope")
                s.envelope = a.enabled;
            if (id == "channel.sampler.attack")
                s.attack = a.value;
            if (id == "channel.sampler.decay")
                s.decay = a.value;
            if (id == "channel.sampler.sustain")
                s.sustain = a.value;
            if (id == "channel.sampler.release")
                s.release = a.value;
        }
    }
    else if (id == "coproducer.accept")
    {
        if (!ch || a.recordedNotes.empty() || a.recordedNotes.size() > 65536 || !std::isfinite(a.length) ||
            a.length <= 0 || a.length > 4096 || !std::isfinite(a.beat) || a.beat < 0 || a.lane < 0 ||
            a.lane > 127 || a.text.length() > 256)
            return juce::Result::fail("Invalid CoProducer part.");
        Pattern generated{next.allocate(), a.text, a.length, {}};
        generated.autoLength = false; // The accepted region includes its explicitly requested rests.
        generated.generation = a.generation;
        for (auto note : a.recordedNotes)
        {
            if (!std::isfinite(note.beat) || !std::isfinite(note.length) || note.beat < 0 ||
                note.length <= 0 || note.beat + note.length > a.length)
                return juce::Result::fail("Generated notes must fit the accepted region.");
            note.id = next.allocate();
            note.channel = ch->id;
            note.step = false;
            generated.notes.push_back(note);
        }
        const auto generatedId = generated.id;
        next.patterns.push_back(std::move(generated));
        if (a.enabled)
            next.placements.push_back({next.allocate(), generatedId, a.lane, a.beat, a.length, 0});
    }
    else if (id == "coproducer.replace")
    {
        if (!pat || !ch || !std::isfinite(a.beat) || a.beat < 0 || !std::isfinite(a.length) ||
            a.length <= 0 || a.length > 4096 || a.beat + a.length > 1000000 || a.recordedNotes.empty() ||
            a.recordedNotes.size() > 65536)
            return juce::Result::fail("Invalid CoProducer replacement region.");
        Placement *placement = nullptr;
        if (a.enabled)
        {
            for (auto &item : next.placements)
                if (item.id == a.target)
                    placement = &item;
            if (!placement || placement->pattern != pat->id || a.beat < placement->offset ||
                a.beat + a.length > std::min(pat->length, placement->offset + placement->length))
                return juce::Result::fail("Replacement must fit one selected clip's source window.");
        }
        const double end = a.beat + a.length;
        std::vector<Note> kept;
        for (const auto &note : pat->notes)
        {
            if (note.channel != ch->id || note.beat >= end || note.beat + note.length <= a.beat ||
                (placement && note.beat < placement->offset))
            {
                kept.push_back(note);
                continue;
            }
            if (std::find(a.ids.begin(), a.ids.end(), note.id) != a.ids.end())
                return juce::Result::fail(
                    "Replacement intersects a protected source note. Choose a new part or another region.");
            if (note.beat < a.beat)
            {
                auto left = note;
                left.length = a.beat - note.beat;
                kept.push_back(left);
            }
            if (note.beat + note.length > end)
            {
                auto right = note;
                right.beat = end;
                right.length = note.beat + note.length - end;
                if (note.beat < a.beat)
                    right.id = next.allocate();
                kept.push_back(right);
            }
        }
        for (auto note : a.recordedNotes)
        {
            if (!std::isfinite(note.beat) || !std::isfinite(note.length) || note.beat < 0 ||
                note.length <= 0 || note.beat + note.length > a.length)
                return juce::Result::fail("Replacement notes must fit the target region.");
            note.id = next.allocate();
            note.channel = ch->id;
            note.beat += a.beat;
            note.step = false;
            kept.push_back(note);
        }
        if (placement)
        {
            auto unique = *pat;
            unique.id = next.allocate();
            unique.name = unique.name.substring(0, 220) + " CoProducer";
            unique.notes = std::move(kept);
            unique.generation = a.generation;
            refreshAutomaticPatternLength(next, unique);
            for (auto &note : unique.notes)
                note.id = next.allocate();
            placement->pattern = unique.id;
            next.patterns.push_back(std::move(unique));
        }
        else
        {
            pat->notes = std::move(kept);
            refreshAutomaticPatternLength(next, *pat);
            pat->generation = a.generation;
        }
    }
    else if (id == "pattern.add")
        next.patterns.push_back({next.allocate(),
                                 "Pattern " + juce::String(int(next.patterns.size()) + 1),
                                 next.numerator * 4. / next.denominator,
                                 {}});
    else if (id == "pattern.cloneSelected")
    {
        if (auto cloned = pattern_management::clone(next, a.ids); cloned.failed())
            return cloned;
    }
    else if (id == "pattern.deleteSelected")
    {
        if (auto removed = pattern_management::remove(next, a.ids); removed.failed())
            return removed;
    }
    else if (id == "pattern.deleteEmpty" || id == "pattern.deleteUnplaced")
    {
        if (auto purged = pattern_management::purge(next, id == "pattern.deleteEmpty" ? "empty" : "unplaced");
            purged.failed())
            return purged;
    }
    else if (id == "pattern.clone")
    {
        if (!pat)
            return missing();
        auto copy = *pat;
        copy.id = next.allocate();
        copy.name += " copy";
        for (auto &n : copy.notes)
            n.id = next.allocate();
        const auto index = std::distance(next.patterns.data(), pat);
        next.patterns.insert(next.patterns.begin() + index + 1, std::move(copy));
    }
    else if (id == "pattern.splitChannels")
    {
        if (!pat)
            return missing();
        const Pattern source = *pat;
        const auto index = std::distance(next.patterns.data(), pat);
        std::vector<Pattern> parts;
        for (const auto &channel : next.channels)
        {
            Pattern part = source;
            std::erase_if(part.notes, [&](const auto &note) { return note.channel != channel.id; });
            std::erase_if(part.channelLoops, [&](const auto &loop) { return loop.channel != channel.id; });
            if (part.notes.empty() && part.channelLoops.empty())
                continue;
            refreshAutomaticPatternLength(next, part);
            part.name = channel.name.trim().replaceCharacters("\r\n", "  ").substring(0, 256);
            if (part.name.isEmpty())
                part.name = "Channel " + juce::String(channel.id);
            parts.push_back(std::move(part));
        }
        if (parts.empty())
            return juce::Result::fail("The selected Pattern has no Channel notes to split");
        const auto extra = parts.size() - 1;
        const auto placements =
            size_t(std::count_if(next.placements.begin(), next.placements.end(),
                                 [&](const auto &clip) { return clip.pattern == source.id; }));
        if (next.patterns.size() + extra > 1024 || next.placements.size() + next.audioPlacements.size() +
                                                           next.automation.placements.size() +
                                                           placements * extra >
                                                       100000)
            return juce::Result::fail("Split by channel would exceed the project's Pattern or clip limit");
        // Partition ownership: retain every note ID, instrument and the first
        // Pattern ID. Linked placements expand to aligned parts on their own
        // original lane, preserving source windows, trims and independent mute.
        std::vector<Id> added;
        for (size_t i = 1; i < parts.size(); ++i)
        {
            parts[i].id = next.allocate();
            added.push_back(parts[i].id);
        }
        std::vector<Placement> clips;
        clips.reserve(placements * extra);
        for (const auto &clip : next.placements)
            if (clip.pattern == source.id)
                for (const auto pattern : added)
                {
                    auto copy = clip;
                    copy.id = next.allocate();
                    copy.pattern = pattern;
                    clips.push_back(copy);
                }
        next.patterns[size_t(index)] = std::move(parts.front());
        next.patterns.insert(next.patterns.begin() + index + 1, std::make_move_iterator(parts.begin() + 1),
                             std::make_move_iterator(parts.end()));
        next.placements.insert(next.placements.end(), clips.begin(), clips.end());
    }
    else if (id == "pattern.group")
    {
        const auto name = a.text.trim();
        if (name.length() > 128 || name.containsChar('\n') || name.containsChar('\r') || a.ids.empty() ||
            a.ids.size() > 1024)
            return juce::Result::fail("Select Patterns and a group name of at most 128 characters");
        std::set<Id> seen;
        for (const auto target : a.ids)
        {
            auto *p = next.pattern(target);
            if (!p || !seen.insert(target).second)
                return missing();
            p->group = name;
        }
    }
    else if (id == "pattern.followAction")
    {
        // text: "" none, again, next, previous, first, last, any, other, stop; value: plays before it (1..64).
        auto *p = next.pattern(a.pattern);
        if (!p)
            return missing();
        const auto plays = a.value == 0 ? 1. : a.value;
        if (!performance::validFollowAction(a.text) || !std::isfinite(plays) || plays < 1 || plays > 64 ||
            std::floor(plays) != plays)
            return juce::Result::fail("followAction.invalid: Action none/again/next/previous/first/last/any/other/stop after 1..64 plays");
        p->followAction = a.text;
        p->followAfter = int(plays);
    }
    else if (id == "pattern.launchQuantum")
    {
        auto *p = next.pattern(a.pattern);
        if (!p)
            return missing();
        if (!validLaunchQuantum(a.value))
            return juce::Result::fail("Launch quantum is -1 (one bar), 0 (immediately) or 1/16..64 beats");
        p->launchQuantum = a.value;
    }
    else if (id == "picker.place")
    {
        if (a.ids.empty() || a.ids.size() > 128 || a.lane < 0 || size_t(a.lane) + a.ids.size() > 128 ||
            !std::isfinite(a.beat) || a.beat < 0)
            return juce::Result::fail("Picker placement needs valid sources, time and available lanes");
        std::set<Id> seen;
        int lane = a.lane;
        for (const auto target : a.ids)
        {
            if (!seen.insert(target).second)
                return missing();
            if (a.text == "pattern")
            {
                const auto *p = next.pattern(target);
                if (!p)
                    return missing();
                next.placements.push_back({next.allocate(), target, lane++, a.beat, p->length, 0, false});
            }
            else if (a.text == "audio")
            {
                const auto *c = next.channel(target);
                if (!c || !c->audioClip)
                    return missing();
                next.audioPlacements.push_back(
                    {next.allocate(), target, lane++, a.beat, 0, c->audioDuration, false});
            }
            else if (a.text == "automation")
            {
                const auto *source = next.automationSource(target);
                if (!source)
                    return missing();
                next.automation.placements.push_back(
                    {next.allocate(), target, a.beat, source->length, 0, lane++, false});
            }
            else
                return juce::Result::fail("Unknown Picker source type");
        }
    }
    else if (id == "pattern.transpose")
    {
        if (!pat || (a.channel != 0 && !ch))
            return missing();
        if (!std::isfinite(a.value) || std::floor(a.value) != a.value || a.value < -127 || a.value > 127)
            return juce::Result::fail("Transpose must be a whole number of semitones from -127 to 127");
        bool found = false;
        for (auto &note : pat->notes)
            if (a.channel == 0 || note.channel == a.channel)
            {
                found = true;
                const int pitch = note.pitch + int(a.value);
                if (pitch < 0 || pitch > 127)
                    return juce::Result::fail("Transpose would move a note outside MIDI pitches 0 to 127");
                note.pitch = pitch;
            }
        if (!found)
            return juce::Result::fail("The selected Pattern Channels have no notes to transpose");
    }
    else if (id == "rack.transpose")
    {
        // args.ids Channels, args.value semitones: their notes in every Pattern move together.
        if (a.ids.empty() || std::set<Id>(a.ids.begin(), a.ids.end()).size() != a.ids.size() ||
            std::any_of(a.ids.begin(), a.ids.end(), [&](Id channel) { return !next.channel(channel); }))
            return juce::Result::fail("rack.transpose.invalidTarget: Select existing, distinct Channels");
        if (!std::isfinite(a.value) || std::floor(a.value) != a.value || a.value < -127 || a.value > 127 ||
            a.value == 0)
            return juce::Result::fail("rack.transpose.invalidValue: A whole, nonzero number of semitones -127..127");
        const std::set<Id> chosenChannels(a.ids.begin(), a.ids.end());
        bool found = false;
        for (auto &pattern : next.patterns)
            for (auto &note : pattern.notes)
                if (chosenChannels.contains(note.channel))
                {
                    found = true;
                    const int pitch = note.pitch + int(a.value);
                    if (pitch < 0 || pitch > 127)
                        return juce::Result::fail("rack.transpose.outOfRange: A note would leave MIDI pitches 0..127");
                    note.pitch = pitch;
                }
        if (!found)
            return juce::Result::fail("rack.transpose.noNotes: The selected Channels have no notes");
    }
    else if (id == "pattern.rename" || id == "pattern.properties")
    {
        if (!pat)
            return missing();
        auto name = a.text.trim();
        if (name.isEmpty() || name.length() > 256 || name.containsChar('\n') || name.containsChar('\r'))
            return juce::Result::fail("Pattern name must contain 1 to 256 characters on one line");
        pat->name = name;
        if (id == "pattern.properties")
        {
            if (!std::isfinite(a.value) || std::floor(a.value) != a.value || a.value < 0 ||
                a.value > 4294967295. || (a.value != 0 && a.value < 4278190080.))
                return juce::Result::fail("Invalid Pattern colour");
            pat->colour = juce::uint32(a.value);
        }
    }
    else if (id == "pattern.insert")
    {
        if (!pat)
            return missing();
        const auto index = std::distance(next.patterns.data(), pat);
        Pattern empty{next.allocate(),
                      "Pattern " + juce::String(int(next.patterns.size()) + 1),
                      next.numerator * 4. / next.denominator,
                      {}};
        next.patterns.insert(next.patterns.begin() + index, std::move(empty));
    }
    else if (id == "pattern.delete")
    {
        if (!pat)
            return missing();
        pattern_management::erase(next, {pat->id}); // Also repairs clip-group membership.
    }
    else if (id == "pattern.move")
    {
        if (!pat)
            return missing();
        if (a.lane < 0 || a.lane >= int(next.patterns.size()))
            return juce::Result::fail("Pattern destination is outside the selector list");
        const auto index = std::distance(next.patterns.data(), pat);
        auto moved = std::move(*pat);
        next.patterns.erase(next.patterns.begin() + index);
        next.patterns.insert(next.patterns.begin() + a.lane, std::move(moved));
    }
    else if (id == "pattern.channelLoop" || id == "pattern.burnChannelLoop")
    {
        if (!pat || !ch || ch->audioClip)
            return juce::Result::fail(
                "channelLoop.unavailableTarget: Existing Pattern and score Channel required");
        if (id == "pattern.channelLoop")
        {
            const auto mode = channelLoopMode(a.text);
            if (a.text != "off" && !mode)
                return juce::Result::fail("channelLoop.invalidMode: Choose off, fixed, step, beat or bar");
            auto found = std::find_if(pat->channelLoops.begin(), pat->channelLoops.end(),
                                      [&](const auto &loop) { return loop.channel == a.channel; });
            if (!mode)
            {
                if (found != pat->channelLoops.end())
                    pat->channelLoops.erase(found);
            }
            else
            {
                ChannelLoop loop{a.channel, *mode, *mode == ChannelLoopMode::fixed ? a.length : 1};
                if (found == pat->channelLoops.end())
                    pat->channelLoops.push_back(loop);
                else
                    *found = loop;
            }
        }
        else
        {
            if (!channelLoop(*pat, a.channel))
                return juce::Result::fail("channelLoop.notEnabled: Enable a Channel loop before burning it");
            auto expanded = rawPatternOccurrences(next, *pat);
            std::vector<Note> notes;
            for (const auto &note : pat->notes)
                if (note.channel != a.channel || note.beat >= pat->length)
                    notes.push_back(note);
            for (const auto &occurrence : expanded)
                if (occurrence.note.channel == a.channel && occurrence.note.beat < pat->length)
                {
                    auto note = occurrence.note;
                    if (occurrence.iteration != 0)
                        note.id = next.allocate();
                    notes.push_back(std::move(note));
                }
            if (notes.size() > 100000)
                return juce::Result::fail("channelLoop.capacity: Burn would exceed100000 source notes");
            pat->notes = std::move(notes);
            std::erase_if(pat->channelLoops, [&](const auto &loop) { return loop.channel == a.channel; });
        }
    }
    else if (id == "pattern.length")
    {
        if (!pat)
            return missing();
        pat->length = a.length;
        pat->autoLength = false;
    }
    else if (id == "pattern.extent")
    {
        if (!pat)
            return juce::Result::fail("patternExtent.missingTarget: Existing Pattern required");
        if (a.text != "auto" && a.text != "manual")
            return juce::Result::fail("patternExtent.invalidMode: Choose auto or manual");
        if (a.text == "manual" && (!std::isfinite(a.length) || a.length < .25 || a.length > 1000000 ||
                                   std::abs(a.length * 4 - std::round(a.length * 4)) > 1.e-9))
            return juce::Result::fail(
                "patternExtent.invalidLength: Use0.25..1000000 quarter-note beats in steps of0.25");
        pat->autoLength = a.text == "auto";
        if (!pat->autoLength)
            pat->length = a.length;
        refreshAutomaticPatternLength(next, *pat);
    }
    else if (id == "pattern.meter")
    {
        if (!pat)
            return juce::Result::fail("patternExtent.missingTarget: Existing Pattern required");
        if (a.enabled &&
            (a.numerator < 1 || a.numerator > 32 ||
             (a.denominator != 2 && a.denominator != 4 && a.denominator != 8 && a.denominator != 16)))
            return juce::Result::fail(
                "patternExtent.invalidMeter: Use numerator1..32 and denominator2,4,8,16");
        pat->numerator = a.enabled ? a.numerator : 0;
        pat->denominator = a.enabled ? a.denominator : 0;
        refreshAutomaticPatternLength(next, *pat);
    }
    else if (id == "steps.compose")
    {
        const bool preview = continuingGesture && editOwner.startsWith("step-compose:");
        if ((!a.enabled || a.compositionSolo) && !preview)
            return juce::Result::fail("stepCompose.invalidOwner: Restore requires the owned preview");
        if (preview) next = *gestureStart;
        if (a.enabled)
            if (auto result =
                    applyStepComposition(next, a.pattern, a.composition, a.retainSteps.value_or(false));
                result.failed())
                return result;
        if (a.compositionSolo)
        {
            CommandArgs solo;
            solo.channel = a.compositionSolo;
            solo.enabled = true;
            solo.soloScope = "global";
            if (auto result = applyChannelMuteCommand(next, "channel.solo", solo); result.failed())
                return result;
        }
    }
    else if (id == "steps.rotate" || id == "steps.clear")
    {
        // args.ids Channels (or args.channel); steps.rotate moves their step-sequencer steps by
        // args.value steps, wrapping inside the Pattern's step count; steps.clear removes them.
        // Piano-roll (non-step) notes stay where they are.
        if (!pat)
            return missing();
        const auto targets = a.ids.empty() ? std::vector<Id>{a.channel} : a.ids;
        const std::set<Id> chosen(targets.begin(), targets.end());
        if (chosen.empty() || std::any_of(chosen.begin(), chosen.end(), [&](Id channel)
                                          { const auto *c = next.channel(channel); return !c || c->audioClip; }))
            return juce::Result::fail("steps.invalidTarget: Choose playable instrument Channels");
        const int count = stepSequenceSize(*pat);
        int delta = 0;
        if (id == "steps.rotate")
        {
            if (!std::isfinite(a.value) || std::floor(a.value) != a.value || a.value == 0 ||
                std::abs(a.value) >= count)
                return juce::Result::fail("steps.rotate.invalidValue: A whole, nonzero step count inside the Pattern");
            delta = int(a.value);
        }
        bool found = false;
        std::vector<Note> result;
        result.reserve(pat->notes.size());
        for (auto note : pat->notes)
        {
            const bool step = note.step && chosen.contains(note.channel) && note.beat >= 0 && note.beat < count * .25;
            if (!step)
            {
                result.push_back(note);
                continue;
            }
            found = true;
            if (id == "steps.clear")
                continue;
            const int cell = int(std::floor(note.beat * 4. + 1.e-9));
            const double offset = note.beat - cell * .25; // Keeps a step's own time shift.
            note.beat = ((cell + delta) % count + count) % count * .25 + offset;
            result.push_back(note);
        }
        if (!found)
            return juce::Result::fail("steps.empty: The Channel has no steps in this Pattern");
        pat->notes = std::move(result);
    }
    else if (id.startsWith("steps.fill"))
    {
        if (!pat)
            return missing();
        const double requested = id == "steps.fill"    ? a.interval
                                 : id == "steps.fill2" ? 2
                                 : id == "steps.fill4" ? 4
                                                       : 8;
        if (requested != 2 && requested != 4 && requested != 8)
            return juce::Result::fail("Fill interval must be 2, 4 or 8 sixteenth-note steps");
        const int interval = int(requested), count = stepSequenceSize(*pat);
        const int hits = (count + interval - 1) / interval;
        const auto targets = a.ids.empty() ? std::vector<Id>{a.channel} : a.ids;
        if (targets.empty() || targets.size() > 128 || size_t(hits) * targets.size() > 100000)
            return juce::Result::fail(
                "Fill exceeds the Pattern note limit; shorten the Pattern or selection");
        std::map<Id, size_t> indices;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            const auto *target = next.channel(targets[i]);
            if (!target || target->audioClip)
                return juce::Result::fail("Fill needs playable instrument Channels");
            if (!indices.emplace(targets[i], i).second)
                return juce::Result::fail("Duplicate fill Channel ID");
        }
        std::vector<std::vector<bool>> occupied(targets.size(), std::vector<bool>(size_t(hits), false));
        std::vector<Note> result;
        result.reserve(pat->notes.size());
        for (auto note : pat->notes)
        {
            const auto target = indices.find(note.channel);
            if (target == indices.end() || note.beat >= pat->length)
            {
                result.push_back(note);
                continue;
            }
            if (a.retainSteps.value_or(false) &&
                (!note.step || std::abs(note.beat * 4 - std::round(note.beat * 4)) > .00001))
                return juce::Result::fail("stepMemory.melodicConversionRequired: Retained basic fills need "
                                          "on-grid steps; use explicit Advanced Fill conversion");
            const int cell = int(std::floor(note.beat * 4.));
            if (cell % interval != 0)
            {
                if (a.retainSteps.value_or(false))
                {
                    note.muted = true;
                    result.push_back(note);
                }
                continue;
            }
            note.beat = cell * .25;
            if (!a.retainSteps.value_or(false) || !note.step)
                note.length = .25;
            note.step = true;
            if (a.retainSteps.value_or(false))
                note.muted = false;
            occupied[target->second][size_t(cell / interval)] = true;
            result.push_back(note);
        }
        size_t additional = 0;
        for (const auto &part : occupied)
            additional += size_t(std::count(part.begin(), part.end(), false));
        if (result.size() + additional > 100000)
            return juce::Result::fail("Fill exceeds the Pattern note limit; no Channels changed");
        result.reserve(result.size() + additional);
        for (size_t part = 0; part < targets.size(); ++part)
            for (int hit = 0; hit < hits; ++hit)
                if (!occupied[part][size_t(hit)])
                    result.push_back(
                        {next.allocate(), targets[part], hit * interval * .25, .25, 60, .8f, true});
        pat->notes = std::move(result);
    }
    else if (id == "steps.release")
    {
        if (auto edited = applyStepRelease(next, continuingGesture ? *gestureStart : canonicalSource, a);
            edited.failed())
            return edited;
    }
    else if (id == "steps.repeat")
    {
        if (auto edited = applyStepRepeat(next, continuingGesture ? *gestureStart : canonicalSource, a);
            edited.failed())
            return edited;
    }
    else if (id == "steps.shift")
    {
        if (auto edited = applyStepShift(next, continuingGesture ? *gestureStart : canonicalSource, a);
            edited.failed())
            return edited;
    }
    else if (id == "steps.pan" || id == "steps.fine" || id == "steps.modX" || id == "steps.modY")
    {
        const auto property = id == "steps.pan"    ? StepExpression::pan
                              : id == "steps.fine" ? StepExpression::fine
                              : id == "steps.modX" ? StepExpression::modX
                                                   : StepExpression::modY;
        if (auto edited =
                applyStepExpression(next, continuingGesture ? *gestureStart : canonicalSource, a, property);
            edited.failed())
            return edited;
    }
    else if (id == "steps.pitch")
    {
        if (auto edited = applyStepPitch(next, continuingGesture ? *gestureStart : canonicalSource, a);
            edited.failed())
            return edited;
    }
    else if (id == "steps.velocity")
    {
        if (auto edited = applyStepVelocity(next, continuingGesture ? *gestureStart : canonicalSource, a);
            edited.failed())
            return edited;
    }
    else if (id == "steps.set" && a.retainSteps.value_or(false))
    {
        if (auto result = applyStepActivation(next, a.pattern, a.channel, a.beat, a.enabled,
                                              step_memory::Retention::mute);
            result.failed())
            return result;
    }
    else if (id == "steps.set")
    {
        if (!pat || !ch)
            return missing();
        const double period = channelLoopLength(next, *pat, ch->id);
        if (period > 0 && (!std::isfinite(a.beat) || a.beat < 0 || a.beat >= pat->length))
            return juce::Result::fail("step.invalidBeat: Looped step must be inside the Pattern window");
        const double stepBeat = a.beat; // Source position; playback ghosts do not redirect editing.
        auto it =
            std::find_if(pat->notes.begin(), pat->notes.end(), [&](auto &n)
                         { return n.channel == ch->id && n.step && std::abs(n.beat - stepBeat) < .00001; });
        if (a.enabled && it != pat->notes.end())
            for (auto &note : pat->notes)
                if (note.channel == ch->id && note.step && std::abs(note.beat - stepBeat) < .00001)
                    note.muted = false;
        if (a.enabled && it == pat->notes.end())
            pat->notes.push_back({next.allocate(), ch->id, stepBeat, .25, 60, .8f, true});
        if (!a.enabled)
            std::erase_if(
                pat->notes, [&](const auto &note)
                { return note.channel == ch->id && note.step && std::abs(note.beat - stepBeat) < .00001; });
    }
    else if (id == "channel.paste")
    {
        if (!pat || !ch)
            return missing();
        auto *source = next.pattern(a.target);
        if (!source)
            return missing();
        std::vector<Note> notes;
        for (auto &n : source->notes)
            if (n.channel == Id(a.value))
            {
                auto copy = n;
                copy.id = next.allocate();
                copy.channel = ch->id;
                notes.push_back(copy);
            }
        std::erase_if(pat->notes, [&](auto &n) { return n.channel == ch->id; });
        pat->notes.insert(pat->notes.end(), notes.begin(), notes.end());
        refreshAutomaticPatternLength(next, *pat);
    }
    else if (id == "notes.importTracks")
    {
        // Notes/controllers carry their 1-based source MIDI track in `channel`.
        const auto *templateChannel = next.channel(a.channel);
        if (!pat || !templateChannel || templateChannel->audioClip || a.recordedNotes.empty())
            return missing();
        std::set<Id> tracks;
        for (const auto &note : a.recordedNotes)
            tracks.insert(note.channel);
        if (tracks.count(0) || *tracks.rbegin() > 256 || next.channels.size() + tracks.size() > size_t(Project::maxChannels))
            return juce::Result::fail("Import at most " + juce::String(Project::maxChannels) +
                                      " Channels in total from MIDI tracks 1..256");
        const auto source = *templateChannel;
        std::map<Id, Id> created;
        for (const auto track : tracks)
        {
            auto channel = source;
            channel.id = next.allocate();
            if (channel.instrument)
                channel.instrument->id = next.allocate();
            const auto &names = a.importTrackNames;
            channel.name = track <= names.size() && names[size_t(track - 1)].isNotEmpty()
                               ? names[size_t(track - 1)].substring(0, 64)
                               : "MIDI track " + juce::String(juce::int64(track));
            channel.muted = false;
            created[track] = channel.id;
            next.channels.push_back(std::move(channel));
        }
        for (auto note : a.recordedNotes)
        {
            note.channel = created[note.channel];
            note.step = false;
            note.id = next.allocate();
            pat->notes.push_back(note);
        }
        for (auto control : a.recordedControllers)
            if (created.count(control.channel))
            {
                control.channel = created[control.channel];
                control.id = next.allocate();
                pat->controllers.push_back(control);
            }
        std::stable_sort(pat->controllers.begin(), pat->controllers.end(),
                         [](const auto &x, const auto &y) { return x.beat < y.beat; });
        if (a.importTempo)
        {
            if (auto applied = applyImportedTempo(next, *a.importTempo); applied.failed())
                return applied;
        }
        refreshAutomaticPatternLength(next, *pat);
    }
    else if (id == "notes.record" || id == "notes.import")
    {
        if (!pat || (a.recordedNotes.empty() && (id != "notes.record" || a.recordedControllers.empty())))
            return missing();
        if (id == "notes.import" && ch && !a.enabled) // Replace: the Channel's controllers go too.
            std::erase_if(pat->controllers, [&](const auto &control) { return control.channel == ch->id; });
        if (id == "notes.import" && a.importTempo)
            if (auto applied = applyImportedTempo(next, *a.importTempo); applied.failed())
                return applied;
        for (auto control : a.recordedControllers)
        {
            if (id == "notes.import" && ch)
                control.channel = ch->id;
            if (!next.channel(control.channel))
                return missing();
            control.id = next.allocate();
            pat->controllers.push_back(control);
        }
        std::stable_sort(pat->controllers.begin(), pat->controllers.end(),
                         [](const auto &x, const auto &y) { return x.beat < y.beat; });
        if (id == "notes.import")
        {
            if (!ch)
                return missing();
            if (!a.enabled)
                std::erase_if(pat->notes, [&](const auto &note) { return note.channel == ch->id; });
        }
        if (id == "notes.record" && a.replaceRecorded)
        {
            std::map<Id, std::pair<double, double>> spans;
            for (const auto &note : a.recordedNotes)
            {
                auto [it, inserted] = spans.try_emplace(note.channel, note.beat, note.beat + note.length);
                if (!inserted)
                    it->second = {std::min(it->second.first, note.beat), std::max(it->second.second, note.beat + note.length)};
            }
            std::erase_if(pat->notes, [&](const auto &note)
                          {
                              const auto span = spans.find(note.channel);
                              return span != spans.end() && !note.step && note.beat < span->second.second &&
                                     note.beat + note.length > span->second.first;
                          });
        }
        for (auto note : a.recordedNotes)
        {
            if (id == "notes.import")
            {
                note.channel = ch->id;
                note.step = false;
            }
            if (!next.channel(note.channel))
                return missing();
            note.id = next.allocate();
            pat->notes.push_back(note);
        }
        refreshAutomaticPatternLength(next, *pat);
        if (a.enabled && id == "notes.record")
        {
            auto found = std::find_if(next.placements.begin(), next.placements.end(), [&](const auto &p)
                                      { return p.id == a.target && p.pattern == pat->id; });
            if (found != next.placements.end())
                found->length = std::max(found->length, pat->length - found->offset);
            else
                next.placements.push_back({next.allocate(), pat->id, a.lane, a.beat, pat->length, 0});
        }
    }
    else if (id.startsWith("notes."))
    {
        if (!pat)
            return missing();
        if (id == "notes.add")
        {
            if (!ch)
                return missing();
            if (ch->audioClip || ((a.slide || a.portamento) && ch->instrument))
                return juce::Result::fail(
                    "noteMode.unsupportedTarget: Note modes require an eligible score source");
            Note note{next.allocate(), ch->id, a.beat, a.length, a.pitch, float(a.value), a.step};
            note.slide = a.slide;
            note.portamento = a.portamento;
            note.colourGroup = a.colourGroup;
            pat->notes.push_back(note);
        }
        if (id == "notes.properties")
        {
            const auto &patch = *a.noteProperties;
            if (auto valid = validateNotePropertiesPatch(patch); valid.failed())
                return valid;
            if (!ch || ch->audioClip || a.ids.empty() ||
                std::set<Id>(a.ids.begin(), a.ids.end()).size() != a.ids.size())
                return juce::Result::fail(
                    "noteMode.invalidTarget: Exact playable Channel and distinct Note IDs required");
            if ((patch.beat || patch.length || patch.pitch) && a.ids.size() != 1)
                return juce::Result::fail(
                    "noteMode.multipleTiming: Absolute timing/pitch edits require one Note");
            if (ch->instrument &&
                (patch.slide || patch.portamento || patch.pan || patch.fineCents || patch.modX || patch.modY))
                return juce::Result::fail(
                    "noteMode.unsupportedTarget: Sampler properties cannot edit native instrument notes");
            for (const auto &note : pat->notes)
                if (chosen(note.id) &&
                    (note.channel != a.channel || (patch.slide.value_or(note.slide) && note.repeatMode != 0)))
                    return juce::Result::fail(
                        "noteMode.incompatibleTarget: Foreign Channel or Slide/Repeat conflict");
            for (auto &note : pat->notes)
                if (chosen(note.id))
                {
                    if (patch.slide)
                        note.slide = *patch.slide;
                    if (patch.portamento)
                        note.portamento = *patch.portamento;
                    if (patch.muted)
                        note.muted = *patch.muted;
                    if (patch.colourGroup)
                        note.colourGroup = *patch.colourGroup;
                    if (patch.pitch)
                        note.pitch = *patch.pitch;
                    if (patch.velocity)
                        note.velocity = float(*patch.velocity);
                    if (patch.pan)
                        note.pan = *patch.pan;
                    if (patch.fineCents)
                        note.fineCents = *patch.fineCents;
                    if (patch.modX)
                        note.modX = *patch.modX;
                    if (patch.modY)
                        note.modY = *patch.modY;
                    if (patch.releaseVelocity)
                        note.releaseVelocity = *patch.releaseVelocity;
                    if (patch.beat)
                        note.beat = *patch.beat;
                    if (patch.length)
                        note.length = *patch.length;
                }
        }
        if (id == "notes.slide" || id == "notes.portamento" || id == "notes.colourGroup")
        {
            if (!ch || ch->audioClip || a.ids.empty() ||
                std::set<Id>(a.ids.begin(), a.ids.end()).size() != a.ids.size())
                return juce::Result::fail(
                    "noteMode.invalidTarget: Exact Pattern, Channel and distinct Note IDs required");
            if (id != "notes.colourGroup" && ch->instrument)
                return juce::Result::fail(
                    "noteMode.unsupportedTarget: Slide and Portamento edit local sampler notes only");
            if (id == "notes.colourGroup" &&
                (!std::isfinite(a.value) || a.value < 0 || a.value > 15 || std::floor(a.value) != a.value))
                return juce::Result::fail("noteMode.invalidValue: Exact colour group0..15 required");
            for (const auto &note : pat->notes)
                if (chosen(note.id) &&
                    (note.channel != a.channel || (id == "notes.slide" && a.enabled && note.repeatMode != 0)))
                    return juce::Result::fail(
                        "noteMode.incompatibleTarget: Foreign Channel or Slide/Repeat conflict");
            for (auto &note : pat->notes)
                if (chosen(note.id))
                {
                    if (id == "notes.slide")
                        note.slide = a.enabled;
                    else if (id == "notes.portamento")
                        note.portamento = a.enabled;
                    else
                        note.colourGroup = int(a.value);
                }
        }
        if (id == "notes.delete")
            std::erase_if(pat->notes, [&](auto &n) { return chosen(n.id); });
        if (id == "notes.move")
            for (auto &n : pat->notes)
                if (chosen(n.id))
                {
                    n.beat = std::max(0., n.beat + a.beat);
                    n.pitch = juce::jlimit(0, 127, n.pitch + a.pitch);
                    if (a.length != 0)
                        n.length = std::max(std::min(n.length, minimumSliceLength), n.length + a.length);
                    n.step = false;
                }
        if (id == "notes.velocity")
        {
            // args.text picks the lane property (default velocity); args.value is the 0..1 lane
            // level (Pan and Fine pitch centred at .5).
            const int target = a.text.isEmpty() ? 0 : noteLaneTargets().indexOf(a.text);
            if (target < 0 || !std::isfinite(a.value) || a.value < 0 || a.value > 1)
                return juce::Result::fail("noteLane.invalidValue: Target velocity/release/pan/modX/modY/fine, "
                                          "level 0..1");
            if (target >= 2)
                for (const auto &n : pat->notes)
                    if (chosen(n.id))
                        if (const auto *owner = next.channel(n.channel); owner && owner->instrument)
                            return juce::Result::fail("noteLane.unsupportedTarget: Pan, Mod X/Y and Fine pitch "
                                                      "are sampler note properties (not plugin instruments)");
            for (auto &n : pat->notes)
                if (chosen(n.id))
                    switch (target)
                    {
                    case 1: n.releaseVelocity = a.value; break;
                    case 2: n.pan = a.value * 2 - 1; break;
                    case 3: n.modX = a.value; break;
                    case 4: n.modY = a.value; break;
                    case 5: n.fineCents = (a.value * 2 - 1) * 100; break;
                    default: n.velocity = float(a.value);
                    }
        }
        if (id == "notes.stretch")
        {
            NoteStretchPlan plan;
            if (auto r = planNoteStretch(next, a.pattern, a.channel, a.ids, a.beat, a.value, plan);
                r.failed())
                return r;
            if (a.value == 1)
                return juce::Result::ok(); // Preserve intentionally short loop bounds on an exact no-op.
            size_t index = 0;
            for (auto &note : pat->notes)
                if (index < plan.notes.size() && note.id == plan.notes[index].id)
                    note = plan.notes[index++];
        }
        if (id == "notes.slice")
        {
            if (!a.slice)
                return juce::Result::fail("Provide a musical Slice line");
            std::vector<NoteSliceCut> cuts;
            if (auto r = planNoteSlices(next, a.pattern, a.channel, a.ids, *a.slice, cuts); r.failed())
                return r;
            std::vector<Note> copies;
            copies.reserve(cuts.size());
            for (const auto &cut : cuts)
            {
                auto &left = pat->notes[cut.index];
                auto right = left;
                right.id = next.allocate();
                right.beat = cut.beat;
                right.length = left.beat + left.length - cut.beat;
                left.length = cut.beat - left.beat;
                left.step = right.step = false;
                copies.push_back(right);
            }
            pat->notes.insert(pat->notes.end(), copies.begin(), copies.end());
        }
        if (id == "notes.script")
        {
            // args.text: the script; ids: the selection it may focus on; length: the snap.
            if (!ch || ch->audioClip)
                return juce::Result::fail("noteScript.invalidTarget: Select a playable Channel");
            std::vector<Note> replacement;
            if (auto ran = note_script::run(next, *pat, ch->id, a.ids, a.length, a.text, replacement); ran.failed())
                return ran;
            std::erase_if(pat->notes, [&](const auto &n) { return n.channel == ch->id; });
            pat->notes.insert(pat->notes.end(), replacement.begin(), replacement.end());
        }
        if (id == "notes.toAutomation")
        {
            // The Channel's controller lane in this Pattern (args.text cc / pitchBend / pressure,
            // CC number args.pitch) becomes an unlinked automation clip (link it from a control's
            // automation menu) at args.beat on Playlist lane args.lane. Values keep their steps.
            const juce::StringArray types{"cc", "pitchBend", "pressure"};
            const int type = types.indexOf(a.text.isEmpty() ? juce::String("cc") : a.text);
            if (!ch || ch->audioClip || type < 0 || (type == 0 && (a.pitch < 0 || a.pitch > 127)) ||
                !std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000 || a.lane < 0 || a.lane > 127)
                return juce::Result::fail("noteAutomation.invalidArguments: Channel, cc/pitchBend/pressure lane, "
                                          "beat >= 0 and Playlist lane 0..127 required");
            std::vector<ControllerEvent> events;
            for (const auto &c : pat->controllers)
                if (c.channel == ch->id && c.type == type && (type != 0 || c.number == a.pitch))
                    events.push_back(c);
            if (events.empty())
                return juce::Result::fail("noteAutomation.empty: This Channel has no events in that lane");
            std::stable_sort(events.begin(), events.end(), [](const auto &x, const auto &y) { return x.beat < y.beat; });
            AutomationSource source;
            source.id = next.allocate();
            source.length = std::max(.25, std::max(pat->length, events.back().beat + .25));
            source.name = (ch->name + (type == 0   ? " CC " + juce::String(a.pitch)
                                       : type == 1 ? juce::String(" Pitch bend")
                                                   : juce::String(" Pressure")))
                              .toStdString();
            for (const auto &event : events)
            {
                const float value = float(juce::jlimit(0., 1., type == 1 ? (event.value + 1) / 2 : event.value));
                if (!source.points.empty() && std::abs(source.points.back().beat - event.beat) < 1.e-9)
                    source.points.back().value = value; // The later event at one beat wins.
                else
                    source.points.push_back({next.allocate(), std::min(event.beat, source.length), value,
                                             source.points.empty() ? AutomationShape::single : AutomationShape::hold, 0});
            }
            // Sources span 0..length: hold the first value from 0 and the last to the end.
            if (source.points.front().beat > 1.e-9)
                source.points.insert(source.points.begin(),
                                     {next.allocate(), 0., source.points.front().value, AutomationShape::single, 0});
            else
                source.points.front().beat = 0;
            source.points[1 < source.points.size() ? 1 : 0].incoming = source.points.size() > 1
                                                                          ? AutomationShape::hold
                                                                          : AutomationShape::single;
            if (source.points.back().beat < source.length - 1.e-9)
                source.points.push_back({next.allocate(), source.length, source.points.back().value, AutomationShape::hold, 0});
            else
                source.points.back().beat = source.length;
            if (source.points.size() > 100000)
                return juce::Result::fail("noteAutomation.limit: Too many events for one clip");
            next.automation.placements.push_back({next.allocate(), source.id, a.beat, source.length, 0, a.lane, false});
            next.automation.sources.push_back(std::move(source));
        }
        if (id == "notes.group" || id == "notes.ungroup")
        {
            if (a.ids.empty() || std::set<Id>(a.ids.begin(), a.ids.end()).size() != a.ids.size())
                return juce::Result::fail("noteGroup.invalidTarget: Select distinct notes first");
            if (id == "notes.group" && a.ids.size() < 2)
                return juce::Result::fail("noteGroup.invalidTarget: Select at least two notes to group");
            size_t found = 0;
            for (const auto &note : pat->notes)
                found += chosen(note.id) ? 1 : 0;
            if (found != a.ids.size())
                return juce::Result::fail("noteGroup.invalidTarget: Every note must exist in the Pattern");
            const Id group = id == "notes.group" ? next.allocate() : 0;
            for (auto &note : pat->notes)
                if (chosen(note.id))
                    note.group = group;
        }
        if (id == "notes.mute" || id == "notes.unmute")
        {
            if (!ch || ch->audioClip)
                return juce::Result::fail("Select a playable Channel for Note mute");
            for (auto &note : pat->notes)
                if (a.ids.empty() ? note.channel == a.channel : chosen(note.id))
                    note.muted = id == "notes.mute";
        }
        if (id == "notes.quantize" || id == "notes.quickQuantize" || id == "notes.quantizeStarts")
        {
            if (id == "notes.quantize" && continuingGesture && !quantizerStart)
                preparedQuantizerStart = std::make_unique<Project>(canonicalSource);
            if (auto result = quantizeNotes(next,
                                            id == "notes.quantize" && continuingGesture && quantizerStart
                                                ? *quantizerStart
                                            : preparedQuantizerStart ? *preparedQuantizerStart
                                                                     : canonicalSource,
                                            a, id);
                result.failed())
                return result;
        }
        if (id.startsWith("notes.controller."))
        {
            // Single controller events of the Pattern: CC (pitch = number, value 0..1), pitchBend
            // (value -1..1), pressure (0..1) or program (value * 127 = program).
            auto valid = [](int type, double value)
            { return std::isfinite(value) && value >= (type == 1 ? -1. : 0.) && value <= 1.; };
            auto apply = [](ControllerEvent &c, double value)
            {
                if (c.type == 3)
                    c.number = juce::jlimit(0, 127, juce::roundToInt(value * 127));
                else
                    c.value = value;
            };
            if (id == "notes.controller.lfo")
            {
                // Replace this Channel's events of one lane (args.text cc / pitchBend / pressure;
                // CC number args.pitch) over the Pattern with an LFO: shape args.target (0 sine,
                // 1 triangle, 2 square, 3 saw), period args.length beats, depth args.value 0..1
                // around centre args.offset, one point per 1/16 beat.
                const juce::StringArray types{"cc", "pitchBend", "pressure"};
                const int type = types.indexOf(a.text.isEmpty() ? juce::String("cc") : a.text);
                if (!ch || type < 0 || (type == 0 && (a.pitch < 0 || a.pitch > 127)) || a.target > 3 ||
                    !std::isfinite(a.length) || a.length < .25 || a.length > 64 || !std::isfinite(a.value) ||
                    a.value <= 0 || a.value > 1 || !valid(type, a.offset))
                    return juce::Result::fail("LFO needs a cc/pitchBend/pressure lane, shape 0..3, period 1/4..64 "
                                              "beats, depth 0..1 and a centre inside the lane range");
                const double span = pat->length > 0 ? pat->length : 4.;
                if (span / .0625 > 8192)
                    return juce::Result::fail("The Pattern is too long for an LFO lane");
                std::erase_if(pat->controllers, [&](const auto &c)
                              { return c.channel == ch->id && c.type == type && (type != 0 || c.number == a.pitch); });
                constexpr double tau = 6.283185307179586;
                const double low = type == 1 ? -1. : 0.;
                for (double beat = 0; beat < span - 1.e-9; beat += .0625)
                {
                    const double phase = std::fmod(beat / a.length, 1.);
                    const double wave = a.target == 0   ? std::sin(tau * phase)
                                        : a.target == 1 ? 1. - 4. * std::abs(phase - .5)
                                        : a.target == 2 ? (phase < .5 ? 1. : -1.)
                                                        : 2. * phase - 1.;
                    ControllerEvent c;
                    c.id = next.allocate();
                    c.channel = ch->id;
                    c.beat = beat;
                    c.type = type;
                    c.number = type == 0 ? a.pitch : 0;
                    apply(c, juce::jlimit(low, 1., a.offset + wave * a.value * (type == 1 ? 1. : .5)));
                    pat->controllers.push_back(c);
                }
                std::stable_sort(pat->controllers.begin(), pat->controllers.end(),
                                 [](const auto &x, const auto &y) { return x.beat < y.beat; });
            }
            else if (id == "notes.controller.add")
            {
                const juce::StringArray types{"cc", "pitchBend", "pressure", "program"};
                const int type = types.indexOf(a.text.isEmpty() ? juce::String("cc") : a.text);
                if (!ch || type < 0 || !std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000 ||
                    !valid(type, a.value) || (type == 0 && (a.pitch < 0 || a.pitch > 127)))
                    return juce::Result::fail("Add a cc/pitchBend/pressure/program event on a Channel, beat >= 0, "
                                              "value 0..1 (pitch bend -1..1), CC number 0..127");
                ControllerEvent c;
                c.id = next.allocate();
                c.channel = ch->id;
                c.beat = a.beat;
                c.type = type;
                c.number = type == 0 ? a.pitch : 0;
                apply(c, a.value);
                pat->controllers.push_back(c);
                std::stable_sort(pat->controllers.begin(), pat->controllers.end(),
                                 [](const auto &x, const auto &y) { return x.beat < y.beat; });
            }
            else if (id == "notes.controller.interpolate")
            {
                // Interpolate tool: inside [beat, beat + length] on one lane of the Channel, the
                // first and last events stay and everything between becomes a straight line of
                // events every 1/16 beat. One Undo step.
                const juce::StringArray types{"cc", "pitchBend", "pressure"};
                const int type = types.indexOf(a.text.isEmpty() ? juce::String("cc") : a.text);
                if (!ch || type < 0 || !std::isfinite(a.beat) || !std::isfinite(a.length) || a.length <= 0 ||
                    (type == 0 && (a.pitch < 0 || a.pitch > 127)))
                    return juce::Result::fail("controller.invalidRange: A cc/pitchBend/pressure lane and a positive range");
                const auto onLane = [&](const ControllerEvent &c)
                {
                    return c.channel == ch->id && c.type == type && (type != 0 || c.number == a.pitch) &&
                           c.beat >= a.beat - 1.e-9 && c.beat <= a.beat + a.length + 1.e-9;
                };
                const ControllerEvent *first = nullptr, *last = nullptr;
                for (const auto &c : pat->controllers)
                    if (onLane(c))
                    {
                        if (!first || c.beat < first->beat)
                            first = &c;
                        if (!last || c.beat > last->beat)
                            last = &c;
                    }
                if (!first || !last || last->beat - first->beat < .0625)
                    return juce::Result::fail("controller.needTwoEvents: The range needs two events at least 1/16 beat apart");
                const auto start = *first, end = *last;
                if ((end.beat - start.beat) / .0625 > 8192)
                    return juce::Result::fail("controller.rangeTooLong: Interpolate at most 512 beats at once");
                std::erase_if(pat->controllers, [&](const auto &c)
                              { return onLane(c) && c.id != start.id && c.id != end.id && c.beat > start.beat &&
                                       c.beat < end.beat; });
                for (double beat = start.beat + .0625; beat < end.beat - 1.e-9; beat += .0625)
                {
                    ControllerEvent c = start;
                    c.id = next.allocate();
                    c.beat = beat;
                    c.value = start.value + (end.value - start.value) * (beat - start.beat) / (end.beat - start.beat);
                    pat->controllers.push_back(c);
                }
                std::stable_sort(pat->controllers.begin(), pat->controllers.end(),
                                 [](const auto &x, const auto &y) { return x.beat < y.beat; });
            }
            else if (id == "notes.controller.set")
            {
                auto found = std::find_if(pat->controllers.begin(), pat->controllers.end(),
                                          [&](const auto &c) { return c.id == a.target; });
                if (found == pat->controllers.end())
                    return missing();
                if (!valid(found->type, a.value))
                    return juce::Result::fail("Controller value is 0..1 (pitch bend -1..1)");
                apply(*found, a.value);
            }
            else
            {
                const auto before = pat->controllers.size();
                std::erase_if(pat->controllers, [&](const auto &c) { return c.id == a.target; });
                if (pat->controllers.size() == before)
                    return missing();
            }
        }
        if (id == "notes.flip")
        {
            // Mirror the selection (or the Channel part) in time within its span, or in pitch
            // around the middle of its range.
            if (a.text != "horizontal" && a.text != "vertical")
                return juce::Result::fail("Flip is horizontal or vertical");
            std::vector<size_t> targets;
            for (size_t i = 0; i < pat->notes.size(); ++i)
                if (a.ids.empty() ? pat->notes[i].channel == a.channel : chosen(pat->notes[i].id))
                    targets.push_back(i);
            if (targets.empty())
                return juce::Result::fail("No notes to flip");
            double start = 1.e18, end = -1.e18;
            int low = 127, high = 0;
            for (auto i : targets)
            {
                const auto &n = pat->notes[i];
                start = std::min(start, n.beat), end = std::max(end, n.beat + n.length);
                low = std::min(low, n.pitch), high = std::max(high, n.pitch);
            }
            for (auto i : targets)
            {
                auto &n = pat->notes[i];
                if (a.text == "horizontal")
                    n.beat = std::max(0., start + end - (n.beat + n.length));
                else
                    n.pitch = juce::jlimit(0, 127, low + high - n.pitch);
            }
        }
        if (id == "notes.insertSpace" || id == "notes.deleteSpace" || id == "notes.trim")
        {
            // The Channel's notes and controller events: [beat, beat + length) is inserted
            // (later events move later; args.enabled slices notes crossing the start) or
            // deleted (events inside are removed, notes crossing it are cut, later ones close up).
            if (!ch || ch->audioClip)
                return juce::Result::fail("Select a playable Channel");
            if (!std::isfinite(a.beat) || !std::isfinite(a.length) || a.beat < 0 || a.length < 1. / 64 ||
                a.beat + a.length > 100000)
                return juce::Result::fail("Select a time range on the Piano roll ruler first (Ctrl+drag)");
            const double start = a.beat, end = a.beat + a.length;
            auto &notes = pat->notes;
            if (id == "notes.trim")
            {
                // Keep only [start, end): notes outside are removed, notes crossing an edge are
                // cut at it, and the Channel's controller events outside are removed. Nothing moves.
                std::erase_if(notes, [&](const auto &n)
                              { return n.channel == a.channel &&
                                       (n.beat >= end - 1.e-9 || n.beat + n.length <= start + 1.e-9); });
                for (auto &n : notes)
                {
                    if (n.channel != a.channel)
                        continue;
                    const double noteEnd = std::min(end, n.beat + n.length);
                    n.beat = std::max(start, n.beat);
                    n.length = std::max(1. / 64, noteEnd - n.beat);
                }
                std::erase_if(pat->controllers, [&](const auto &c)
                              { return c.channel == a.channel && (c.beat < start - 1.e-9 || c.beat >= end - 1.e-9); });
            }
            else if (id == "notes.insertSpace")
            {
                std::vector<Note> tails;
                for (auto &n : notes)
                {
                    if (n.channel != a.channel)
                        continue;
                    if (n.beat >= start - 1.e-9)
                        n.beat += a.length;
                    else if (a.enabled && n.beat + n.length > start + 1.e-9)
                    {
                        auto tail = n;
                        tail.id = next.allocate();
                        tail.beat = end;
                        tail.length = n.beat + n.length - start;
                        n.length = start - n.beat;
                        tails.push_back(tail);
                    }
                }
                notes.insert(notes.end(), tails.begin(), tails.end());
                for (auto &c : pat->controllers)
                    if (c.channel == a.channel && c.beat >= start - 1.e-9)
                        c.beat += a.length;
            }
            else
            {
                std::erase_if(notes, [&](const auto &n)
                              { return n.channel == a.channel && n.beat >= start - 1.e-9 && n.beat < end - 1.e-9; });
                for (auto &n : notes)
                {
                    if (n.channel != a.channel)
                        continue;
                    if (n.beat < start && n.beat + n.length > start)
                        n.length = std::max(1. / 64, start - n.beat + std::max(0., n.beat + n.length - end));
                    else if (n.beat >= end - 1.e-9)
                        n.beat -= a.length;
                }
                std::erase_if(pat->controllers, [&](const auto &c)
                              { return c.channel == a.channel && c.beat >= start - 1.e-9 && c.beat < end - 1.e-9; });
                for (auto &c : pat->controllers)
                    if (c.channel == a.channel && c.beat >= end - 1.e-9)
                        c.beat -= a.length;
            }
        }
        if (id == "notes.articulate" || id == "notes.scaleLevels")
        {
            std::vector<size_t> targets;
            for (size_t i = 0; i < pat->notes.size(); ++i)
                if (a.ids.empty() ? pat->notes[i].channel == a.channel : chosen(pat->notes[i].id))
                    targets.push_back(i);
            if (targets.empty())
                return juce::Result::fail("No notes to edit");
            if (id == "notes.articulate")
            {
                // Each note lasts args.value (0.05..1) of the time to the next later note start
                // of the same Channel; the last keeps its gap to the Pattern end (at least a step).
                if (!std::isfinite(a.value) || a.value < .05 || a.value > 1)
                    return juce::Result::fail("Articulation is 5..100% of the gap");
                std::vector<double> starts;
                for (auto i : targets)
                    starts.push_back(pat->notes[i].beat);
                std::sort(starts.begin(), starts.end());
                const double end = std::max(pat->length, starts.back() + .25);
                for (auto i : targets)
                {
                    auto &n = pat->notes[i];
                    const auto after = std::upper_bound(starts.begin(), starts.end(), n.beat + 1.e-9);
                    const double gap = (after == starts.end() ? end : *after) - n.beat;
                    n.length = std::max(1. / 64, gap * a.value);
                }
            }
            else
            {
                // velocity' = velocity * args.value + args.offset, inverted first when args.enabled.
                if (!std::isfinite(a.value) || a.value < 0 || a.value > 4 || !std::isfinite(a.offset) ||
                    std::abs(a.offset) > 1)
                    return juce::Result::fail("Scale is 0..4 and offset -1..1");
                for (auto i : targets)
                {
                    auto &n = pat->notes[i];
                    const double base = a.enabled ? 1. - n.velocity : n.velocity;
                    n.velocity = juce::jlimit(.01, 1., base * a.value + a.offset);
                }
            }
        }
        if (id == "notes.rotate" || id == "notes.discardLengths" || id == "notes.limit" || id == "notes.flam")
        {
            std::vector<size_t> targets;
            for (size_t i = 0; i < pat->notes.size(); ++i)
                if (a.ids.empty() ? pat->notes[i].channel == a.channel : chosen(pat->notes[i].id))
                    targets.push_back(i);
            if (targets.empty())
                return juce::Result::fail("No notes to edit");
            if (id == "notes.rotate")
            {
                // Move by args.beat; notes leaving the span of whole bars they occupy wrap around.
                const double bar = next.numerator * 4. / std::max(1, next.denominator);
                if (!std::isfinite(a.beat) || std::abs(a.beat) < 1.e-9 || std::abs(a.beat) > 64)
                    return juce::Result::fail("Rotate by a non-zero step of at most 64 beats");
                double start = 1.e18, end = -1.e18;
                for (auto i : targets)
                    start = std::min(start, pat->notes[i].beat), end = std::max(end, pat->notes[i].beat);
                const double from = std::floor(start / bar + 1.e-9) * bar;
                const double span = std::max(bar, std::ceil((end + 1.e-9) / bar) * bar - from);
                for (auto i : targets)
                {
                    auto &n = pat->notes[i];
                    double rel = std::fmod(n.beat - from + a.beat, span);
                    if (rel < 0)
                        rel += span;
                    n.beat = from + rel;
                }
            }
            else if (id == "notes.discardLengths")
            {
                if (!std::isfinite(a.length) || a.length < 1. / 64 || a.length > 64)
                    return juce::Result::fail("Note length is 1/64..64 beats");
                for (auto i : targets)
                    pat->notes[i].length = a.length;
            }
            else if (id == "notes.limit")
            {
                // Fold notes by octaves into [args.pitch, args.value].
                const int low = a.pitch, high = int(a.value);
                if (low < 0 || high > 127 || high - low < 11)
                    return juce::Result::fail("The range must span at least an octave within 0..127");
                for (auto i : targets)
                {
                    auto &n = pat->notes[i];
                    while (n.pitch < low)
                        n.pitch += 12;
                    while (n.pitch > high)
                        n.pitch -= 12;
                }
            }
            else
            {
                // Flam: a softer grace note args.length beats before each note.
                if (!std::isfinite(a.length) || a.length < 1. / 128 || a.length > 1)
                    return juce::Result::fail("Flam offset is 1/128..1 beat");
                if (!std::isfinite(a.value) || a.value <= 0 || a.value > 1)
                    return juce::Result::fail("Flam velocity is 0..1 of the main note");
                if (pat->notes.size() + targets.size() > 100000)
                    return juce::Result::fail("The Pattern would exceed 100000 notes");
                std::vector<Note> graces;
                for (auto i : targets)
                {
                    auto grace = pat->notes[i];
                    grace.id = next.allocate();
                    grace.beat = std::max(0., grace.beat - a.length);
                    grace.length = std::min(grace.length, a.length);
                    grace.velocity = juce::jlimit(.01, 1., grace.velocity * a.value);
                    graces.push_back(grace);
                }
                pat->notes.insert(pat->notes.end(), graces.begin(), graces.end());
            }
        }
        if (id == "notes.claw")
        {
            // Cut each note (selection or Channel part) into args.length steps and keep only the
            // steps whose index matches the rhythm in args.text ('x' keep, '.' drop, repeating).
            std::vector<size_t> targets;
            for (size_t i = 0; i < pat->notes.size(); ++i)
                if (a.ids.empty() ? pat->notes[i].channel == a.channel : chosen(pat->notes[i].id))
                    targets.push_back(i);
            if (targets.empty())
                return juce::Result::fail("No notes to claw");
            if (!std::isfinite(a.length) || a.length < 1. / 32 || a.length > 4)
                return juce::Result::fail("Claw step is 1/32..4 beats");
            const auto rhythm = a.text.isEmpty() ? juce::String("x.") : a.text;
            if (rhythm.length() > 64 || !rhythm.containsOnly("x.") || !rhythm.containsChar('x'))
                return juce::Result::fail("Claw rhythm uses x (keep) and . (drop), at most 64 steps");
            std::vector<Note> pieces;
            std::set<Id> removed;
            for (auto i : targets)
            {
                const auto source = pat->notes[i];
                removed.insert(source.id);
                int step = 0;
                for (double start = source.beat; start < source.beat + source.length - 1.e-9; start += a.length, ++step)
                    if (rhythm[step % rhythm.length()] == 'x')
                    {
                        auto piece = source;
                        piece.id = next.allocate();
                        piece.beat = start;
                        piece.length = std::min(a.length, source.beat + source.length - start);
                        pieces.push_back(piece);
                    }
                if (pieces.size() > 100000)
                    return juce::Result::fail("Claw would make too many notes");
            }
            std::erase_if(pat->notes, [&](const auto &n) { return removed.contains(n.id); });
            pat->notes.insert(pat->notes.end(), pieces.begin(), pieces.end());
        }
        if (id == "notes.riff")
        {
            // Replace the Channel part with a riff over the Pattern: a seeded rhythm on an
            // args.length grid (density args.value) and a melody that moves mostly by scale steps
            // (args.text scale, args.pitch root), returning to the root at each bar start.
            if (!ch || ch->audioClip)
                return juce::Result::fail("Select a playable Channel");
            const auto &scales = score::scaleShapes();
            const auto shape = std::find_if(scales.begin(), scales.end(), [&](const auto &s2) { return a.text == s2.name; });
            if (shape == scales.end())
                return juce::Result::fail("Unknown scale: " + a.text);
            if (!std::isfinite(a.length) || a.length < 1. / 8 || a.length > 2 || !std::isfinite(a.value) || a.value <= 0 ||
                a.value > 1 || a.pitch < 12 || a.pitch > 108)
                return juce::Result::fail("Riff step is 1/8..2 beats, density 0..1, root 12..108");
            const double span = pat->length > 0 ? pat->length : 4.;
            const double bar = next.numerator * 4. / std::max(1, next.denominator);
            std::vector<int> ladder; // Scale degrees across two octaves around the root.
            for (int octave = -1; octave <= 1; ++octave)
                for (int interval : shape->intervals)
                    ladder.push_back(a.pitch + octave * 12 + interval);
            std::sort(ladder.begin(), ladder.end());
            const int rootIndex = int(std::find(ladder.begin(), ladder.end(), a.pitch) - ladder.begin());
            std::erase_if(pat->notes, [&](const auto &n) { return n.channel == a.channel; });
            juce::Random random(juce::int64(a.seed.value_or(1)));
            // One bar of rhythm, repeated (riffs repeat); the first step always plays.
            std::vector<bool> rhythm;
            for (double beat = 0; beat < bar - 1.e-9; beat += a.length)
                rhythm.push_back(rhythm.empty() || random.nextFloat() < float(a.value));
            int index = rootIndex;
            int step = 0;
            for (double beat = 0; beat < span - 1.e-9; beat += a.length, ++step)
            {
                const auto inBar = size_t(step) % rhythm.size();
                if (inBar == 0)
                    index = rootIndex;
                if (!rhythm[inBar])
                    continue;
                if (inBar != 0)
                {
                    const float r = random.nextFloat();
                    const int move = r < .35f ? 1 : r < .7f ? -1 : r < .85f ? 2 : r < .95f ? -2 : 0;
                    index = juce::jlimit(0, int(ladder.size()) - 1, index + move);
                }
                Note n;
                n.id = next.allocate();
                n.channel = a.channel;
                n.beat = beat;
                size_t following = inBar + 1;
                while (following < rhythm.size() && !rhythm[following])
                    ++following;
                n.length = std::max(a.length * .5, a.length * double(following - inBar) * .9);
                n.pitch = juce::jlimit(0, 127, ladder[size_t(index)]);
                n.velocity = inBar == 0 ? .9 : .65 + .2 * random.nextFloat();
                pat->notes.push_back(n);
            }
        }
        if (id == "notes.randomize")
        {
            // Replace the Channel part with random notes over the Pattern: args.text scale,
            // args.pitch root (lowest note), args.length grid step, args.value density 0..1,
            // args.seed. Notes stay inside one octave above the root plus its top note.
            if (!ch || ch->audioClip)
                return juce::Result::fail("Select a playable Channel");
            const auto &scales = score::scaleShapes();
            const auto shape = std::find_if(scales.begin(), scales.end(),
                                            [&](const auto &s2) { return a.text == s2.name; });
            if (shape == scales.end())
                return juce::Result::fail("Unknown scale: " + a.text);
            if (!std::isfinite(a.length) || a.length < 1. / 16 || a.length > 4)
                return juce::Result::fail("Randomize step is 1/16..4 beats");
            if (!std::isfinite(a.value) || a.value <= 0 || a.value > 1)
                return juce::Result::fail("Randomize density is 0..1");
            if (a.pitch < 0 || a.pitch > 115)
                return juce::Result::fail("Randomize root is 0..115");
            const double span = std::max(a.length, pat->length > 0 ? pat->length : 4.);
            if (span / a.length > 4096)
                return juce::Result::fail("Too many steps to randomize");
            std::vector<int> pitches;
            for (int octave = 0; octave < 2; ++octave)
                for (int interval : shape->intervals)
                    if (octave == 0 || interval == 0)
                        pitches.push_back(a.pitch + octave * 12 + interval);
            std::erase_if(pat->notes, [&](const auto &n) { return n.channel == a.channel; });
            juce::Random random(juce::int64(a.seed.value_or(1)));
            for (double beat = 0; beat < span - 1.e-9; beat += a.length)
                if (random.nextFloat() < float(a.value))
                {
                    Note n;
                    n.id = next.allocate();
                    n.channel = a.channel;
                    n.beat = beat;
                    n.length = a.length * (random.nextFloat() < .25f ? 2 : 1);
                    n.pitch = pitches[size_t(random.nextInt(int(pitches.size())))];
                    n.velocity = .55 + .4 * random.nextFloat();
                    pat->notes.push_back(n);
                }
        }
        if (id == "notes.paste")
        {
            // Pasted notes keep their spacing; the earliest lands at args.beat, on this Channel.
            if (!ch || ch->audioClip)
                return juce::Result::fail("Select a playable Channel to paste notes into");
            auto board = a.noteClipboard;
            if (!board || board->notes.empty())
            {
                auto copied = std::make_shared<NoteClipboard>();
                copied->origin = 1.e18;
                for (const auto &n : pat->notes)
                    if (chosen(n.id))
                        copied->notes.push_back(n), copied->origin = std::min(copied->origin, n.beat);
                board = copied;
            }
            if (!board || board->notes.empty())
                return juce::Result::fail("Copy or cut notes first (Ctrl+C / Ctrl+X)");
            if (!std::isfinite(a.beat) || a.beat < 0 || a.beat > 100000)
                return juce::Result::fail("Paste position is 0..100000 beats");
            if (pat->notes.size() + board->notes.size() > 100000)
                return juce::Result::fail("The Pattern would exceed 100000 notes");
            const double shift = a.beat - board->origin;
            std::map<Id, Id> regrouped;
            for (auto copy : board->notes)
            {
                if (copy.group != 0)
                {
                    auto [slot, fresh] = regrouped.try_emplace(copy.group, 0);
                    if (fresh)
                        slot->second = next.allocate();
                    copy.group = slot->second;
                }
                copy.id = next.allocate();
                copy.channel = a.channel;
                copy.beat = std::max(0., copy.beat + shift);
                pat->notes.push_back(copy);
            }
        }
        if (id == "notes.clearControllers")
        {
            const auto before = pat->controllers.size();
            std::erase_if(pat->controllers, [&](const auto &c) { return c.channel == a.channel; });
            if (pat->controllers.size() == before)
                return juce::Result::fail("This Channel has no recorded controllers in the Pattern");
        }
        if (id == "notes.articulation")
        {
            // value: 1-based articulation of each note's Channel expression map; 0 removes it.
            const int articulation = int(a.value);
            if (!std::isfinite(a.value) || a.value != articulation || articulation < 0 ||
                articulation > maximumArticulations)
                return juce::Result::fail("expressionMap.invalidArticulation: 0 (none) or an entry number");
            int changed = 0;
            for (auto &n : pat->notes)
                if (a.ids.empty() ? n.channel == a.channel : chosen(n.id))
                {
                    const auto *owner = next.channel(n.channel);
                    if (articulation > 0 && (!owner || articulation > int(owner->articulations.size())))
                        return juce::Result::fail("expressionMap.unmapped: The note's Channel has no articulation " +
                                                  juce::String(articulation));
                    changed += n.articulation != articulation;
                    n.articulation = articulation;
                }
            if (changed == 0)
                return juce::Result::fail("expressionMap.noChange: No note changed");
        }
        if (id == "notes.followChords")
        {
            // Song position of the Pattern: its first Playlist placement (beat 0 without one).
            if (next.chords.empty())
                return juce::Result::fail("chord.empty: Write chords on the Chord Track first");
            if (a.ids.empty() && (!ch || ch->audioClip))
                return juce::Result::fail("Select a playable Channel for score tools");
            std::vector<size_t> targets;
            for (size_t i = 0; i < pat->notes.size(); ++i)
                if (a.ids.empty() ? pat->notes[i].channel == a.channel : chosen(pat->notes[i].id))
                    targets.push_back(i);
            double offset = -1;
            for (const auto &placement : next.placements)
                if (placement.pattern == pat->id && (offset < 0 || placement.beat < offset))
                    offset = placement.beat;
            if (score::followChords(*pat, targets, next.chords, std::max(0., offset)) == 0)
                return juce::Result::fail("chord.noChange: The notes already follow the Chord Track");
        }
        if (id == "notes.chordify" || id == "notes.arpeggiate" || id == "notes.strum" ||
            id == "notes.humanize" || id == "notes.scaleSnap" || id == "notes.maqam")
        {
            if (a.ids.empty() && (!ch || ch->audioClip))
                return juce::Result::fail("Select a playable Channel for score tools");
            std::vector<size_t> targets;
            for (size_t i = 0; i < pat->notes.size(); ++i)
                if (a.ids.empty() ? pat->notes[i].channel == a.channel : chosen(pat->notes[i].id))
                    targets.push_back(i);
            const auto seed = a.seed.value_or(1);
            const auto result =
                id == "notes.chordify"     ? score::chordify(next, *pat, targets, a.text, int(a.value))
                : id == "notes.arpeggiate" ? score::arpeggiate(next, *pat, targets, a.text, a.length,
                                                                a.value > 0 ? a.value : 1., seed)
                : id == "notes.strum"      ? score::strum(*pat, targets, a.text, a.length, a.value)
                : id == "notes.humanize"   ? score::humanize(*pat, targets, a.beat, a.value, a.offset, seed)
                : id == "notes.maqam"      ? score::maqamSnap(*pat, targets, a.text, ((a.pitch % 12) + 12) % 12)
                                           : score::scaleSnap(*pat, targets, a.text, ((a.pitch % 12) + 12) % 12);
            if (result.failed())
                return result;
        }
        if (id == "notes.legato" || id == "notes.chop" || id == "notes.glue")
        {
            // An explicit selection may include editable ghost Channels. Without
            // a selection, operate on the current Channel part, never the Rack.
            if (a.ids.empty() && (!ch || ch->audioClip))
                return juce::Result::fail("Select a playable Channel for score tools");
            std::vector<size_t> targets;
            for (size_t i = 0; i < pat->notes.size(); ++i)
                if (a.ids.empty() ? pat->notes[i].channel == a.channel : chosen(pat->notes[i].id))
                    targets.push_back(i);
            if (id == "notes.legato")
            {
                std::sort(targets.begin(), targets.end(),
                          [&](size_t x, size_t y)
                          {
                              const auto &left = pat->notes[x];
                              const auto &right = pat->notes[y];
                              if (left.channel != right.channel)
                                  return left.channel < right.channel;
                              return left.beat < right.beat;
                          });
                // Simultaneous chord notes share the next strictly later onset.
                // Final onset groups retain their lengths; no invented tail.
                for (size_t i = 0; i < targets.size();)
                {
                    const auto &first = pat->notes[targets[i]];
                    size_t end = i + 1;
                    while (end < targets.size() && pat->notes[targets[end]].channel == first.channel &&
                           pat->notes[targets[end]].beat == first.beat)
                        ++end;
                    if (end < targets.size() && pat->notes[targets[end]].channel == first.channel)
                        for (size_t j = i; j < end; ++j)
                        {
                            auto &note = pat->notes[targets[j]];
                            note.length = pat->notes[targets[end]].beat - note.beat;
                            note.step = false;
                        }
                    i = end;
                }
            }
            else if (id == "notes.glue")
            {
                // Stable interval union per Channel/pitch. Only selected notes
                // participate; this preserves gaps and independent Channel parts.
                std::sort(targets.begin(), targets.end(),
                          [&](size_t x, size_t y)
                          {
                              const auto &left = pat->notes[x];
                              const auto &right = pat->notes[y];
                              if (left.channel != right.channel)
                                  return left.channel < right.channel;
                              if (left.pitch != right.pitch)
                                  return left.pitch < right.pitch;
                              if (left.muted != right.muted)
                                  return left.muted < right.muted;
                              if (left.pan != right.pan)
                                  return left.pan < right.pan;
                              if (left.fineCents != right.fineCents)
                                  return left.fineCents < right.fineCents;
                              if (left.modX != right.modX)
                                  return left.modX < right.modX;
                              if (left.modY != right.modY)
                                  return left.modY < right.modY;
                              if (left.slide != right.slide)
                                  return left.slide < right.slide;
                              if (left.portamento != right.portamento)
                                  return left.portamento < right.portamento;
                              if (left.colourGroup != right.colourGroup)
                                  return left.colourGroup < right.colourGroup;
                              if (left.releaseVelocity != right.releaseVelocity)
                                  return left.releaseVelocity < right.releaseVelocity;
                              if (left.repeatMode != right.repeatMode)
                                  return left.repeatMode < right.repeatMode;
                              if (left.shiftPercent != right.shiftPercent)
                                  return left.shiftPercent < right.shiftPercent;
                              if (left.beat != right.beat)
                                  return left.beat < right.beat;
                              return left.id < right.id;
                          });
                std::set<Id> removed;
                std::optional<size_t> survivor;
                for (auto index : targets)
                {
                    auto &note = pat->notes[index];
                    if (survivor)
                    {
                        auto &left = pat->notes[*survivor];
                        const auto end = left.beat + left.length;
                        // Absorb only double round-off, never a musical tick or
                        // snap cell. Decimal quick chops remain joinable.
                        const auto tolerance = 8 * std::numeric_limits<double>::epsilon() *
                                               std::max({1., std::abs(end), std::abs(note.beat)});
                        if (left.channel == note.channel && left.pitch == note.pitch &&
                            left.muted == note.muted && left.pan == note.pan &&
                            left.fineCents == note.fineCents && left.shiftPercent == note.shiftPercent &&
                            left.repeatMode == note.repeatMode &&
                            left.releaseVelocity == note.releaseVelocity && left.modX == note.modX &&
                            left.modY == note.modY && left.slide == note.slide &&
                            left.portamento == note.portamento && left.colourGroup == note.colourGroup &&
                            note.beat <= end + tolerance)
                        {
                            left.length = std::max(end, note.beat + note.length) - left.beat;
                            left.step = false;
                            removed.insert(note.id);
                            continue;
                        }
                    }
                    survivor = index;
                }
                std::erase_if(pat->notes, [&](const auto &note) { return removed.contains(note.id); });
            }
            else
            {
                if (!std::isfinite(a.length) || a.length < .03125 || a.length > 64)
                    return juce::Result::fail(
                        "Chop interval must be 1/128 to 16 whole notes (0.03125..64 beats)");
                size_t added = 0;
                for (auto i : targets)
                {
                    const double pieces = std::max(1., std::ceil(pat->notes[i].length / a.length));
                    if (pieces > 4097 || added + size_t(pieces - 1) > 4096)
                        return juce::Result::fail("Quick chop would add more than 4096 notes; use a coarser "
                                                  "snap or smaller selection");
                    added += size_t(pieces - 1);
                }
                if (pat->notes.size() + added > 100000)
                    return juce::Result::fail("Quick chop exceeds the Pattern note limit");
                std::vector<Note> copies;
                copies.reserve(added);
                for (auto i : targets)
                {
                    const auto original = pat->notes[i];
                    const auto count = size_t(std::max(1., std::ceil(original.length / a.length)));
                    if (count == 1)
                        continue;
                    pat->notes[i].length = a.length;
                    pat->notes[i].step = false;
                    for (size_t part = 1; part < count; ++part)
                    {
                        auto note = original;
                        note.id = next.allocate();
                        note.beat = original.beat + double(part) * a.length;
                        note.length = std::min(a.length, original.length - double(part) * a.length);
                        note.step = false;
                        copies.push_back(note);
                    }
                }
                pat->notes.insert(pat->notes.end(), copies.begin(), copies.end());
            }
        }
        if (id == "notes.duplicate")
        {
            std::vector<Note> copies;
            std::map<Id, Id> regrouped;
            for (auto &n : pat->notes)
                if (chosen(n.id))
                {
                    auto copy = n;
                    if (copy.group != 0)
                    {
                        auto [slot, fresh] = regrouped.try_emplace(copy.group, 0);
                        if (fresh)
                            slot->second = next.allocate();
                        copy.group = slot->second;
                    }
                    copy.id = next.allocate();
                    copy.beat += a.beat;
                    copies.push_back(copy);
                }
            pat->notes.insert(pat->notes.end(), copies.begin(), copies.end());
        }
        refreshAutomaticPatternLength(next, *pat);
    }
    else if (id == "audio.add")
    {
        Id source = a.channel;
        if (source == 0)
        {
            if (!juce::File::isAbsolutePath(a.text) || a.offset != 0)
                return juce::Result::fail("Audio source needs an absolute path and full duration");
            Channel channel;
            channel.id = next.allocate();
            channel.name = sourceName(a.text);
            channel.source = a.text;
            channel.audioClip = true;
            channel.group = a.target;
            channel.audioDuration = a.length;
            channel.mixer = a.mixer;
            source = channel.id;
            next.channels.push_back(std::move(channel));
        }
        if (!next.channel(source) || !next.channel(source)->audioClip)
            return juce::Result::fail("Select an Audio Clip Channel");
        next.audioPlacements.push_back({next.allocate(), source, a.lane, a.beat, a.offset, a.length});
    }
    else if (id == "audio.takes")
    {
        // One recording, several windows (loop passes): one source Channel, one clip per
        // take on its own lane, grouped so the takes move and slice together.
        // Several input tracks recorded together: one source Channel and take group each.
        const auto files = a.takeFiles.empty() ? std::vector<juce::String>{a.text} : a.takeFiles;
        if (!juce::File::isAbsolutePath(a.text) || !(a.length > 0) || a.audioTakes.empty() ||
            a.audioTakes.size() > 64 || files.size() > 16 || files.front() != a.text)
            return juce::Result::fail("Recorded takes need an absolute file, its duration and 1..64 windows");
        for (const auto &file : files)
            if (!juce::File::isAbsolutePath(file))
                return juce::Result::fail("Recorded take files must be absolute paths");
        // A Mixer insert target feeds file k into insert mixer + k (Master stays Master).
        auto insertFor = [&](size_t index) { return a.mixer > 0 ? a.mixer + int(index) : 0; };
        for (size_t index = 0; index < files.size(); ++index)
            if (insertFor(index) > 0 && std::none_of(next.mixer.begin(), next.mixer.end(), [&](const auto &insert)
                                                     { return insert.index == insertFor(index); }))
                return juce::Result::fail("Mixer insert " + juce::String(insertFor(index)) + " does not exist");
        for (const auto &take : a.audioTakes)
            if (!(take.duration > 0) || take.offset < 0 || take.offset + take.duration > a.length + 1.e-6 ||
                !(take.beat >= 0) || take.lane < 0 || take.lane > 127 || take.channel >= files.size())
                return juce::Result::fail("Take window outside the recorded source or Playlist");
        for (size_t index = 0; index < files.size(); ++index)
        {
            Channel channel;
            channel.id = next.allocate();
            channel.name = sourceName(files[index]);
            channel.source = files[index];
            channel.audioClip = true;
            channel.audioDuration = a.length;
            channel.mixer = insertFor(index);
            const auto source = channel.id;
            next.channels.push_back(std::move(channel));
            ClipGroup group;
            for (const auto &take : a.audioTakes)
            {
                if (take.channel != index)
                    continue;
                AudioPlacement clip{next.allocate(), source, take.lane, take.beat, take.offset, take.duration};
                clip.muted = take.muted;
                group.members.push_back(clip.id);
                next.audioPlacements.push_back(clip);
            }
            if (group.members.size() > 1)
            {
                group.id = next.allocate();
                next.clipGroups.push_back(std::move(group));
            }
        }
    }
    else if (id == "audio.flattenComp")
    {
        // Finish comping: delete the group's muted audio takes and, when the playing pieces
        // do not overlap, move them onto the group's top track. Other clips are untouched.
        auto group = std::find_if(next.clipGroups.begin(), next.clipGroups.end(), [&](const auto &g)
                                  { return std::find(g.members.begin(), g.members.end(), a.target) != g.members.end(); });
        if (group == next.clipGroups.end())
            return juce::Result::fail("Choose a recorded take (a grouped Audio Clip)");
        std::set<Id> members(group->members.begin(), group->members.end());
        std::vector<AudioPlacement *> kept;
        int topLane = 128;
        for (auto &clip : next.audioPlacements)
            if (members.count(clip.id))
            {
                topLane = std::min(topLane, clip.lane);
                if (!clip.muted)
                    kept.push_back(&clip);
            }
        if (topLane == 128)
            return juce::Result::fail("That group has no Audio Clip takes");
        if (kept.empty())
            return juce::Result::fail("Every take is muted: choose a take to keep first");
        std::sort(kept.begin(), kept.end(), [](auto *x, auto *y) { return x->beat < y->beat; });
        bool overlap = false; // Comp crossfades (10 ms) are expected; longer overlaps stay on their tracks.
        for (size_t i = 1; i < kept.size(); ++i)
            overlap = overlap || next.songSeconds(next.audioEndBeat(kept[i - 1]->beat, kept[i - 1]->duration)) -
                                         next.songSeconds(kept[i]->beat) > .1;
        if (!overlap)
            for (auto *clip : kept)
                clip->lane = topLane;
        std::erase_if(next.audioPlacements, [&](const auto &clip) { return members.count(clip.id) && clip.muted; });
    }
    else if (id == "audio.comp")
    {
        // Comping: the chosen take plays over its own span; every other audio take of
        // its group is cut at the span edges and muted inside it. Outside is untouched.
        auto found = std::find_if(next.audioPlacements.begin(), next.audioPlacements.end(),
                                  [&](const auto &clip) { return clip.id == a.target; });
        if (found == next.audioPlacements.end())
            return missing();
        auto target = a.target;
        if (a.compRange)
        {
            // Swipe comp: cut the chosen take at the range edges first; the inner piece is
            // the one that plays, the outer pieces keep their mute state and group.
            const double clipEnd = next.audioEndBeat(found->beat, found->duration);
            const double r0 = std::max((*a.compRange)[0], found->beat), r1 = std::min((*a.compRange)[1], clipEnd);
            if (!(r1 - r0 > 1.e-6))
                return juce::Result::fail("The comp range must overlap the chosen take");
            auto inner = *found;
            std::vector<AudioPlacement> outer;
            if (r0 > found->beat + 1.e-9)
            {
                auto left = *found;
                left.duration = next.songSeconds(r0) - next.songSeconds(found->beat);
                left.fadeOut = 0;
                outer.push_back(left);
                inner.id = next.allocate();
                inner.beat = r0;
                inner.offset += left.duration;
                inner.duration -= left.duration;
                inner.fadeIn = 0;
            }
            if (r1 < clipEnd - 1.e-9)
            {
                auto right = inner;
                right.id = next.allocate();
                const double kept = next.songSeconds(r1) - next.songSeconds(inner.beat);
                right.beat = r1;
                right.offset += kept;
                right.duration -= kept;
                right.fadeIn = 0;
                inner.duration = kept;
                inner.fadeOut = 0;
                outer.push_back(right);
            }
            if (!outer.empty())
            {
                for (auto &piece : outer)
                    piece.muted = found->muted;
                if (inner.id == found->id)
                    *found = inner;
                else
                    *found = outer.front();
                std::vector<Id> added;
                for (const auto &piece : outer)
                    if (piece.id != found->id)
                    {
                        added.push_back(piece.id);
                        next.audioPlacements.push_back(piece);
                    }
                if (inner.id != a.target)
                {
                    added.push_back(inner.id);
                    next.audioPlacements.push_back(inner);
                }
                for (auto &group : next.clipGroups)
                    if (std::find(group.members.begin(), group.members.end(), a.target) != group.members.end())
                        group.members.insert(group.members.end(), added.begin(), added.end());
                target = inner.id;
                found = std::find_if(next.audioPlacements.begin(), next.audioPlacements.end(),
                                     [&](const auto &clip) { return clip.id == target; });
            }
        }
        const double from = found->beat, to = next.audioEndBeat(found->beat, found->duration);
        std::vector<Id> takes;
        for (const auto &group : next.clipGroups)
            if (std::find(group.members.begin(), group.members.end(), target) != group.members.end())
                for (auto member : group.members)
                    if (member != target &&
                        std::any_of(next.audioPlacements.begin(), next.audioPlacements.end(),
                                    [&](const auto &clip) { return clip.id == member; }) &&
                        std::find(takes.begin(), takes.end(), member) == takes.end())
                        takes.push_back(member);
        if (takes.empty())
            return juce::Result::fail("Comp needs a clip from a take group (record audio with Loop on, "
                                      "or group overlapping audio clips)");
        constexpr double epsilon = 1.e-9;
        std::vector<AudioPlacement> pieces;
        std::vector<std::pair<Id, Id>> derived; // New piece -> the take it was cut from.
        for (auto &clip : next.audioPlacements)
        {
            if (std::find(takes.begin(), takes.end(), clip.id) == takes.end())
                continue;
            const double end = next.audioEndBeat(clip.beat, clip.duration);
            if (end <= from + epsilon || clip.beat >= to - epsilon)
                continue;
            const auto root = clip.id;
            std::vector<AudioPlacement> parts{clip};
            for (double cut : {from, to})
            {
                auto &last = parts.back();
                const double lastEnd = next.audioEndBeat(last.beat, last.duration);
                if (cut > last.beat + epsilon && cut < lastEnd - epsilon)
                {
                    auto right = last;
                    const double left = next.songSeconds(cut) - next.songSeconds(last.beat);
                    right.id = next.allocate();
                    right.beat = cut;
                    right.offset += left;
                    right.duration -= left;
                    right.fadeIn = 0;
                    last.duration = left;
                    last.fadeOut = 0;
                    parts.push_back(right);
                }
            }
            for (auto &part : parts)
            {
                const double partEnd = next.audioEndBeat(part.beat, part.duration);
                if (part.beat >= from - epsilon && partEnd <= to + epsilon)
                    part.muted = true;
            }
            clip = parts.front();
            for (size_t i = 1; i < parts.size(); ++i)
            {
                pieces.push_back(parts[i]);
                derived.push_back({parts[i].id, root});
            }
        }
        for (auto &clip : next.audioPlacements)
            if (clip.id == target)
                clip.muted = false;
        next.audioPlacements.insert(next.audioPlacements.end(), pieces.begin(), pieces.end());
        // Equal-length crossfades at both comp edges: the chosen take starts half a fade
        // early and the take that plays next to it ends/starts half a fade into it, both
        // faded over the overlap, when their source windows allow it.
        constexpr double crossfade = .01;
        const double half = crossfade / 2;
        auto earlier = [&](double beat) { return next.songBeat(next.songSeconds(beat) - half); };
        auto sourceLength = [&](const AudioPlacement &clip)
        {
            const auto *channel = next.channel(clip.channel);
            return channel ? channel->audioDuration : 0.;
        };
        auto *chosen = &*std::find_if(next.audioPlacements.begin(), next.audioPlacements.end(),
                                      [&](const auto &clip) { return clip.id == target; });
        for (int edge = 0; edge < 2; ++edge)
        {
            const double at = edge == 0 ? from : to;
            for (auto &other : next.audioPlacements)
            {
                if (other.muted || other.id == target ||
                    (std::find(takes.begin(), takes.end(), other.id) == takes.end() &&
                     std::none_of(derived.begin(), derived.end(),
                                  [&](const auto &piece) { return piece.first == other.id; })))
                    continue;
                const double otherEnd = next.audioEndBeat(other.beat, other.duration);
                const bool before = edge == 0 && std::abs(otherEnd - at) < epsilon;
                const bool after = edge == 1 && std::abs(other.beat - at) < epsilon;
                if (!before && !after)
                    continue;
                if (before && chosen->offset >= half && other.offset + other.duration + half <= sourceLength(other) &&
                    chosen->duration > crossfade * 2 && other.duration > crossfade)
                {
                    other.duration += half;
                    other.fadeOut = crossfade;
                    chosen->beat = earlier(chosen->beat);
                    chosen->offset -= half;
                    chosen->duration += half;
                    chosen->fadeIn = crossfade;
                }
                if (after && chosen->offset + chosen->duration + half <= sourceLength(*chosen) && other.offset >= half &&
                    chosen->duration > crossfade * 2 && other.duration > crossfade)
                {
                    chosen->duration += half;
                    chosen->fadeOut = crossfade;
                    other.beat = earlier(other.beat);
                    other.offset -= half;
                    other.duration += half;
                    other.fadeIn = crossfade;
                }
            }
        }
        for (auto &group : next.clipGroups)
            for (const auto &[piece, root] : derived)
                if (std::find(group.members.begin(), group.members.end(), root) != group.members.end())
                    group.members.push_back(piece);
    }
    else if (id == "audio.stretchApply")
    {
        // The clip now plays a prepared stretched file (text, `length` seconds) from its start.
        // Its old source Channel goes away when nothing else uses it; Undo restores both.
        auto found = std::find_if(next.audioPlacements.begin(), next.audioPlacements.end(),
                                  [&](const auto &clip) { return clip.id == a.target; });
        if (found == next.audioPlacements.end())
            return missing();
        if (!juce::File::isAbsolutePath(a.text) || !(a.length > 0))
            return juce::Result::fail("Stretched source needs an absolute file and its duration");
        const auto *old = next.channel(found->channel);
        if (!old)
            return missing();
        Channel channel = *old;
        channel.id = next.allocate();
        // A tempo fit stays the same sample to the user: same name and Channel Rack place.
        const bool fit = a.warpBpm > 0;
        if (!fit)
            channel.name = sourceName(a.text);
        channel.source = a.text;
        channel.audioDuration = a.length;
        const double scale = found->duration > 0 ? a.length / found->duration : 1;
        const auto oldChannel = found->channel;
        // A warp keeps the original source (the first warp's) so re-warps never stack.
        if (a.warpBpm > 0 && !(found->warpBpm > 0 && next.channel(found->warpChannel)))
        {
            found->warpChannel = oldChannel;
            found->warpOffset = found->offset;
            found->warpDuration = found->duration;
        }
        const auto endedWarp = a.warpBpm > 0 ? Id(0) : found->warpChannel;
        found->warpBpm = a.warpBpm;
        if (!(a.warpBpm > 0))
            found->warpChannel = 0, found->warpOffset = found->warpDuration = 0;
        found->channel = channel.id;
        found->offset = 0;
        found->duration = a.length;
        found->fadeIn = a.step ? 0 : found->fadeIn * scale;
        found->fadeOut = a.step ? 0 : found->fadeOut * scale;
        if (fit)
        {
            const auto at = std::find_if(next.channels.begin(), next.channels.end(),
                                         [&](const auto &c) { return c.id == oldChannel; });
            next.channels.insert(at == next.channels.end() ? at : at + 1, std::move(channel));
        }
        else
            next.channels.push_back(std::move(channel));
        if (std::none_of(next.audioPlacements.begin(), next.audioPlacements.end(), [&](const auto &clip)
                         { return clip.channel == oldChannel || (clip.warpBpm > 0 && clip.warpChannel == oldChannel); }))
            std::erase_if(next.channels, [&](const auto &c) { return c.id == oldChannel; });
        // A plain stretch/edit ends a warp; its original source goes when nothing uses it.
        if (endedWarp && std::none_of(next.audioPlacements.begin(), next.audioPlacements.end(), [&](const auto &clip)
                                      { return clip.channel == endedWarp || clip.warpChannel == endedWarp; }))
            std::erase_if(next.channels, [&](const auto &c) { return c.id == endedWarp; });
    }
    else if (id == "audio.fade")
    {
        const auto targets = a.ids.empty() ? std::vector<Id>{a.target} : a.ids;
        if (!a.fadeIn && !a.fadeOut)
            return juce::Result::fail("Set fadeIn and/or fadeOut in seconds");
        for (auto value : {a.fadeIn, a.fadeOut})
            if (value && (!std::isfinite(*value) || *value < 0 || *value > 60))
                return juce::Result::fail("Fades are 0..60 seconds");
        for (auto target : targets)
        {
            auto found = std::find_if(next.audioPlacements.begin(), next.audioPlacements.end(),
                                      [&](const auto &clip) { return clip.id == target; });
            if (found == next.audioPlacements.end())
                return missing();
            const double in = a.fadeIn.value_or(found->fadeIn), out = a.fadeOut.value_or(found->fadeOut);
            if (in + out > found->duration + 1.e-9)
                return juce::Result::fail("Fades are longer than the clip");
            found->fadeIn = in;
            found->fadeOut = out;
        }
    }
    else if (id == "audio.trim")
    {
        const auto targets = a.ids.empty() ? std::vector<Id>{a.target} : a.ids;
        for (auto target : targets)
            if (std::none_of(next.audioPlacements.begin(), next.audioPlacements.end(),
                             [&](const auto &clip) { return clip.id == target; }))
                return missing();
        for (auto &clip : next.audioPlacements)
            if (std::find(targets.begin(), targets.end(), clip.id) != targets.end())
            {
                clip.offset = a.offset;
                clip.duration = a.length;
            }
    }
    else if (id == "clip.group" || id == "clip.ungroup")
    {
        if (auto result = changeClipGroup(next, clipTargets, id == "clip.group"); result.failed())
            return result;
    }
    else if (id == "clip.add")
    {
        if (!pat)
            return missing();
        next.placements.push_back({next.allocate(), pat->id, a.lane, a.beat, a.length, 0});
    }
    else if (id == "clip.mute")
    {
        for (auto &clip : next.placements)
            if (chosen(clip.id))
                clip.muted = a.enabled;
        for (auto &clip : next.audioPlacements)
            if (chosen(clip.id))
                clip.muted = a.enabled;
        for (auto &clip : next.automation.placements)
            if (chosen(clip.id))
                clip.muted = a.enabled;
    }
    else if (id == "clip.delete")
    {
        std::erase_if(next.placements, [&](auto &p) { return chosen(p.id); });
        std::erase_if(next.audioPlacements, [&](auto &p) { return chosen(p.id); });
        std::erase_if(next.automation.placements, [&](auto &p) { return chosen(p.id); });
    }
    else if (id == "clip.rippleDelete")
    {
        // Ripple delete: remove the clips and pull every later clip on the same track left by
        // the removed length, closing the gap (latest first so earlier gaps stay exact).
        std::vector<std::tuple<double, double, int>> gaps; // start, length, lane
        for (const auto &p : next.placements)
            if (chosen(p.id))
                gaps.push_back({p.beat, p.length, p.lane});
        for (const auto &p : next.audioPlacements)
            if (chosen(p.id))
                gaps.push_back({p.beat, next.songBeat(next.songSeconds(p.beat) + p.duration) - p.beat, p.lane});
        for (const auto &p : next.automation.placements)
            if (chosen(p.id))
                gaps.push_back({p.beat, p.length, p.lane});
        if (gaps.empty())
            return missing();
        std::erase_if(next.placements, [&](auto &p) { return chosen(p.id); });
        std::erase_if(next.audioPlacements, [&](auto &p) { return chosen(p.id); });
        std::erase_if(next.automation.placements, [&](auto &p) { return chosen(p.id); });
        std::sort(gaps.begin(), gaps.end(), [](const auto &x, const auto &y) { return std::get<0>(x) > std::get<0>(y); });
        for (const auto &[start, length, lane] : gaps)
        {
            const double end = start + length - 1.e-9;
            for (auto &p : next.placements)
                if (p.lane == lane && p.beat >= end)
                    p.beat = std::max(0., p.beat - length);
            for (auto &p : next.audioPlacements)
                if (p.lane == lane && p.beat >= end)
                    p.beat = std::max(0., p.beat - length);
            for (auto &p : next.automation.placements)
                if (p.lane == lane && p.beat >= end)
                    p.beat = std::max(0., p.beat - length);
        }
    }
    else if (id == "clip.duplicate")
    {
        std::map<Id, Id> copiedMembers;
        std::vector<Placement> copies;
        for (auto &p : next.placements)
            if (chosen(p.id))
            {
                auto copy = p;
                copy.id = next.allocate();
                copiedMembers.emplace(p.id, copy.id);
                copy.beat += a.beat;
                copies.push_back(copy);
            }
        next.placements.insert(next.placements.end(), copies.begin(), copies.end());
        std::vector<AudioPlacement> audioCopies;
        for (const auto &clip : next.audioPlacements)
            if (chosen(clip.id))
            {
                auto copy = clip;
                copy.id = next.allocate();
                copiedMembers.emplace(clip.id, copy.id);
                copy.beat += a.beat;
                audioCopies.push_back(copy);
            }
        next.audioPlacements.insert(next.audioPlacements.end(), audioCopies.begin(), audioCopies.end());
        std::vector<AutomationPlacement> automationCopies;
        for (const auto &clip : next.automation.placements)
            if (chosen(clip.id))
            {
                auto copy = clip;
                copy.id = next.allocate();
                copiedMembers.emplace(clip.id, copy.id);
                copy.beat += a.beat;
                automationCopies.push_back(copy);
            }
        next.automation.placements.insert(next.automation.placements.end(), automationCopies.begin(),
                                          automationCopies.end());
        duplicateClipGroups(next, copiedMembers);
    }
    else if (id == "clip.move")
    {
        if (auto result = moveClipSelection(next, clipTargets, a.beat, a.lane, a.length); result.failed())
            return result;
    }
    else if (id == "clip.slice")
    {
        std::map<Id, Id> slicedMembers;
        if (!sliceClipsAt(next, chosen, a.beat, slicedMembers))
            return juce::Result::fail("Slice inside a clip");
        extendSlicedClipGroups(next, slicedMembers);
    }
    else if (id == "clip.chop")
    {
        // Cut each chosen clip at every multiple of `length` beats on the Song grid.
        if (!std::isfinite(a.length) || a.length < .0625 || a.length > 64)
            return juce::Result::fail("Chop length is 1/16..64 beats");
        double first = 1.e18, last = -1.e18;
        for (const auto &clip : next.placements)
            if (chosen(clip.id))
                first = std::min(first, clip.beat), last = std::max(last, clip.beat + clip.length);
        for (const auto &clip : next.automation.placements)
            if (chosen(clip.id))
                first = std::min(first, clip.beat), last = std::max(last, clip.beat + clip.length);
        for (const auto &clip : next.audioPlacements)
            if (chosen(clip.id))
                first = std::min(first, clip.beat), last = std::max(last, next.audioEndBeat(clip.beat, clip.duration));
        if (!(last > first))
            return juce::Result::fail("Select clips to chop");
        if ((last - first) / a.length > 4096)
            return juce::Result::fail("Chop would make more than 4096 pieces");
        std::set<Id> pieces;
        std::map<Id, Id> slicedMembers;
        const auto inChop = [&](Id clip) { return chosen(clip) || pieces.contains(clip); };
        for (double cut = std::ceil(first / a.length + 1.e-9) * a.length; cut < last - 1.e-9; cut += a.length)
        {
            std::map<Id, Id> made;
            sliceClipsAt(next, inChop, cut, made);
            for (const auto &[from, to] : made)
                pieces.insert(to), slicedMembers.emplace(from, to);
        }
        if (pieces.empty())
            return juce::Result::fail("The selected clips are shorter than one piece");
        extendSlicedClipGroups(next, slicedMembers);
    }
    else if (id == "clip.trimStart")
    {
        // Left-edge trim of a Pattern clip: the end stays put; the window start moves with it.
        auto it = std::find_if(next.placements.begin(), next.placements.end(),
                               [&](const auto &p) { return p.id == a.target; });
        if (it == next.placements.end())
            return missing();
        const double end = it->beat + it->length;
        if (!std::isfinite(a.beat) || a.beat < 0 || a.beat > end - 1. / 64)
            return juce::Result::fail("The clip start must stay before its end (at least 1/64 beat)");
        const double offset = it->offset + (a.beat - it->beat);
        if (offset < -1.e-9)
            return juce::Result::fail("The clip cannot start before its Pattern start");
        it->offset = std::max(0., offset);
        it->beat = a.beat;
        it->length = end - a.beat;
    }
    else if (id == "clip.quantize")
    {
        // Snap the start of each chosen clip to the grid (length and content unchanged).
        if (!std::isfinite(a.length) || a.length < .0625 || a.length > 64)
            return juce::Result::fail("Quantize grid is 1/16..64 beats");
        const auto snapped = [&](double beat) { return std::max(0., std::round(beat / a.length) * a.length); };
        bool any = false;
        for (auto &clip : next.placements)
            if (chosen(clip.id))
                clip.beat = snapped(clip.beat), any = true;
        for (auto &clip : next.audioPlacements)
            if (chosen(clip.id))
                clip.beat = snapped(clip.beat), any = true;
        for (auto &clip : next.automation.placements)
            if (chosen(clip.id))
                clip.beat = snapped(clip.beat), any = true;
        if (!any)
            return juce::Result::fail("Select clips to quantize");
    }
    else if (id == "clip.paste")
    {
        auto board = a.clipboard;
        if (!board || board->empty())
        {
            auto copied = std::make_shared<ClipClipboard>();
            copied->origin = 1.e18;
            for (const auto &c : next.placements)
                if (chosen(c.id))
                    copied->patterns.push_back(c), copied->origin = std::min(copied->origin, c.beat);
            for (const auto &c : next.audioPlacements)
                if (chosen(c.id))
                    copied->audio.push_back(c), copied->origin = std::min(copied->origin, c.beat);
            for (const auto &c : next.automation.placements)
                if (chosen(c.id))
                    copied->automation.push_back(c), copied->origin = std::min(copied->origin, c.beat);
            board = copied;
        }
        if (!board || board->empty())
            return juce::Result::fail("Copy or cut clips first (Ctrl+C / Ctrl+X)");
        if (!std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000)
            return juce::Result::fail("Paste position is 0..1000000 beats");
        const double shift = a.beat - board->origin;
        if (next.placements.size() + next.audioPlacements.size() + next.automation.placements.size() +
                board->patterns.size() + board->audio.size() + board->automation.size() >
            100000)
            return juce::Result::fail("Paste would exceed the project's clip limit");
        for (auto clip : board->patterns)
        {
            if (!next.pattern(clip.pattern))
                return juce::Result::fail("A copied clip's Pattern no longer exists");
            clip.id = next.allocate();
            clip.beat += shift;
            next.placements.push_back(clip);
        }
        for (auto clip : board->audio)
        {
            if (!next.channel(clip.channel))
                return juce::Result::fail("A copied audio clip's source no longer exists");
            clip.id = next.allocate();
            clip.beat += shift;
            next.audioPlacements.push_back(clip);
        }
        for (auto clip : board->automation)
        {
            if (!next.automationSource(clip.source))
                return juce::Result::fail("A copied automation clip's source no longer exists");
            clip.id = next.allocate();
            clip.beat += shift;
            next.automation.placements.push_back(clip);
        }
    }
    else if (id == "time.insert" || id == "time.delete")
    {
        // Time selection [beat, beat + length): insert moves everything at or after its start
        // later (optionally slicing clips that cross it first: args.enabled); delete removes
        // what is inside (clips cut at both edges) and pulls everything after it earlier.
        // Markers, tempo and time-signature changes move with the content.
        if (!std::isfinite(a.beat) || !std::isfinite(a.length) || a.beat < 0 || a.length < .0625 ||
            a.beat + a.length > 1000000)
            return juce::Result::fail("Select a time range on the ruler first (Ctrl+drag)");
        const double start = a.beat, end = a.beat + a.length, length = a.length;
        const auto every = [](Id) { return true; };
        std::map<Id, Id> slicedMembers;
        if (id == "time.delete" || a.enabled)
            sliceClipsAt(next, every, start, slicedMembers);
        if (id == "time.delete")
        {
            sliceClipsAt(next, every, end, slicedMembers);
            const auto inside = [&](double beat) { return beat >= start - 1.e-9 && beat < end - 1.e-9; };
            std::erase_if(next.placements, [&](const auto &c) { return inside(c.beat); });
            std::erase_if(next.audioPlacements, [&](const auto &c) { return inside(c.beat); });
            std::erase_if(next.automation.placements, [&](const auto &c) { return inside(c.beat); });
            std::erase_if(next.markers, [&](const auto &m) { return inside(m.beat); });
            std::erase_if(next.tempoChanges, [&](const auto &t) { return inside(t.beat); });
            std::erase_if(next.meterChanges, [&](const auto &m) { return inside(m.beat); });
        }
        const double shift = id == "time.insert" ? length : -length;
        const double from = id == "time.insert" ? start : end;
        const auto moves = [&](double beat) { return beat >= from - 1.e-9; };
        for (auto &c : next.placements)
            if (moves(c.beat))
                c.beat = std::max(0., c.beat + shift);
        for (auto &c : next.audioPlacements)
            if (moves(c.beat))
                c.beat = std::max(0., c.beat + shift);
        for (auto &c : next.automation.placements)
            if (moves(c.beat))
                c.beat = std::max(0., c.beat + shift);
        for (auto &m : next.markers)
            if (moves(m.beat))
                m.beat = std::max(0., m.beat + shift);
        for (auto &t : next.tempoChanges)
            if (moves(t.beat))
                t.beat = std::max(0., t.beat + shift);
        for (auto &m : next.meterChanges)
            if (moves(m.beat))
                m.beat = std::max(0., m.beat + shift);
        extendSlicedClipGroups(next, slicedMembers);
    }
    else if (id == "clip.slip")
    {
        double minimum = -1.e9, maximum = 1.e9;
        bool found = false;
        for (auto &clip : next.automation.placements)
            if (chosen(clip.id))
            {
                found = true;
                minimum = std::max(minimum, -clip.offset);
                maximum =
                    std::min(maximum, next.automationSource(clip.source)->length - clip.offset - clip.length);
            }
        for (auto &clip : next.placements)
            if (chosen(clip.id))
            {
                found = true;
                minimum = std::max(minimum, -clip.offset);
                maximum = std::min(maximum, std::max(0., next.pattern(clip.pattern)->length - clip.length) -
                                                clip.offset);
            }
        for (auto &clip : next.audioPlacements)
            if (chosen(clip.id))
            {
                found = true;
                minimum = std::max(minimum, -clip.offset * next.songTempo().bpmAt(clip.beat) / 60.);
                maximum = std::min(maximum,
                                   (next.channel(clip.channel)->audioDuration - clip.offset - clip.duration) *
                                       next.songTempo().bpmAt(clip.beat) / 60.);
            }
        if (!found || minimum > maximum)
            return juce::Result::fail("Select clips with a valid source window");
        const auto delta = juce::jlimit(minimum, maximum, a.beat);
        for (auto &clip : next.placements)
            if (chosen(clip.id))
                clip.offset += delta;
        for (auto &clip : next.audioPlacements)
            if (chosen(clip.id))
                clip.offset += delta * 60. / next.songTempo().bpmAt(clip.beat);
        for (auto &clip : next.automation.placements)
            if (chosen(clip.id))
                clip.offset += delta;
    }
    else if (id == "clip.unique" &&
             std::any_of(next.automation.placements.begin(), next.automation.placements.end(),
                         [&](const auto &clip) { return clip.id == a.target; }))
    {
        if (auto result = applyAutomationCommand(next, "automation.unique", a); result.failed())
            return result;
    }
    else if (id == "clip.unique" && std::any_of(next.audioPlacements.begin(), next.audioPlacements.end(),
                                                [&](const auto &clip) { return clip.id == a.target; }))
    {
        auto it = std::find_if(next.audioPlacements.begin(), next.audioPlacements.end(),
                               [&](const auto &clip) { return clip.id == a.target; });
        auto copy = *next.channel(it->channel);
        copy.id = next.allocate();
        copy.name += " unique";
        it->channel = copy.id;
        next.channels.push_back(std::move(copy));
    }
    else if (id == "clip.splitChannels")
    {
        // One clip becomes one clip per Channel, each on its own track (the clip's track
        // and the ones below), at the same position and window. Only this clip changes:
        // the source Pattern stays for its other placements.
        auto it = std::find_if(next.placements.begin(), next.placements.end(),
                               [&](auto &p) { return p.id == a.target; });
        if (it == next.placements.end())
            return missing();
        const auto *source = next.pattern(it->pattern);
        if (!source)
            return missing();
        const Placement original = *it;
        std::vector<Pattern> parts;
        for (const auto &channel : next.channels)
        {
            Pattern part = *source;
            std::erase_if(part.notes, [&](const auto &note) { return note.channel != channel.id; });
            std::erase_if(part.controllers, [&](const auto &event) { return event.channel != channel.id; });
            std::erase_if(part.channelLoops, [&](const auto &loop) { return loop.channel != channel.id; });
            if (part.notes.empty() && part.channelLoops.empty())
                continue;
            if (!parts.empty())
                part.automation.clear(); // Pattern automation stays with the first part only.
            part.name = (source->name + " - " + channel.name).trim().substring(0, 256);
            parts.push_back(std::move(part));
        }
        if (parts.empty())
            return juce::Result::fail("This Pattern clip has no Channel notes to split");
        if (parts.size() == 1)
            return juce::Result::fail("This Pattern clip uses one Channel; there is nothing to split");
        if (next.patterns.size() + parts.size() > 1024 || original.lane + int(parts.size()) > 1000)
            return juce::Result::fail("Split by channel would exceed the project's Pattern or track limit");
        next.placements.erase(it);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            auto &part = parts[i];
            part.id = next.allocate();
            for (auto &n : part.notes)
                n.id = next.allocate();
            for (auto &event : part.controllers)
                event.id = next.allocate();
            Placement clip = original;
            clip.id = next.allocate();
            clip.pattern = part.id;
            clip.lane = original.lane + int(i);
            next.placements.push_back(clip);
            next.patterns.push_back(std::move(part));
        }
    }
    else if (id == "clip.merge" || id == "clip.mergeSimilar")
    {
        // The selected Pattern clips become one new Pattern holding what actually plays in
        // them (Channel loops, clip repeats and windows rendered into plain notes), placed
        // once from the earliest clip on its track.
        std::vector<Placement> clips;
        for (const auto target : a.ids)
            if (const auto found = std::find_if(next.placements.begin(), next.placements.end(),
                                                [&](const auto &p) { return p.id == target; });
                found != next.placements.end())
                clips.push_back(*found);
        if (clips.size() < 2 || a.ids.size() > 1024)
            return juce::Result::fail("Select two or more Pattern clips to merge");
        std::sort(clips.begin(), clips.end(), [](const auto &x, const auto &y)
                  { return std::tie(x.beat, x.lane) < std::tie(y.beat, y.lane); });
        const double start = clips.front().beat;
        double end = start;
        for (const auto &clip : clips)
            end = std::max(end, clip.beat + clip.length);
        Pattern merged;
        merged.id = next.allocate();
        merged.name = "Merged";
        merged.autoLength = false;
        merged.length = std::max(.25, end - start);
        if (const auto *first = next.pattern(clips.front().pattern))
            merged.colour = first->colour;
        for (const auto &clip : clips)
        {
            const auto *pattern = next.pattern(clip.pattern);
            if (!pattern)
                return missing();
            const auto occurrences = rawPatternOccurrences(next, *pattern);
            forEachClipRepeat(clip.length, clip.offset, pattern->length,
                              [&](double position, double length, double offset, std::uint32_t)
                              {
                                  const double base = clip.beat - start + position - offset;
                                  for (const auto &occurrence : occurrences)
                                  {
                                      const auto &n = occurrence.note;
                                      if (n.beat < offset || n.beat >= offset + length)
                                          continue;
                                      Note copy = n;
                                      copy.id = next.allocate();
                                      copy.beat = base + n.beat;
                                      copy.length = std::min(n.length, offset + length - n.beat);
                                      copy.muted = n.muted || clip.muted;
                                      merged.notes.push_back(copy);
                                  }
                                  for (const auto &event : pattern->controllers)
                                      if (event.beat >= offset && event.beat < offset + length)
                                      {
                                          auto copy = event;
                                          copy.id = next.allocate();
                                          copy.beat = base + event.beat;
                                          merged.controllers.push_back(copy);
                                      }
                              });
        }
        if (merged.notes.size() > 100000)
            return juce::Result::fail("The merged Pattern would exceed 100000 notes");
        std::set<Id> removed;
        for (const auto &clip : clips)
            removed.insert(clip.id);
        std::erase_if(next.placements, [&](const auto &p) { return removed.contains(p.id); });
        next.placements.push_back({next.allocate(), merged.id, clips.front().lane, start, merged.length, 0, false});
        if (id == "clip.mergeSimilar")
        {
            // Every other repeat of the same stack (same Patterns, tracks, windows, mute and
            // spacing, at any time) is replaced by a clip of the merged Pattern too.
            const auto same = [](const Placement &x, const Placement &y)
            {
                return x.pattern == y.pattern && x.lane == y.lane && std::abs(x.length - y.length) < 1.e-9 &&
                       std::abs(x.offset - y.offset) < 1.e-9 && x.muted == y.muted;
            };
            std::vector<double> starts;
            for (const auto &candidate : next.placements)
                if (same(candidate, clips.front()))
                    starts.push_back(candidate.beat);
            std::sort(starts.begin(), starts.end());
            int repeats = 0;
            for (const double at : starts)
            {
                std::vector<Id> members;
                for (const auto &clip : clips)
                {
                    const double beat = at + clip.beat - start;
                    const auto found = std::find_if(next.placements.begin(), next.placements.end(),
                                                    [&](const auto &p)
                                                    {
                                                        return std::abs(p.beat - beat) < 1.e-9 && same(p, clip) &&
                                                               std::find(members.begin(), members.end(), p.id) ==
                                                                   members.end();
                                                    });
                    if (found == next.placements.end())
                        break;
                    members.push_back(found->id);
                }
                if (members.size() != clips.size())
                    continue;
                std::erase_if(next.placements, [&](const auto &p)
                              { return std::find(members.begin(), members.end(), p.id) != members.end(); });
                next.placements.push_back({next.allocate(), merged.id, clips.front().lane, at, merged.length, 0, false});
                ++repeats;
            }
            merged.name = "Merged x" + juce::String(repeats + 1);
        }
        next.patterns.push_back(std::move(merged));
    }
    else if (id == "clip.unique")
    {
        auto it = std::find_if(next.placements.begin(), next.placements.end(),
                               [&](auto &p) { return p.id == a.target; });
        if (it == next.placements.end())
            return missing();
        auto *original = next.pattern(it->pattern);
        if (!original)
            return missing();
        auto copy = *original;
        copy.id = next.allocate();
        copy.name += " unique";
        for (auto &n : copy.notes)
            n.id = next.allocate();
        it->pattern = copy.id;
        next.patterns.push_back(std::move(copy));
    }
    else if (isMixerInsertLifecycleCommand(id))
    {
        if (id == "mixer.insert.create")
        {
            // args.value: how many inserts to add (default 1, at most 32), one Undo step.
            // args.target > 0: parallel chains (FX container) of that insert: each new insert is fed
            // by a unity send from it and outputs where it does, so its effects run in parallel
            // with the source's own (dry) path.
            const int count = a.value == 0 ? 1 : int(a.value);
            if (!std::isfinite(a.value) || a.value != std::floor(a.value) || count < 1 || count > 32)
                return juce::Result::fail("mixer.insert.invalidCount: Add 1..32 inserts at once");
            const auto *parallelSource = a.target > 0 && a.target <= std::numeric_limits<int>::max()
                                             ? mixerInsert(next, int(a.target))
                                             : nullptr;
            if (a.target != 0 && !parallelSource)
                return juce::Result::fail("mixer.parallel.missingSource: Existing non-Master source insert required");
            const auto sourceName = parallelSource ? parallelSource->name : juce::String{};
            const auto dryRoutes = parallelSource ? parallelSource->routes : std::vector<MixerRoute>{};
            for (int i = 0; i < count; ++i)
            {
                Id reserved = std::max(next.nextId, mixerInsertNextId);
                for (const auto &row : next.mixer)
                    reserved = std::max(reserved, Id(row.index) + 1);
                const auto name = a.text.isNotEmpty() || !parallelSource
                                      ? a.text
                                      : (sourceName + " Parallel " + juce::String(i + 1)).substring(0, 64).trim();
                if (auto result = createMixerInsert(next, reserved, name); result.failed())
                    return result;
                if (parallelSource)
                {
                    const auto row = [&](int index)
                    {
                        const auto found = std::find_if(next.mixer.begin(), next.mixer.end(),
                                                        [&](const auto &m) { return m.index == index; });
                        return found == next.mixer.end() ? nullptr : &*found;
                    };
                    auto *source = row(int(a.target));
                    auto *branch = row(int(reserved));
                    if (!source || !branch)
                        return missing();
                    branch->routes = dryRoutes; // Same destinations and levels as the dry path.
                    source->routes.push_back({int(reserved), 1.f});
                }
            }
        }
        else if (auto result =
                     deleteMixerInsert(next, a.mixer, a.mixerDelete.value_or(MixerInsertDeletePolicy{}));
                 result.failed())
            return result;
    }
    else if (isMixerGroupCommand(id))
    {
        if (auto result = applyMixerGroup(next, id, a); result.failed()) return result;
    }
    else if (isMixerInsertOrderCommand(id))
    {
        if (auto result = applyMixerInsertOrder(next, id, a); result.failed()) return result;
    }
    else if (id == "mixer.route" || id == "mixer.routeOnly" || id == "mixer.send")
    {
        if (a.target < 0 || a.target > std::numeric_limits<int>::max())
            return juce::Result::fail("Invalid Mixer destination index");
        auto result = id == "mixer.send"
                          ? setMixerSend(next, a.mixer, int(a.target), a.value)
                          : setMixerRoute(next, a.mixer, int(a.target), id == "mixer.routeOnly" || a.enabled,
                                          id == "mixer.routeOnly");
        if (result.failed())
            return result;
    }
    else if (id == "mixer.stereoSeparation" || id == "mixer.phaseInvert" || id == "mixer.stereoSwap" ||
             id == "mixer.eq" || id == "mixer.eqReset" || id == "mixer.delay" || id == "mixer.dock" || id == "mixer.reset" || id == "mixer.trackControls" || id == "mixer.gain" || id == "mixer.pan" || id == "mixer.mute" || id == "mixer.rename" ||
             id == "mixer.colour" || id == "mixer.solo" || id == "mixer.soloConnected" ||
             id == "mixer.unsolo" || id == "mixer.unmuteAll")
    {
        if (auto result = applyMixerControl(next, id, a); result.failed())
            return result;
    }
    if (id.startsWith("automation."))
        if (auto applied = applyAutomationCommand(next, id, a); applied.failed())
            return applied;
    if (!next.clipGroups.empty())
    {
        const auto beforeClips = projectClipIds(canonicalSource), afterClips = projectClipIds(next);
        if (!std::includes(afterClips.begin(), afterClips.end(), beforeClips.begin(), beforeClips.end()))
            pruneClipGroups(next);
    }
    if (id.startsWith("arrangement."))
    {
        if (a.text.length() > 128)
            return juce::Result::fail("Arrangement names are at most 128 characters");
        auto stored = std::find_if(next.arrangements.begin(), next.arrangements.end(),
                                   [&](const auto &x) { return x.id == a.target; });
        const bool current = a.target == 0 || (next.arrangementId != 0 && a.target == next.arrangementId);
        if (id == "arrangement.merge")
        {
            // Copies of a stored arrangement's clips and markers land at args.beat in the open
            // one (blend); with args.enabled (insert) later material first moves right by the
            // merged length. The source arrangement stays as it is.
            if (current || stored == next.arrangements.end())
                return juce::Result::fail("Choose another stored arrangement to merge");
            if (!std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000)
                return juce::Result::fail("Merge position is 0..1000000 beats");
            auto copy = arrangements::cloneWithNewIds(next, *stored, stored->name);
            double length = 0;
            for (const auto &c : copy.placements)
                length = std::max(length, c.beat + c.length);
            for (const auto &c : copy.audioPlacements)
                length = std::max(length, next.audioEndBeat(c.beat, c.duration));
            for (const auto &c : copy.automationPlacements)
                length = std::max(length, c.beat + c.length);
            if (length <= 0)
                return juce::Result::fail("That arrangement has no clips");
            if (a.enabled)
            {
                const auto moves = [&](double beat) { return beat >= a.beat - 1.e-9; };
                for (auto &c : next.placements)
                    if (moves(c.beat))
                        c.beat += length;
                for (auto &c : next.audioPlacements)
                    if (moves(c.beat))
                        c.beat += length;
                for (auto &c : next.automation.placements)
                    if (moves(c.beat))
                        c.beat += length;
                for (auto &m : next.markers)
                    if (moves(m.beat))
                        m.beat += length;
            }
            for (auto c : copy.placements)
                c.beat += a.beat, next.placements.push_back(c);
            for (auto c : copy.audioPlacements)
                c.beat += a.beat, next.audioPlacements.push_back(c);
            for (auto c : copy.automationPlacements)
                c.beat += a.beat, next.automation.placements.push_back(c);
            for (auto m : copy.markers)
                m.beat += a.beat, next.markers.push_back(m);
            for (auto &g : copy.clipGroups)
                next.clipGroups.push_back(g);
        }
        else if (id == "arrangement.add" || id == "arrangement.clone")
        {
            if (next.arrangements.size() >= 255)
                return juce::Result::fail("Too many arrangements");
            auto active = arrangements::takeActive(next);
            Arrangement fresh;
            if (id == "arrangement.clone")
                fresh = arrangements::cloneWithNewIds(next, active,
                                                      a.text.isNotEmpty() ? a.text : active.name + " copy");
            else
            {
                fresh.id = next.allocate();
                fresh.name = a.text.isNotEmpty() ? a.text
                                                 : "Arrangement " + juce::String(int(next.arrangements.size()) + 2);
            }
            next.arrangements.push_back(std::move(active));
            arrangements::makeActive(next, std::move(fresh));
        }
        else if (id == "arrangement.select")
        {
            if (current)
                return juce::Result::fail("That arrangement is already open");
            if (stored == next.arrangements.end())
                return missing();
            auto chosen = std::move(*stored);
            next.arrangements.erase(stored);
            next.arrangements.push_back(arrangements::takeActive(next));
            arrangements::makeActive(next, std::move(chosen));
        }
        else if (id == "arrangement.rename")
        {
            if (a.text.trim().isEmpty())
                return juce::Result::fail("Arrangements need a name");
            if (current)
                next.arrangementName = a.text.trim();
            else if (stored != next.arrangements.end())
                stored->name = a.text.trim();
            else
                return missing();
        }
        else if (id == "arrangement.delete")
        {
            if (next.arrangements.empty())
                return juce::Result::fail("The last arrangement cannot be deleted");
            if (current)
            {
                auto replacement = std::move(next.arrangements.front());
                next.arrangements.erase(next.arrangements.begin());
                (void)arrangements::takeActive(next);
                arrangements::makeActive(next, std::move(replacement));
            }
            else if (stored != next.arrangements.end())
                next.arrangements.erase(stored);
            else
                return missing();
        }
    }
    if (id == "channel.sliceToChannels")
    {
        // One Channel per slice (same sample, its own start/end window) and a Pattern that
        // replays the slices in their original order: Slicex-style pads.
        const auto *source = next.channel(a.channel);
        if (!source || source->audioClip || source->instrument)
            return juce::Result::fail("Slice a sampler Channel");
        const auto &points = a.slicePoints;
        if (points.size() < 2 || points.size() > 64 || points.front() != 0 || !(a.length > 0))
            return juce::Result::fail("Provide 2..64 slice starts (first 0) and the source duration");
        for (size_t i = 1; i < points.size(); ++i)
            if (!(points[i] > points[i - 1]) || points[i] >= 1)
                return juce::Result::fail("Slice starts must increase inside the sample");
        const auto original = *source;
        const double secondsPerBeat = 60. / next.tempo;
        Pattern part;
        part.id = next.allocate();
        part.name = original.name + " slices";
        for (size_t i = 0; i < points.size(); ++i)
        {
            Channel slice = original;
            slice.id = next.allocate();
            slice.name = original.name + " " + juce::String(int(i) + 1);
            slice.sampler.start = points[i];
            slice.sampler.end = i + 1 < points.size() ? points[i + 1] : original.sampler.end;
            slice.sampler.reverse = false;
            slice.sampler.loop = false;
            next.channels.push_back(slice);
            const double beat = points[i] * a.length / secondsPerBeat;
            const double end = (i + 1 < points.size() ? points[i + 1] : 1.) * a.length / secondsPerBeat;
            part.notes.push_back({next.allocate(), slice.id, beat, std::max(.0625, end - beat), 60, .8f});
        }
        const double beats = a.length / secondsPerBeat, bar = next.numerator * 4. / next.denominator;
        part.length = std::max(bar, std::ceil(beats / bar - 1.e-9) * bar);
        part.autoLength = false;
        next.patterns.push_back(std::move(part));
    }
    if (id == "media.relink")
    {
        // Points Channels at found files again; every other setting (name, tempo, envelopes) stays.
        if (a.relinks.empty() || a.relinks.size() > size_t(Project::maxChannels))
            return juce::Result::fail("Relink 1.." + juce::String(Project::maxChannels) + " Channels");
        for (const auto &[channelId, path] : a.relinks)
        {
            auto *channel = next.channel(channelId);
            if (!channel || channel->instrument || channel->source.startsWith("builtin:") ||
                !juce::File::isAbsolutePath(path) || path.length() > 4096)
                return juce::Result::fail("Relink needs sample/audio Channels and absolute file paths");
            channel->source = path;
        }
    }
    if (id.startsWith("meter.change."))
    {
        // beat (> 0, on a bar line of the meter before it), numerator 1..32, denominator 2/4/8/16.
        auto found = std::find_if(next.meterChanges.begin(), next.meterChanges.end(),
                                  [&](const auto &change) { return change.id == a.target; });
        if (id != "meter.change.add" && found == next.meterChanges.end())
            return missing();
        if (id == "meter.change.delete")
            next.meterChanges.erase(found);
        else
        {
            if (a.numerator < 1 || a.numerator > 32 ||
                (a.denominator != 2 && a.denominator != 4 && a.denominator != 8 && a.denominator != 16) ||
                !std::isfinite(a.beat) || a.beat <= 0 || a.beat > 1000000)
                return juce::Result::fail("Time signatures are 1..32 over 2, 4, 8 or 16 at a beat after 0");
            auto others = next.meterChanges;
            if (found != next.meterChanges.end())
                others.erase(others.begin() + (found - next.meterChanges.begin()));
            if (std::any_of(others.begin(), others.end(), [&](const auto &c) { return std::abs(c.beat - a.beat) < 1.e-9; }))
                return juce::Result::fail("A time-signature change already exists at that beat");
            if (others.size() >= 1024)
                return juce::Result::fail("Too many time-signature changes");
            Project probe = next;
            probe.meterChanges = others;
            std::sort(probe.meterChanges.begin(), probe.meterChanges.end(), [](auto &x, auto &y) { return x.beat < y.beat; });
            if (probe.barPosition(a.beat).beatInBar > 1.e-6)
                return juce::Result::fail("A time-signature change must start on a bar line");
            if (found != next.meterChanges.end())
                *found = {found->id, a.beat, a.numerator, a.denominator};
            else
                next.meterChanges.push_back({next.allocate(), a.beat, a.numerator, a.denominator});
        }
        std::sort(next.meterChanges.begin(), next.meterChanges.end(), [](auto &x, auto &y) { return x.beat < y.beat; });
        // Later changes must still sit on bar lines of the meters before them.
        auto check = next;
        for (size_t i = 0; i < next.meterChanges.size(); ++i)
        {
            check.meterChanges.assign(next.meterChanges.begin(), next.meterChanges.begin() + std::ptrdiff_t(i));
            if (check.barPosition(next.meterChanges[i].beat).beatInBar > 1.e-6)
                return juce::Result::fail("That would move a later time-signature change off its bar line");
        }
    }
    if (id == "performance.assignPad")
    {
        // value = pad 1..16; pattern = the Pattern it launches, 0 = back to the n-th Pattern.
        const int pad = juce::roundToInt(a.value);
        if (pad < 1 || pad > 16 || (a.pattern != 0 && !next.pattern(a.pattern)))
            return juce::Result::fail("Assign pads 1..16 to an existing Pattern (or 0 to reset)");
        next.padPatterns[size_t(pad - 1)] = a.pattern;
    }
    if (id == "performance.saveScene")
    {
        // value = scene 1..8; ids = the Patterns it launches (empty clears the scene).
        const int scene = juce::roundToInt(a.value);
        if (scene < 1 || scene > 8 || a.ids.size() > 1024)
            return juce::Result::fail("Scenes are 1..8 with at most 1024 Patterns");
        for (const auto pattern : a.ids)
            if (!next.pattern(pattern))
                return missing();
        next.performanceScenes[size_t(scene - 1)] = a.ids;
    }
    if (id.startsWith("track."))
    {
        // Playlist tracks are lanes 0..127; settings live in a sparse list kept sorted by lane.
        if (a.lane < 0 || a.lane > 127)
            return juce::Result::fail("Playlist tracks are lanes 0..127");
        auto entry = [&](int lane) -> PlaylistTrack &
        {
            for (auto &t : next.tracks)
                if (t.lane == lane)
                    return t;
            next.tracks.push_back({lane});
            return next.tracks.back();
        };
        // Moves every clip and track setting from lane `from` by `delta` (inside 0..127).
        auto remap = [&](auto &&lanes)
        {
            for (auto &clip : next.placements)
                clip.lane = lanes(clip.lane);
            for (auto &clip : next.audioPlacements)
                clip.lane = lanes(clip.lane);
            for (auto &clip : next.automation.placements)
                clip.lane = lanes(clip.lane);
            for (auto &t : next.tracks)
                t.lane = lanes(t.lane);
        };
        auto occupied = [&](int lane)
        {
            return std::any_of(next.placements.begin(), next.placements.end(), [&](auto &c) { return c.lane == lane; }) ||
                   std::any_of(next.audioPlacements.begin(), next.audioPlacements.end(),
                               [&](auto &c) { return c.lane == lane; }) ||
                   std::any_of(next.automation.placements.begin(), next.automation.placements.end(),
                               [&](auto &c) { return c.lane == lane; });
        };
        if (id == "track.rename")
        {
            if (a.text.length() > 128 || a.text.containsChar('\n') || a.text.containsChar('\r'))
                return juce::Result::fail("Track names are one line of at most 128 characters");
            entry(a.lane).name = a.text.trim();
        }
        else if (id == "track.colour")
        {
            if (!std::isfinite(a.value) || a.value < 0 || a.value > 4294967295. ||
                (a.value != 0 && (juce::uint32(a.value) >> 24) != 255))
                return juce::Result::fail("Track colour is an opaque ARGB value, or 0 for the theme colour");
            entry(a.lane).colour = juce::uint32(a.value);
        }
        else if (id == "track.mute")
            entry(a.lane).muted = a.enabled;
        else if (id == "track.group")
        {
            if (a.lane == 0 && a.enabled)
                return juce::Result::fail("The first track has no track above to group with");
            entry(a.lane).groupedAbove = a.enabled;
        }
        else if (id == "track.lock")
            entry(a.lane).locked = a.enabled;
        else if (id == "track.height")
        {
            if (!std::isfinite(a.value) || a.value < .5 || a.value > 3)
                return juce::Result::fail("Track height is .5..3 times the lane height");
            entry(a.lane).height = a.value;
        }
        else if (id == "track.instrument")
        {
            // FL-style instrument track: the track belongs to one Channel (0 makes it a free track).
            if (a.channel != 0)
            {
                const auto *channel = next.channel(a.channel);
                if (!channel || channel->audioClip)
                    return juce::Result::fail("Instrument tracks take a sampler or instrument Channel");
            }
            entry(a.lane).channel = a.channel;
            if (a.channel != 0)
                entry(a.lane).audio = false;
        }
        else if (id == "track.audio")
        {
            entry(a.lane).audio = a.enabled;
            if (a.enabled)
                entry(a.lane).channel = 0;
        }
        else if (id == "track.instrumentClip")
        {
            // A new Pattern for the track's Channel, placed at `beat` (one bar, one Undo).
            const auto *track = next.track(a.lane);
            const auto *channel = track && track->channel ? next.channel(track->channel) : nullptr;
            if (!channel)
                return juce::Result::fail("This is not an instrument track");
            if (!std::isfinite(a.beat) || a.beat < 0)
                return juce::Result::fail("Clip position must be a beat >= 0");
            int count = 1;
            for (const auto &pattern : next.patterns)
                if (pattern.name.startsWith(channel->name + " "))
                    ++count;
            const double bar = next.numerator * 4. / next.denominator;
            const auto pattern = next.allocate();
            next.patterns.push_back({pattern, channel->name + " " + juce::String(count), bar, {}});
            next.placements.push_back({next.allocate(), pattern, a.lane, a.beat, bar, 0});
        }
        else if (id == "track.collapse")
        {
            int head = a.lane; // Collapse acts on the group the lane belongs to.
            while (head > 0 && next.track(head) && next.track(head)->groupedAbove)
                --head;
            if (!next.track(head + 1) || !next.track(head + 1)->groupedAbove)
                return juce::Result::fail("That track is not in a group");
            entry(head).collapsed = a.enabled;
        }
        else if (id == "track.solo")
        {
            // FL track solo: this track plays alone; soloing it again unmutes every track.
            std::set<int> used{a.lane};
            for (const auto &clip : next.placements)
                used.insert(clip.lane);
            for (const auto &clip : next.audioPlacements)
                used.insert(clip.lane);
            for (const auto &clip : next.automation.placements)
                used.insert(clip.lane);
            for (const auto &t : next.tracks)
                used.insert(t.lane);
            const bool soloed = !next.laneMuted(a.lane) &&
                                std::all_of(used.begin(), used.end(),
                                            [&](int lane) { return lane == a.lane || next.laneMuted(lane); });
            for (const int lane : used)
                entry(lane).muted = soloed ? false : lane != a.lane;
        }
        else if (id == "track.insert")
        {
            if (occupied(127) || next.track(127))
                return juce::Result::fail("The last track (128) is in use: nothing can move down");
            remap([&](int lane) { return lane >= a.lane ? lane + 1 : lane; });
        }
        else if (id == "track.delete")
        {
            std::erase_if(next.placements, [&](auto &c) { return c.lane == a.lane; });
            std::erase_if(next.audioPlacements, [&](auto &c) { return c.lane == a.lane; });
            std::erase_if(next.automation.placements, [&](auto &c) { return c.lane == a.lane; });
            std::erase_if(next.tracks, [&](auto &t) { return t.lane == a.lane; });
            remap([&](int lane) { return lane > a.lane ? lane - 1 : lane; });
        }
        else if (id == "track.clone")
        {
            // A copy of the track and its clips right below it; later tracks move down.
            if (occupied(127) || next.track(127))
                return juce::Result::fail("The last track (128) is in use: nothing can move down");
            std::optional<PlaylistTrack> settings;
            if (const auto *t = next.track(a.lane))
                settings = *t;
            std::vector<Placement> patterns;
            std::vector<AudioPlacement> audio;
            std::vector<AutomationPlacement> automation;
            for (const auto &c : next.placements)
                if (c.lane == a.lane)
                    patterns.push_back(c);
            for (const auto &c : next.audioPlacements)
                if (c.lane == a.lane)
                    audio.push_back(c);
            for (const auto &c : next.automation.placements)
                if (c.lane == a.lane)
                    automation.push_back(c);
            remap([&](int lane) { return lane > a.lane ? lane + 1 : lane; });
            for (auto c : patterns)
                c.id = next.allocate(), c.lane = a.lane + 1, next.placements.push_back(c);
            for (auto c : audio)
                c.id = next.allocate(), c.lane = a.lane + 1, next.audioPlacements.push_back(c);
            for (auto c : automation)
                c.id = next.allocate(), c.lane = a.lane + 1, next.automation.placements.push_back(c);
            if (settings)
            {
                auto copy = *settings;
                copy.lane = a.lane + 1;
                copy.groupedAbove = false;
                copy.collapsed = false;
                if (copy.name.isNotEmpty())
                    copy.name = (copy.name + " 2").substring(0, 128);
                next.tracks.push_back(copy);
            }
        }
        else if (id == "track.autoName")
        {
            // Name and colour after the earliest Pattern clip (else audio clip source) on the track.
            const Placement *first = nullptr;
            for (const auto &c : next.placements)
                if (c.lane == a.lane && (!first || c.beat < first->beat))
                    first = &c;
            juce::String name;
            juce::uint32 colour = 0;
            if (first)
                if (const auto *p = next.pattern(first->pattern))
                    name = p->name, colour = p->colour;
            if (name.isEmpty())
            {
                const AudioPlacement *audio = nullptr;
                for (const auto &c : next.audioPlacements)
                    if (c.lane == a.lane && (!audio || c.beat < audio->beat))
                        audio = &c;
                if (audio)
                    if (const auto *ch = next.channel(audio->channel))
                        name = ch->name;
            }
            if (name.isEmpty())
                return juce::Result::fail("This track has no Pattern or audio clip to name it after");
            auto &t = entry(a.lane);
            t.name = name.trim().substring(0, 128);
            if (colour && (colour >> 24) == 255)
                t.colour = colour;
        }
        else if (id == "track.reset")
        {
            auto &t = entry(a.lane);
            t.name = {};
            t.colour = 0;
        }
        else if (id == "track.move")
        {
            const int other = a.lane + (a.value < 0 ? -1 : 1);
            if ((a.value != -1 && a.value != 1) || other < 0 || other > 127)
                return juce::Result::fail("Move a track up (-1) or down (+1) within lanes 0..127");
            remap([&](int lane) { return lane == a.lane ? other : lane == other ? a.lane : lane; });
        }
        std::erase_if(next.tracks, [](auto &t)
                      { return t.name.isEmpty() && t.colour == 0 && !t.muted && !t.groupedAbove && !t.collapsed && !t.locked && t.height == 1 && t.channel == 0 && !t.audio; });
        for (auto &t : next.tracks)
            t.groupedAbove = t.groupedAbove && t.lane > 0; // A move to the top track ungroups it.
        std::sort(next.tracks.begin(), next.tracks.end(), [](auto &x, auto &y) { return x.lane < y.lane; });
    }
    if (id == "channel.mtsEsp")
    {
        // At most one Channel is the MTS-ESP tuning source; enabling moves it here.
        auto *target = next.channel(a.channel);
        if (!target || target->audioClip)
            return missing();
        for (auto &channel : next.channels)
            channel.mtsEsp = false;
        target->mtsEsp = a.enabled;
    }
    if (id == "channel.mpe")
    {
        auto *target = next.channel(a.channel);
        if (!target || !target->instrument)
            return juce::Result::fail("MPE tuning is for plugin instrument Channels");
        target->mpe = a.enabled;
    }
    if (id == "channel.scale")
    {
        // Any-size scale mapped to consecutive keys from a root key; an empty scale clears it.
        auto *target = next.channel(a.channel);
        if (!target || target->audioClip)
            return missing();
        if (!a.scale || a.scale->size() > 128 || a.pitch < 0 || a.pitch > 127)
            return juce::Result::fail("Give 0..128 rising degree cents (last = period) and a root key 0..127");
        for (size_t i = 0; i < a.scale->size(); ++i)
            if (!std::isfinite((*a.scale)[i]) || (*a.scale)[i] <= 0 || (*a.scale)[i] > 4800 ||
                (i > 0 && (*a.scale)[i] <= (*a.scale)[i - 1]))
                return juce::Result::fail("Scale degrees must rise above 0, at most 4800 cents");
        target->scale = *a.scale;
        target->scaleRoot = a.pitch;
    }
    if (id == "channel.tuning")
    {
        auto *target = next.channel(a.channel);
        if (!target || target->audioClip)
            return missing();
        if (!a.tuning)
            return juce::Result::fail("Provide 12 cent offsets (C..B)");
        for (auto cents : *a.tuning)
            if (!std::isfinite(cents) || std::abs(cents) > 100)
                return juce::Result::fail("Tuning offsets are +/-100 cents");
        target->tuning = *a.tuning;
        target->scale.clear(); // A 12-tone table replaces a keyboard-mapped scale.
    }
    if (id == "tempo.fromAutomation")
    {
        // A freehand tempo curve: the automation clip's drawn shape (0..1) maps to
        // offset..value BPM across its placement, sampled every 1/4 beat as linear ramps
        // (collinear samples dropped). Changes inside the span are replaced; the clip stays.
        const auto clip = std::find_if(next.automation.placements.begin(), next.automation.placements.end(),
                                       [&](const auto &p) { return p.id == std::int64_t(a.target); });
        if (clip == next.automation.placements.end())
            return missing();
        const auto source = std::find_if(next.automation.sources.begin(), next.automation.sources.end(),
                                         [&](const auto &s) { return s.id == clip->source; });
        if (source == next.automation.sources.end() || source->points.empty() || !(source->length > 0))
            return juce::Result::fail("The automation clip has no drawn points");
        if (!std::isfinite(a.offset) || !std::isfinite(a.value) || a.offset < 10 || a.value > 999 ||
            a.offset >= a.value)
            return juce::Result::fail("Give the BPM range: offset (bottom) < value (top), 10..999");
        const double from = clip->beat, length = clip->length;
        auto bpmAt = [&](double t)
        {
            const double position = std::fmod(clip->offset + t, source->length);
            return a.offset + (a.value - a.offset) * juce::jlimit(0., 1., double(automationSourceValueAt(*source, position)));
        };
        std::vector<std::pair<double, double>> samples;
        for (double t = 0; t < length - 1.e-9; t += .25)
            samples.emplace_back(t, bpmAt(t));
        samples.emplace_back(length, bpmAt(std::max(0., length - 1.e-6)));
        std::vector<std::pair<double, double>> kept{samples.front()};
        for (size_t i = 1; i + 1 < samples.size(); ++i)
        {
            const auto &p = kept.back(), &q = samples[i + 1];
            const double predicted = p.second + (q.second - p.second) * (samples[i].first - p.first) / (q.first - p.first);
            if (std::abs(samples[i].second - predicted) > .01)
                kept.push_back(samples[i]);
        }
        kept.push_back(samples.back());
        std::erase_if(next.tempoChanges, [&](const auto &change)
                      { return change.beat >= from - 1.e-9 && change.beat <= from + length + 1.e-9; });
        if (next.tempoChanges.size() + kept.size() > 4096)
            return juce::Result::fail("Too many tempo changes; use a shorter clip");
        if (from > 0)
            next.tempoChanges.push_back({next.allocate(), from, kept.front().second, false});
        else
            next.tempo = juce::jlimit(20., 400., kept.front().second);
        for (size_t i = 1; i < kept.size(); ++i)
            next.tempoChanges.push_back({next.allocate(), from + kept[i].first, kept[i].second, true});
        std::sort(next.tempoChanges.begin(), next.tempoChanges.end(),
                  [](const auto &x, const auto &y) { return x.beat < y.beat; });
    }
    if (id == "tempo.curve")
    {
        // A shaped glide from the tempo at `beat` to `value` BPM over `length` beats, drawn as
        // 16 linear ramps; changes inside the span are replaced, later ones are kept.
        const double from = a.beat, to = a.beat + a.length;
        const auto shape = a.text.isEmpty() ? juce::String("linear") : a.text;
        if (!std::isfinite(from) || from < 0 || !std::isfinite(a.length) || a.length < .25 || to > 1000000 ||
            !std::isfinite(a.value) || a.value < 10 || a.value > 999)
            return juce::Result::fail("Tempo curves need beat >= 0, length >= 0.25 beats and 10..999 BPM");
        if (shape != "linear" && shape != "ease" && shape != "in" && shape != "out")
            return juce::Result::fail("Curve shapes: linear, ease, in or out");
        const double start = from > 0 ? next.songTempo().bpmAt(from) : next.tempo;
        std::erase_if(next.tempoChanges, [&](const auto &change)
                      { return change.beat >= from - 1.e-9 && change.beat <= to + 1.e-9; });
        if (next.tempoChanges.size() + 17 > 4096)
            return juce::Result::fail("Too many tempo changes");
        if (from > 0)
            next.tempoChanges.push_back({next.allocate(), from, start, false});
        else
            next.tempo = start;
        constexpr int pieces = 16;
        for (int k = 1; k <= pieces; ++k)
        {
            const double x = double(k) / pieces;
            const double y = shape == "ease" ? .5 - .5 * std::cos(juce::MathConstants<double>::pi * x)
                             : shape == "in" ? x * x
                             : shape == "out" ? 1 - (1 - x) * (1 - x)
                                              : x;
            next.tempoChanges.push_back({next.allocate(), from + a.length * x, start + (a.value - start) * y, true});
        }
        std::sort(next.tempoChanges.begin(), next.tempoChanges.end(),
                  [](const auto &x, const auto &y) { return x.beat < y.beat; });
    }
    if (id.startsWith("tempo.change."))
    {
        // value = BPM, beat = Song beat (> 0), enabled = ramp into this tempo.
        auto found = std::find_if(next.tempoChanges.begin(), next.tempoChanges.end(),
                                  [&](const auto &change) { return change.id == a.target; });
        if (id != "tempo.change.add" && found == next.tempoChanges.end())
            return missing();
        if (id != "tempo.change.delete" &&
            (!std::isfinite(a.beat) || a.beat <= 0 || a.beat > 1000000 || !std::isfinite(a.value) || a.value < 10 ||
             a.value > 999))
            return juce::Result::fail("Tempo changes need beat > 0 and 10..999 BPM");
        if (id == "tempo.change.add")
        {
            if (next.tempoChanges.size() >= 4096)
                return juce::Result::fail("Too many tempo changes");
            if (std::any_of(next.tempoChanges.begin(), next.tempoChanges.end(),
                            [&](const auto &change) { return std::abs(change.beat - a.beat) < 1.e-9; }))
                return juce::Result::fail("A tempo change already exists at that beat");
            next.tempoChanges.push_back({next.allocate(), a.beat, a.value, a.step});
        }
        else if (id == "tempo.change.set")
        {
            if (std::any_of(next.tempoChanges.begin(), next.tempoChanges.end(),
                            [&](const auto &change)
                            { return change.id != a.target && std::abs(change.beat - a.beat) < 1.e-9; }))
                return juce::Result::fail("A tempo change already exists at that beat");
            *found = {found->id, a.beat, a.value, a.step};
        }
        else
            next.tempoChanges.erase(found);
        std::sort(next.tempoChanges.begin(), next.tempoChanges.end(),
                  [](const auto &x, const auto &y) { return x.beat < y.beat; });
    }
    if (id == "pattern.marker.add" || id == "pattern.marker.rename" || id == "pattern.marker.delete")
    {
        // Pattern markers: named positions inside a Pattern (Piano roll ruler). add: beat, text;
        // rename: target, text; delete: target. One Undo step.
        auto *part = next.pattern(a.pattern);
        if (!part)
            return missing();
        auto found = std::find_if(part->markers.begin(), part->markers.end(), [&](const auto &m) { return m.id == a.target; });
        if (a.text.length() > 128)
            return juce::Result::fail("Marker names are at most 128 characters");
        if (id == "pattern.marker.add")
        {
            if (!std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000 || part->markers.size() >= 1000)
                return juce::Result::fail("patternMarker.invalid: Beat 0..1000000, at most 1000 markers");
            part->markers.push_back({next.allocate(), a.beat,
                                     a.text.isNotEmpty() ? a.text : "Marker " + juce::String(int(part->markers.size()) + 1)});
            std::stable_sort(part->markers.begin(), part->markers.end(), [](const auto &x, const auto &y) { return x.beat < y.beat; });
        }
        else if (found == part->markers.end())
            return missing();
        else if (id == "pattern.marker.rename")
            found->name = a.text;
        else
            part->markers.erase(found);
    }
    if (id == "channel.receiveMidi")
    {
        // Receive notes from: live MIDI on channel value (1..16) plays and records on this Channel
        // whatever is selected; 0 turns it off. One Channel per MIDI channel (others are cleared).
        auto *target = next.channel(a.channel);
        const int receive = int(a.value);
        if (!target)
            return juce::Result::fail("midiReceive.missingChannel: Existing Channel required");
        if (!std::isfinite(a.value) || a.value != receive || receive < 0 || receive > 16)
            return juce::Result::fail("midiReceive.invalidChannel: MIDI channel 1..16, or 0 for off");
        if (receive > 0)
            for (auto &other : next.channels)
                if (other.receiveMidiChannel == receive)
                    other.receiveMidiChannel = 0;
        target->receiveMidiChannel = receive;
    }
    if (id == "channel.articulations")
    {
        // text: "Name=pitch" entries separated by ';' or new lines; pitch is a MIDI number or a note
        // name (C5 = 60 as in the Piano roll, C0 = 0). Empty clears the map; notes keep their numbers (ignored if unmapped).
        auto *target = next.channel(a.channel);
        if (!target)
            return juce::Result::fail("expressionMap.missingChannel: Existing Channel required");
        std::vector<Articulation> map;
        for (auto entry : juce::StringArray::fromTokens(a.text.replace("\n", ";"), ";", ""))
        {
            entry = entry.trim();
            if (entry.isEmpty())
                continue;
            const auto name = entry.upToLastOccurrenceOf("=", false, false).trim();
            const auto key = entry.fromLastOccurrenceOf("=", false, false).trim();
            auto trigger = parseArticulationTrigger(key);
            if (!entry.containsChar('=') || name.isEmpty() || name.length() > 32 || name.containsChar(';') || !trigger)
                return juce::Result::fail("expressionMap.invalidEntry: Use Name=keyswitch, e.g. Legato=24, Staccato=C#2, "
                                          "Pizz=CC32:64 or Tremolo=PC5 ('" + entry + "')");
            trigger->name = name;
            map.push_back(*trigger);
        }
        if (map.size() > size_t(maximumArticulations))
            return juce::Result::fail("expressionMap.tooMany: At most 64 articulations");
        target->articulations = std::move(map);
    }
    if (id.startsWith("chord."))
    {
        auto sortChords = [&] { std::stable_sort(next.chords.begin(), next.chords.end(),
                                                  [](const auto &x, const auto &y) { return x.beat < y.beat; }); };
        auto validSymbol = [](const juce::String &text) { return !score::chordPitchClasses(text).empty(); };
        if (id == "chord.add")
        {
            if (!std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000)
                return juce::Result::fail("chord.invalidBeat: Chord beat is 0..1000000");
            if (!validSymbol(a.text))
                return juce::Result::fail("chord.invalidSymbol: Use a chord symbol such as C, Am7, F#maj7, Bb/D");
            std::erase_if(next.chords, [&](const auto &c) { return std::abs(c.beat - a.beat) < 1.e-6; });
            if (next.chords.size() >= 10000)
                return juce::Result::fail("Too many chords");
            next.chords.push_back({next.allocate(), a.beat, a.text.trim()});
            sortChords();
        }
        else if (id == "chord.set")
        {
            auto found = std::find_if(next.chords.begin(), next.chords.end(), [&](const auto &c) { return c.id == a.target; });
            if (found == next.chords.end())
                return missing();
            if (!validSymbol(a.text))
                return juce::Result::fail("chord.invalidSymbol: Use a chord symbol such as C, Am7, F#maj7, Bb/D");
            found->symbol = a.text.trim();
        }
        else if (id == "chord.delete")
        {
            const auto before = next.chords.size();
            std::erase_if(next.chords, [&](const auto &c) { return std::find(a.ids.begin(), a.ids.end(), c.id) != a.ids.end(); });
            if (a.ids.empty() || next.chords.size() + a.ids.size() != before)
                return missing();
        }
        else if (id == "chord.fill")
        {
            // text: chords separated by | or spaces ("C | Am | F | G"); one chord per bar from `beat`
            // (bar lengths follow the meter), replacing chords inside the written span. One Undo step.
            juce::StringArray symbols;
            symbols.addTokens(a.text.replace("|", " "), " ", "");
            symbols.removeEmptyStrings();
            if (symbols.isEmpty() || symbols.size() > 512)
                return juce::Result::fail("chord.invalidProgression: Write 1..512 chords, e.g. C | Am | F | G");
            for (const auto &symbol : symbols)
                if (!validSymbol(symbol))
                    return juce::Result::fail("chord.invalidSymbol: Unknown chord " + symbol);
            if (!std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000)
                return juce::Result::fail("chord.invalidBeat: Start beat is 0..1000000");
            double beat = a.beat;
            std::vector<ChordEvent> written;
            for (const auto &symbol : symbols)
            {
                written.push_back({next.allocate(), beat, symbol});
                beat += std::max(.25, next.barPosition(beat).barLength);
            }
            std::erase_if(next.chords, [&](const auto &c) { return c.beat >= a.beat - 1.e-6 && c.beat < beat - 1.e-6; });
            next.chords.insert(next.chords.end(), written.begin(), written.end());
            sortChords();
        }
    }
    if (id.startsWith("marker."))
    {
        auto found = std::find_if(next.markers.begin(), next.markers.end(),
                                  [&](const auto &marker) { return marker.id == a.target; });
        if (id != "marker.add" && id != "marker.addEvery" && id != "marker.addPair" && found == next.markers.end())
            return missing();
        if ((id == "marker.add" || id == "marker.move") && (!std::isfinite(a.beat) || a.beat < 0 || a.beat > 1000000))
            return juce::Result::fail("Marker beat is 0..1000000");
        if ((id == "marker.add" || id == "marker.rename") && a.text.length() > 128)
            return juce::Result::fail("Marker names are at most 128 characters");
        if (id == "marker.addEvery" || id == "marker.addPair")
        {
            // addEvery: a marker on every `value`-th bar (1..64) up to the Song end (meter
            // changes respected; existing marker positions skipped). addPair: markers at
            // `beat` and `beat + length` (a time selection). One Undo step.
            std::vector<double> beats;
            if (id == "marker.addPair")
            {
                if (!std::isfinite(a.length) || a.length <= 0 || a.beat + a.length > 1000000)
                    return juce::Result::fail("Select a time range first");
                beats = {a.beat, a.beat + a.length};
            }
            else
            {
                const int every = int(a.value);
                if (!std::isfinite(a.value) || every < 1 || every > 64 || double(every) != a.value)
                    return juce::Result::fail("Add a marker every 1..64 bars");
                double end = 4;
                for (const auto &p : next.placements)
                    end = std::max(end, p.beat + p.length);
                for (const auto &p : next.audioPlacements)
                    end = std::max(end, p.beat + p.duration * next.tempo / 60.);
                for (const auto &p : next.automation.placements)
                    end = std::max(end, p.beat + p.length);
                int bar = 0;
                for (double beat = 0; beat < end - 1.e-9 && beats.size() < 2000;)
                {
                    if (bar % every == 0)
                        beats.push_back(beat);
                    beat += std::max(.25, next.barPosition(beat).barLength);
                    ++bar;
                }
            }
            int added = 0;
            for (const double beat : beats)
            {
                if (std::any_of(next.markers.begin(), next.markers.end(),
                                [&](const auto &m) { return std::abs(m.beat - beat) < 1.e-6; }))
                    continue;
                if (next.markers.size() >= 10000)
                    return juce::Result::fail("Too many markers");
                const auto bar = next.barPosition(beat);
                next.markers.push_back({next.allocate(), beat, "Bar " + juce::String(bar.bar + 1)}); // 1-based like the ruler.
                ++added;
            }
            if (added == 0)
                return juce::Result::fail("Markers already exist at those positions");
            std::stable_sort(next.markers.begin(), next.markers.end(),
                             [](const auto &x, const auto &y) { return x.beat < y.beat; });
        }
        if (id == "marker.add")
        {
            if (next.markers.size() >= 10000)
                return juce::Result::fail("Too many markers");
            next.markers.push_back({next.allocate(), a.beat,
                                    a.text.isNotEmpty() ? a.text
                                                        : "Marker " + juce::String(int(next.markers.size()) + 1)});
        }
        else if (id == "marker.rename")
            found->name = a.text;
        else if (id == "marker.action")
        {
            if (!validMarkerAction(a.text))
                return juce::Result::fail("marker.invalidAction: none (empty), start, loop, skip or pause");
            if (a.text == "start")
                for (auto &other : next.markers)
                    if (other.action == "start")
                        other.action.clear(); // One Start marker per Song.
            found->action = a.text;
        }
        else if (id == "marker.move")
            found->beat = a.beat;
        else if (id == "marker.delete")
            next.markers.erase(found);
    }
    if (!next.arrangements.empty())
        arrangements::pruneStored(next); // Deleted Patterns/Channels/sources leave no stored clip behind.
    reconcileSoloChannels(canonicalSource, next);
    reconcileMixerSolo(next);
    for (auto &clip : next.audioPlacements)
        if (clip.fadeIn + clip.fadeOut > clip.duration)
        {
            const double scale = clip.duration / (clip.fadeIn + clip.fadeOut);
            clip.fadeIn *= scale;
            clip.fadeOut *= scale;
        }
    if (id.startsWith("steps.") || id == "pattern.channelLoop")
        if (auto *pattern = next.pattern(a.pattern))
            refreshAutomaticPatternLength(next, *pattern);
    if (id == "channel.delete" || id == "rack.deleteSelected" || id == "project.meter")
        for (auto &pattern : next.patterns)
            refreshAutomaticPatternLength(next, pattern);
    // Locked Playlist tracks: no clip edit may add, move, change or remove a clip on them.
    if (id.startsWith("clip.") || id.startsWith("audio.") || id == "notes.record" || id.startsWith("automation.clip"))
    {
        const auto lockedLane = [&](int lane)
        {
            const auto *before = canonicalSource.track(lane);
            const auto *after = next.track(lane);
            return (before && before->locked) || (after && after->locked);
        };
        const auto clipsOn = [&](const Project &p, int lane)
        {
            juce::StringArray rows;
            for (const auto &c : p.placements)
                if (c.lane == lane)
                    rows.add(juce::String(c.id) + "p" + juce::String(c.pattern) + "@" + juce::String(c.beat, 6) + "+" +
                             juce::String(c.length, 6) + "o" + juce::String(c.offset, 6) + (c.muted ? "m" : ""));
            for (const auto &c : p.audioPlacements)
                if (c.lane == lane)
                    rows.add(juce::String(c.id) + "a" + juce::String(c.channel) + "@" + juce::String(c.beat, 6) + "+" +
                             juce::String(c.duration, 6) + "o" + juce::String(c.offset, 6) + (c.muted ? "m" : "") + "f" +
                             juce::String(c.fadeIn, 4) + "/" + juce::String(c.fadeOut, 4));
            for (const auto &c : p.automation.placements)
                if (c.lane == lane)
                    rows.add(juce::String(c.id) + "u" + juce::String(c.source) + "@" + juce::String(c.beat, 6) + "+" +
                             juce::String(c.length, 6) + "o" + juce::String(c.offset, 6) + (c.muted ? "m" : ""));
            rows.sort(false);
            return rows.joinIntoString(";");
        };
        for (int lane = 0; lane < 128; ++lane)
            if (lockedLane(lane) && clipsOn(canonicalSource, lane) != clipsOn(next, lane))
                return juce::Result::fail("Track " + juce::String(lane + 1) + " is locked; unlock it to edit its clips");
    }
    auto status = next.validate();
    if (status.failed())
        return status;
    if (next.state().isEquivalentTo(canonicalSource.state()))
    {
        if (preparedOutput)
        {
            std::unique_ptr<PreparedControllerChange> prepared;
            if (auto result = prepareSnapshotChange(std::move(next), canonicalSource, info->name, false,
                                                    false, dirty, prepared);
                result.failed())
                return result;
            prepared->incrementRevision = false;
            prepared->clearQuantizer = false;
            prepared->commandNoOp = true;
            *preparedOutput = std::move(prepared);
            return juce::Result::ok();
        }
        if (localTakeover)
            return cancelGestureChecked();
        return juce::Result::ok();
    }
    std::unique_ptr<PreparedControllerChange> prepared;
    if (auto result = prepareSnapshotChange(
            std::move(next), capturedBaseline ? *capturedBaseline : canonicalSource, info->name,
            !continuingGesture && !withoutHistory, localTakeover, true, prepared);
        result.failed())
        return result;
    prepared->clearQuantizer = localTakeover || id != "notes.quantize";
    if (id == "plugin.effect.replace")
        mixerChainNextId = std::max(mixerChainNextId, prepared->nextProject.nextId);
    if (id == "mixer.insert.create")
        mixerInsertNextId = std::max(mixerInsertNextId, prepared->nextProject.nextId);
    // Replacement reservations survive a discarded prepared ticket or an
    // abandoned Undo/Redo branch. Failed validation/mutation never reserves.

    if (preparedOutput)
    {
        *preparedOutput = std::move(prepared);
        return juce::Result::ok(); // Prepared, NOT published or observer-delivered.
    }
    if (auto result = validatePreparedChange(*prepared); result.failed())
        return result;
    if (!commitPreparedChange(*prepared))
        return juce::Result::fail("history.stalePreparedChange: Publication refused");
    // A new quantizer baseline was fully copied before any canonical mutation.
    // Swapping null/prepared ownership here cannot allocate or destroy resources.
    if (preparedQuantizerStart)
        quantizerStart.swap(preparedQuantizerStart);
    deliverPendingNotifications();
    return juce::Result::ok();
}
} // namespace vibera
