// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ScoreTools.h"
#include "Studio.h"
#include "VideoPlayer.h"
#include "MidiScore.h"
#include "synth/SynthMcp.h"
#include "synth/SynthUserPresets.h"
#include "PluginStateEncoding.h"
#include "effects/NativeEffectsMcp.h"
#include "AutomationRecordingContracts.h"
#include "MixerInsertLifecycle.h"
#include "MixerInsertOrder.h"
#include "MixerEffectsContracts.h"
#include "MixerRouting.h"
#include "Swing.h"
#include "SwingCommands.h"
#include "Editors.h"
#include "DetachedWindow.h"
#include "InstrumentWindow.h"
#include "McpContracts.h"
#include "ChannelAppearanceCommands.h"
#include "MixerMacroContracts.h"
#include "AudioLogger.h"
#include "MixerChainWorkflowContracts.h"
#include "McpSchema.h"
#include "SamplerTempoContracts.h"
#include "PluginIoContracts.h"
#include "RackContracts.h"
#include "StepRelease.h"
#include "PatternChannelLoops.h"
#include "GrooveContracts.h"
#include "RecordingContracts.h"
#include "../tests/SyntheticDevice.h" // Existing CONTROL-only acceptance dispatch utility.
#include <stdexcept>
namespace vibera
{
namespace
{
juce::var obj()
{
    return juce::var(new juce::DynamicObject);
}
void set(juce::var &v, const char *key, juce::var value)
{
    v.getDynamicObject()->setProperty(key, std::move(value));
}
double number(const juce::var &args, const char *key, double fallback)
{
    if (!args.hasProperty(key))
        return fallback;
    auto value = args[key];
    if ((!value.isInt() && !value.isInt64() && !value.isDouble()) || !std::isfinite(double(value)))
        throw std::runtime_error("Argument must be a finite number");
    return double(value);
}
int integer(const juce::var &args, const char *key, int fallback, int low, int high)
{
    auto value = number(args, key, fallback);
    if (value < low || value > high || std::floor(value) != value)
        throw std::runtime_error("Integer argument out of range");
    return int(value);
}
Id identity(const juce::var &args, const char *key, Id fallback)
{
    auto value = number(args, key, double(fallback));
    if (value < 0 || value > 1000000000 || std::floor(value) != value)
        throw std::runtime_error("Invalid identity");
    return Id(value);
}
juce::var textResult(juce::var data)
{
    if (data.isObject() && !data.hasProperty("completionState"))
        set(data, "completionState", "completed");
    auto result = obj(), text = obj();
    set(text, "type", "text");
    set(text, "text", juce::JSON::toString(data, true));
    set(result, "content", juce::Array<juce::var>{text});
    set(result, "structuredContent", data);
    if (data["completionState"].toString() == "failed")
        set(result, "isError", true);
    auto completion = obj();
    set(completion, "state",
        data.hasProperty("completionState") ? data["completionState"] : juce::var("completed"));
    set(completion, "scope",
        "Application state; pending audio publication and asynchronous jobs are reported separately.");
    set(result, "completion", completion);
    return result;
}
bool allowed(const juce::String &name)
{
    const auto *command = mcpCommandInfo(name);
    return command && (juce::String(command->mcp) == "command" || juce::String(command->mcp) == "action");
}
juce::var nativeEditorStateSchema()
{
    return juce::JSON::parse(R"JSON({
      "type":"object","additionalProperties":false,
      "required":["instance","known","exists","visible","minimised","revision","busy","availability","nativeWidgetControl","nativeInterfaceAvailable"],
      "properties":{
        "instance":{"type":"string","description":"Worker-instance identity. Window revision and request tokens never cross instances."},
        "nativeProcessId":{"type":"integer","minimum":0,"description":"PID of this exact live prepared worker; zero if unavailable. Diagnostic ownership only, validate OS creation identity and instance again before scoped input."},
        "known":{"type":"boolean","description":"False after unavailable/dead worker; other booleans then do not prove closed."},
        "exists":{"type":"boolean"},
        "visible":{"type":"boolean","description":"Actual wrapper showing and not minimised; not OS foreground, monitor or listening proof."},
        "minimised":{"type":"boolean"},"revision":{"type":"integer","minimum":0},
        "busy":{"type":"boolean","description":"Physical pending/running editor work; completed unread results are not busy."},
        "availability":{"type":"string","enum":["unknown","available","unsupported","creation_failed","controls_only"],"description":"controls_only is an actual host-control window for an editorless plugin, never a native interface attachment."},
        "nativeWidgetControl":{"type":"boolean","const":false},
        "nativeInterfaceAvailable":{"type":["boolean","null"],"description":"True for native availability, false for known editorless/unsupported capability, null when unknown or creation failed."},
        "wrapperView":{"type":"string","enum":["plugin","settings","parameters"]},
        "wrapperReady":{"type":"boolean"},"wrapperPending":{"type":"boolean"},
        "wrapperParameter":{"type":["integer","null"],"minimum":0,"maximum":4294967295},
        "wrapperGeneration":{"type":["integer","null"],"minimum":1,"maximum":4294967295},
        "wrapperNotifications":{"type":"boolean","description":"Session-native edit notification setting; automation and saved values remain active."},
        "wrapperLastTweakedParameter":{"type":["integer","null"],"minimum":0,"maximum":4294967295},
        "wrapperLastTweakedGeneration":{"type":["integer","null"],"minimum":1,"maximum":4294967295},
        "wrapperOperation":{"type":"string","description":"Actual canonical pending job token, if present. Poll vibera_plugin action=job; admission is not completion."}
      }
    })JSON");
}
juce::var lastTweakedPluginSchema()
{
    return juce::JSON::parse(R"JSON({"type":["object","null"],"additionalProperties":false,
      "description":"Newest unambiguous completed native parameter gesture, or null. Not arbitrary native widget control. Rediscover after worker/project replacement or notification changes.",
      "required":["plugin","instance","projectIdentity","parameter","generation","serial","completedTicks","gestureValue"],
      "properties":{
        "plugin":{"type":"integer","minimum":1},"instance":{"type":"string"},
        "projectIdentity":{"type":"string"},"parameter":{"type":"integer","minimum":0,"maximum":4294967295},
        "generation":{"type":"integer","minimum":1},"serial":{"type":"integer","minimum":1},
        "completedTicks":{"type":"integer","minimum":1,"description":"Monotonic native completion stamp; ordering only, not musical time."},
        "gestureValue":{"type":"number","minimum":0,"maximum":1}}})JSON");
}
int modifiers(const juce::var &args)
{
    int flags = 0;
    for (auto pair : {std::pair{"ctrl", juce::ModifierKeys::ctrlModifier},
                      std::pair{"shift", juce::ModifierKeys::shiftModifier},
                      std::pair{"alt", juce::ModifierKeys::altModifier}})
    {
        if (args.hasProperty(pair.first) && !args[pair.first].isBool())
            throw std::runtime_error("Modifier must be boolean");
        if (bool(args[pair.first]))
            flags |= pair.second;
    }
    return flags;
}
} // namespace
void Studio::pollMcpGesture()
{
    if (!mcpGesture || mcpGesture->state != "pending")
        return;
    auto &gesture = *mcpGesture;
    if (gesture.project != controller.projectIdentity || controller.gestureOwner() != gesture.id ||
        !controller.hasGesture())
    {
        gesture.state = "cancelled";
        gesture.reason = "Local edit or project replacement superseded the gesture";
        return;
    }
    const bool selectionChanged = (gesture.domain == "stepVelocity" || gesture.domain == "stepGraph" ||
                                   gesture.domain == "stepActivation" || gesture.domain == "swing" ||
                                   gesture.domain == "samplerVoices" || gesture.domain == "samplerFilter" ||
                                   gesture.domain == "samplerModulation" || gesture.domain == "noteMute") &&
                                  (selectedPattern != gesture.pattern || selectedChannel != gesture.channel);
    const bool recording = (recordArmed || midiTaking) && gesture.domain == "document";
    if (selectionChanged || recording ||
        juce::Time::getMillisecondCounterHiRes() - gesture.lastActivity >= gesture.leaseMs)
    {
        const auto result = controller.cancelGestureChecked(gesture.id);
        if (result.failed())
        {
            gesture.reason = result.getErrorMessage();
            return; // Owned baseline remains pending; never acknowledge a failed cancel.
        }
        gesture.state = "cancelled";
        gesture.reason = selectionChanged ? "Selection changed; starting project restored"
                         : recording      ? "Local recording superseded the gesture"
                                          : "Idle lease expired; starting project restored";
    }
}
juce::var Studio::mcpAppearanceOperation(const juce::var &args)
{
    // Same theme calls as Settings > Appearance: preset, font, single colours, reset, Save.
    const auto action = args["action"].toString();
    if (action == "set" || action == "reset")
    {
        if (action == "reset")
            resetTheme();
        if (args.hasProperty("preset"))
        {
            const auto name = args["preset"].toString();
            if (name == "default")
            {
                const auto kept = uiFont;
                resetTheme();
                uiFont = kept;
            }
            else
            {
                const auto found = std::find_if(themePresets.begin(), themePresets.end(),
                                                 [&](const auto &p) { return name == p.name; });
                if (found == themePresets.end())
                    throw McpFailure("invalid_arguments", "Unknown preset; see state.presets");
                applyThemePreset(*found);
            }
        }
        if (args.hasProperty("font"))
        {
            const auto font = args["font"].toString();
            if (font != defaultUiFont && !juce::Font::findAllTypefaceNames().contains(font))
                throw McpFailure("invalid_arguments", "The font is not installed");
            uiFont = font;
        }
        if (const auto *colours = args["colours"].getDynamicObject())
        {
            std::vector<std::pair<juce::Colour *, juce::Colour>> next;
            for (const auto &entry : colours->getProperties())
            {
                const auto role = std::find_if(themeRoles.begin(), themeRoles.end(),
                                                [&](const auto &r) { return entry.name.toString() == r.name; });
                const auto text = entry.value.toString();
                if (role == themeRoles.end())
                    throw McpFailure("invalid_arguments", "Unknown colour role " + entry.name.toString());
                if (text.length() != 8 || !text.containsOnly("0123456789abcdefABCDEF"))
                    throw McpFailure("invalid_arguments", "Colours are 8 hex digits AARRGGBB");
                next.push_back({role->colour, juce::Colour::fromString(text)});
            }
            for (const auto &[colour, value] : next)
                *colour = value;
        }
        applyAppearance();
        if (bool(args["save"]))
            if (auto saved = saveTheme(appearanceFile()); saved.failed())
                throw McpFailure("appearance.saveFailed", saved.getErrorMessage());
    }
    else if (action != "state")
        throw McpFailure("invalid_arguments", "Unknown appearance action");
    auto result = obj(), colours = obj();
    for (const auto &role : themeRoles)
        set(colours, role.name, role.colour->toString());
    juce::Array<juce::var> presets{"default"};
    for (const auto &p : themePresets)
        presets.add(p.name);
    set(result, "completionState", "completed");
    set(result, "font", uiFont);
    set(result, "colours", colours);
    set(result, "presets", presets);
    set(result, "saved", appearanceFile().existsAsFile());
    return result;
}
juce::var Studio::mcpTools()
{
    auto tools = juce::JSON::parse(R"JSON([
      {"name":"vibera_state","description":"Read bounded project/editor/transport state, without plugin state or sample paths.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
      {"name":"vibera_commands","description":"Discover registered operations, explicit mappings/gaps and optional typed argument contracts. Paginated; includeSchemas=true with a command ID retrieves its contract.","inputSchema":{"type":"object","properties":{"command":{"type":"string"},"includeSchemas":{"type":"boolean","default":false},"offset":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":64,"default":32}},"additionalProperties":false}},
      {"name":"vibera_command","description":"Execute an undoable document edit, undo or redo on this owned instance. Optional revision rejects stale edits.","inputSchema":{"type":"object","properties":{"command":{"type":"string"},"args":{"type":"object"},"revision":{"type":"integer","minimum":0}},"required":["command"],"additionalProperties":false}},
      {"name":"vibera_select","description":"Select existing Pattern/Channel/editor scope or an already loaded project plugin UI. Plugin requests are separate from Pattern/Channel selection; no discovery, plugin loading, parameter writes or filesystem access.","inputSchema":{"type":"object","properties":{"pattern":{"type":"integer"},"channel":{"type":"integer"},"scope":{"type":"integer","minimum":0,"maximum":8},"plugin":{"type":"integer","minimum":1,"maximum":1000000000},"pluginView":{"type":"string","enum":["controls","native","closeNative"]},"parameter":{"type":"integer","minimum":0,"maximum":4294967295},"lastTweaked":{"type":"boolean"},"instance":{"type":"string","maxLength":100}},"additionalProperties":false}},
      {"name":"vibera_transport","description":"Play, stop, seek, set whole-Pattern/Song looping or switch mode through shared application actions. previewNote (stopped transport only) plays one note on a Channel through every current setting, like the sampler Preview processed button; pitch defaults to the sampler root (60 when Auto). State is not listening evidence.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["play","stop","seek","pattern","song","loop","previewNote","jump","jumpQuantize","guide","midiInput","previewSample","drumPad","typing"]},"typingRoot":{"type":"integer","minimum":2,"maximum":7,"description":"Only action typing: the typing keyboard's lowest C octave (C2..C7), like the Typing keys right-click menu; state.typingKeys.root (MIDI note)."},"typingVelocity":{"type":"integer","minimum":1,"maximum":127,"description":"Only action typing: typing keyboard note velocity; state.typingKeys.velocity."},"pad":{"type":"integer","minimum":1,"maximum":16,"description":"Only action drumPad: the Drum Pads pad (1 bottom-left; pads play the first 16 sampler Channels). on:true (default) selects its Channel and holds its root key on the live input, like striking the pad (heard in playback, captured by recording); on:false releases it. velocity 0..1 (default 0.85). Read back state.drumPads."},"on":{"type":"boolean","default":true},"velocity":{"type":"number","minimum":0,"maximum":1},"device":{"type":"string","maxLength":1024,"description":"Only action midiInput: open this MIDI input (an identifier from state.midiInput.devices; empty closes the input), like the Settings > MIDI device list. Every midiInput call rescans the inputs (like the page's Rescan button): call it with no fields first, then read state.midiInput.devices. Read back state.midiInput.device and .deviceStatus."},"countInBars":{"type":"integer","minimum":0,"maximum":2,"description":"Only action guide: recording count-in bars (0 off). Read back as state.countInBars."},"sound":{"type":"integer","minimum":0,"maximum":2,"description":"Only action guide: metronome / count-in sound (0 Click, 1 Wood, 2 Beep); state.guideSound."},"gain":{"type":"number","minimum":0,"maximum":1,"description":"Only action guide: metronome / count-in volume; state.guideGain."},"metronome":{"type":"boolean","description":"Only action guide: metronome on or off; state.metronome."},"midiChannel":{"type":"integer","minimum":0,"maximum":16,"description":"Only action midiInput: MIDI input channel filter (0 all channels, 1..16); state.midiInput.channel. Refused while a take is active."},"quantizeStart":{"type":"boolean","description":"Only action midiInput: quantize recorded note starts; state.midiInput.quantizeStart."},"quantizeEnd":{"type":"boolean","description":"Only action midiInput: quantize recorded note ends; state.midiInput.quantizeEnd."},"quantize":{"type":"integer","enum":[0,-1,1,2,4,8],"description":"Only action jumpQuantize: Quantize song jumps (0 off, -1 next beat, 1/2/4/8 next bar group). action jump moves the playhead to beat now, or at that boundary while the Song plays; read state.pendingSongJump."},"channel":{"type":"integer","minimum":1},"pitch":{"type":"integer","minimum":0,"maximum":127},"seconds":{"type":"number","minimum":0.05,"maximum":4,"default":1.5},"beat":{"type":"number","minimum":0},"enabled":{"type":"boolean","default":true,"description":"For action loop: idempotently enable/disable entire current Pattern or Song looping. No recording take may be active when changing it."},"range":{"type":["array","null"],"items":{"type":"number","minimum":0},"minItems":2,"maxItems":2,"description":"For action loop: set the Playlist time selection [startBeat, endBeat] (null clears it). In Song mode looping plays only the selection; it is also the audio punch range."}},"required":["action"],"additionalProperties":false}},
      {"name":"vibera_editor","description":"Show, hide or maximize a native JUCE editor: 0 Rack, 1 Piano, 2 Playlist, 3 Mixer.","inputSchema":{"type":"object","properties":{"editor":{"type":"integer","minimum":0,"maximum":4,"description":"0 Rack, 1 Piano, 2 Playlist, 3 Mixer, 4 Browser (detach/dock/move/layout only)."},"action":{"type":"string","enum":["show","hide","maximize","detach","dock","grouping","snap","stamp","laneTarget","guide","move","layout","browserFolder","browserForget","browserTagFilter","browserSetTags","browserPreviewSeek","browserTagSearch","scroll"]},"vertical":{"type":"boolean","default":false,"description":"Only action scroll: the vertical scroll bar (default horizontal)."},"start":{"type":"number","minimum":0,"description":"Only action scroll (editors 0..3): move the editor's scroll bar thumb to start, like dragging it; units as in state.editorViews[editor].horizontal / .vertical = [start, span, domain] (beats, steps, rows or pixels). Clamped to the domain."},"span":{"type":"number","exclusiveMinimum":0,"description":"Only action scroll: also zoom to this visible span where the bar zooms (state.editorViews[editor].horizontalZoom / .verticalZoom); ignored elsewhere."},"text":{"type":"string","maxLength":64,"description":"Only editor 4 browserTagSearch: the text typed in the tag list's search box (empty shows all tags); state.browser.tagSearch and .visibleTags."},"tag":{"type":"string","maxLength":64,"description":"Only editor 4 browserTagFilter: select or clear this tag in the Browser tag filter, like clicking it in the tag list (state.browser.tagFilter; state.browser.tags lists the known tags)."},"tags":{"type":"array","maxItems":64,"items":{"type":"string","maxLength":64},"description":"Only editor 4 browserSetTags: replace the tags of the file at path, like the Edit tags dialog (empty clears them)."},"position":{"type":"number","minimum":0,"maximum":1,"description":"Only editor 4 browserPreviewSeek: restart the current sample preview at this fraction, like Shift+click on the preview waveform (state.browser.previewPosition)."},"path":{"type":"string","maxLength":32767,"description":"Only editor 4 browserFolder (open this folder in the Browser; remember adds it to the saved folders, default true) / browserForget (remove a saved folder; files are not touched). Read back state.browser.folder and .libraries."},"remember":{"type":"boolean","default":true},"bounds":{"type":"array","minItems":4,"maxItems":4,"items":{"type":"integer"},"description":"Only action move: [x, y, width, height] in the workspace, like dragging the window header; read back as state.editorBounds."},"browserWidth":{"type":"number","description":"Only action layout: Browser width 180..360 (clamped like the splitter); state.workspaceLayout."},"playlistRatio":{"type":"number","description":"Only action layout: Channel Rack share of the left column 0.12..0.8."},"rackWidth":{"type":"number","description":"Only action layout: left column width 330..900."},"mixerHeight":{"type":"number","description":"Only action layout: Mixer height 168..360."},"coProducerWidth":{"type":"number","description":"Only action layout: CoProducer dock width 340..760."},"scale":{"type":"string","description":"Only action guide: a scale (major, minor, dorian, ...) or, with maqam:true, a maqam (rast, bayati, ...); empty clears the guide."},"root":{"type":"integer","minimum":0,"maximum":11,"description":"Only action guide: root pitch class, 0 = C."},"maqam":{"type":"boolean","description":"Only action guide: scale names a maqam."},"snapIncoming":{"type":"boolean","description":"Only action guide: snap incoming MIDI (hardware and typing keyboard) to the guide before it plays and records; read back as state.pianoGuide."},"target":{"type":"string","enum":["velocity","release","pan","modX","modY","fine"],"description":"Only editor1/action laneTarget: the note property the Piano roll event lane shows and paints (notes.velocity args.text). Read back as state.pianoLaneTarget."},"chord":{"type":"string","description":"Only action stamp: a Chordify shape name (e.g. major, minor) so drawing a Piano roll note places that chord; empty turns the stamp off. Read back as state.pianoStamp."},"enabled":{"type":"boolean","default":true,"description":"Only editor2/action grouping: temporarily enable/disable group targeting; retain membership. Refused during active gestures."},"snap":{"type":"number","minimum":0,"maximum":32,"description":"Only action snap: the shared Piano roll/Playlist grid in beats (0 = none; e.g. 0.25 step, 1/3 triplet, 4 bar). Read back as state.snap."},"line":{"type":"boolean","description":"Only action snap: true snaps pointer edits to the visible grid lines (zoom dependent) instead of snap; read back as state.snapLine."},"markers":{"type":"boolean","description":"Only action snap: true snaps Playlist positions to markers (distances to bars); read back as state.snapMarkers."}},"required":["editor","action"],"additionalProperties":false}},
      {"name":"vibera_update","description":"In-app updates from the project's GitHub Releases. status reads the phase (idle, checking, upToDate, available, downloading, ready, installing, failed), the current version, the newest available release with its notes and installable asset, download progress and the update settings. check starts a background check (poll status; completes as upToDate, available or failed) without opening dialogs. Downloading and installing need the user's approval in the app (Tools > Check for updates), so they are not MCP actions.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","check","settings","showOffer","offerInstall"]},"autoCheck":{"type":"boolean","description":"settings: check automatically on start (daily)."},"includePreview":{"type":"boolean","description":"settings: include preview releases. settings changes only the given fields (Tools > Check for updates menu). showOffer opens the update offer window for the user when a release is available; offerInstall opens the install confirmation when a download is ready. Both only show the same approval dialogs as the menu: the user decides in the app."}},"required":["action"],"additionalProperties":false}},
      {"name":"vibera_snapshot","description":"Capture this instance as a bounded JUCE component PNG including the header. With channel: that local Channel's settings window instead (opened if needed) (page 0 Sample, 1 Envelope, 2 Miscellaneous). Not native PrintWindow, physical input or listening evidence.","inputSchema":{"type":"object","properties":{"channel":{"type":"integer","minimum":1},"page":{"type":"integer","minimum":0,"maximum":2}},"additionalProperties":false}},
      {"name":"vibera_key","description":"Run an allowed contextual shortcut through the real JUCE handler. File/dialog/macro commands and held typing-performance input are excluded. Not OS keyboard input.","inputSchema":{"type":"object","properties":{"key":{"type":"integer","minimum":1,"maximum":65535},"ctrl":{"type":"boolean"},"shift":{"type":"boolean"},"alt":{"type":"boolean"}},"required":["key"],"additionalProperties":false}},
      {"name":"vibera_pointer","description":"Run paired down/drag/up or cancel through actual editor/child mouse handlers, or one wheel step (phase wheel, deltaY; positive is up/away). Logical editor pixels from state; left/right/middle/x1 buttons. Not global or physical mouse input.","inputSchema":{"type":"object","properties":{"editor":{"type":"integer","minimum":0,"maximum":3},"phase":{"type":"string","enum":["down","drag","up","cancel","wheel"]},"deltaY":{"type":"number","minimum":-4,"maximum":4},"x":{"type":"number"},"y":{"type":"number"},"button":{"type":"string","enum":["left","right","middle","x1"]},"clicks":{"type":"integer","minimum":1,"maximum":3},"ctrl":{"type":"boolean"},"shift":{"type":"boolean"},"alt":{"type":"boolean"}},"required":["editor","phase"],"additionalProperties":false}}
      ,{"name":"vibera_controller","description":"Production state/setBinding use the same validated Settings mapping service. Full binding edits save atomically before metadata publication, never Project Undo or MIDI port reopen. Selected/fixed/banked source and explicit mixer.send destination are preserved; invocation validates actual project targets. learn {index} arms MIDI Learn for an existing mapping row like the Learn button (the next control moved on the control port is bound; read controllerLearning), cancel disarms it. preset with replace:true loads the Launchkey MK3 mappings instead of the current ones and saves them (the confirmed Load preset of the page). ports {input, output} rescans the MIDI ports and opens the control ports by identifier from controllerPorts.inputs / .outputs (empty = off; the lists are filled by a ports call, never by state), refused when input is the performance MIDI input; connectLaunchkey finds and opens the Launchkey MK3 DAW ports like the Connect button. Port actions open hardware MIDI ports. Configure/inject remain owned --mcp-test diagnostics only; no production MIDI injection or filesystem arguments.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["state","setBinding","removeBinding","preset","configure","learn","cancel","inject","ports","connectLaunchkey"]},"replace":{"type":"boolean","description":"preset: must be true; the current mappings are replaced."},"input":{"type":"string","maxLength":1024,"description":"ports: control input identifier (controllerPorts.inputs), empty = off."},"output":{"type":"string","maxLength":1024,"description":"ports: feedback output identifier (controllerPorts.outputs), empty = off."},"profile":{"type":"object"},"index":{"type":"integer","minimum":0,"maximum":511,"description":"setBinding: existing row or exact append(size), no gaps; at most512 mappings. removeBinding: the existing row to delete (later rows move up)."},"port":{"type":"integer","minimum":0,"maximum":1},"kind":{"type":"integer","minimum":0,"maximum":6},"channel":{"type":"integer","minimum":0,"maximum":16},"number":{"type":"integer","minimum":0,"maximum":127},"value":{"type":"integer","minimum":0,"maximum":16383},"binding":{"type":"object","description":"Complete mapping; authoritative schema supplied from MidiController validator."},"expectedProfile":{"type":"string","minLength":64,"maxLength":64,"pattern":"^[0-9a-f]{64}$","description":"Required for setBinding/removeBinding. Exact controllerProfileIdentity from state; binds runtime mappings and available saved bytes. Re-read after any mapping/bank/profile change."}},"required":["action"],"additionalProperties":false}}
    ])JSON");
    const auto additions = juce::JSON::parse(R"JSON([
      {"name":"vibera_picker","description":"Semantic Playlist Picker selection, filtering, navigation and source placement. Uses the same component model/canonical commands as UI, without mouse simulation.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","type","filter","groupFilter","select","place","width","visible","openRack","scroll"]},"kind":{"type":"string","enum":["pattern","audio","automation"]},"text":{"type":"string","maxLength":128},"ids":{"type":"array","maxItems":128,"uniqueItems":true,"items":{"type":"integer","minimum":1}},"beat":{"type":"number","minimum":0,"maximum":1000000,"default":0},"lane":{"type":"integer","minimum":0,"maximum":127,"default":0},"width":{"type":"integer","minimum":120,"maximum":280},"scrollY":{"type":"integer","minimum":0,"description":"Logical vertical pixels; maximumScrollY is returned in state."},"visible":{"type":"boolean"},"revision":{"type":"integer","minimum":0}},"required":["action"]}},
      {"name":"vibera_project","description":"Shared application new/open/save/export and asynchronous job status. Absolute paths; existing outputs require overwrite=true, dirty replacement requires discardChanges=true. Save/export returns pending until job poll is completed/failed. exportMidi snapshots the selected Piano Roll Channel part at 960 PPQ, encodes Shift and Repeat timing once but remains unswung and uncropped, omits muted notes and per-note Pan/Fine Pitch/Mod X/Y (MIDI1 score cannot retain them). Color groups encode MIDI channels1..16. The Channel's CC, pitch bend, channel pressure and program changes are exported at their beats. Silent Slide events are omitted; per-note Porta is not emulated. Successful export reports expressionNotesOmitted as the union of unsupported occurrences plus midiExpressionNotesOmitted/midiSlideNotesOmitted/midiPortamentoNotesOmitted details; pending/failed counts remain null. Project/WAV preserve expression. Reports actual job outcome. Song export 48kHz/Song/cut-tail matches UI; the path extension picks the format: .wav (24-bit), .flac (24-bit lossless), .ogg (Vorbis 256 kbps) or .mp3 (320 kbps through a user-installed LAME: lame.exe on PATH or VIBERA_LAME_EXECUTABLE; the job fails truthfully without it), encoded after rendering. stems=true exports one WAV per Mixer insert fed by a Channel into the folder path. export with pattern=<stable Pattern ID> renders only that Pattern once from beat0 (no Playlist clips/automation clips). savePreset/loadPreset take channel and an absolute .vibch path: savePreset captures fresh native state for instrument Channels before writing (job may be pending); loadPreset applies the file as one Undo step (sample found beside the preset when moved). With pattern and placeClip=true the path is ignored: the WAV goes to the project's Rendered folder (Documents/VibeRa Studio/Rendered when unsaved) and, after the job completes, is imported as a Playlist audio clip at the Pattern's first placement on the first free lane (its own Undo step; read clips to confirm).","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","new","open","save","diagnostics","loudness","saveTemplate","templates","effectImpulse","effectSmartDisable","saveTrackState","openTrackState","exportTracks","exportTrackGroups","export","exportMidi","job","savePreset","loadPreset","stretchClip","warpClip","editClip","sliceChannel","video","perform","importMidi","missingMedia","relinkMedia","alignClip","cancelExport","vocalPitch","vocalPitchApply","audioLogger","audioLoggerSave","envelopePresets","saveEnvelopePreset","loadEnvelopePreset"]},"name":{"type":"string","maxLength":64,"description":"saveEnvelopePreset/loadEnvelopePreset: preset name. Save stores the channel's volume envelope (on, attack, decay, sustain, release); load applies it as one Undo step (channel.sampler.envelopeApply). envelopePresets lists the names."},"minutes":{"type":"number","minimum":0.2,"maximum":5,"description":"audioLogger: minutes of master output to keep (default 2; RAM ~23 MB per minute at 48 kHz). enabled:false turns it off. Needs an audio device; offline exports are not logged. audioLoggerSave writes the last seconds (0 = all held) to the absolute .wav path (24-bit stereo). Read state.audioLogger."},"seconds":{"type":"number","minimum":0,"maximum":300},"shifts":{"type":"array","maxItems":4096,"items":{"type":"object","additionalProperties":false,"required":["note"],"properties":{"note":{"type":"integer","minimum":0,"description":"Note index from vocalPitch."},"semitones":{"type":"number","minimum":-24,"maximum":24},"formant":{"type":"number","minimum":-12,"maximum":12,"description":"Formant shift in semitones (voice character)."},"time":{"type":"number","minimum":-1,"maximum":1,"description":"Move the note in time by seconds; the gaps around it stretch, the clip length stays."}}},"description":"vocalPitchApply: move these notes by semitones (fractions allowed)."},"preserveFormants":{"type":"boolean","default":true,"description":"vocalPitchApply: keep the voice character when notes move (formant correction)."},"editor":{"type":"boolean","default":false,"description":"vocalPitch: also open the graphical Vocal pitch editor window for the clip."},"snap":{"type":"number","minimum":0,"maximum":1,"description":"vocalPitchApply: also pull every other note this fraction of the way to its nearest semitone (1 = fully in tune). vocalPitch analyses the Audio Clip clip (monophonic voice or instrument, up to ten minutes) into notes with start/end seconds and median pitch, plus a 10 ms pitch curve; vocalPitchApply renders a new source with each note transposed (length kept, one Undo) as a job to poll."},"track":{"type":"integer","minimum":1,"maximum":256,"description":"importMidi: only this 1-based MIDI track (default all)."},"blend":{"type":"boolean","default":false,"description":"importMidi: keep the Channel's existing notes and controllers (default replaces them)."},"realign":{"type":"boolean","default":false,"description":"importMidi: move the first note to beat 0."},"midiChannels":{"type":"array","minItems":1,"maxItems":16,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":16},"description":"importMidi: only events on these MIDI channels (default all), like the dialog's channel tiles."},"split":{"type":"boolean","default":false,"description":"importMidi: one new Channel per MIDI track with notes, each a settings clone of the target Channel named after its track (blend is ignored)."},"useTempo":{"type":"boolean","default":false,"description":"importMidi: also replace the project tempo map and time signature with the file's (same Undo step). Notes, CC, pitch bend, channel pressure and program changes go into the Pattern (default selected) for the Channel (default selected); program changes reach plugin instruments in playback."},"on":{"type":"boolean","default":true,"description":"perform: launch (true) or stop (false) the Pattern. Launched Patterns are placed on the next bar on their own lane (64+) and repeat until stopped; stopping the transport ends the performance."},"close":{"type":"boolean","description":"video: close the player."},"offset":{"type":"number","description":"video: seconds of video at Song time zero."},"sensitivity":{"type":"number","minimum":0,"maximum":1,"description":"sliceChannel: onset sensitivity (higher finds more slices, at most 16). Creates one sampler Channel per slice and a Pattern replaying them (one Undo)."},"edit":{"type":"string","enum":["reverse","normalize","gain","pitch","consolidate"],"description":"editClip: offline edit into a new source file (one Undo). consolidate bakes the clip window and its fades; pitch shifts by value semitones keeping the length."},"value":{"type":"number","description":"editClip: normalize target dBFS (-48..0, default -0.3 in the UI), gain dB (+/-48) or pitch semitones (-24..24, nonzero)."},"clip":{"type":"integer","minimum":1,"description":"stretchClip/warpClip/alignClip: Audio Clip placement ID."},"bpm":{"type":"number","minimum":10,"maximum":522,"description":"warpClip: tempo the clip was played at (default the base tempo). Each source beat is rendered onto the matching Song beat from the clip start, following tempo jumps and ramps (pitch kept, new file, one Undo)."},"ratio":{"type":"number","minimum":0.25,"maximum":4,"description":"stretchClip: new length / old length (2 = half speed), pitch kept."},"beats":{"type":"number","exclusiveMinimum":0,"description":"stretchClip: fit the clip to this many beats instead of a ratio. alignClip: grid in beats (default 0.25); the clip and its take group move so the first transient lands on the nearest grid line (one Undo)."},"channel":{"type":"integer","minimum":1},"path":{"type":"string","maxLength":32767},"operation":{"type":"string","maxLength":100},"mixer":{"type":"integer","minimum":0,"description":"saveTrackState/openTrackState: the Mixer track; path is an absolute .vibfx directly in the configured chain preset directory. Save writes the effect chain plus the track controls; Open applies the chain, then the controls (one Undo step each)."},"enabled":{"type":"boolean","default":true,"description":"effectSmartDisable: turn smart disable on (true) or off for the VibeRa effect in plugin. When on, after a second of silent input and output the effect stops processing until input returns (saves CPU; tails finish first). The effect restarts with the new state; read effects[].smartDisable in vibera_mixer state."},"plugin":{"type":"integer","minimum":1,"description":"effectImpulse/effectSmartDisable: the effect's plugin ID; for effectImpulse the Convolution Reverb's (vibera_plugin list); path is the absolute WAV/AIFF/FLAC impulse. The job completes when the reload is requested; read the effect state (e.g. after save) to confirm."},"template":{"type":"string","maxLength":64,"default":"empty","description":"new: empty, demo, or the name of a user project template (listed by action templates); saveTemplate: the template name to write (overwrites one of the same name)."},"discardChanges":{"type":"boolean","default":false},"overwrite":{"type":"boolean","default":false,"description":"loudness reads the absolute audio path (WAV/FLAC/AIFF/OGG, up to 30 min) and returns integratedLufs, shortTermMaxLufs, momentaryMaxLufs, loudnessRangeLu, truePeakDbtp and samplePeakDbfs (ITU-R BS.1770 / EBU R128; null for silence). A single-file export also shows these in the status. diagnostics: replace an existing report. diagnostics writes a shareable text report (version, OS, audio device, plugin workers, recent errors) to the absolute .txt path with the user folder and name replaced, and returns it as report."},"pattern":{"type":"integer","minimum":1},"placeClip":{"type":"boolean","default":false},"sampleRate":{"type":"integer","enum":[44100,48000],"description":"export: render rate (kept for later exports; default 48000)."},"outputChannels":{"type":"string","enum":["stereo","mono","left","right"],"description":"export: WAV channels (kept for later exports; default stereo): mono merges L+R at half level, left/right keep one side."},"dither":{"type":"boolean","description":"export: TPDF dither at the output LSB for 16/24-bit WAV (kept for later exports; default off)."},"tail":{"type":"string","enum":["cut","leave","wrap"],"description":"export: what happens after the Song end (kept for later exports; default cut): cut stops there, leave keeps effect tails until silence (at most 8 s), wrap mixes the tails onto the start for seamless loops."},"bitDepth":{"type":"integer","enum":[16,24,32],"description":"export: WAV bits, 32 = float (kept for later exports; default 24). FLAC uses 24-bit, OGG/MP3 their codec."},"stems":{"type":"boolean","default":false,"description":"export only: path is a folder; one WAV per Mixer insert fed by a Channel ('NN Name.wav'), each rendered with that insert soloed with its connections."},"armedOnly":{"type":"boolean","default":false,"description":"export with stems=true: render only the Mixer tracks armed for rendering (vibera_command mixer.renderArm toggles the selected track; state.mixerRenderArmed lists them)."}},"required":["action"]}},
      {"name":"vibera_batch","description":"Execute 1-64 canonical document commands in one undoable transaction. Validation failure rolls back the full batch. No file/runtime/UI actions or pointer simulation.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"commands":{"type":"array","maxItems":64,"items":{"type":"object","additionalProperties":false,"properties":{"command":{"type":"string"},"args":{"type":"object"}},"required":["command"]}},"revision":{"type":"integer","minimum":0}},"required":["commands"]}},
      {"name":"vibera_mixer","description":"Select an existing Mixer source insert through the shared UI application command, inspect routes, availability and controls, or clear transient per-side sample-overload indications. Selection is workspace state, not project Undo. Edit routes with canonical mixer.route/mixer.routeOnly/mixer.send. Native third-party UI is not exposed. browseEffects navigates the actual Settings Plugins browser for an exact existing insert; scrollEffects scrolls ordered slots. Neither inserts or opens a plugin.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","select","resetOverload","browseEffects","scrollEffects"]},"insert":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Existing Mixer insert index, required for select/resetOverload/browseEffects. browseEffects opens Settings Plugins for this exact insert without inserting a plugin; read effectsBrowser for actual navigation state. Master0 allowed. Reset requires prepared meter, clears sample-overload latches through its returned completed-block fence; in-flight/new overload can re-latch. Transient/no Undo, project change or recording interruption."},"offset":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required for scrollEffects; zero-based first visible slot, at most effectsPane.maximumOffset for current insert. Navigation only; no Undo."}},"required":["action"]}},
      {"name":"vibera_inspect","description":"Paginated canonical project state, using stable IDs. Notes and noteOccurrences use the supplied or active Pattern. noteOccurrences exposes raw sources plus non-persistent Channel-loop appearances with real sourceId, iteration and display geometry; edits always target sourceId, never display beat or iteration. Does not expose opaque plugin state blobs.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"entity":{"type":"string","enum":["patterns","notes","noteOccurrences","channels","clips","mixer","clipGroups","clipGroupMembers","markers","chords","tracks","meters","tempo","controllers","arrangements","automationSources","automationPoints","automationLinks","automationInitialValues","modulators","macros","scheduledNotes","scheduledControls"]},"group":{"type":"integer","minimum":1,"description":"Required for clipGroupMembers; paginated stable member IDs."},"source":{"type":"integer","minimum":1,"maximum":1000000000,"description":"Required only for automationPoints/automationLinks; stable Automation source ID, not placement."},"pattern":{"type":"integer","minimum":1},"offset":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":512,"default":128}},"required":["entity"]}}
    ])JSON");
    tools.getArray()->add(mixerMacrosTool());
    tools.getArray()->add(juce::JSON::parse(
        R"JSON({"name":"vibera_appearance","description":"Interface appearance, like Settings > Appearance: state lists the font, every colour role (AARRGGBB) and the built-in presets. set applies preset (default or a listed palette; keeps the font), font (an installed typeface) and colours {role: AARRGGBB} live; save:true also keeps it after restart. reset restores the default palette and font. Not Undo steps; not project data.","inputSchema":{"type":"object","additionalProperties":false,"required":["action"],"properties":{"action":{"type":"string","enum":["state","set","reset"]},"preset":{"type":"string","maxLength":64},"font":{"type":"string","maxLength":128},"colours":{"type":"object","additionalProperties":{"type":"string","pattern":"^[0-9a-fA-F]{8}$"}},"save":{"type":"boolean","default":false}}}})JSON"));
    tools.getArray()->add(samplerTempoTool());
    tools.getArray()->add(automationRecordingTool());
    tools.getArray()->add(juce::JSON::parse(
        R"JSON({"name":"vibera_effects","description":"Discover individually insertable built-in VibeRa effects. Always available without installation or scanning. Get an exact descriptor then use canonical plugin.effect.insert/replace, existing Mixer slot controls and vibera_plugin parameter/history services. Each instance has one fixed effect type.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["list","descriptor"]},"type":{"type":"string","maxLength":64,"description":"Exact effect type slug from list; required only for descriptor."}},"required":["action"]}})JSON"));
    tools.getArray()->add(synth::synthToolDefinition());
    tools.getArray()->add(pianoNoteModesToolDefinition());
    const auto interactionTools = juce::JSON::parse(R"JSON([
      {"name":"vibera_selection","description":"Semantic note/Playlist clip selection through the shared UI selection model. Stable IDs in the currently selected Pattern; no document change or Undo entry. Range selection uses note starts or overlap in quarter-note beats, inclusive MIDI pitches or zero-based lanes.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","set","all","clear","range","lasso"]},"points":{"type":"array","minItems":3,"maxItems":4096,"items":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},"description":"Only action lasso (notes): the Magic lasso path as [beat, pitch] points; notes whose start lies inside are selected."},"target":{"type":"string","enum":["notes","clips"]},"mode":{"type":"string","enum":["replace","add","remove","toggle"],"default":"replace"},"ids":{"type":"array","maxItems":10000,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000}},"start":{"type":"number","minimum":0,"default":0},"end":{"type":"number","exclusiveMinimum":0},"low":{"type":"integer","minimum":0,"maximum":127,"default":0,"description":"Inclusive MIDI pitch for notes or lane index for clips."},"high":{"type":"integer","minimum":0,"maximum":127,"default":127},"criterion":{"type":"string","enum":["starts","overlaps"],"default":"starts"},"allChannels":{"type":"boolean","default":false},"loopPreview":{"type":"boolean","default":false,"description":"Only notes/range. Apply the starts or overlaps criterion to the Piano Roll loop occurrence view and select distinct real source IDs, including raw dormant notes. Never materializes copies or changes musical state."},"revision":{"type":"integer","minimum":0}},"required":["action","target"]}},
      {"name":"vibera_gesture","description":"Leased continuous canonical document edits, sharing the UI gesture/Undo implementation. Begin returns an ownership token; update applies one command live; commit creates one Undo step; cancel restores the exact starting project/dirty state. Local UI edits preempt and cancel the remote gesture. Only state/inspect/discovery and this tool are admitted while owned. Poll state to verify cancellation or expiry; no mouse simulation.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["begin","update","commit","cancel","state"]},"operation":{"type":"string","maxLength":100},"label":{"type":"string","maxLength":128,"default":"AI continuous edit"},"command":{"type":"string"},"args":{"type":"object"},"domain":{"type":"string","enum":["document","stepVelocity","stepGraph","stepActivation","swing","samplerVoices","samplerFilter","samplerModulation","noteMute","mixerSend","mixerControl","mixerEffects"],"default":"document","description":"stepVelocity leases only steps.velocity on the active Pattern/Channel, so a held MIDI take can continue. stepGraph permits steps.pitch/steps.velocity and steps.shift/steps.repeat/steps.release plus local-sampler-only steps.pan/steps.fine/steps.modX/steps.modY on its captured Pattern/Channel with the same history/recording policy. stepActivation permits only steps.set on its captured Pattern/Channel, retaining an armed or actively capturing MIDI take. Raw source positions, the Workflow retention policy and canonical validation remain authoritative; retained pending takes still refuse begin. swing leases only project.swing/channel.swingMix on the active target and also retains recording. noteMute leases only notes.mute/unmute in its original Pattern/context and retains a held MIDI take. samplerVoices leases Cut/Cut-by/Cut-itself/Max-poly plus Mono/Portamento/Slide time/Gate/Full portamento on the captured local sampler Channel. samplerModulation leases only canonical modulation parameters on the captured local sampler Channel, preserving armed/capturing MIDI. samplerFilter leases only local sampler filterType/filterCutoff/filterResonance on the original selected Channel and retains armed or capturing MIDI. Document domain refuses armed recording. All domains share canonical history, cancellation and expiry. mixerSend leases a specific existing source/destination send level, including during a held MIDI take. mixerControl leases gain/pan/stereo separation on one captured sourceInsert (Master allowed), including during a held take. mixerEffects permits only plugin.effect.mix on a stable effect ID within sourceInsert and mixer.effects.bypass on that insert, including armed/capturing/retained MIDI; other source/recording guards stay unchanged."},"leaseMs":{"type":"integer","minimum":250,"maximum":120000,"default":30000,"description":"Idle lease; each successful update renews it. Timer expiry cancels without committing."},"revision":{"type":"integer","minimum":0},"sourceInsert":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required sourceInsert at begin for mixerSend, mixerControl or mixerEffects; stable existing insert index."},"targetInsert":{"type":"integer","minimum":0,"maximum":2147483647,"description":"Required targetInsert at begin for mixerSend only; other domains refuse it."}},"required":["action"]}}
    ])JSON");
    tools.getArray()->add(juce::JSON::parse(
        R"JSON({"name":"vibera_plugin","description":"Discover loaded project built-in effects and VST3 instruments/effects with native parameter metadata, refresh/prepare automation asynchronously, open shared Controls or the actual native editor with acknowledged outcomes, inspect native window state, toggle session notifications or recover saved state. Uses the same application services as UI. Supported normalized scalar writes are async actual ACK plus fresh-state history jobs (active MIDI takes use explicit ACK-only writes without state capture/history); Undo/Redo use bounded structural restoration. Arbitrary hidden/program state and native widgets are unsupported. Poll jobs until terminal; recovery can lose uncaptured state/restart voices.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["list","buses","parameters","refresh","notifications","controls","closeControls","openEditor","closeEditor","wrapper","editorState","recover","automate","set","job","cancel"]},"view":{"type":"string","enum":["plugin","settings","parameters"],"default":"plugin","description":"wrapper action selects this view in the same native editor window. Optional parameter+generation select an exposed parameter in Parameters."},"plugin":{"type":"integer","minimum":1,"maximum":1000000000},"instance":{"type":"string","maxLength":100,"description":"Exact runtime instance from list. Required for mutating requests; empty only for a missing runtime placeholder."},"parameter":{"type":"integer","minimum":0,"maximum":4294967295},"generation":{"type":"integer","minimum":1,"maximum":4294967295},"automation":{"type":"string","enum":["create","init","removeInit"]},"value":{"type":"number","minimum":0,"maximum":1},"gestureOpen":{"type":"boolean","default":false,"description":"set only: true begins/continues a scalar gesture; reuse its operation token for updates; false ends and awaits actual ACK/history. Idle gestures finish after30s. This does not control vendor UI widgets."},"enabled":{"type":"boolean"},"operation":{"type":"string","maxLength":100},"revision":{"type":"integer","minimum":0},"offset":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":512,"default":64}},"required":["action"]}})JSON"));
    tools.getArray()->add(juce::JSON::parse(
        R"JSON({"name":"vibera_catalogue","description":"Shared Windows x64 VST3 Settings discovery service. Read configured/standard folders, scan progress/results/blacklist, resolve stable catalogue IDs to descriptors for canonical plugin creation commands, start/rescan or request cancellation. Scans are asynchronous and global to this app, not project Undo. Poll the operation until terminal; per-plugin failures are blacklisted and reported. Standard locations default to the configured value (initially true). Test mode uses a disposable catalogue and never modifies user plugin settings.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","list","descriptor","scan","job","cancel"]},"id":{"type":"string","maxLength":4096,"description":"Opaque stable class ID from list; never a row index."},"section":{"type":"string","enum":["plugins","blacklist","failures"],"default":"plugins"},"kind":{"type":"string","enum":["all","instruments","effects"],"default":"all"},"customFolders":{"type":"array","maxItems":128,"items":{"type":"string","minLength":1,"maxLength":4096},"description":"Absolute Windows folder paths. Omitted retains configured folders; empty scans only standard locations when enabled."},"standardLocations":{"type":"boolean","description":"Include standard VST3 locations; omitted retains configuration, initially true."},"retryBlacklisted":{"type":"boolean","default":false,"description":"Explicitly retry previously failed bundles. Cancellation retains the previous blacklist/catalogue/settings."},"operation":{"type":"string","maxLength":100},"offset":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":128,"default":64}},"required":["action"]}})JSON"));
    {
        auto definition = juce::JSON::parse(
            R"({"name":"vibera_stretch","description":"Shared nondestructive Piano Roll selection stretch. Begin captures explicit notes and their earliest onset anchor. Update sets an absolute time/duration ratio against the unchanged source. Commit is one canonical edit; cancel/expiry leaves source/history/audio untouched. Poll actual completion and planned notes. Source-dependent ratio limits are returned. Score scaling does not time-stretch audio files.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","begin","update","commit","cancel"]},"ids":{"type":"array","minItems":1,"maxItems":100000,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000}},"ratio":{"type":"number","minimum":0.000001,"maximum":1000000,"description":"Absolute duration/onset scale about the returned anchor, not incremental; limits depend on the selected source."},"leaseMs":{"type":"integer","minimum":250,"maximum":120000,"default":30000},"operation":{"type":"string","maxLength":100},"offset":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":512,"default":64}},"required":["action"]}})");
        tools.getArray()->add(definition);
    }
    tools.getArray()->add(juce::JSON::parse(
            R"({"name":"vibera_dialog","description":"Test mode only: read or answer the app's open modal JUCE dialog in-process (no OS input). state lists its title, text fields and buttons; submit sets fields by name and presses the named button through its real handler.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","submit"]},"fields":{"type":"object"},"button":{"type":"string","maxLength":64}},"required":["action"]}})"));
    {
        auto definition = juce::JSON::parse(
            R"({"name":"vibera_slice","description":"Shared Piano Roll cut-line preview/session. Begin captures the current Pattern/Channel; update validates a musical line and returns planned targets without mutating score/audio/history. Commit executes canonical notes.slice once; cancel/lease expiry leaves the source unchanged. Local actions preempt remote preview. Poll state for actual terminal outcomes and actual created right-note IDs; no pointer simulation. Empty IDs target the active part unless line.allChannels explicitly includes ghosts. Preview is not a completed edit.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","begin","update","commit","cancel"]},"operation":{"type":"string","maxLength":100},"leaseMs":{"type":"integer","minimum":250,"maximum":120000,"default":30000},"ids":{"type":"array","maxItems":100000,"uniqueItems":true,"items":{"type":"integer","minimum":1,"maximum":1000000000}},"offset":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":512,"default":64}},"required":["action"]}})");
        definition["inputSchema"]["properties"].getDynamicObject()->setProperty("slice", sliceLineSchema());
        tools.getArray()->add(definition);
    }
    for (const auto &tool : *interactionTools.getArray())
        tools.getArray()->add(tool);
    for (const auto &tool : *additions.getArray())
        tools.getArray()->add(tool);
    tools.getArray()->add(rackToolDefinition());
    {
        auto definition = obj();
        set(definition, "name", "vibera_stepCompose");
        set(definition, "description", "Shared Advanced Fill: multi-Channel drafts, Manual/Every-N/Euclidean/Dice/rotation, baseline-relative audible preview, exact Cancel and one-history Accept. Begin requires explicit Pattern and Channel IDs and no active recording/edit lease. Opening never changes score. Updates replace specified drafts, not incremental fields. Reset restores opening drafts. State includes final masks and actual pending/completed/cancelled outcomes; document completion does not acknowledge DSP adoption.");
        set(definition, "inputSchema", stepComposeSessionSchema());
        set(definition, "outputSchema", stepComposeSessionOutputSchema());
        tools.getArray()->add(definition);
    }
    tools.getArray()->add(grooveToolDefinition());
    tools.getArray()->add(recordingToolDefinition());
    tools.getArray()->add(mixerChainWorkflowTool());
    tools.getArray()->add(aiToolDefinition());
    tools.getArray()->add(audioRecordingToolDefinition());
    for (auto &tool : *tools.getArray())
    {
        auto annotations = obj();
        auto name = tool["name"].toString();
        if (name == "vibera_gesture")
        {
            auto output = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
              "required":["operation","completionState","reason","label","domain","pattern","channel","projectIdentity","revision","leaseMs","undoAvailable"],
              "properties":{"operation":{"type":"string"},"completionState":{"type":"string","enum":["pending","completed","cancelled"]},
                "reason":{"type":"string"},"label":{"type":"string"},"pattern":{"type":"integer","minimum":0},
                "channel":{"type":"integer","minimum":0},"projectIdentity":{"type":"string"},"revision":{"type":"integer","minimum":0},
                "undoAvailable":{"type":"boolean"}}})JSON");
            // The authoritative input contract owns domain names, target bounds and lease units.
            // Readback describes the captured target, including terminal/cancelled gestures.
            for (const auto *key : {"domain", "sourceInsert", "targetInsert", "leaseMs"})
                output["properties"].getDynamicObject()->setProperty(key, tool["inputSchema"]["properties"][key]);
            set(tool, "outputSchema", output);
        }
        if (name == "vibera_plugin")
        {
            tool["inputSchema"]["properties"]["action"]["enum"].getArray()->add("audioIo");
            tool["inputSchema"]["properties"]["action"]["enum"].getArray()->add("chain");
            const auto chainInputFields = juce::JSON::parse(R"JSON({
              "chainAction":{"type":"string","enum":["copy","paste","save","loadApply","configureDirectory","sidechain"],"description":"Containing Mixer EFFECT chain, not an individual plugin/instrument preset. Exact effect instance pins the owning insert/slot independent of selection."},
              "path":{"type":"string","minLength":1,"maxLength":240,"description":"Absolute LOCAL .vibfx file for save/loadApply, under the explicitly configured existing chain directory."},
              "directory":{"type":"string","minLength":1,"maxLength":200,"description":"Existing absolute LOCAL directory for configureDirectory; no folder creation."},
              "overwrite":{"type":"boolean","default":false,"description":"Explicit consent to replace the exact save path; never implicit."}
            })JSON");
            for (const auto &field : chainInputFields.getDynamicObject()->getProperties())
                tool["inputSchema"]["properties"].getDynamicObject()->setProperty(field.name, field.value);
            tool["inputSchema"]["properties"].getDynamicObject()->setProperty(
                "detector", commandArgumentSchema("plugin.effect.sidechain")["properties"]["detector"]);
            tool["inputSchema"]["properties"].getDynamicObject()->setProperty(
                "pluginIo", commandArgumentSchema("plugin.io")["properties"]["pluginIo"]);
            auto output = juce::JSON::parse(
                R"({"type":"object","additionalProperties":true,"properties":{"completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},"operation":{"type":"string","description":"Session-owned token; poll action=job. Acceptance is not completion. Retained with at most64 plugin jobs; old terminal jobs may expire."},"outcome":{"type":"string","enum":["opened","brought_to_front","closed","unsupported","creation_failed","native_failure","invalid_request","cancelled","worker_unavailable","timeout","invalid_acknowledgement"]},"dispatched":{"type":"boolean"},"nativeCompletionKnown":{"type":"boolean"},"elapsedMs":{"type":["integer","null"],"minimum":0,"description":"Stamped admission-to-native-completion duration, not delayed polling time."},"documentChanged":{"type":"boolean"},"undoable":{"type":"boolean"},"items":{"type":"array","items":{"type":"object","additionalProperties":true,"properties":{}}}}})");
            auto properties = output["properties"];
            set(properties, "chainAction", chainInputFields["chainAction"]);
            set(properties, "path",
                juce::JSON::parse(
                    R"({"type":"string","maxLength":240,"description":"Requested chain file; empty for operations without a file."})"));
            set(properties, "phase",
                juce::JSON::parse(
                    R"({"type":"string","description":"Actual choice/close/canonical/terminal stage; pending is not completion."})"));
            set(properties, "mixer",
                juce::JSON::parse(
                    R"({"type":"integer","minimum":0,"description":"Captured containing insert identity, independent of later selection."})"));
            set(properties, "slot",
                juce::JSON::parse(
                    R"({"type":"integer","minimum":0,"description":"Captured zero-based containing effect slot."})"));
            set(properties, "configuredDirectory",
                juce::JSON::parse(
                    R"({"type":"string","description":"Actual session chain-file service directory."})"));
            set(properties, "editorClosures",
                juce::JSON::parse(
                    R"({"type":"array","items":{"type":"object","additionalProperties":true,"properties":{"plugin":{"type":"integer","minimum":1},"instance":{"type":"string"},"serial":{"type":"integer","minimum":1},"outcome":{"type":"string"},"nativeCompletionKnown":{"type":"boolean"},"windowExists":{"type":"boolean"}}}})"));
            const auto scalarFields = juce::JSON::parse(R"JSON({
              "nativeApplied":{"type":"boolean","description":"Actual successful worker ACK or witnessed native gesture; acceptance alone is false."},
              "nativeDispatched":{"type":"boolean"},"historyCommitted":{"type":"boolean"},
              "parameter":{"type":"integer","minimum":0,"maximum":4294967295},
              "requestedGeneration":{"type":"integer","minimum":1,"maximum":4294967295},
              "automationAction":{"type":"string","enum":["automation.plugin.create","automation.plugin.init","automation.plugin.init.remove"]},
              "sourceGestureSerial":{"type":"integer","minimum":1,"description":"Completed native gesture captured by Tools Last tweaked; a newer gesture invalidates pending automation."},
              "beforeKnown":{"type":"boolean"},
              "before":{"type":["number","null"],"minimum":0,"maximum":1},
              "after":{"type":["number","null"],"minimum":0,"maximum":1},
              "recordingWriteOnly":{"type":"boolean"},
              "historyUnavailableReason":{"type":"string","enum":["","recordingCaptureProtected"]},
              "completionScope":{"type":"string","enum":["nativeScalarAckAndCanonicalHistory","nativeScalarAckDuringRecording"]},
              "edits":{"type":"array","maxItems":64,"items":{"type":"object","required":["parameter","before","after"],"properties":{
                "parameter":{"type":"integer","minimum":0,"maximum":4294967295},
                "before":{"type":"number","minimum":0,"maximum":1},"after":{"type":"number","minimum":0,"maximum":1}}}}
            })JSON");
            for (const auto &field : scalarFields.getDynamicObject()->getProperties())
                properties.getDynamicObject()->setProperty(field.name, field.value);
            set(properties, "audioIo", pluginIoStateSchema());
            set(properties, "requestedIo", commandArgumentSchema("plugin.io")["properties"]["pluginIo"]);
            set(properties, "canonicalOperation",
                juce::JSON::parse(
                    R"({"type":"string","description":"Underlying owned canonical chain/structural job; parent audioIo/chain operation remains observable through vibera_plugin action=job across worker replacement."})"));
            set(properties, "structuralOperation",
                juce::JSON::parse(
                    R"({"type":"object","additionalProperties":true,"properties":{"completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},"documentChanged":{"type":"boolean"},"undoable":{"type":"boolean"},"nativeRecoveryRequired":{"type":"boolean"}}})"));
            set(properties, "nativeEditor", nativeEditorStateSchema());
            set(properties, "editorState", nativeEditorStateSchema());
            auto rowProperties = properties["items"]["items"]["properties"];
            set(rowProperties, "nativeOutputMeter", juce::JSON::parse(R"JSON({
                "type":"object","additionalProperties":false,
                "required":["status","available","leftPeak","rightPeak","units","scope"],
                "properties":{
                    "status":{"type":"string","enum":["unsupported","notProcessed","ready","invalid","unavailable"]},
                    "available":{"type":"boolean"},
                    "leftPeak":{"type":["number","null"],"minimum":0},
                    "rightPeak":{"type":["number","null"],"minimum":0},
                    "units":{"type":"string","const":"linear sample peak"},
                    "scope":{"type":"string","description":"Latest retained native processor block peaks, before Host wet/dry/bypass; not physical output or packet-coherent freshness. CONTROL observation may retain stopped values."}
                }})JSON"));
            set(rowProperties, "nativeEditor", nativeEditorStateSchema());
            set(rowProperties, "detector", juce::JSON::parse(R"JSON({"type":"object",
                "description":"Persisted model dependency (connected/sourceMixer/inputBus/channels), separate from actual worker bus negotiation and Apply activation.",
                "properties":{"connected":{"type":"boolean"},"sourceMixer":{"type":"integer","minimum":1},
                    "inputBus":{"type":"integer","minimum":1,"maximum":31},"channels":{"type":"integer","enum":[1,2]}}})JSON"));
            const auto chainFields = mixerChainPluginStateSchema()["properties"];
            for (const auto &property : chainFields.getDynamicObject()->getProperties())
                rowProperties.getDynamicObject()->setProperty(property.name, property.value);
            set(tool, "outputSchema", output);
            tool["inputSchema"]["properties"]["action"].getDynamicObject()->setProperty(
                "description", "openEditor/closeEditor require exact current plugin/instance, no physical "
                               "editor/state/parameter/catalogue or file work in flight; accepted requests "
                               "are pending and must be polled via job. editorState is non-mutating. "
                               "Cancellation before dispatch only; too_late requires outcome polling. Window "
                               "operations do not enter project Undo or expose arbitrary native widgets.");
        }
        if (name == "vibera_state")
            set(tool, "outputSchema", juce::JSON::parse(R"JSON({"type":"object","additionalProperties":true,
              "properties":{"historyNotifications":{"type":"object","additionalProperties":false,"required":["controllerPending","undoRequestPending","undoObserverDeliveryAcknowledged"],"description":"Post-commit control work. Pending notification does not undo a completed mutation; asynchronous request acceptance never proves observer delivery.","properties":{"controllerPending":{"type":"boolean"},"undoRequestPending":{"type":"boolean"},"undoObserverDeliveryAcknowledged":{"type":"boolean","const":false}}},"instrumentOpening":{"type":"object","additionalProperties":false,"required":["phase","channel","plugin","projectionReady"],"description":"UI Channel-name intent waiting for its matching projection/canonical admission; not an accepted native job or completed editor. MCP clients request openEditor after preparation, retry explicit busy, and poll actual job outcomes.","properties":{"phase":{"type":"string","enum":["idle","waiting"]},"channel":{"type":"integer","minimum":0},"plugin":{"type":"integer","minimum":0},"projectionReady":{"type":"boolean"}}},"audio":{"type":"object","additionalProperties":true,"properties":{
                "pluginSourceState":{"type":"string","enum":["notRequired","controlOnly","pending","active","fault","unavailable"],"default":"notRequired","description":"Ordinary prepared live-plugin context. active requires a successfully closed physical source callback; pending/arm is not audio completion. controlOnly permits device-free native controls, not live DSP."},
                "sendAutomationHealthy":{"anyOf":[{"type":"boolean"},{"type":"null"}],"description":"Selected Mixer send/Master-gate evaluator fault readback; null without an Edit. true is not an audible activation/settling acknowledgement."},
                "pluginTransportPackets":{"type":"integer","minimum":0,"maximum":8,"default":0,"description":"Prepared worker pipeline quanta. Zero means no ordinary live source projection; latencySamples includes this transport."},
                "maximumPhysicalFrames":{"type":"integer","minimum":0,"maximum":8192,"default":0,"description":"Admitted maximum physical callback frames, independent of UI/MCP request sizes. Ordinary replacement supports at most one native device quantum."},
                "maximumSourceFrames":{"type":"integer","minimum":0,"maximum":9012,"default":0,"description":"Prepared source-frame quota for the admitted physical callback and native speed envelope; not actual frames processed."},
                "pluginDeviceEpoch":{"type":"integer","minimum":0,"default":0,"description":"Runtime physical device identity; same-rate/same-buffer restart changes it. Zero when no ordinary live source projection exists."},
                "pluginWorkers":{"type":"array","items":{"type":"object","additionalProperties":true,"properties":{
                  "auxiliaryInputBus":{"type":"integer","minimum":-1,"maximum":31,"default":-1,"description":"Actual negotiated native input bus; -1 means none. Requested detector assignment uses plugin.effect.sidechain; this field reports actual worker negotiation, not a capability catalogue or proof of current audio activation."},
                  "auxiliaryInputChannels":{"type":"integer","enum":[0,1,2],"default":0,"description":"Actual negotiated detector channels; not a catalogue of all plugin bus capabilities."},
                  "auxiliaryRejected":{"type":"integer","minimum":0,"description":"Sticky invalid detector-buffer count. Samples became silence; export cannot report successful delivery."},
                  "bypassTransitionSamples":{"type":"integer","minimum":1,"maximum":960,"description":"Prepared five-millisecond healthy bypass ramp in samples. Initial bypass and failed-worker fallback are immediate."},
                  "bypassBlend":{"type":["number","null"],"minimum":0,"maximum":1,"description":"Actual processing readback:0 wet,1 latency-aligned dry (silent for instruments); null before processing. Model target commits separately from DSP settling."},
                  "wetBlend":{"type":["number","null"],"minimum":0,"maximum":1,"description":"Actual last processing-owner host wet-ramp value; null before processing. Effective processed contribution also depends on bypassBlend. Separate snapshots are not an atomic combined frame or heard-output proof."},
                  "latencyDeclared":{"type":"object","additionalProperties":false,"required":["nativeSamples","generation"],"description":"Latest coherent native declaration in samples. Generation is scoped to worker instance; neither declares graph adoption.","properties":{"nativeSamples":{"type":"integer","minimum":0,"maximum":131055},"generation":{"type":"integer","minimum":1}}},
                  "latencyConsumed":{"type":["object","null"],"additionalProperties":false,"required":["nativeSamples","generation","sequence"],"description":"Actual wet packet consumed by the processing owner; dry bypass uses this same latency. Null before consumption or during bounded publication. This is not a graph/PDC completion acknowledgement.","properties":{"nativeSamples":{"type":"integer","minimum":0,"maximum":131055},"generation":{"type":"integer","minimum":1},"sequence":{"type":"integer","minimum":0}}}
                }}}
              }}}})JSON"));
        if (name == "vibera_mixer")
        {
            auto* actions = tool["inputSchema"]["properties"]["action"]["enum"].getArray();
            for (auto action : {"selectMany", "selectAll", "selectGroup"}) actions->add(action);
            auto *fields = tool["inputSchema"]["properties"].getDynamicObject();
            fields->setProperty("ids", mixerSelectionIdsSchema());
            fields->setProperty("mode", juce::JSON::parse(R"({"type":"string","enum":["replace","add","remove","toggle","range"],"default":"replace","description":"selectMany only. range takes one destination and current anchor in current display order; at least one insert remains selected."})"));
            fields->setProperty("active", juce::JSON::parse(R"({"type":"integer","minimum":0,"maximum":2147483647,"description":"selectMany only; exact selected source. Omitted resolves deterministically after membership changes. Does not assign a send destination."})"));
            fields->setProperty("group", juce::JSON::parse(R"({"type":"string","minLength":32,"maxLength":32,"pattern":"^[0-9a-f]{32}$","description":"selectGroup only; existing UUID group from organisation readback."})"));
            for (auto action : {"routingState", "routingJob", "cancelRouting", "devicePreparationState", "retryDevicePreparation", "cancelDevicePreparation"}) actions->add(action);
            tool["inputSchema"]["properties"].getDynamicObject()->setProperty("operation",
                juce::JSON::parse(R"({"type":"integer","minimum":1,"maximum":9007199254740991,"description":"Required for routingJob/cancelRouting and retryDevicePreparation/cancelDevicePreparation; optional for devicePreparationState. Routing retains32 terminal jobs; device preparation retains current token only."})"));
            auto output = juce::JSON::parse(
                R"({"type":"object","additionalProperties":true,"properties":{"completionState":{"type":"string","enum":["completed"]}}})");
            output["properties"].getDynamicObject()->setProperty("effectsBrowser",
                                                                 mixerEffectsBrowserSchema());
            output["properties"].getDynamicObject()->setProperty("effectsPane", mixerEffectsPaneSchema());
            output["properties"].getDynamicObject()->setProperty("organisation", mixerOrganisationStateSchema());
            auto* completionStates = output["properties"]["completionState"]["enum"].getArray();
            completionStates->add("pending"); completionStates->add("failed"); completionStates->add("cancelled");
            output["properties"].getDynamicObject()->setProperty("routing", channelRoutingSchema());
            output["properties"].getDynamicObject()->setProperty("devicePreparation", devicePreparationSchema());
            output["properties"].getDynamicObject()->setProperty("error", channelRoutingSchema()["properties"]["error"]);
            tool.getDynamicObject()->setProperty("outputSchema", output);
        }
        if (name == "vibera_state")
        {
            auto properties = tool["outputSchema"]["properties"];
            if (!properties["audio"].isObject())
                set(properties, "audio",
                    juce::JSON::parse(R"({"type":"object","additionalProperties":true,"properties":{}})"));
            if (!properties["audio"]["properties"].isObject())
                properties["audio"].getDynamicObject()->setProperty("properties", obj());
            properties["audio"]["properties"].getDynamicObject()->setProperty("lastTweakedPlugin",
                                                                              lastTweakedPluginSchema());
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("mixerOrder", mixerInsertOrderStateSchema());
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("mixerOrganisation", mixerOrganisationStateSchema());
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("channelRouting", channelRoutingSchema());
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("devicePreparation", devicePreparationSchema());
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("mixerEffectsBrowser",
                                                                               mixerEffectsBrowserSchema());
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("mixerEffectsPane",
                                                                               mixerEffectsPaneSchema());
        }
        if (name == "vibera_state")
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("recording",
                                                                               recordingStateSchema());
        if (name == "vibera_state")
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty(
                "quantizerPreview",
                juce::JSON::parse(R"JSON({"type":["object","null"],"additionalProperties":true,
                  "description":"Actual local Quantizer preview. Null before opening; pending retains its original owner/baseline after refused Accept. Finish or cancel before file operations.",
                  "properties":{"completionState":{"type":"string","enum":["pending","completed","cancelled"]},
                    "pattern":{"type":"integer","minimum":1},"channel":{"type":"integer","minimum":0},
                    "noteIds":{"type":"array","items":{"type":"integer","minimum":1}},
                    "fileDialogPending":{"type":"boolean"},"error":{"type":"object","additionalProperties":false,
                      "required":["code","message"],"properties":{"code":{"type":"string","const":"history_refused"},"message":{"type":"string"}}}},
                  "required":["completionState"]})JSON"));
        if (name == "vibera_controller")
        {
            tool.getDynamicObject()->setProperty(
                "description", "Global state/setBinding use atomic settings persistence. "
                               "samplerLearnStart/State/Accept/Cancel learn fixed project-owned sampler CC "
                               "links through canonical document history; start/capture remain pending until "
                               "explicit acceptance. Save the project separately. Existing "
                               "injection/ports/preset remain owned-test diagnostics only.");
            auto *properties = tool["inputSchema"]["properties"].getDynamicObject();
            auto operations = obj();
            for (const auto *action :
                 {"samplerLearnStart", "samplerLearnState", "samplerLearnAccept", "samplerLearnCancel"})
            {
                const auto schema = samplerControllerLearnArguments(action);
                tool["inputSchema"]["properties"]["action"]["enum"].getArray()->add(action);
                for (const auto &field : schema["properties"].getDynamicObject()->getProperties())
                    if (field.name != juce::Identifier("action"))
                        properties->setProperty(field.name, field.value);
                operations.getDynamicObject()->setProperty(action, schema);
            }
            tool.getDynamicObject()->setProperty("samplerLearnOperations", operations);
            tool["inputSchema"]["properties"].getDynamicObject()->setProperty(
                "binding", MidiController::bindingSchema());
            tool.getDynamicObject()->setProperty("outputSchema", juce::JSON::parse(R"JSON({
              "type":"object","additionalProperties":true,"required":["completionState"],"properties":{
                "completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},
                "controllerProfile":{"type":"object"},
                "samplerControllers":{"type":"object","additionalProperties":true},
                "controllerProfileIdentity":{"type":"string"},"controllerProfileIdentityKnown":{"type":"boolean"},
                "controllerProfileIdentityError":{"type":"string"},
                "controllerBindingPersistence":{"type":"string","enum":["persistentSettings","sessionOnly"]},
                "controllerParameterActions":{"type":"array","items":{"type":"string"}},
                "controllerBindingCommands":{"type":"array","items":{"type":"string"}},
                "controllerBindingChange":{"type":"object","additionalProperties":false,
                  "required":["index","configurationCommitted","persisted","projectUndoable","completionScope"],
                  "properties":{"index":{"type":"integer","minimum":0,"maximum":511},
                    "configurationCommitted":{"type":"boolean","const":true},"persisted":{"type":"boolean"},
                    "projectUndoable":{"type":"boolean","const":false},
                    "completionScope":{"type":"string","const":"controllerBindingConfiguration"}}}}})JSON"));
            auto profileSchema = juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
              "required":["version","profile","mixerBank","potMode","faderMode","input","output","bindings"],
              "properties":{"version":{"type":"integer","const":1},"profile":{"type":"string"},
                "mixerBank":{"type":"integer","minimum":0},"potMode":{"type":"integer","minimum":0,"maximum":9},
                "faderMode":{"type":"integer","minimum":0,"maximum":9},"input":{"type":"string"},
                "output":{"type":"string"},"bindings":{"type":"array","maxItems":512}}})JSON");
            profileSchema["properties"]["bindings"].getDynamicObject()->setProperty(
                "items", MidiController::bindingSchema());
            tool["outputSchema"]["properties"].getDynamicObject()->setProperty("controllerProfile",
                                                                               profileSchema);
        }
        if (name == "vibera_select")
            tool["inputSchema"]["properties"]["scope"].getDynamicObject()->setProperty("maximum",
                                                                                       int(Scope::sampler));
        set(annotations, "readOnlyHint",
            name == "vibera_state" || name == "vibera_commands" || name == "vibera_snapshot" ||
                name == "vibera_effects");
        set(annotations, "destructiveHint",
            name == "vibera_command" || name == "vibera_key" || name == "vibera_pointer" ||
                name == "vibera_controller" || name == "vibera_plugin" || name == "vibera_catalogue" ||
                name == "vibera_groove" || name == "vibera_recording" || name == "vibera_macros" ||
                name == "vibera_mixerchain" || name == "vibera_automationRecording" || name == "vibera_samplerTempo" || name == "vibera_synth" || name == "vibera_stepCompose");
        set(annotations, "openWorldHint", false);
        set(tool, "annotations", annotations);
        if (name != "vibera_state" && name != "vibera_commands" && name != "vibera_snapshot" &&
            name != "vibera_inspect" && name != "vibera_mixerchain" && name != "vibera_automationRecording" &&
            name != "vibera_effects")
            tool["inputSchema"]["properties"].getDynamicObject()->setProperty(
                "projectIdentity",
                juce::JSON::parse("{\"type\":\"string\",\"maxLength\":100,\"description\":\"Optional current "
                                  "project-session identity guard.\"}"));
    }
    return tools;
}
juce::var Studio::mcpCall(const juce::String &name, const juce::var &args,
                          juce::String *admittedPluginOperation, const PluginGesture *lastTweakWitness)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const juce::ScopedValueSetter<juce::String *> structuralOwner(canonicalStructuralAdmissionOperation,
                                                                  admittedPluginOperation);
    pollMcpGesture();
    pollNoteSlice();
    pollNoteStretch();
    pollStepCompose();
    pollPluginOperations();
    if(name=="vibera_synth")
    {
        const auto action=args["action"].toString();
        if(action=="list" || action=="listUserPresets" || action=="descriptor" || action=="browser" ||
           action=="filter" || action=="favorite" || action=="navigate" || action=="parameters" ||
           action=="status" || action=="previewOn" || action=="previewOff" || action=="panic")
            return mcpSynthOperation(args); // Readback/preferences or independent preview; no score/history mutation.
    }
    if (name == "vibera_stepCompose")
    {
        const auto result = stepComposeAction(args);
        if (result.failed())
            throw McpFailure(result.getErrorMessage().upToFirstOccurrenceOf(":", false, false), result.getErrorMessage());
        return textResult(stepComposeState());
    }
    const bool noteModesRead =
        name == "vibera_note_modes" &&
        (args["action"].toString() == "state" || args["action"].toString() == "propertiesState" ||
         args["action"].toString() == "propertiesCancel");
    const bool composeRead = noteModesRead || name == "vibera_effects" || name == "vibera_state" ||
                             name == "vibera_inspect" || name == "vibera_commands" ||
                             name == "vibera_snapshot" ||
                             (name == "vibera_project" &&
                              (args["action"].toString() == "state" || args["action"].toString() == "job")) ||
                             (name == "vibera_selection" && args["action"].toString() == "state");
    const bool composeTransport =
        (name == "vibera_transport" && (args["action"].toString() == "play" || args["action"].toString() == "stop")) ||
        (name == "vibera_command" && (args["command"].toString() == "transport.play" || args["command"].toString() == "transport.stop"));
    if (stepComposePending() && !composeRead && !composeTransport)
        throw McpFailure("stepCompose.busy", "Accept or Cancel Advanced Fill first");
    // Immutable compiled-in discovery is available during every editing/session lease.
    if (name == "vibera_effects")
        return textResult(effects::nativeEffectsDiscovery(args));
    // AI configuration/jobs/chat never mutate the document directly; placement and agent
    // edits go through the same canonical paths as every other caller.
    if (name == "vibera_ai")
        return textResult(mcpAiOperation(args));
    if (name == "vibera_appearance")
        return textResult(mcpAppearanceOperation(args));
    // Input/monitor preferences and the take readback; takes themselves follow the transport.
    if (name == "vibera_audioRecording")
        return textResult(audioRecordingOperation(args));
    routingCoordinator.poll({patternMode, selectedPattern});
    serviceAutomationRecording();
    if (name == "vibera_automationRecording")
        return textResult(automationRecordingOperation(args));
    const bool tempoQuery = name == "vibera_samplerTempo" && args["action"].toString() != "apply";
    const bool recordingReadOnly =
        noteModesRead || tempoQuery || name == "vibera_state" || name == "vibera_inspect" ||
        name == "vibera_commands" || name == "vibera_snapshot" ||
        (name == "vibera_recording" && args["action"].toString() == "state") ||
        (name == "vibera_controller" &&
         (args["action"].toString() == "state" || args["action"].toString() == "samplerLearnState" ||
          args["action"].toString() == "samplerLearnCancel")) ||
        (name == "vibera_mixer" && args["action"].toString() == "state");
    const bool recordingStop = (name == "vibera_transport" && args["action"].toString() == "stop") ||
                               (name == "vibera_command" && args["command"].toString() == "transport.stop");
    const bool recordingPlay =
        automationTouch && automationTouch->phase == MixerAutomationTake::Phase::armed && !audio.playing() &&
        ((name == "vibera_transport" && args["action"].toString() == "play") ||
         (name == "vibera_command" && args["command"].toString() == "transport.play"));
    const bool recordingValue =
        name == "vibera_command" &&
        (args["command"].toString() == "mixer.gain" || args["command"].toString() == "mixer.pan" ||
         args["command"].toString() == "mixer.stereoSeparation" ||
         args["command"].toString() == "mixer.send");
    // Mixer pointer input during a take is the UI's own touch-recording path (each value goes
    // through the same recorder admission as recordingValue).
    const bool recordingPointer = name == "vibera_pointer" && int(args["editor"]) == 3;
    if (automationRecordingActive() && !recordingReadOnly && !recordingStop && !recordingPlay &&
        !recordingValue && !recordingPointer &&
        !(controllerTestMode && name == "vibera_controller" && args["action"].toString() == "inject"))
        throw McpFailure("automationRecording.busy",
                         "Finish/retry/cancel the automation take before other mutations");
    // Dedicated nonmutating workflow owns its exact UI/MCP admission and session queries.
    if (name == "vibera_mixerchain") return textResult(mixerChainOperation(args));
    const bool macroQuery = name == "vibera_macros" && args["action"].toString() == "state";
    const bool controllerQuery =
        name == "vibera_controller" &&
        (args["action"].toString() == "state" || args["action"].toString() == "samplerLearnState" ||
         args["action"].toString() == "samplerLearnCancel");
    const bool routingQuery =
        noteModesRead || tempoQuery || macroQuery || controllerQuery ||
        (name == "vibera_mixer" &&
         (args["action"].toString() == "state" || args["action"].toString() == "routingState" ||
          args["action"].toString() == "routingJob" || args["action"].toString() == "cancelRouting" ||
          args["action"].toString() == "devicePreparationState" ||
          args["action"].toString() == "retryDevicePreparation" ||
          args["action"].toString() == "cancelDevicePreparation"));
    const bool preparationReadOnly =
        name == "vibera_state" || name == "vibera_inspect" || name == "vibera_commands" ||
        name == "vibera_snapshot" || routingQuery ||
        (name == "vibera_command" && (args["command"].toString() == "transport.stop" ||
                                      args["command"].toString() == "transport.play")) ||
        (name == "vibera_transport" &&
         (args["action"].toString() == "play" || args["action"].toString() == "stop")) ||
        (name == "vibera_recording" && args["action"].toString() == "state") ||
        (name == "vibera_project" &&
         (args["action"].toString() == "state" || args["action"].toString() == "job")) ||
        (name == "vibera_plugin" &&
         (args["action"].toString() == "list" || args["action"].toString() == "parameters" ||
          args["action"].toString() == "buses" || args["action"].toString() == "job" ||
          args["action"].toString() == "editorState"));
    if (devicePreparationBusy() && !preparationReadOnly)
        throw McpFailure("device.busy", "Finish or cancel the current native preparation; inspect devicePreparationState");
    if (noteStretchPending() && !routingQuery && name != "vibera_stretch" &&
        !(name == "vibera_recording" && args["action"].toString() == "state") && name != "vibera_state" &&
        name != "vibera_inspect" && name != "vibera_commands" && name != "vibera_snapshot" &&
        !(name == "vibera_project" &&
          (args["action"].toString() == "state" || args["action"].toString() == "job")) &&
        !(name == "vibera_selection" && args["action"].toString() == "state") &&
        !(mcpPointerTarget && !noteStretchSession->remote &&
          (name == "vibera_pointer" || name == "vibera_key")))
        throw McpFailure("stretch_owned", "Update, commit or cancel the active stretch preview first");
    if (noteSlicePending() && !routingQuery && name != "vibera_slice" &&
        !(name == "vibera_recording" && args["action"].toString() == "state") && name != "vibera_state" &&
        name != "vibera_inspect" && name != "vibera_commands" && name != "vibera_snapshot" &&
        !(name == "vibera_project" &&
          (args["action"].toString() == "state" || args["action"].toString() == "job")) &&
        !(name == "vibera_selection" && args["action"].toString() == "state") &&
        !(mcpPointerTarget && !noteSliceSession->remote &&
          (name == "vibera_pointer" || name == "vibera_key")))
        throw McpFailure("slice_owned", "Update, commit or cancel the active cut-line preview first");
    if (mcpGesture && mcpGesture->state == "pending" && !mcpGestureDispatch && !routingQuery &&
        name != "vibera_gesture" && !(name == "vibera_recording" && args["action"].toString() == "state") &&
        name != "vibera_state" && name != "vibera_inspect" && name != "vibera_commands" &&
        name != "vibera_snapshot" && name != "vibera_groove" &&
        !(name == "vibera_plugin" &&
          (args["action"].toString() == "list" || args["action"].toString() == "parameters" ||
           args["action"].toString() == "buses" || args["action"].toString() == "job")) &&
        !(name == "vibera_catalogue" && args["action"].toString() != "scan" &&
          args["action"].toString() != "cancel") &&
        !(name == "vibera_project" &&
          (args["action"].toString() == "job" || args["action"].toString() == "state")) &&
        !(name == "vibera_rack" &&
          (args["action"].toString() == "state" || args["action"].toString() == "job" ||
           args["action"].toString() == "cancel")) &&
        !(name == "vibera_selection" && args["action"].toString() == "state"))
        throw McpFailure("gesture_owned", "Update, commit or cancel the leased edit gesture first");
    if (args.hasProperty("projectIdentity") &&
        args["projectIdentity"].toString() != controller.projectIdentity &&
        !(name == "vibera_samplerTempo" &&
          (args["action"].toString() == "job" || args["action"].toString() == "cancel")) &&
        !(name == "vibera_plugin" &&
          (args["action"].toString() == "job" || args["action"].toString() == "cancel")) &&
        !(name == "vibera_catalogue" &&
          (args["action"].toString() == "job" || args["action"].toString() == "cancel" ||
           args["action"].toString() == "state")) &&
        !(name == "vibera_project" &&
          (args["action"].toString() == "job" || args["action"].toString() == "state")) &&
        !(name == "vibera_rack" &&
          (args["action"].toString() == "job" || args["action"].toString() == "cancel")) &&
        !(name == "vibera_mixer" &&
          (args["action"].toString() == "routingJob" || args["action"].toString() == "cancelRouting")))
        throw McpFailure("stale_project",
                         "Project session changed; rediscover stable targets before retrying");
    if (mcpPointerTarget && !routingQuery && name != "vibera_state" && name != "vibera_snapshot" &&
        !(name == "vibera_recording" && args["action"].toString() == "state") && name != "vibera_commands" &&
        name != "vibera_pointer" &&
        !(name == "vibera_rack" &&
          (args["action"].toString() == "state" || args["action"].toString() == "job" ||
           args["action"].toString() == "cancel")))
        throw std::runtime_error("Finish or cancel the owned MCP pointer gesture first");
    if (name == "vibera_samplerTempo")
        return textResult(samplerTempoOperation(args));
    const auto requireFinishedTake = [this]
    {
        if (auto result = finishMidiTake(); result.failed())
            throw McpFailure("take_pending", result.getErrorMessage());
    };
    if (name == "vibera_recording")
    {
        const auto request = parseRecordingRequest(args);
        const auto &action = request.action;
        const auto serial = request.serial;
        const auto before = controller.revision;
        auto result = juce::Result::ok();
        try
        {
            if (action == "finish")
                result = finishMidiTake();
            else if (action == "retry")
                result = retryMidiTake(serial);
            else if (action == "cancel")
                result = cancelPendingMidiTake(serial);
            else if (action != "state")
                throw McpFailure("unsupported_operation", "Unknown MIDI recording action");
        }
        catch (const McpFailure &)
        {
            throw;
        }
        catch (...)
        {
            result = juce::Result::fail(
                "take.preparation_failed: Operation did not return; inspect retained take before retry");
        }
        auto data = obj();
        set(data, "action", action);
        set(data, "recordArmed", recordArmed);
        set(data, "documentChanged", before != controller.revision);
        set(data, "recording", midiTakeState());
        const auto offset = request.offset;
        const auto limit = request.limit;
        const std::vector<Note> *notes = nullptr;
        bool partial = false;
        if (pendingMidiTake)
        {
            const auto phase = pendingMidiTake->phase;
            notes = phase == PendingMidiTake::Phase::ready || phase == PendingMidiTake::Phase::applying
                        ? &pendingMidiTake->args.recordedNotes
                        : &midiTake.finishedNotes();
            partial = midiTaking || phase == PendingMidiTake::Phase::sealing ||
                      phase == PendingMidiTake::Phase::invalidCapture || midiTake.heldCount() != 0 ||
                      midiTake.hasPendingEvent();
        }
        juce::Array<juce::var> rows;
        const auto total = notes ? int(notes->size()) : 0;
        for (int index = offset; index < total && rows.size() < limit; ++index)
        {
            const auto &note = (*notes)[size_t(index)];
            auto row = obj();
            set(row, "id", 0);
            set(row, "channel", juce::int64(note.channel));
            set(row, "beat", note.beat);
            set(row, "length", note.length);
            set(row, "pitch", note.pitch);
            set(row, "velocity", note.velocity);
            set(row, "releaseVelocity", note.releaseVelocity);
            set(row, "modX", note.modX);
            set(row, "modY", note.modY);
            set(row, "releaseMidiVelocity", int(noteReleaseMidiVelocity(note.releaseVelocity)));
            rows.add(row);
        }
        set(data, "notes", rows);
        set(data, "notesTotal", total);
        set(data, "notesOffset", offset);
        set(data, "notesPartial", partial);
        set(data, "completionState",
            result.failed()                                             ? "failed"
            : action == "cancel"                                        ? "cancelled"
            : action != "state" && (midiTaking || hasPendingMidiTake()) ? "pending"
                                                                        : "completed");
        if (result.failed())
        {
            auto error = obj();
            const auto message = result.getErrorMessage();
            const auto separator = message.indexOfChar(':');
            set(error, "code",
                separator > 0 ? message.substring(0, separator) : juce::String("take.command_rejected"));
            set(error, "message", message);
            set(data, "error", error);
        }
        return textResult(data);
    }
    if (name == "vibera_stretch")
    {
        const auto action = args["action"].toString();
        juce::StringArray allowed{"action", "projectIdentity"};
        if (action == "state")
            allowed.addArray({"operation", "offset", "limit"});
        else if (action == "begin")
            allowed.addArray({"ids", "leaseMs"});
        else if (action == "update")
            allowed.addArray({"operation", "ratio"});
        else
            allowed.add("operation");
        for (const auto &field : args.getDynamicObject()->getProperties())
            if (!allowed.contains(field.name.toString()))
                throw McpFailure("invalid_arguments", "Parameter is not applicable to this stretch action");
        if (action == "state" && args.hasProperty("operation") &&
            (!noteStretchSession || args["operation"].toString() != noteStretchSession->id))
            throw McpFailure("stale_operation", "Stretch session changed");
        juce::Result result = juce::Result::ok();
        if (action == "begin")
        {
            std::vector<Id> ids;
            if (const auto *values = args["ids"].getArray())
                for (const auto &v : *values)
                {
                    auto target = obj();
                    set(target, "id", v);
                    ids.push_back(identity(target, "id", 0));
                }
            result = beginNoteStretch(ids, true, integer(args, "leaseMs", 30000, 250, 120000));
        }
        else if (action != "state")
        {
            if (!noteStretchSession || !noteStretchSession->remote ||
                args["operation"].toString() != noteStretchSession->id)
                throw McpFailure("stale_operation",
                                 "Stretch token does not identify an owned remote preview");
            if (action == "update")
            {
                if (!args.hasProperty("ratio"))
                    throw McpFailure("invalid_arguments", "Provide an absolute ratio");
                result = updateNoteStretch(number(args, "ratio", 1));
            }
            else if (action == "commit" || action == "cancel")
                result = finishNoteStretch(action == "commit");
            else
                throw McpFailure("unsupported_operation", "Unknown stretch action");
        }
        if (result.failed())
            throw McpFailure("validation_failed", result.getErrorMessage());
        return textResult(
            noteStretchState(integer(args, "offset", 0, 0, 1000000), integer(args, "limit", 64, 1, 512)));
    }
    if (name == "vibera_dialog")
    {
        if (!controllerTestMode)
            throw McpFailure("unavailable_operation", "vibera_dialog exists only in test mode");
        auto *alert = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        auto response = obj();
        set(response, "open", alert != nullptr);
        if (!alert)
        {
            if (args["action"].toString() == "submit")
                throw McpFailure("unavailable_target", "No modal dialog is open");
            return textResult(response);
        }
        juce::Array<juce::var> fields, buttons;
        for (auto *child : alert->getChildren())
        {
            if (auto *editor = dynamic_cast<juce::TextEditor *>(child))
            {
                auto row = obj();
                set(row, "name", editor->getName());
                set(row, "text", editor->getText());
                fields.add(row);
            }
            if (auto *button = dynamic_cast<juce::Button *>(child))
                buttons.add(button->getButtonText());
        }
        set(response, "title", alert->getName());
        set(response, "fields", fields);
        set(response, "buttons", buttons);
        if (args["action"].toString() == "submit")
        {
            if (const auto *values = args["fields"].getDynamicObject())
                for (const auto &field : values->getProperties())
                {
                    auto *editor = alert->getTextEditor(field.name.toString());
                    if (!editor)
                        throw McpFailure("invalid_arguments", "No dialog field named " + field.name.toString());
                    editor->setText(field.value.toString(), juce::sendNotification);
                }
            auto *button = alert->getButton(args["button"].toString());
            if (!button)
                throw McpFailure("invalid_arguments", "No dialog button named " + args["button"].toString());
            button->triggerClick();
            set(response, "submitted", true);
        }
        return textResult(response);
    }
    if (name == "vibera_slice")
    {
        const auto action = args["action"].toString();
        juce::StringArray allowedFields{"action", "projectIdentity"};
        if (action == "state")
            allowedFields.addArray({"operation", "offset", "limit"});
        else if (action == "begin")
            allowedFields.add("leaseMs");
        else if (action == "update")
            allowedFields.addArray({"operation", "slice", "ids"});
        else
            allowedFields.add("operation");
        for (const auto &field : args.getDynamicObject()->getProperties())
            if (!allowedFields.contains(field.name.toString()))
                throw McpFailure("invalid_arguments", "Parameter is not applicable to this Slice action");
        if (action == "state" && args.hasProperty("operation") &&
            (!noteSliceSession || args["operation"].toString() != noteSliceSession->id))
            throw McpFailure("stale_operation", "Slice session changed");
        if (action == "begin")
        {
            const auto result = beginNoteSlice(true, integer(args, "leaseMs", 30000, 250, 120000));
            if (result.failed())
                throw McpFailure("unavailable_operation", result.getErrorMessage());
        }
        else if (action != "state")
        {
            if (!noteSliceSession || !noteSliceSession->remote ||
                args["operation"].toString() != noteSliceSession->id)
                throw McpFailure("stale_operation", "Slice token does not identify an owned remote preview");
            juce::Result result = juce::Result::ok();
            if (action == "update")
            {
                NoteSliceLine line;
                if (auto r = parseSliceLine(args["slice"], line); r.failed())
                    throw McpFailure("invalid_arguments", r.getErrorMessage());
                std::vector<Id> ids;
                if (auto *values = args["ids"].getArray())
                    for (const auto &v : *values)
                    {
                        auto target = obj();
                        set(target, "id", v);
                        ids.push_back(identity(target, "id", 0));
                    }
                result = updateNoteSlice(line, ids);
            }
            else if (action == "commit" || action == "cancel")
                result = finishNoteSlice(action == "commit");
            else
                throw McpFailure("unsupported_operation", "Unknown Slice action");
            if (result.failed())
                throw McpFailure("validation_failed", result.getErrorMessage());
        }
        return textResult(
            noteSliceState(integer(args, "offset", 0, 0, 1000000), integer(args, "limit", 64, 1, 512)));
    }
    if (name == "vibera_rack")
        return textResult(mcpRackOperation(args));
    if (auto *modal = juce::Component::getCurrentlyModalComponent();
        modal && (name == "vibera_key" || name == "vibera_pointer"))
    {
        const auto phase = args["phase"].toString();
        if (name == "vibera_key" && integer(args, "key", 0, 1, 65535) == juce::KeyPress::escapeKey)
        {
            if (!modal->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)))
                throw std::runtime_error("Modal UI did not handle Escape");
            return mcpCall("vibera_state", obj());
        }
        if (name != "vibera_pointer" || !mcpPointerTarget || (phase != "up" && phase != "cancel"))
            throw std::runtime_error("Close modal UI before editor input");
    }
    if (name == "vibera_synth") return mcpSynthOperation(args);
    if (name == "vibera_plugin")
        return mcpPluginOperation(args, admittedPluginOperation, lastTweakWitness);
    if (name == "vibera_catalogue")
        return mcpCatalogueOperation(args);
    if (name == "vibera_macros") return mcpMacroOperation(args);
    if (name == "vibera_mixer")
    {
        const auto action = args["action"].toString();
        if (action == "devicePreparationState" || action == "retryDevicePreparation" || action == "cancelDevicePreparation")
        {
            if (args.hasProperty("insert") || args.hasProperty("offset"))
                throw McpFailure("invalid_arguments", "Device preparation accepts no insert/offset");
            std::uint64_t operation = 0;
            if (action != "devicePreparationState" || args.hasProperty("operation"))
            {
                const auto raw = number(args, "operation", 0);
                if (raw < 1 || raw > 9007199254740991. || std::floor(raw) != raw)
                    throw McpFailure("invalid_arguments", "Exact positive current preparation token required");
                operation = static_cast<std::uint64_t>(raw);
            }
            auto result = juce::Result::ok();
            if (action == "retryDevicePreparation") result = retryDevicePreparation(operation);
            else if (action == "cancelDevicePreparation") result = cancelDevicePreparation(operation);
            auto response = obj();
            auto state = devicePreparationState(action == "retryDevicePreparation" && result.wasOk() ? 0 : operation);
            set(response, "devicePreparation", state);
            set(response, "completionState", state["completionState"]);
            if (result.failed())
            {
                set(response, "completionState", "failed");
                auto error = obj();
                set(error, "code", result.getErrorMessage().upToFirstOccurrenceOf(":", false, false));
                set(error, "message", result.getErrorMessage()); set(response, "error", error);
            }
            return textResult(response); // Accepted retry/cancel stays pending until actual service/drain.
        }
        if (action == "routingState" || action == "routingJob" || action == "cancelRouting")
        {
            if (args.hasProperty("insert") || args.hasProperty("offset") ||
                (action == "routingState" && args.hasProperty("operation")))
                throw McpFailure("invalid_arguments", "Routing queries accept no insert/offset");
            std::uint64_t operation = 0;
            if (action != "routingState")
            {
                const auto raw = number(args, "operation", 0);
                if (raw < 1 || raw > 9007199254740991. || std::floor(raw) != raw)
                    throw McpFailure("invalid_arguments", "Exact positive routing operation required");
                operation = static_cast<std::uint64_t>(raw);
            }
            auto response = obj();
            auto routing = channelRoutingState(operation);
            set(response, "routing", routing);
            set(response, "completionState", routing["completionState"]);
            if (action == "cancelRouting")
            {
                const auto cancelled = routingCoordinator.cancel(operation);
                if (cancelled.accepted())
                {
                    routing = channelRoutingState(operation);
                    set(response, "routing", routing);
                    set(response, "completionState", routing["completionState"]);
                    return textResult(response);
                }
                set(response, "completionState", "failed");
                const auto code = cancelled.error == ChannelRoutingCoordinator::Error::tooLate
                    ? "routing.tooLate" : "routing.unknownOperation";
                set(response, "errorCode", code);
                auto error = obj(); set(error, "code", code);
                set(error, "message", "Published document/audio operations cannot be cancelled; use checked Undo after completion");
                set(response, "error", error);
            }
            return textResult(response);
        }
        if (args.hasProperty("operation"))
            throw McpFailure("invalid_arguments", "operation applies only to routingJob/cancelRouting");
        if ((args.hasProperty("ids") || args.hasProperty("mode") || args.hasProperty("active")) && action != "selectMany")
            throw McpFailure("invalid_arguments", "ids/mode/active apply only to selectMany");
        if (args.hasProperty("group") && action != "selectGroup")
            throw McpFailure("invalid_arguments", "group applies only to selectGroup");
        if (args.hasProperty("offset") && action != "scrollEffects")
            throw McpFailure("invalid_arguments", "offset is only valid for scrollEffects");
        if (action == "selectMany" || action == "selectAll" || action == "selectGroup")
        {
            if (args.hasProperty("insert") || args.hasProperty("offset"))
                throw McpFailure("invalid_arguments", "Multi/group selection uses explicit ids/group, not insert/offset");
            juce::Result selected = juce::Result::ok();
            if (action == "selectGroup")
            {
                if (!args.hasProperty("group")) throw McpFailure("invalid_arguments", "selectGroup requires UUID");
                selected = selectMixerGroup(args["group"].toString());
            }
            else
            {
                std::vector<int> ids;
                if (action == "selectAll") for (const auto &insert : controller.project.mixer) ids.push_back(insert.index);
                else
                {
                    auto *items = args["ids"].getArray();
                    if (!items) throw McpFailure("invalid_arguments", "selectMany requires ids");
                    for (const auto &item : *items)
                    { auto value = obj(); set(value, "id", item); ids.push_back(integer(value, "id", -1, 0, 2147483647)); }
                }
                const auto active = args.hasProperty("active") ? std::optional<int>(integer(args, "active", -1, 0, 2147483647))
                                  : action == "selectAll" ? std::optional<int>(selectedInsert) : std::nullopt;
                selected = selectMixerMany(ids, args.getProperty("mode", "replace").toString(), active);
            }
            if (selected.failed()) throw McpFailure(selected.getErrorMessage().upToFirstOccurrenceOf(":", false, false), selected.getErrorMessage());
        }
        else if (action == "select")
        {
            if (!args.hasProperty("insert"))
                throw McpFailure("invalid_arguments", "select requires insert");
            if (auto r = selectMixerInsert(integer(args, "insert", 0, 0, 2147483647)); r.failed())
                throw McpFailure(controller.hasGesture() ? "busy" : "unavailable_target",
                                 r.getErrorMessage());
        }
        else if (action == "scrollEffects")
        {
            if (!args.hasProperty("offset") || args.hasProperty("insert"))
                throw McpFailure("invalid_arguments",
                                 "scrollEffects requires offset and uses the selected insert");
            auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
            if (auto result = editor->setEffectSlotsOffset(integer(args, "offset", 0, 0, 2147483647));
                result.failed())
                throw McpFailure("invalid_arguments", result.getErrorMessage());
        }
        else if (action == "browseEffects")
        {
            if (!args.hasProperty("insert"))
                throw McpFailure("invalid_arguments", "browseEffects requires insert");
            juce::String code;
            if (auto result = browseMixerEffects(integer(args, "insert", 0, 0, 2147483647), code);
                result.failed())
                throw McpFailure(code, result.getErrorMessage());
            auto response = obj();
            set(response, "completionState", "completed");
            set(response, "effectsBrowser", mixerEffectBrowserState());
            set(response, "effectsPane",
                static_cast<Editor *>(windows[3]->getContentComponent())->effectSlotsState());
            set(response, "undoable", false);
            return textResult(response);
        }
        else if (action == "resetOverload")
        {
            if (!args.hasProperty("insert"))
                throw McpFailure("invalid_arguments", "resetOverload requires insert");
            const auto insert = integer(args, "insert", 0, 0, 2147483647);
            std::uint64_t fence = 0;
            if (auto result = resetMixerOverload(insert, fence); result.failed())
                throw McpFailure("unavailable_target", result.getErrorMessage());
            auto response = obj();
            set(response, "completionState", "completed");
            set(response, "insert", insert);
            set(response, "clearedThroughBlock", juce::int64(fence));
            set(response, "meter", mixerMeterState(insert));
            set(response, "undoable", false);
            return textResult(response);
        }
        else if (args.hasProperty("insert"))
            throw McpFailure("invalid_arguments", "state does not accept an insert target");
        auto response = mixerRoutingState();
        set(response, "effectsBrowser", mixerEffectBrowserState());
        set(response, "effectsPane",
            static_cast<Editor *>(windows[3]->getContentComponent())->effectSlotsState());
        set(response, "completionState", "completed");
        return textResult(response);
    }
    if (name == "vibera_selection")
    {
        if (args.hasProperty("revision") && number(args, "revision", -1) != double(controller.revision))
            throw McpFailure("stale_revision", "Read current project state before changing selection");
        const auto action = args["action"].toString();
        const bool clips = args["target"].toString() == "clips";
        if (args.hasProperty("loopPreview") && (clips || action != "range"))
            throw McpFailure("invalid_arguments", "loopPreview applies only to a notes range");
        const bool loopPreview = bool(args.getProperty("loopPreview", false));
        if (action != "state")
        {
            std::vector<Id> ids;
            if (action == "set")
            {
                if (!args.hasProperty("ids"))
                    throw McpFailure("invalid_arguments", "set requires ids, including [] to clear");
                for (const auto &id : *args["ids"].getArray())
                    ids.push_back(Id(juce::int64(id)));
            }
            else if (action == "lasso")
            {
                // Magic lasso: points [[beat, pitch], ...] close into a path; notes whose start lies
                // inside are selected (selected Channel unless allChannels).
                const auto *points = args["points"].getArray();
                if (clips || !points || points->size() < 3 || points->size() > 4096)
                    throw McpFailure("invalid_arguments", "lasso takes notes and 3..4096 [beat, pitch] points");
                std::vector<std::pair<double, double>> path;
                for (const auto &point : *points)
                {
                    const auto *xy = point.getArray();
                    if (!xy || xy->size() != 2 || !std::isfinite(double((*xy)[0])) || !std::isfinite(double((*xy)[1])))
                        throw McpFailure("invalid_arguments", "Each lasso point is [beat, pitch]");
                    path.push_back({double((*xy)[0]), double((*xy)[1])});
                }
                ids = lassoNotes(path, bool(args["allChannels"]));
            }
            else if (action == "all" || action == "range")
            {
                const double start = number(args, "start", 0), end = number(args, "end", 1.e12);
                const int low = integer(args, "low", 0, 0, 127), high = integer(args, "high", 127, 0, 127);
                if (action == "range" && (!args.hasProperty("end") || end <= start || high < low))
                    throw McpFailure("invalid_arguments", "range requires end > start and high >= low");
                auto matches = [&](double beat, double length, int row)
                {
                    return action == "all" ||
                           (row >= low && row <= high && beat < end &&
                            (args["criterion"].toString() == "overlaps" ? beat + length > start
                                                                        : beat >= start));
                };
                if (clips)
                {
                    for (const auto &clip : controller.project.placements)
                        if (matches(clip.beat, clip.length, clip.lane))
                            ids.push_back(clip.id);
                    for (const auto &clip : controller.project.audioPlacements)
                        if (matches(clip.beat, controller.project.audioEndBeat(clip.beat, clip.duration) - clip.beat, clip.lane))
                            ids.push_back(clip.id);
                    for (const auto &clip : controller.project.automation.placements)
                        if (matches(clip.beat, clip.length, clip.lane))
                            ids.push_back(clip.id);
                }
                else if (const auto *pattern = controller.project.pattern(selectedPattern))
                {
                    if (loopPreview)
                        ids = pianoOccurrenceRange(
                            pianoNoteOccurrences(controller.project, *pattern), start, end, low, high,
                            bool(args["allChannels"]) ? std::set<Id>{} : std::set<Id>{selectedChannel},
                            args["criterion"].toString() != "overlaps");
                    else
                        for (const auto &note : pattern->notes)
                            if ((bool(args["allChannels"]) || note.channel == selectedChannel) &&
                                matches(note.beat, note.length, note.pitch))
                                ids.push_back(note.id);
                }
            }
            const auto result = selectItems(
                clips, ids, action == "clear" ? "replace" : args.getProperty("mode", "replace").toString());
            if (result.failed())
                throw McpFailure("unavailable_target", result.getErrorMessage());
        }
        auto result = obj();
        juce::Array<juce::var> ids;
        for (auto id : clips ? selectedClips : selectedNotes)
            ids.add(juce::int64(id));
        set(result, "ids", ids);
        set(result, "target", clips ? "clips" : "notes");
        set(result, "pattern", juce::int64(selectedPattern));
        set(result, "channel", juce::int64(selectedChannel));
        set(result, "projectIdentity", controller.projectIdentity);
        set(result, "revision", juce::int64(controller.revision));
        set(result, "documentChanged", false);
        return textResult(result);
    }
    if (name == "vibera_gesture")
    {
        const auto action = args["action"].toString();
        if (action != "begin" && (args.hasProperty("sourceInsert") || args.hasProperty("targetInsert")))
            throw McpFailure("invalid_arguments", "Mixer targets are captured only at gesture begin");
        if (args.hasProperty("domain") && action != "begin")
            throw McpFailure("invalid_arguments", "Gesture domain is selected only at begin");
        if (action != "state" && action != "cancel" && args.hasProperty("revision") &&
            number(args, "revision", -1) != double(controller.revision))
            throw McpFailure("stale_revision", "Read current state before changing or committing a gesture");
        if (action == "begin")
        {
            const auto domain = args.getProperty("domain", "document").toString();
            if (domain != "document" && domain != "stepVelocity" && domain != "stepGraph" &&
                domain != "stepActivation" && domain != "swing" && domain != "samplerVoices" &&
                domain != "samplerFilter" && domain != "samplerModulation" && domain != "noteMute" &&
                domain != "mixerSend" && domain != "mixerControl" && domain != "mixerEffects")
                throw McpFailure("invalid_arguments", "Unknown gesture domain");
            if (domain == "mixerEffects")
            {
                juce::String code;
                if (auto result = admitMixerChainControl(code); result.failed())
                    throw McpFailure(code, result.getErrorMessage());
            }
            if (controller.hasGesture() || (hasPendingMidiTake() && domain != "mixerEffects") ||
                ((midiTaking || recordArmed) && domain == "document") || midiFileBusy || afterPluginState ||
                pendingExport || audio.rendering() || mcpExportOperation.isNotEmpty() ||
                std::any_of(
                    pluginOperations.begin(), pluginOperations.end(), [](const auto &job)
                    { return job.state == "pending" && job.kind != "refresh" && job.kind != "editor"; }))
                throw McpFailure("busy", "Finish the active gesture, armed recording or project I/O first");
            McpGesture gesture;
            gesture.domain = domain;
            gesture.pattern = selectedPattern;
            gesture.channel = selectedChannel;
            if (domain == "samplerFilter" || domain == "samplerModulation")
            {
                const auto *channel = controller.project.channel(gesture.channel);
                if (!channel || channel->instrument || channel->audioClip)
                    throw McpFailure("unavailable_target", "Select a local sampler Channel first");
            }
            if (domain == "mixerSend")
            {
                if (!args.hasProperty("sourceInsert") || !args.hasProperty("targetInsert"))
                    throw McpFailure("invalid_arguments",
                                     "mixerSend begin requires sourceInsert and targetInsert");
                gesture.sourceInsert = integer(args, "sourceInsert", 0, 1, 2147483647);
                gesture.targetInsert = integer(args, "targetInsert", 0, 0, 2147483647);
                if (!mixerRoute(controller.project, gesture.sourceInsert, gesture.targetInsert))
                    throw McpFailure("unavailable_target",
                                     "Enable the Mixer route before beginning its level gesture");
            }
            else if (domain == "mixerControl" || domain == "mixerEffects")
            {
                if (!args.hasProperty("sourceInsert") || args.hasProperty("targetInsert"))
                    throw McpFailure("invalid_arguments", "Mixer control requires sourceInsert only");
                gesture.sourceInsert = integer(args, "sourceInsert", 0, 0, 2147483647);
                if (!mixerInsert(controller.project, gesture.sourceInsert))
                    throw McpFailure("unavailable_target", "Mixer insert is unavailable");
            }
            else if (args.hasProperty("sourceInsert") || args.hasProperty("targetInsert"))
                throw McpFailure("invalid_arguments", "Insert targets apply only to mixerSend begin");
            gesture.id = juce::Uuid().toString();
            gesture.project = controller.projectIdentity;
            gesture.label = args.getProperty("label", "AI continuous edit").toString().trim();
            if (gesture.label.isEmpty())
                throw McpFailure("invalid_arguments", "Gesture label must not be empty");
            gesture.leaseMs = integer(args, "leaseMs", 30000, 250, 120000);
            gesture.lastActivity = juce::Time::getMillisecondCounterHiRes();
            if (routingPending()) throw McpFailure("routing.pending", "Await native selection before a gesture");
            if (!controller.beginGesture(gesture.id))
                throw McpFailure("busy", "Another edit owns the gesture");
            mcpGesture = std::move(gesture);
        }
        else
        {
            if (!mcpGesture)
                throw McpFailure("unavailable_operation", "No remote gesture; begin one first");
            if ((action != "state" || args.hasProperty("operation")) &&
                args["operation"].toString() != mcpGesture->id)
                throw McpFailure("stale_operation", "Gesture ownership token does not match");
            auto &gesture = *mcpGesture;
            if (action == "update")
            {
                if (gesture.state != "pending")
                    throw McpFailure("gesture_finished", "Gesture is terminal; begin a new transaction");
                if (args["command"].toString() == "channel.mixer")
                    throw McpFailure("routing.unsupportedGesture", "Use one canonical routing command outside a preview lease");
                const auto *info = mcpCommandInfo(args["command"].toString());
                if (gesture.domain == "stepVelocity" &&
                    (args["command"].toString() != "steps.velocity" ||
                     identity(args["args"], "pattern", selectedPattern) != gesture.pattern ||
                     identity(args["args"], "channel", selectedChannel) != gesture.channel ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure("gesture_domain", "Step velocity lease targets only its original "
                                                       "Pattern/Channel and steps.velocity command");
                if (gesture.domain == "stepActivation" &&
                    (args["command"].toString() != "steps.set" ||
                     identity(args["args"], "pattern", selectedPattern) != gesture.pattern ||
                     identity(args["args"], "channel", selectedChannel) != gesture.channel ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure(
                        "gesture_domain",
                        "Step activation lease permits only steps.set on its original Pattern/Channel");
                if (gesture.domain == "stepGraph" &&
                    ((args["command"].toString() != "steps.velocity" &&
                      args["command"].toString() != "steps.pitch" &&
                      args["command"].toString() != "steps.pan" &&
                      args["command"].toString() != "steps.fine" &&
                      args["command"].toString() != "steps.modX" &&
                      args["command"].toString() != "steps.modY" &&
                      args["command"].toString() != "steps.shift" &&
                      args["command"].toString() != "steps.repeat" &&
                      args["command"].toString() != "steps.release") ||
                     identity(args["args"], "pattern", selectedPattern) != gesture.pattern ||
                     identity(args["args"], "channel", selectedChannel) != gesture.channel ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure("gesture_domain",
                                     "Step Graph lease permits only supported "
                                     "velocity/pitch/pan/fine/shift/repeat/release/modX/modY edits "
                                     "on its original Pattern/Channel");
                if (gesture.domain == "swing" &&
                    ((args["command"].toString() != "project.swing" &&
                      args["command"].toString() != "channel.swingMix") ||
                     identity(args["args"], "channel", selectedChannel) != gesture.channel ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure(
                        "gesture_domain",
                        "Swing lease allows only global/active-Channel Swing Mix on its original target");
                if (gesture.domain == "samplerVoices" &&
                    ((!isSamplerVoiceModeCommand(args["command"].toString()) &&
                      args["command"].toString() != "channel.sampler.cut" &&
                      args["command"].toString() != "channel.sampler.cutBy" &&
                      args["command"].toString() != "channel.sampler.maxPoly" &&
                      args["command"].toString() != "channel.sampler.cutItself") ||
                     identity(args["args"], "channel", selectedChannel) != gesture.channel ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure("gesture_domain",
                                     "Sampler-voice lease allows only Cut/Cut-by/Cut-itself/Max-poly and the "
                                     "five voice-mode controls on its "
                                     "original Channel");
                if (gesture.domain == "samplerModulation" &&
                    (!args["command"].toString().startsWith("channel.sampler.modulation.") ||
                     identity(args["args"], "channel", selectedChannel) != gesture.channel ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure("gesture_domain", "Sampler modulation lease permits only modulation "
                                                       "parameters on its original Channel");
                if (gesture.domain == "samplerFilter" &&
                    ((args["command"].toString() != "channel.sampler.filterType" &&
                      args["command"].toString() != "channel.sampler.filterCutoff" &&
                      args["command"].toString() != "channel.sampler.filterResonance") ||
                     identity(args["args"], "channel", selectedChannel) != gesture.channel ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure("gesture_domain", "Sampler-filter lease allows only filter type, "
                                                       "cutoff and resonance on its original Channel");
                if (!info || !info->documentEdit || !allowed(info->id))
                    throw McpFailure("unsupported_operation",
                                     "Gesture updates require a canonical document command");
                if (gesture.domain == "mixerEffects")
                {
                    const auto *chain = mixerInsert(controller.project, gesture.sourceInsert);
                    const auto target = identity(args["args"], "target", 0);
                    const bool effect =
                        chain && std::any_of(chain->effects.begin(), chain->effects.end(),
                                             [target](const auto &effect) { return effect.id == target; });
                    if (!((info->id == "plugin.effect.mix" && effect) ||
                          (info->id == "mixer.effects.bypass" &&
                           integer(args["args"], "mixer", -1, 0, 2147483647) == gesture.sourceInsert)))
                        throw McpFailure(
                            "gesture_domain",
                            "Mixer-effects lease allows host mix/chain bypass only on its captured insert");
                }
                if (gesture.domain == "mixerControl" &&
                    ((info->id != "mixer.gain" && info->id != "mixer.pan" && info->id != "mixer.stereoSeparation") ||
                     integer(args["args"], "mixer", 0, 0, 2147483647) != gesture.sourceInsert))
                    throw McpFailure("gesture_domain",
                                     "Mixer-control lease permits gain/pan/stereo separation on its captured insert only");
                if (gesture.domain == "mixerSend" &&
                    (info->id != "mixer.send" ||
                     integer(args["args"], "mixer", 0, 0, 2147483647) != gesture.sourceInsert ||
                     integer(args["args"], "target", 0, 0, 2147483647) != gesture.targetInsert))
                    throw McpFailure("gesture_domain",
                                     "Mixer-send lease permits only its captured source/destination level");
                if (gesture.domain == "noteMute" &&
                    ((info->id != "notes.mute" && info->id != "notes.unmute") ||
                     identity(args["args"], "pattern", selectedPattern) != gesture.pattern ||
                     selectedPattern != gesture.pattern || selectedChannel != gesture.channel))
                    throw McpFailure(
                        "gesture_domain",
                        "Note-mute lease permits only score mute/unmute in its original Pattern/context");
                auto values = obj();
                set(values, "command", args["command"]);
                set(values, "args", args.getProperty("args", obj()));
                if (args.hasProperty("revision"))
                    set(values, "revision", args["revision"]);
                const juce::ScopedValueSetter<bool> dispatch(mcpGestureDispatch, true);
                // Invalid updates leave earlier valid previews intact; they do not
                // renew the lease. No partial command passes Controller validation.
                mcpCall("vibera_command", values);
                gesture.lastActivity = juce::Time::getMillisecondCounterHiRes();
            }
            else if (gesture.state == "pending" && (action == "commit" || action == "cancel"))
            {
                const auto result = action == "commit"
                                        ? controller.endGestureChecked(gesture.label, gesture.id)
                                        : controller.cancelGestureChecked(gesture.id);
                if (result.failed())
                {
                    gesture.reason = result.getErrorMessage();
                    throw McpFailure("history_refused", result.getErrorMessage());
                }
                gesture.state = action == "commit" ? "completed" : "cancelled";
                gesture.reason = action == "commit" ? "One canonical Undo transaction committed"
                                                    : "Starting project restored";
            }
        }
        auto result = obj();
        set(result, "operation", mcpGesture->id);
        set(result, "completionState", mcpGesture->state);
        set(result, "reason", mcpGesture->reason);
        set(result, "label", mcpGesture->label);
        set(result, "domain", mcpGesture->domain);
        if (mcpGesture->domain == "mixerControl" || mcpGesture->domain == "mixerEffects")
            set(result, "sourceInsert", mcpGesture->sourceInsert);
        if (mcpGesture->domain == "mixerSend")
        {
            set(result, "sourceInsert", mcpGesture->sourceInsert);
            set(result, "targetInsert", mcpGesture->targetInsert);
        }
        set(result, "pattern", juce::int64(mcpGesture->pattern));
        set(result, "channel", juce::int64(mcpGesture->channel));
        set(result, "projectIdentity", mcpGesture->project);
        set(result, "revision", juce::int64(controller.revision));
        set(result, "leaseMs", mcpGesture->leaseMs);
        set(result, "undoAvailable", controller.undo.canUndo());
        return textResult(result);
    }
    if (name == "vibera_groove")
    {
        const auto action = args["action"].toString();
        auto result = obj();
        if (action == "presets")
        {
            juce::Array<juce::var> presets;
            for (const auto &groove : factoryGrooves())
                presets.add(grooveState(groove));
            set(result, "presets", presets);
        }
        else
        {
            GrooveTemplate groove;
            juce::Result valid = juce::Result::ok();
            if (action == "capture")
            {
                const auto source = args.getProperty("source", "current").toString();
                if (source != "current" && source != "previewBaseline")
                    throw McpFailure("invalid_arguments", "Unknown groove capture source");
                const auto *project =
                    source == "previewBaseline" ? controller.quantizerBaseline() : &controller.project;
                if (!project)
                    throw McpFailure("unavailable_operation", "No original Quantizer preview baseline");
                const auto pattern = identity(args, "pattern", selectedPattern),
                           channel = identity(args, "channel", selectedChannel);
                const auto *score = project->pattern(pattern);
                std::vector<Id> ids;
                if (args.hasProperty("ids"))
                {
                    const auto *values = args["ids"].getArray();
                    if (!values || values->size() > 100000)
                        throw McpFailure("invalid_arguments",
                                         "Capture ids must be an array of at most100000 identities");
                    for (const auto &value : *values)
                    {
                        auto item = obj();
                        set(item, "id", value);
                        ids.push_back(identity(item, "id", 0));
                    }
                }
                if (args.hasProperty("name") && !args["name"].isString())
                    throw McpFailure("invalid_arguments", "Groove name must be a string");
                valid = captureGroove(*project, pattern, channel, ids,
                                      number(args, "cycleBeats", score ? score->length : 4),
                                      args.getProperty("name", "Captured groove").toString(), groove);
            }
            else if (action == "preview")
            {
                if (!quantizerGuide || !controller.hasQuantizerPreview())
                    throw McpFailure("unavailable_operation", "No active groove preview");
                groove = *quantizerGuide;
            }
            else if (action == "load" || action == "save")
            {
                if (!args["path"].isString() || !juce::File::isAbsolutePath(args["path"].toString()) ||
                    args["path"].toString().length() > 32767)
                    throw McpFailure("invalid_arguments", "Groove file path must be absolute");
                const juce::File file(args["path"].toString());
                if (action == "load")
                    valid = loadGroove(file, groove);
                else
                {
                    if (args.hasProperty("overwrite") && !args["overwrite"].isBool())
                        throw McpFailure("invalid_arguments", "overwrite must be boolean");
                    valid = parseGroove(args["groove"], groove);
                    if (valid.wasOk())
                        valid = saveGroove(groove, file, bool(args.getProperty("overwrite", false)));
                }
            }
            else if (action == "validate")
                valid = parseGroove(args["groove"], groove);
            else
                throw McpFailure("invalid_arguments", "Unknown groove action");
            if (valid.failed())
                throw McpFailure(action == "load" || action == "save" ? "groove_io_failed" : "invalid_groove",
                                 valid.getErrorMessage());
            set(result, "groove", grooveState(groove));
        }
        set(result, "documentChanged", false);
        set(result, "revision", juce::int64(controller.revision));
        return textResult(result);
    }
    if (name == "vibera_controller")
    {
        const auto action = args["action"].toString();
        const auto readback = [&](bool committed = false, int changedIndex = -1)
        {
            auto current = mcpCall("vibera_state", obj())["structuredContent"];
            set(current, "completionState", "completed");
            juce::String identity;
            const auto identified = midiController.bindingProfileIdentity(identity);
            set(current, "controllerProfileIdentity", identity);
            set(current, "controllerProfileIdentityKnown", identified.wasOk());
            if (identified.failed())
                set(current, "controllerProfileIdentityError", identified.getErrorMessage());
            set(current, "controllerBindingPersistence",
                midiController.bindingChangesPersisted() ? "persistentSettings" : "sessionOnly");
            juce::Array<juce::var> parameters, commands;
            for (const auto &id : MidiController::parameterActions())
                parameters.add(id);
            for (const auto &command : commandRegistry())
                if (command.userAction)
                    commands.add(command.id);
            {
                // Settings > Controller port lists and Learn state (ports / connectLaunchkey / learn / cancel).
                auto ports = obj();
                set(ports, "input", midiController.inputIdentifier());
                set(ports, "output", midiController.outputIdentifier());
                set(ports, "status", midiController.status());
                juce::Array<juce::var> inputs, outputs; // From the last ports / connectLaunchkey call (a rescan).
                for (const auto &device : controlInputDevices)
                {
                    auto entry = obj();
                    set(entry, "name", device.name);
                    set(entry, "identifier", device.identifier);
                    inputs.add(entry);
                }
                for (const auto &device : controlOutputDevices)
                {
                    auto entry = obj();
                    set(entry, "name", device.name);
                    set(entry, "identifier", device.identifier);
                    outputs.add(entry);
                }
                set(ports, "inputs", inputs);
                set(ports, "outputs", outputs);
                set(current, "controllerPorts", ports);
                set(current, "controllerLearning", midiController.learnIndex);
            }
            set(current, "controllerParameterActions", parameters);
            set(current, "controllerBindingCommands", commands);
            if (committed)
            {
                auto change = obj();
                set(change, "index", changedIndex);
                set(change, "configurationCommitted", true);
                set(change, "persisted", midiController.bindingChangesPersisted());
                set(change, "projectUndoable", false);
                set(change, "completionScope", "controllerBindingConfiguration");
                set(current, "controllerBindingChange", change);
            }
            return textResult(current);
        };
        if (action == "state" || action == "setBinding" || action == "removeBinding")
        {
            const juce::StringArray allowed =
                action == "state"           ? juce::StringArray{"action", "projectIdentity"}
                : action == "removeBinding" ? juce::StringArray{"action", "index", "expectedProfile", "projectIdentity"}
                                            : juce::StringArray{"action", "index", "binding", "expectedProfile", "projectIdentity"};
            for (const auto &property : args.getDynamicObject()->getProperties())
                if (!allowed.contains(property.name.toString()))
                    throw McpFailure("controller.invalidArguments",
                                     "Unknown argument for controller operation");
            if (action == "state")
                return readback();
            if (action == "removeBinding")
            {
                // Same guarded service as Settings > Controllers Remove.
                if (!args.hasProperty("index") || !args["expectedProfile"].isString())
                    throw McpFailure("controller.invalidArguments", "index/expectedProfile required");
                const auto removed = midiController.removeBinding(size_t(integer(args, "index", -1, 0, 511)),
                                                                  args["expectedProfile"].toString());
                if (removed.failed())
                    throw McpFailure(removed.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                                     removed.getErrorMessage());
                return readback();
            }
            if (!args.hasProperty("index") || !args["expectedProfile"].isString() ||
                !args.hasProperty("binding"))
                throw McpFailure("controller.invalidArguments",
                                 "Complete binding/index/expectedProfile required");
            const int index = integer(args, "index", -1, 0, 511);
            ControllerBinding candidate;
            if (auto parsed = MidiController::parseBinding(args["binding"], candidate); parsed.failed())
                throw McpFailure("controller.invalidBinding", parsed.getErrorMessage());
            const auto result =
                midiController.setBinding(size_t(index), candidate, args["expectedProfile"].toString());
            if (result.failed())
                throw McpFailure(result.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                                 result.getErrorMessage());
            return readback(true, index);
        }
        if (action.startsWith("samplerLearn"))
            return textResult(samplerControllerOperation(args));
        if (action == "learn" || action == "cancel" || action == "preset" || action == "ports" ||
            action == "connectLaunchkey")
        {
            // The Settings > Controller buttons, through the same Studio / MidiController calls.
            const juce::StringArray allowed = action == "learn"    ? juce::StringArray{"action", "index"}
                                              : action == "preset" ? juce::StringArray{"action", "replace"}
                                              : action == "ports"  ? juce::StringArray{"action", "input", "output"}
                                                                   : juce::StringArray{"action"};
            for (const auto &property : args.getDynamicObject()->getProperties())
                if (!allowed.contains(property.name.toString()))
                    throw McpFailure("controller.invalidArguments", "Unknown argument for controller operation");
            if (action == "learn")
            {
                if (!args.hasProperty("index") || midiController.bindings.empty())
                    throw McpFailure("controller.invalidArguments", "learn requires the index of an existing mapping");
                const int index = int(number(args, "index", -1));
                if (index < 0 || index >= int(midiController.bindings.size()))
                    throw McpFailure("controller.invalidArguments", "learn requires the index of an existing mapping");
                midiController.armLearn(index);
                if (midiController.learnIndex != index) // armLearn reports refusals in lastMessage.
                    throw McpFailure("controller.learnRefused", midiController.lastMessage);
            }
            else if (action == "cancel")
                midiController.cancelLearn();
            else if (action == "preset")
            {
                if (!args.hasProperty("replace") || !bool(args["replace"]))
                    throw McpFailure("controller.confirmationRequired",
                                     "preset replaces the current mappings; pass replace:true");
                loadLaunchkeyPreset();
            }
            else if (action == "ports")
            {
                controlInputDevices = juce::MidiInput::getAvailableDevices(); // Rescan, like opening the page.
                controlOutputDevices = juce::MidiOutput::getAvailableDevices();
                const auto wantedInput = args["input"].toString(), wantedOutput = args["output"].toString();
                bool inputKnown = wantedInput.isEmpty(), outputKnown = wantedOutput.isEmpty();
                for (const auto &device : controlInputDevices)
                    inputKnown = inputKnown || device.identifier == wantedInput;
                for (const auto &device : controlOutputDevices)
                    outputKnown = outputKnown || device.identifier == wantedOutput;
                if (!inputKnown || !outputKnown)
                    throw McpFailure("controller.unknownPort", "input/output are identifiers from controllerPorts");
                if (const auto opened = setControllerPorts(wantedInput, wantedOutput); opened.failed())
                    throw McpFailure("controller.portRefused", opened.getErrorMessage());
            }
            else
            {
                juce::String text;
                if (const auto connected = connectLaunchkey(text); connected.failed())
                    throw McpFailure(connected.getErrorMessage().startsWith("controller.launchkeyNotFound")
                                         ? "controller.launchkeyNotFound"
                                         : "controller.portRefused",
                                     text);
            }
            return readback();
        }
        if (!controllerTestMode)
            throw McpFailure("controller.diagnosticOnly",
                             "MIDI injection and configure remain owned diagnostics");
        if (action == "configure")
        {
            const auto profile = args["profile"];
            if (profile["input"].toString().isNotEmpty() || profile["output"].toString().isNotEmpty())
                throw std::runtime_error("MCP controller profiles cannot open hardware ports");
            const auto result = midiController.importState(profile);
            if (result.failed())
                throw std::runtime_error(result.getErrorMessage().toStdString());
        }
        else if (action == "inject")
        {
            const int port = integer(args, "port", 1, 0, 1), kind = integer(args, "kind", 0, 0, 6);
            const int channel =
                integer(args, "channel", kind >= 4 ? 0 : 1, kind >= 4 ? 0 : 1, kind >= 4 ? 0 : 16);
            const int number = integer(args, "number", 0, 0, kind >= 3 ? 0 : 127);
            const int value = integer(args, "value", 127, 0, kind == 3 ? 16383 : 127);
            juce::MidiMessage message;
            if (kind == 0)
                message = juce::MidiMessage::controllerEvent(channel, number, value);
            if (kind == 1)
                message = juce::MidiMessage::noteOn(channel, number, juce::uint8(value));
            if (kind == 2)
                message = juce::MidiMessage::programChange(channel, number);
            if (kind == 3)
                message = juce::MidiMessage::pitchWheel(channel, value);
            if (kind == 4)
                message = juce::MidiMessage::midiStart();
            if (kind == 5)
                message = juce::MidiMessage::midiStop();
            if (kind == 6)
                message = juce::MidiMessage::midiContinue();
            if (port == 0)
                audio.midiInput.handleIncomingMidiMessage(nullptr, message);
            else
                audio.midiInput.controls.observe(message, 1);
            controllerMcpRejected = false;
            const juce::ScopedValueSetter<bool> injection(controllerMcpInjection, true);
            midiController.drain();
            if (controllerMcpRejected)
            {
                if (status.startsWith("controller.") || status.startsWith("samplerVoice."))
                    throw McpFailure(status.upToFirstOccurrenceOf(":", false, false), status);
                throw std::runtime_error(
                    "Mapped action not exposed through MCP; filesystem/dialog boundary preserved");
            }
        }
        else if (action != "state")
            throw std::runtime_error("Unknown controller test action");
        return readback();
    }
    if (name == "vibera_snapshot")
    {
        auto *component = getTopLevelComponent();
        if (args.hasProperty("channel"))
        {
            const Id id = juce::int64(args["channel"]);
            if (const auto *c = controller.project.channel(id); c && !c->instrument)
                openInstrument(id);
            const auto found = instrumentWindows.find(id);
            if (found == instrumentWindows.end() || !found->second || !found->second->isVisible())
                throw McpFailure("unavailable_target", "No local Channel settings window for that Channel");
            if (args.hasProperty("page"))
                found->second->showPage(int(args["page"]));
            component = found->second.get();
        }
        auto scale = juce::jmin(1., 1600. / component->getWidth(), 1000. / component->getHeight());
        auto image = component->createComponentSnapshot(component->getLocalBounds(), true, float(scale));
        juce::MemoryOutputStream bytes;
        if (!juce::PNGImageFormat{}.writeImageToStream(image, bytes))
            throw std::runtime_error("PNG encoding failed");
        auto result = obj(), content = obj();
        set(content, "type", "image");
        set(content, "mimeType", "image/png");
        set(content, "data", juce::Base64::toBase64(bytes.getData(), bytes.getDataSize()));
        set(result, "content", juce::Array<juce::var>{content});
        return result;
    }
    if (name == "vibera_note_modes")
        return textResult(pianoNoteModes(args));
    if (name == "vibera_update")
    {
        const auto updateAction = args["action"].toString();
        if (updateAction == "check")
            if (auto started = requestUpdateCheck(); started.failed())
                throw McpFailure(started.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                                 started.getErrorMessage().fromFirstOccurrenceOf(": ", false, false));
        if (updateAction == "settings")
        {
            if (!args.hasProperty("autoCheck") && !args.hasProperty("includePreview"))
                throw McpFailure("invalid_arguments", "settings takes autoCheck and/or includePreview");
            for (const char *key : {"autoCheck", "includePreview"})
                if (args.hasProperty(key))
                    if (const auto saved = setUpdateSetting(key, bool(args[key])); saved.failed())
                        throw McpFailure("invalid_arguments", saved.getErrorMessage());
        }
        if (updateAction == "showOffer")
        {
            // The menu's "Show <release>..." item: only offered while a release is available.
            if (!availableUpdate || updatePhase != "available")
                throw McpFailure("update.notAvailable", "No update is available to show; run check and poll status");
            showUpdateOffer();
        }
        if (updateAction == "offerInstall")
        {
            if (updatePhase != "ready")
                throw McpFailure("update.notReady", "No downloaded update is ready to install");
            offerUpdateInstall();
        }
        return textResult(updateStatus());
    }
    if (name == "vibera_project")
        return textResult(mcpProjectOperation(args));
    if (name == "vibera_picker")
    {
        auto *editor = static_cast<Editor *>(windows[2]->getContentComponent());
        auto *picker = editor->clipPicker();
        if (args.hasProperty("revision") && number(args, "revision", -1) != double(controller.revision))
            throw McpFailure("stale_revision", "Read current state before editing the Picker");
        const auto action = args["action"].toString();
        if (action == "type")
        {
            const auto kind = args["kind"].toString();
            if (kind != "pattern" && kind != "audio" && kind != "automation")
                throw McpFailure("invalid_arguments", "type requires kind");
            picker->setKind(kind == "pattern" ? 0 : kind == "audio" ? 1 : 2);
        }
        else if (action == "filter" || action == "groupFilter")
        {
            if (!args.hasProperty("text"))
                throw McpFailure("invalid_arguments", "Filter requires text (empty clears it)");
            if (action == "filter")
                picker->setFilter(args["text"].toString());
            else
            {
                const auto name = args["text"].toString();
                if (name.isNotEmpty() &&
                    std::none_of(controller.project.patterns.begin(), controller.project.patterns.end(),
                                 [&](const auto &pattern) { return pattern.group == name; }))
                    throw McpFailure("unavailable_target", "Pattern filter group does not exist");
                picker->setGroupFilter(args["text"].toString());
            }
        }
        else if (action == "select")
        {
            const auto *ids = args["ids"].getArray();
            if (!ids)
                throw McpFailure("invalid_arguments", "Selection requires explicit source ids");
            std::vector<Id> selection;
            for (const auto &id : *ids)
            {
                const auto target = juce::int64(id);
                if (std::find(picker->visibleIds().begin(), picker->visibleIds().end(), target) ==
                    picker->visibleIds().end())
                    throw McpFailure("unavailable_target", "Source is missing or filtered out");
                selection.push_back(target);
            }
            picker->selectIds(selection);
        }
        else if (action == "place")
        {
            auto selection = editor->pickerSelection();
            selection.beat = number(args, "beat", 0);
            selection.lane = integer(args, "lane", 0, 0, 127);
            if (!editor->placeSources(selection))
                throw McpFailure("validation_failed", status);
        }
        else if (action == "width")
            editor->setPickerWidth(integer(args, "width", -1, 120, 280));
        else if (action == "scroll")
            picker->setScrollOffset(integer(args, "scrollY", -1, 0, picker->maximumScrollOffset()));
        else if (action == "visible")
        {
            if (!args.hasProperty("visible"))
                throw McpFailure("invalid_arguments", "visible requires a boolean");
            editor->setPickerShown(bool(args["visible"]));
        }
        else if (action == "openRack")
            showEditor(0);
        else if (action != "state")
            throw McpFailure("unsupported_operation", "Unknown Picker action");
        auto state = obj();
        juce::Array<juce::var> visible, selection;
        for (const auto id : picker->visibleIds())
            visible.add(juce::int64(id));
        for (const auto id : picker->selectedIds())
            selection.add(juce::int64(id));
        set(state, "visible", editor->isPickerShown());
        set(state, "width", editor->actualPickerWidth());
        set(state, "scrollY", picker->scrollOffset());
        set(state, "maximumScrollY", picker->maximumScrollOffset());
        set(state, "kind", picker->sourceKind());
        set(state, "text", picker->filterText());
        set(state, "group", picker->groupFilter());
        set(state, "sourceIds", visible);
        set(state, "selection", selection);
        set(state, "projectIdentity", controller.projectIdentity);
        set(state, "revision", juce::int64(controller.revision));
        return textResult(state);
    }
    if (name == "vibera_batch")
    {
        if (routingPending()) throw McpFailure("routing.pending", "Await native selection first");
        if (controller.hasGesture() || midiTaking || hasPendingMidiTake())
            throw McpFailure("busy",
                             "Finish the active gesture or finish/retry/cancel the MIDI take before a batch");
        if (args.hasProperty("revision") && number(args, "revision", -1) != double(controller.revision))
            throw McpFailure("stale_revision", "Read state before a batch edit");
        const auto *steps = args["commands"].getArray();
        if (!steps || steps->isEmpty())
            throw McpFailure("invalid_arguments", "A batch needs 1 to 64 document commands");
        for (const auto &step : *steps)
        {
            if (step["command"].toString() == "plugin.io" ||
                step.getProperty("args", obj()).hasProperty("pluginIo"))
                throw McpFailure(
                    "pluginIo.unsupportedBatch",
                    "Run plugin.io separately and await its native transaction; no batch changes made");
            const auto *info = mcpCommandInfo(step["command"].toString());
            if (!info || !info->documentEdit || juce::String(info->mcp) != "command")
                throw McpFailure("unsupported_operation", "Batch supports canonical document edits only");
        }
        // A pure display batch is cheap to validate against a private document.
        // Refuse an invalid later target/patch before publishing ANY earlier
        // edit, rather than publishing and cancelling a transient history gesture.
        const bool appearanceBatch =
            std::all_of(steps->begin(), steps->end(), [](const auto &step)
                        { return isChannelAppearanceCommand(step["command"].toString()); });
        if (appearanceBatch)
        {
            auto candidate = controller.project;
            for (const auto &step : *steps)
            {
                const auto name = step["command"].toString();
                const auto values = step.getProperty("args", obj());
                validateMcpSchema(commandArgumentSchema(name), values);
                CommandArgs parsed;
                parsed.channel = identity(values, "channel", 0);
                if (const auto *ids = values["ids"].getArray())
                    for (const auto &id : *ids)
                        parsed.ids.push_back(Id(juce::int64(id)));
                if (auto result = decodeChannelAppearance(values, parsed); result.failed())
                    throw McpFailure("channelAppearance.invalidArguments", result.getErrorMessage());
                if (auto result = applyChannelAppearance(candidate, name, parsed); result.failed())
                    throw McpFailure("validation_failed", result.getErrorMessage());
            }
        }
        const bool anyRouting = std::any_of(steps->begin(), steps->end(), [](const auto &step)
                                            { return step["command"].toString() == "channel.mixer"; });
        if (anyRouting)
        {
            if (!std::all_of(steps->begin(), steps->end(),
                             [](const auto &step) { return step["command"].toString() == "channel.mixer"; }))
                throw McpFailure("routing.unsupportedMixedSequence",
                                 "Route/content batch needs one joint plan");
            std::vector<CommandArgs> assignments;
            for (const auto &step : *steps)
            {
                const auto values = step.getProperty("args", obj());
                validateMcpSchema(commandArgumentSchema("channel.mixer"), values);
                CommandArgs assignment;
                assignment.channel = identity(values, "channel", selectedChannel);
                assignment.mixer = integer(values, "mixer", 0, 0, 2147483647);
                assignments.push_back(assignment);
            }
            const auto result = applyRoutingSequence(assignments, "MCP Channel routing batch");
            if (lastRoutingDispatchOperation)
                return textResult(channelRoutingState(lastRoutingDispatchOperation));
            if (result.failed())
                throw McpFailure("routing.refused", result.getErrorMessage());
            return textResult(channelRoutingState(lastRoutingDispatchOperation));
        }
        if (audio.channelRoutingReadback().overlayActive || audio.documentProjectionDeferred())
        {
            bool structural = false;
            for (const auto &step : *steps)
            {
                const auto values = step.getProperty("args", obj());
                validateMcpSchema(commandArgumentSchema(step["command"].toString()), values);
                CommandArgs probe;
                probe.channel = identity(values, "channel", selectedChannel);
                probe.target = step["command"].toString().startsWith("clip.") ? identity(values, "target", 0) : 0;
                if (auto* ids = values["ids"].getArray())
                    for (const auto& id : *ids)
                    { auto target = obj(); set(target, "id", id); probe.ids.push_back(isMixerGroupCommand(step["command"].toString())
                        ? Id(integer(target, "id", -1, step["command"].toString().startsWith("mixer.selection.") ? 0 : 1, 2147483647))
                        : identity(target, "id", 0)); }
                structural |= routingStructuralCommand(step["command"].toString(), probe);
            }
            if (structural)
                if (auto ready = restoreRoutingBeforeStructural(); ready.failed())
                    throw McpFailure("routing.restoreRefused", ready.getErrorMessage());
        }
        const auto batchRevisionBefore = controller.revision;
        const juce::ScopedValueSetter<juce::String> batchOwner(mcpBatchOwner,
                                                               "mcp-batch-" + juce::Uuid().toString());
        if (!controller.beginGesture(mcpBatchOwner))
            throw McpFailure("gesture_owned", "Cannot acquire document batch gesture");
        try
        {
            for (const auto &step : *steps)
                mcpCall("vibera_command", step);
            const auto result = controller.endGestureChecked("MCP document batch", mcpBatchOwner);
            if (result.failed())
                throw McpFailure("history_refused", result.getErrorMessage());
        }
        catch (...)
        {
            const auto rollback = controller.cancelGestureChecked(mcpBatchOwner);
            if (rollback.failed())
                throw McpFailure("rollback_pending", rollback.getErrorMessage());
            throw;
        }
        if (appearanceBatch)
        {
            auto response = mcpCall("vibera_state", obj())["structuredContent"];
            set(response, "completionState", "completed");
            set(response, "completionScope", "canonicalChannelAppearanceBatch");
            set(response, "changed", batchRevisionBefore != controller.revision);
            set(response, "dspSettlingAcknowledged", false);
            return textResult(response);
        }
        return mcpCall("vibera_state", obj());
    }
    if (name == "vibera_inspect")
    {
        auto data = obj();
        juce::Array<juce::var> rows;
        const auto entity = args["entity"].toString();
        const bool automationChild = entity == "automationPoints" || entity == "automationLinks";
        if (args.hasProperty("source") != automationChild)
            throw McpFailure("automation.invalidArguments",
                             "source is required only for automationPoints/automationLinks");
        if ((args.hasProperty("group") && entity != "clipGroupMembers") ||
            (entity == "clipGroupMembers" && !args.hasProperty("group")))
            throw McpFailure("invalid_arguments", "group is required only for clipGroupMembers");
        const auto offset = integer(args, "offset", 0, 0, 2147483647);
        const auto limit = integer(args, "limit", 128, 1, 512);
        int total = 0;
        auto append = [&](const auto &value)
        {
            if (total >= offset && rows.size() < limit)
                rows.add(value);
            ++total;
        };
        if (entity == "automationSources")
            for (const auto &source : controller.project.automation.sources)
            {
                auto row = obj(); set(row, "id", juce::int64(source.id)); set(row, "name", juce::String::fromUTF8(source.name.c_str()));
                set(row, "length", source.length); set(row, "pointCount", int(source.points.size()));
                set(row, "linkCount", int(source.links.size())); set(row, "colour", juce::int64(source.colour)); append(row);
            }
        else if (automationChild)
        {
            const auto *source = controller.project.automationSource(identity(args, "source", 0));
            if (!source) throw McpFailure("automation.missingSource", "Stable Automation source is unavailable");
            set(data, "source", juce::int64(source->id));
            if (entity == "automationPoints")
                for (const auto &point : source->points)
                {
                    if (total < offset || rows.size() >= limit) { ++total; continue; }
                    auto row = obj(); set(row, "id", juce::int64(point.id)); set(row, "beat", point.beat);
                    set(row, "value", point.value); set(row, "shape", int(point.incoming)); set(row, "tension", point.tension); append(row);
                }
            else
                for (const auto &link : source->links)
                {
                    if (total < offset || rows.size() >= limit) { ++total; continue; }
                    auto row = obj(); set(row, "kind", int(link.key.kind)); set(row, "owner", juce::int64(link.key.owner));
                    set(row, "parameter", juce::int64(link.key.parameter)); set(row, "minimum", link.minimum); set(row, "maximum", link.maximum); append(row);
                }
        }
        else if (entity == "scheduledNotes")
        {
            // What Pattern playback sends for the Pattern (default selected), in order: notes plus
            // expression-map keyswitches (note 0) placed just before their articulated notes.
            const auto pattern = args.hasProperty("pattern") ? Id(juce::int64(args["pattern"])) : selectedPattern;
            if (!controller.project.pattern(pattern))
                throw McpFailure("invalid_arguments", "Existing Pattern required");
            for (const auto &e : sequence(controller.project, true, pattern))
            {
                auto row = obj();
                set(row, "channel", juce::int64(e.channel));
                set(row, "beat", e.beat);
                set(row, "length", e.length);
                set(row, "pitch", e.pitch);
                set(row, "velocity", e.velocity);
                set(row, "note", juce::int64(e.note));
                set(row, "keyswitch", e.note == 0);
                append(row);
            }
        }
        else if (entity == "scheduledControls")
        {
            // MIDI control messages Pattern playback sends to plugin Channels (Pattern controllers,
            // expression-map CC / program changes, Channel PITCH), in time order per Channel.
            const auto pattern = args.hasProperty("pattern") ? Id(juce::int64(args["pattern"])) : selectedPattern;
            if (!controller.project.pattern(pattern))
                throw McpFailure("invalid_arguments", "Existing Pattern required");
            if (const auto doc = compileLiveDocument(controller.project, true, pattern, 0))
                for (const auto &channel : doc->channels)
                    for (const auto &control : channel.controls)
                    {
                        auto row = obj();
                        set(row, "channel", juce::int64(channel.id));
                        set(row, "seconds", control.time);
                        set(row, "status", int(control.bytes[0]));
                        set(row, "data1", int(control.bytes[1]));
                        set(row, "data2", int(control.bytes[2]));
                        append(row);
                    }
        }
        else if (entity == "macros")
        {
            AutomationProgram program;
            std::string error;
            const bool compiled = AutomationProgram::compile(controller.project.automation, true, program, error,
                                                             controller.project.automationDefaults());
            for (const auto &m : controller.project.automation.macros)
            {
                auto row = obj();
                set(row, "id", juce::int64(m.id));
                set(row, "name", juce::String::fromUTF8(m.name.c_str()));
                set(row, "value", m.value);
                juce::Array<juce::var> targets;
                for (const auto &link : m.targets)
                {
                    auto target = obj();
                    set(target, "kind", int(link.key.kind));
                    set(target, "owner", juce::int64(link.key.owner));
                    set(target, "parameter", juce::int64(link.key.parameter));
                    set(target, "minimum", link.minimum);
                    set(target, "maximum", link.maximum);
                    const auto *lane = compiled ? program.find(link.key) : nullptr;
                    set(target, "currentValue", lane ? juce::var(lane->valueAt(0)) : juce::var());
                    targets.add(target);
                }
                set(row, "targets", targets);
                append(row);
            }
        }
        else if (entity == "modulators")
        {
            // The compiled program proves what playback reads: quarter-cycle samples from beat 0.
            AutomationProgram program;
            std::string error;
            const bool compiled = AutomationProgram::compile(controller.project.automation, true, program, error,
                                                             controller.project.automationDefaults());
            for (const auto &m : controller.project.automation.modulators)
            {
                auto row = obj();
                set(row, "id", juce::int64(m.id));
                set(row, "name", juce::String::fromUTF8(m.name.c_str()));
                set(row, "kind", int(m.link.key.kind));
                set(row, "owner", juce::int64(m.link.key.owner));
                set(row, "parameter", juce::int64(m.link.key.parameter));
                set(row, "minimum", m.link.minimum);
                set(row, "maximum", m.link.maximum);
                set(row, "shape", juce::String(modulatorShapeName(m.shape)));
                set(row, "period", m.period);
                set(row, "depth", m.depth);
                set(row, "phase", m.phase);
                set(row, "enabled", m.enabled);
                const auto *lane = compiled ? program.find(m.link.key) : nullptr;
                set(row, "active", lane != nullptr && m.enabled);
                juce::Array<juce::var> samples;
                if (lane)
                    for (int i = 0; i < 4; ++i)
                        samples.add(lane->valueAt(m.period * i / 4));
                set(row, "quarterCycleValues", samples);
                append(row);
            }
        }
        else if (entity == "automationInitialValues")
            for (const auto &value : controller.project.automation.initialValues)
            {
                auto row = obj(); set(row, "kind", int(value.key.kind)); set(row, "owner", juce::int64(value.key.owner));
                set(row, "parameter", juce::int64(value.key.parameter)); set(row, "value", value.value); append(row);
            }
        else if (entity == "patterns")
            for (const auto &p : controller.project.patterns)
            {
                auto row = obj();
                set(row, "id", juce::int64(p.id));
                set(row, "name", p.name);
                set(row, "group", p.group);
                set(row, "length", p.length);
                set(row, "autoLength", p.autoLength);
                set(row, "numerator", p.numerator);
                set(row, "denominator", p.denominator);
                set(row, "effectiveNumerator", patternNumerator(controller.project, p));
                set(row, "effectiveDenominator", patternDenominator(controller.project, p));
                set(row, "barBeats", patternBarBeats(controller.project, p));
                set(row, "noteCount", int(p.notes.size()));
                {
                    juce::Array<juce::var> marks;
                    for (const auto &m : p.markers)
                    {
                        auto entry = obj();
                        set(entry, "id", juce::int64(m.id));
                        set(entry, "beat", m.beat);
                        set(entry, "name", m.name);
                        marks.add(entry);
                    }
                    set(row, "markers", marks);
                }
                juce::Array<juce::var> loops;
                for (const auto &loop : p.channelLoops)
                {
                    auto item = obj();
                    set(item, "channel", juce::int64(loop.channel));
                    set(item, "mode", channelLoopModeName(loop.mode));
                    set(item, "length", loop.length);
                    set(item, "resolvedLength", channelLoopLength(controller.project, p, loop.channel));
                    loops.add(item);
                }
                set(row, "channelLoops", loops);
                append(row);
            }
        else if (entity == "noteOccurrences")
        {
            const auto *pattern = controller.project.pattern(identity(args, "pattern", selectedPattern));
            if (!pattern)
                throw McpFailure("unavailable_target", "Pattern does not exist");
            set(data, "pattern", juce::int64(pattern->id));
            set(data, "coordinateSpace", "rawUnswungPianoPreview");
            set(data, "documentChanged", false);
            for (const auto &appearance : pianoNoteOccurrences(controller.project, *pattern))
            {
                if (total < offset || rows.size() >= limit)
                {
                    ++total;
                    continue;
                }
                const auto &n = appearance.note;
                auto row = obj();
                set(row, "id", juce::int64(n.id));
                set(row, "sourceId", juce::int64(n.id));
                set(row, "channel", juce::int64(n.channel));
                set(row, "beat", n.beat);
                set(row, "length", n.length);
                set(row, "sourceBeat", appearance.sourceBeat);
                set(row, "sourceLength", appearance.sourceLength);
                set(row, "iteration", int(appearance.iteration));
                set(row, "repeated", appearance.repeated());
                set(row, "dormant", appearance.dormant);
                set(row, "clippedEnd", appearance.clippedEnd);
                set(row, "pitch", n.pitch);
                set(row, "velocity", n.velocity);
                set(row, "step", n.step);
                set(row, "muted", n.muted);
                set(row, "slide", n.slide);
                set(row, "portamento", n.portamento);
                set(row, "colourGroup", n.colourGroup);
                set(row, "noteGroup", juce::int64(n.group));
                set(row, "articulation", n.articulation);
                set(row, "midiChannel", n.colourGroup + 1);
                set(row, "transitionSupported",
                    controller.project.channel(n.channel) &&
                        !controller.project.channel(n.channel)->instrument &&
                        !controller.project.channel(n.channel)->audioClip);
                set(row, "transitionAcknowledged", juce::var{});
                set(row, "pan", n.pan);
                set(row, "fineCents", n.fineCents);
                set(row, "modX", n.modX);
                set(row, "modY", n.modY);
                set(row, "shiftPercent", n.shiftPercent);
                set(row, "repeatMode", n.repeatMode);
                set(row, "repeatRate", noteRepeatRate(n.repeatMode)->label);
                set(row, "repeatIntervalBeats", noteRepeatInterval(n.repeatMode));
                set(row, "releaseVelocity", n.releaseVelocity);
                set(row, "releaseMidiVelocity", int(noteReleaseMidiVelocity(n.releaseVelocity)));
                const auto *releaseChannel = controller.project.channel(n.channel);
                set(row, "releaseVelocityAudioResponse",
                    releaseChannel ? noteReleaseAudioResponse(*releaseChannel) : "unavailable");
                set(row, "shiftDelayBeats", noteShiftDelay(n.shiftPercent));
                set(row, "shiftSupported",
                    controller.project.channel(n.channel) &&
                        !controller.project.channel(n.channel)->audioClip);
                const auto *expressionChannel = controller.project.channel(n.channel);
                const bool localExpression =
                    expressionChannel && !expressionChannel->instrument && !expressionChannel->audioClip;
                set(row, "noteExpressionSupported", localExpression);
                set(row, "noteModulationSupported", localExpression);
                set(row, "noteModulationScope",
                    localExpression
                        ? "Original local sampler composition: clamp(global + 2*(note-0.5),0,1). Mod X "
                          "offsets cutoff; Mod Y offsets resonance. Filter Off is a no-op. Source metadata, "
                          "not a voice/DSP ACK."
                        : "Mod X/Y stored only; native/VST3 and Audio Clip per-note sampler modulation "
                          "unsupported.");
                set(row, "noteModulationAcknowledged", juce::var{});
                set(row, "noteExpressionScope",
                    localExpression ? "Local sampler; model state, check pendingAudioUpdate separately"
                                    : "Pan/Fine stored only; native/VST3 Pan/Fine unsupported. Shift uses "
                                      "score scheduling");
                append(row);
            }
        }
        else if (entity == "notes")
        {
            const auto *pattern = controller.project.pattern(identity(args, "pattern", selectedPattern));
            if (!pattern)
                throw McpFailure("unavailable_target", "Pattern does not exist");
            for (const auto &n : pattern->notes)
            {
                auto row = obj();
                set(row, "id", juce::int64(n.id));
                set(row, "channel", juce::int64(n.channel));
                set(row, "beat", n.beat);
                set(row, "length", n.length);
                set(row, "pitch", n.pitch);
                set(row, "velocity", n.velocity);
                set(row, "step", n.step);
                set(row, "muted", n.muted);
                set(row, "slide", n.slide);
                set(row, "portamento", n.portamento);
                set(row, "colourGroup", n.colourGroup);
                set(row, "noteGroup", juce::int64(n.group));
                set(row, "articulation", n.articulation);
                set(row, "midiChannel", n.colourGroup + 1);
                set(row, "transitionSupported",
                    controller.project.channel(n.channel) &&
                        !controller.project.channel(n.channel)->instrument &&
                        !controller.project.channel(n.channel)->audioClip);
                set(row, "transitionAcknowledged", juce::var{});
                set(row, "pan", n.pan);
                set(row, "fineCents", n.fineCents);
                set(row, "modX", n.modX);
                set(row, "modY", n.modY);
                set(row, "shiftPercent", n.shiftPercent);
                set(row, "repeatMode", n.repeatMode);
                set(row, "repeatRate", noteRepeatRate(n.repeatMode)->label);
                set(row, "repeatIntervalBeats", noteRepeatInterval(n.repeatMode));
                set(row, "releaseVelocity", n.releaseVelocity);
                set(row, "releaseMidiVelocity", int(noteReleaseMidiVelocity(n.releaseVelocity)));
                const auto *releaseChannel = controller.project.channel(n.channel);
                set(row, "releaseVelocityAudioResponse",
                    releaseChannel ? noteReleaseAudioResponse(*releaseChannel) : "unavailable");
                set(row, "shiftDelayBeats", noteShiftDelay(n.shiftPercent));
                set(row, "shiftSupported",
                    controller.project.channel(n.channel) &&
                        !controller.project.channel(n.channel)->audioClip);
                const auto *expressionChannel = controller.project.channel(n.channel);
                const bool localExpression =
                    expressionChannel && !expressionChannel->instrument && !expressionChannel->audioClip;
                set(row, "noteExpressionSupported", localExpression);
                set(row, "noteModulationSupported", localExpression);
                set(row, "noteModulationScope",
                    localExpression
                        ? "Original local sampler composition: clamp(global + 2*(note-0.5),0,1). Mod X "
                          "offsets cutoff; Mod Y offsets resonance. Filter Off is a no-op. Source metadata, "
                          "not a voice/DSP ACK."
                        : "Mod X/Y stored only; native/VST3 and Audio Clip per-note sampler modulation "
                          "unsupported.");
                set(row, "noteModulationAcknowledged", juce::var{});
                set(row, "noteExpressionScope",
                    localExpression ? "Local sampler; model state, check pendingAudioUpdate separately"
                                    : "Pan/Fine stored only; native/VST3 Pan/Fine unsupported. Shift uses "
                                      "score scheduling");
                const auto *channel = controller.project.channel(n.channel);
                const auto delay = channel ? swingDelay(n.beat, controller.project.swing, *channel) : 0;
                set(row, "swingDelayBeats", delay);
                set(row, "playbackBeat", n.beat + delay + noteShiftDelay(n.shiftPercent));
                set(row, "playbackLength",
                    channel && channel->truncateSwing ? std::max(.000001, n.length - delay) : n.length);
                append(row);
            }
        }
        else if (entity == "mixer")
            for (const auto &m : controller.project.mixer)
            {
                auto row = obj();
                juce::Array<juce::var> effects;
                set(row, "index", m.index);
                set(row, "position", *mixerInsertPosition(controller.project, m.index));
                set(row, "routes", mixerRoutesState(controller.project, m.index));
                set(row, "routingTargets", mixerRoutesState(controller.project, m.index, true));
                set(row, "name", m.name);
                set(row, "gain", m.gain);
                set(row, "pan", m.pan);
                juce::Array<juce::var> automationTargets;
                for (const auto control : {MixerAutomationControl::gain, MixerAutomationControl::pan,
                                           MixerAutomationControl::stereoSeparation})
                {
                    MixerAutomationTarget target;
                    if (resolveMixerAutomationTarget(controller.project, m.index, control, target).wasOk())
                    {
                        auto entry = mixerAutomationTargetState(target);
                        const auto initialized = std::find_if(controller.project.automation.initialValues.begin(),
                            controller.project.automation.initialValues.end(), [&](const auto &value) { return value.key == target.key; });
                        set(entry, "initializedValue", initialized == controller.project.automation.initialValues.end()
                            ? juce::var{} : juce::var(initialized->value));
                        automationTargets.add(entry);
                    }
                }
                for (const auto &route : m.routes)
                {
                    MixerAutomationTarget target;
                    if (resolveMixerAutomationTarget(controller.project, m.index,
                                                     MixerAutomationControl::send, target, route.target)
                            .failed())
                        continue;
                    auto entry = mixerAutomationTargetState(target);
                    const auto initialized =
                        std::find_if(controller.project.automation.initialValues.begin(),
                                     controller.project.automation.initialValues.end(),
                                     [&](const auto &value) { return value.key == target.key; });
                    set(entry, "initializedValue",
                        initialized == controller.project.automation.initialValues.end()
                            ? juce::var{}
                            : juce::var(initialized->value));
                    juce::Array<juce::var> linkedSources;
                    for (const auto &source : controller.project.automation.sources)
                        if (std::any_of(source.links.begin(), source.links.end(),
                                        [&](const auto &link) { return link.key == target.key; }))
                            linkedSources.add(juce::int64(source.id));
                    set(entry, "linkedSources", linkedSources);
                    automationTargets.add(entry);
                }
                set(row, "automationTargets", automationTargets);
                set(row, "stereoSeparation", m.stereoSeparation);
                {
                    juce::Array<juce::var> bands;
                    for (int band = 0; band < 3; ++band)
                    {
                        auto item = obj();
                        set(item, "gainDb", m.eq.gain[size_t(band)]);
                        set(item, "frequencyHz", m.eq.freq[size_t(band)]);
                        bands.add(item);
                    }
                    set(row, "eq", bands);
                    set(row, "delayMs", m.delayMs);
                    set(row, "dock", m.dock == 0 ? "middle" : m.dock < 0 ? "left" : "right");
                }
                set(row, "phaseInverted", m.phaseInverted);
                set(row, "stereoSwapped", m.stereoSwapped);
                set(row, "stereoTargetScope", "post-effects pre-pan/fader, saved target; five-ms ramp is not acknowledged");
                set(row, "muted", m.muted);
                set(row, "colour", juce::int64(m.colour));
                set(row, "effectsBypassed", m.effectsBypassed);
                set(row, "solo",
                    controller.project.mixerSolo && controller.project.mixerSolo->insert == m.index);
                set(row, "meter", mixerMeterState(m.index));
                for (const auto &fx : m.effects)
                {
                    auto effect = obj();
                    set(effect, "id", juce::int64(fx.id));
                    set(effect, "name", fx.name);
                    set(effect, "bypass", fx.bypass);
                    set(effect, "smartDisable", effectSmartDisabled(fx.id));
                    set(effect, "wet", fx.wet);
                    set(effect, "detector", pluginDetectorState(controller.project, fx.id));
                    set(effect, "effectiveBypass", m.effectsBypassed || fx.bypass);
                    effects.add(effect);
                }
                set(row, "effects", effects);
                append(row);
            }
        else if (entity == "channels")
            for (const auto &c : controller.project.channels)
            {
                auto row = obj();
                set(row, "id", juce::int64(c.id));
                set(row, "name", c.name);
                set(row, "colour", juce::int64(c.colour));
                set(row, "icon", c.icon);
                set(row, "source", c.source);
                set(row, "receiveMidiChannel", c.receiveMidiChannel);
                {
                    juce::Array<juce::var> map;
                    for (size_t k = 0; k < c.articulations.size(); ++k)
                    {
                        auto entry = obj();
                        set(entry, "articulation", int(k + 1));
                        set(entry, "name", c.articulations[k].name);
                        set(entry, "keyswitch", c.articulations[k].keyswitch);
                        set(entry, "kind", juce::String(c.articulations[k].kind == 1   ? "cc"
                                                        : c.articulations[k].kind == 2 ? "program"
                                                                                       : "note"));
                        set(entry, "value", c.articulations[k].value);
                        set(entry, "trigger", articulationTriggerText(c.articulations[k]));
                        map.add(entry);
                    }
                    set(row, "articulations", map);
                }
                set(row, "group", juce::int64(c.group));
                set(row, "mixer", c.mixer);
                set(row, "gain", c.gain);
                set(row, "pan", c.pan);
                set(row, "muted", c.muted);
                set(row, "muteLocked", c.muteLocked);
                set(row, "audioClip", c.audioClip);
                if (std::any_of(c.tuning.begin(), c.tuning.end(), [](double cents) { return cents != 0; }))
                {
                    juce::Array<juce::var> cents;
                    for (auto value : c.tuning)
                        cents.add(value);
                    set(row, "tuning", cents);
                }
                if (!c.scale.empty())
                {
                    juce::Array<juce::var> degrees;
                    for (auto value : c.scale)
                        degrees.add(value);
                    set(row, "scale", degrees);
                    set(row, "scaleRoot", c.scaleRoot);
                }
                if (c.mpe)
                    set(row, "mpe", true);
                if (c.mtsEsp)
                {
                    set(row, "mtsEsp", true);
                    set(row, "mtsEspActive", mtsEsp.registered());
                    set(row, "mtsEspClients", mtsEsp.clients());
                }
                if (c.instrument)
                    set(row, "plugin", juce::int64(c.instrument->id));
                append(row);
            }
        else if (entity == "arrangements")
        {
            const auto &project = controller.project;
            auto row = obj();
            set(row, "id", juce::int64(project.arrangementId));
            set(row, "name", project.arrangementName.isNotEmpty() ? project.arrangementName : juce::String("Arrangement 1"));
            set(row, "current", true);
            set(row, "clips", int(project.placements.size() + project.audioPlacements.size() +
                                  project.automation.placements.size()));
            append(row);
            for (const auto &a : project.arrangements)
            {
                auto stored = obj();
                set(stored, "id", juce::int64(a.id));
                set(stored, "name", a.name);
                set(stored, "current", false);
                set(stored, "clips", int(a.placements.size() + a.audioPlacements.size() + a.automationPlacements.size()));
                append(stored);
            }
        }
        else if (entity == "controllers")
        {
            const auto *part = controller.project.pattern(identity(args, "pattern", selectedPattern));
            if (!part)
                throw McpFailure("unavailable_target", "Pattern does not exist");
            for (const auto &control : part->controllers)
            {
                auto row = obj();
                set(row, "id", juce::int64(control.id));
                set(row, "channel", juce::int64(control.channel));
                set(row, "beat", control.beat);
                set(row, "type", control.type == 1   ? "pitchBend"
                                 : control.type == 2 ? "pressure"
                                 : control.type == 3 ? "program"
                                                     : "cc");
                set(row, "number", control.number);
                set(row, "value", control.value);
                set(row, "midiChannel", control.midiChannel + 1);
                append(row);
            }
        }
        else if (entity == "tempo")
        {
            const auto map = controller.project.songTempo();
            auto base = obj();
            set(base, "id", 0);
            set(base, "beat", 0.);
            set(base, "bpm", controller.project.tempo);
            set(base, "ramp", false);
            set(base, "seconds", 0.);
            append(base);
            for (const auto &change : controller.project.tempoChanges)
            {
                auto row = obj();
                set(row, "id", juce::int64(change.id));
                set(row, "beat", change.beat);
                set(row, "bpm", change.bpm);
                set(row, "ramp", change.ramp);
                set(row, "seconds", map.secondsAt(change.beat));
                append(row);
            }
        }
        else if (entity == "chords")
        {
            for (const auto &chord : controller.project.chords)
            {
                auto row = obj();
                set(row, "id", juce::int64(chord.id));
                set(row, "beat", chord.beat);
                set(row, "symbol", chord.symbol);
                juce::Array<juce::var> tones;
                for (const int tone : score::chordPitchClasses(chord.symbol))
                    tones.add(tone);
                set(row, "pitchClasses", tones);
                append(row);
            }
        }
        else if (entity == "markers")
        {
            auto sorted = controller.project.markers;
            std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.beat < b.beat; });
            for (const auto &marker : sorted)
            {
                auto row = obj();
                set(row, "id", juce::int64(marker.id));
                set(row, "beat", marker.beat);
                set(row, "name", marker.name);
                set(row, "action", marker.action);
                append(row);
            }
        }
        else if (entity == "meters")
        {
            auto base = obj(); // The project meter from beat 0, then each change.
            set(base, "id", 0);
            set(base, "beat", 0.);
            set(base, "numerator", controller.project.numerator);
            set(base, "denominator", controller.project.denominator);
            set(base, "bar", 1);
            append(base);
            for (const auto &change : controller.project.meterChanges)
            {
                auto row = obj();
                set(row, "id", juce::int64(change.id));
                set(row, "beat", change.beat);
                set(row, "numerator", change.numerator);
                set(row, "denominator", change.denominator);
                set(row, "bar", controller.project.barPosition(change.beat).bar + 1);
                append(row);
            }
        }
        else if (entity == "tracks")
            for (const auto &track : controller.project.tracks) // Only lanes with settings; others are defaults.
            {
                auto row = obj();
                set(row, "lane", track.lane);
                set(row, "name", track.name);
                set(row, "colour", juce::int64(track.colour));
                set(row, "muted", track.muted);
                set(row, "locked", track.locked);
                set(row, "height", track.height);
                set(row, "groupedAbove", track.groupedAbove);
                set(row, "collapsed", track.collapsed);
                set(row, "channel", juce::int64(track.channel)); // Instrument track owner, 0 = free track.
                set(row, "audio", track.audio);
                append(row);
            }
        else if (entity == "clipGroups")
            for (const auto &group : controller.project.clipGroups)
            {
                auto row = obj();
                set(row, "id", juce::int64(group.id));
                set(row, "memberCount", int(group.members.size()));
                append(row);
            }
        else if (entity == "clipGroupMembers")
        {
            const auto id = identity(args, "group", 0);
            const auto &groups = controller.project.clipGroups;
            const auto found =
                std::find_if(groups.begin(), groups.end(), [&](const auto &g) { return g.id == id; });
            if (found == groups.end())
                throw McpFailure("unavailable_target", "Clip group does not exist");
            for (auto member : found->members)
            {
                auto row = obj();
                set(row, "id", juce::int64(member));
                append(row);
            }
        }
        else if (entity == "clips")
        {
            std::unordered_map<Id, Id> groupByClip;
            for (const auto &group : controller.project.clipGroups)
                for (auto member : group.members)
                    groupByClip.emplace(member, group.id);
            auto add = [&](Id id, const char *kind, Id source, int lane, double beat, double length,
                           double offset, bool muted, const AudioPlacement *audio = nullptr)
            {
                auto row = obj();
                set(row, "id", juce::int64(id));
                set(row, "kind", kind);
                const auto group = groupByClip.find(id);
                set(row, "groupId", juce::int64(group == groupByClip.end() ? 0 : group->second));
                set(row, "source", juce::int64(source));
                set(row, "lane", lane);
                set(row, "beat", beat);
                set(row, "length", length);
                set(row, "offset", offset);
                set(row, "muted", muted);
                set(row, "sourceUnits", juce::String(kind) == "audio" ? "seconds" : "beats");
                if (audio)
                {
                    set(row, "fadeIn", audio->fadeIn);
                    set(row, "fadeOut", audio->fadeOut);
                }
                append(row);
            };
            for (const auto &c : controller.project.placements)
                add(c.id, "pattern", c.pattern, c.lane, c.beat, c.length, c.offset, c.muted);
            for (const auto &c : controller.project.audioPlacements)
                add(c.id, "audio", c.channel, c.lane, c.beat, c.duration, c.offset, c.muted, &c);
            for (const auto &c : controller.project.automation.placements)
                add(c.id, "automation", c.source, c.lane, c.beat, c.length, c.offset, c.muted);
        }
        else
            throw McpFailure("unsupported_operation", "Unknown entity kind");
        set(data, "items", rows);
        set(data, "total", total);
        set(data, "nextOffset", offset + rows.size() < total ? juce::var(offset + rows.size()) : juce::var{});
        set(data, "projectIdentity", controller.projectIdentity);
        set(data, "revision", juce::int64(controller.revision));
        return textResult(data);
    }
    if (name == "vibera_commands")
    {
        juce::Array<juce::var> names;
        for (auto &command : commandRegistry())
            if (allowed(command.id))
                names.add(command.id);
        auto result = obj();
        set(result, "commands", names);
        set(result, "contractVersion", 1);
        if (args["command"].toString().startsWith("automation.sampler."))
            set(result, "samplerAutomationContract", samplerAutomationDiscovery());
        const auto offset = integer(args, "offset", 0, 0, 2147483647);
        const auto limit = integer(args, "limit", 32, 1, 64);
        const auto all = mcpCommandContracts(bool(args["includeSchemas"]), args["command"].toString());
        juce::Array<juce::var> page;
        const auto *operations = all.getArray();
        for (int i = offset; i < operations->size() && page.size() < limit; ++i)
            page.add((*operations)[i]);
        set(result, "operations", page);
        set(result, "total", operations->size());
        set(result, "nextOffset",
            offset + page.size() < operations->size() ? juce::var(offset + page.size()) : juce::var{});
        set(result, "coverageComplete", false);
        set(result, "coverageNote",
            "Registered commands and custom GUI surfaces still have unimplemented/unverified mappings. "
            "Pointer tools never establish semantic coverage.");
        return textResult(result);
    }
    if (name == "vibera_command")
    {
        lastRoutingDispatchOperation = 0;
        lastMixerEffectReplacementOperation.clear();
        lastMixerEffectSidechainOperation.clear();
        lastLifecycleOperation.clear();
        lastStructuralOperation.clear();
        if (args.hasProperty("revision") && number(args, "revision", -1) != double(controller.revision))
            throw McpFailure("stale_revision", "Read current project state before editing");
        auto commandName = args["command"].toString();
        if ((commandName == "steps.velocity" || commandName == "steps.pitch" || commandName == "steps.pan" ||
             commandName == "steps.fine" || commandName == "steps.shift" || commandName == "steps.repeat" ||
             commandName == "steps.release" || commandName == "steps.modX" || commandName == "steps.modY" ||
             isSwingCommand(commandName)) &&
            controller.hasGesture() && !mcpGestureDispatch)
            throw McpFailure("gesture_owned", "Finish the local edit, or update the owned semantic gesture");
        auto values = args.getProperty("args", obj());
        const auto *definition = mcpCommandInfo(commandName);
        if (!definition || !allowed(commandName))
            throw McpFailure("unsupported_operation",
                             "Command has no implemented semantic MCP adapter; inspect vibera_commands");
        validateMcpSchema(
            definition->documentEdit
                ? commandArgumentSchema(commandName)
                : juce::JSON::parse("{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"),
            values);
        if (isMixerInsertOrderCommand(commandName))
        {
            juce::String code;
            if (auto result = admitMixerInsertOrder(code, mcpBatchOwner); result.failed())
                throw McpFailure(code, result.getErrorMessage());
        }
        if (commandName == "plugin.effect.mix" || commandName == "mixer.effects.bypass")
        {
            juce::String code;
            if (auto result = admitMixerChainControl(code); result.failed())
                throw McpFailure(code, result.getErrorMessage());
        }
        if (commandName == "plugin.effect.move")
        {
            juce::String code;
            if (auto result = admitMixerEffectMove(code); result.failed())
                throw McpFailure(code, result.getErrorMessage());
        }
        if (commandName == "edit.undo")
        {
            requireFinishedTake();
            const auto result = controller.undoEditChecked();
            if (lastRoutingDispatchOperation) return textResult(channelRoutingState(lastRoutingDispatchOperation));
            if (lastStructuralOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastStructuralOperation));
            if (lastLifecycleOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastLifecycleOperation));
            if (lastMixerChainHistoryOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastMixerChainHistoryOperation));
            if (result.failed()) throw McpFailure("history_refused", result.getErrorMessage());
        }
        else if (commandName == "edit.redo")
        {
            requireFinishedTake();
            const auto result = controller.redoEditChecked();
            if (lastRoutingDispatchOperation) return textResult(channelRoutingState(lastRoutingDispatchOperation));
            if (lastStructuralOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastStructuralOperation));
            if (lastLifecycleOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastLifecycleOperation));
            if (lastMixerChainHistoryOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastMixerChainHistoryOperation));
            if (result.failed()) throw McpFailure("history_refused", result.getErrorMessage());
        }
        else
        {
            if (juce::String(definition->mcp) == "action")
            {
                if (commandName == "piano.selectionZoom" &&
                    static_cast<Editor *>(windows[1]->getContentComponent())->repeatInterval() <= 0)
                    throw McpFailure("unavailable_operation", "Select a Piano roll timeline region first");
                if (commandName == "playlist.zoomSelection" && selectedClips.empty())
                    throw McpFailure("unavailable_operation", "Select Playlist clips first");
                const bool wasPlaying = audio.playing();
                if (commandName.startsWith("sampler.view.") && !instrumentWindows.contains(selectedChannel))
                    throw McpFailure("unavailable_target", "Open the selected Channel instrument first");
                if (hasPendingMidiTake() &&
                    (commandName == "transport.play" || commandName == "transport.loop"))
                    throw McpFailure("take_pending",
                                     "Retry/cancel the retained MIDI take before changing transport");
                if (commandName != "transport.stop" && commandName != "selection.cancel" &&
                    commandName != "recording.cancel" && !recordingPlay)
                    if (auto finished = finishRackEditsChecked(); finished.failed())
                        throw McpFailure("history_refused", finished.getErrorMessage());
                if (commandName == "channel.pluginControls")
                {
                    const auto *channel = controller.project.channel(selectedChannel);
                    if (!channel || !channel->instrument)
                        throw McpFailure("unavailable_target", "Select a Channel with a plugin instrument");
                    const auto plugin = channel->instrument->id;
                    const auto host = audio.preparedPluginHost(controller.project, plugin);
                    juce::String operation, code;
                    const auto admitted =
                        requestPluginEditor(plugin, true, host ? host->runtimeIdentity() : juce::String{},
                                            operation, code, PluginWrapperView::parameters);
                    if (admitted.failed())
                        throw McpFailure(code, admitted.getErrorMessage());
                    auto response = obj();
                    set(response, "operation", operation);
                    set(response, "kind", "editor");
                    set(response, "completionState", "pending");
                    set(response, "plugin", juce::int64(plugin));
                    set(response, "documentChanged", false);
                    set(response, "undoable", false);
                    return textResult(response);
                }
                if (commandName.startsWith("tools.lastTweaked."))
                {
                    const auto target = lastPluginTweak();
                    if (!target)
                        throw McpFailure("unavailable_target",
                                         "Finish an unambiguous native plugin gesture first");
                    const auto suffix = commandName.fromFirstOccurrenceOf("tools.lastTweaked.", false, false);
                    juce::String operation, code;
                    const auto admitted =
                        requestLastPluginTweak(*target,
                                               suffix == "controls" ? suffix
                                               : suffix == "create" ? "automation.plugin.create"
                                               : suffix == "init"   ? "automation.plugin.init"
                                                                    : "automation.plugin.init.remove",
                                               operation, code);
                    if (admitted.failed())
                        throw McpFailure(code, admitted.getErrorMessage());
                    auto response = obj();
                    set(response, "operation", operation);
                    set(response, "kind", suffix == "controls" ? "editor" : "automate");
                    set(response, "completionState", "pending");
                    set(response, "pollTool", "vibera_plugin");
                    set(response, "plugin", juce::int64(target->plugin));
                    set(response, "parameter", juce::int64(target->gesture.id));
                    set(response, "documentChanged", false);
                    set(response, "undoable", false);
                    return textResult(response);
                }
                if (!performAction(commandName))
                    throw McpFailure("unavailable_operation",
                                     "Application command is not available in the current context");
                if (commandName == "steps.composeDialog")
                    return textResult(stepComposeState());
                if (commandName == "transport.record" || commandName == "transport.stop")
                {
                    auto response = mcpCall("vibera_state", obj())["structuredContent"];
                    if (hasPendingMidiTake())
                    {
                        auto error = obj();
                        set(error, "code", "take_pending");
                        set(error, "message", status);
                        set(response, "error", error);
                        set(response, "completionState", "failed");
                    }
                    return textResult(response);
                }
                if (commandName == "transport.play" && devicePreparationBusy())
                {
                    auto response = obj(); set(response, "completionState", "pending");
                    set(response, "devicePreparation", devicePreparationState()); return textResult(response);
                }
                if (commandName == "transport.play" && audio.playing() == wasPlaying)
                    throw McpFailure("playback_unavailable",
                                     "Audio device/project preparation unavailable; read audio state");
                return mcpCall("vibera_state", obj());
            }
            if (!allowed(commandName))
                throw std::runtime_error("Command not exposed through MCP");
            if (!values.isVoid() && !values.isObject())
                throw std::runtime_error("args must be an object");
            if (auto *object = values.getDynamicObject())
            {
                static const juce::StringArray fields{"pattern",
                                                      "channel",
                                                      "target",
                                                      "beat",
                                                      "length",
                                                      "pitch",
                                                      "value",
                                                      "mixer",
                                                      "lane",
                                                      "numerator",
                                                      "denominator",
                                                      "enabled",
                                                      "step",
                                                      "ids",
                                                      "text",
                                                      "offset",
                                                      "generation",
                                                      "recordedNotes",
                                                      "takes",
                                                      "fadeIn",
                                                      "seed",
                                                      "tuning",
                                                      "scale",
                                                      "slicePoints",
                                                      "range",
                                                      "files",
                                                      "fadeOut",
                                                      "binding",
                                                      "interval",
                                                      "restore",
                                                      "retainSteps",
                                                      "soloScope",
                                                      "grouping",
                                                      "strength",
                                                      "sensitivity",
                                                      "durationStrength",
                                                      "velocityStrength",
                                                      "zeroVelocityDeletes",
                                                      "groove",
                                                      "slice",
                                                      "mixerPosition",
                                                      "appearance",
                                                      "channelSort",
                                                      "automationControl",
                                                      "automationSendTarget",
                                                      "samplerParameter",
                                                      "minimum",
                                                      "maximum",
                                                      "mixerGroup",
                                                      "mixerDelete",
                                                      "detector",
                                                      "pluginIo",
                                                      "controllerLink",
                                                      "replacements",
                                                      "slide",
                                                      "portamento",
                                                      "colourGroup",
                                                      "noteProperties"};
                for (int i = 0; i < object->getProperties().size(); ++i)
                {
                    if (commandName == "automation.source.clone" &&
                        object->getProperties().getName(i) != juce::Identifier("target") &&
                        object->getProperties().getName(i) != juce::Identifier("text"))
                        throw McpFailure("automation.invalidArguments",
                                         "Source cloning accepts only target and clear/preserve text policy");
                    if (!(commandName == "steps.compose" && object->getProperties().getName(i) == juce::Identifier("recipes")) &&
                        !fields.contains(object->getProperties().getName(i).toString()))
                        throw std::runtime_error(
                            "Unsupported command argument; inspect CommandArgs documentation");
                }
            }
            if (commandName == "automation.source.clone" &&
                (!values.hasProperty("target") ||
                 (values.hasProperty("text") &&
                  (!values["text"].isString() ||
                   (values["text"].toString() != "clear" && values["text"].toString() != "preserve")))))
                throw McpFailure("automation.invalidArguments",
                                 "Existing source target and optional clear/preserve policy required");
            const bool samplerTarget = isSamplerAutomationTargetCommand(commandName);
            if (samplerTarget)
            {
                const auto schema = samplerAutomationArguments(commandName);
                if (!values.isObject())
                    throw McpFailure("automation.invalidArguments",
                                     "Sampler automation requires typed arguments");
                for (const auto &field : *schema["required"].getArray())
                    if (!values.hasProperty(field.toString()))
                        throw McpFailure("automation.invalidArguments",
                                         "Missing sampler automation field: " + field.toString());
                const auto *properties = values.getDynamicObject();
                for (const auto &property : properties->getProperties())
                    if (!schema["properties"].hasProperty(property.name))
                        throw McpFailure("automation.invalidArguments",
                                         "Unsupported sampler automation argument: " +
                                             property.name.toString());
            }
            else if (values.hasProperty("samplerParameter") || values.hasProperty("minimum") ||
                     values.hasProperty("maximum"))
                throw McpFailure("automation.invalidArguments",
                                 "Sampler target/range fields belong only to sampler automation commands");
            CommandArgs a;
            if (values.hasProperty("noteProperties"))
            {
                if (commandName != "notes.properties")
                    throw McpFailure("noteMode.invalidArguments",
                                     "noteProperties belongs only to notes.properties");
                NotePropertiesPatch patch;
                if (auto parsed = decodeNotePropertiesPatch(values["noteProperties"], patch); parsed.failed())
                    throw McpFailure("noteMode.invalidArguments", parsed.getErrorMessage());
                a.noteProperties = patch;
            }
            a.slide = bool(values.getProperty("slide", false));
            a.portamento = bool(values.getProperty("portamento", false));
            a.colourGroup = integer(values, "colourGroup", 0, 0, 15);
            if (isSamplerControllerCommand(commandName))
            {
                if (const auto parsed = decodeSamplerControllerArguments(commandName, values, a);
                    parsed.failed())
                    throw McpFailure("controller.invalidArguments", parsed.getErrorMessage());
            }
            else if (values.hasProperty("controllerLink") || values.hasProperty("replacements"))
                throw McpFailure("controller.invalidArguments",
                                 "Controller metadata belongs only to project link commands");
            if (samplerTarget)
            {
                a.samplerAutomationParameter = std::uint32_t(integer(values, "samplerParameter", -1, 0, 159));
                if (!lookupSamplerAutomationParameter(*a.samplerAutomationParameter))
                    throw McpFailure("automation.unsupportedSamplerParameter",
                                     "Unsupported destination/field code");
            }
            if (values.hasProperty("soloScope"))
            {
                if (commandName != "channel.solo" || !values["soloScope"].isString() ||
                    (values["soloScope"].toString() != "global" && values["soloScope"].toString() != "group"))
                    throw McpFailure("channel.solo.invalidScope", "Expected global or group on channel.solo");
                a.soloScope = values["soloScope"].toString();
            }
            if ((values.hasProperty("appearance") || values.hasProperty("channelSort")) &&
                commandName != "channel.properties" && commandName != "rack.properties" &&
                commandName != "rack.sort")
                throw McpFailure("channelAppearance.invalidArguments",
                                 "Appearance/sort fields belong only to Channel properties/sort commands");
            if (auto parsed = decodeChannelAppearance(values, a); parsed.failed())
                throw McpFailure("channelAppearance.invalidArguments", parsed.getErrorMessage());
            if (values.hasProperty("pluginIo"))
            {
                if (commandName != "plugin.io")
                    throw McpFailure("pluginIo.invalidArguments", "pluginIo belongs only to plugin.io");
                const auto io = values["pluginIo"];
                for (const auto *key : {"inputBus", "outputBus", "inputChannels", "monoSource"})
                    if (!io.hasProperty(key))
                        throw McpFailure("pluginIo.invalidArguments",
                                         "Complete typed input/output selection required");
                a.pluginIo =
                    PluginMainIo{integer(io, "inputBus", -1, 0, 31), integer(io, "outputBus", -1, 0, 31),
                                 integer(io, "inputChannels", -1, 1, 2), integer(io, "monoSource", -1, 0, 3)};
            }
            if (values.hasProperty("detector"))
            {
                if (commandName != "plugin.effect.sidechain")
                    throw McpFailure("sidechain.invalidArguments", "Detector belongs only to sidechain");
                std::optional<PluginDetectorAssignment> assignment;
                const auto detector = values["detector"];
                if (!detector.isVoid())
                {
                    if (!detector.isObject())
                        throw McpFailure("sidechain.invalidArguments", "Detector must be null or object");
                    for (const auto &field : detector.getDynamicObject()->getProperties())
                        if (field.name != juce::Identifier("sourceMixer") &&
                            field.name != juce::Identifier("inputBus") &&
                            field.name != juce::Identifier("channels"))
                            throw McpFailure("sidechain.invalidArguments", "Unknown detector field");
                    for (const auto *field : {"sourceMixer", "inputBus", "channels"})
                        if (!detector.hasProperty(field))
                            throw McpFailure("sidechain.invalidArguments",
                                             "Complete detector object required");
                    assignment = PluginDetectorAssignment{integer(detector, "sourceMixer", -1, 1, 2147483647),
                                                          integer(detector, "inputBus", -1, 1, 31),
                                                          integer(detector, "channels", -1, 1, 2)};
                }
                a.detector.emplace(assignment);
            }
            if (values.hasProperty("mixerDelete"))
            {
                const auto policy = values["mixerDelete"];
                if (commandName != "mixer.insert.delete" || !policy.isObject())
                    throw McpFailure("mixer.invalidDeletePolicy",
                                     "Typed deletion policy belongs only to insert delete");
                MixerInsertDeletePolicy parsed;
                for (const auto &field : policy.getDynamicObject()->getProperties())
                    if (field.name != juce::Identifier("replacement") &&
                        field.name != juce::Identifier("removeEffects") &&
                        field.name != juce::Identifier("removeAutomation") &&
                        field.name != juce::Identifier("removeGroupMembership"))
                        throw McpFailure("mixer.invalidDeletePolicy", "Unknown deletion policy field");
                if (policy.hasProperty("replacement"))
                    parsed.replacement = integer(policy, "replacement", -1, 0, 2147483647);
                for (const auto *flag : {"removeEffects", "removeAutomation", "removeGroupMembership"})
                    if (policy.hasProperty(flag) && !policy[flag].isBool())
                        throw McpFailure("mixer.invalidDeletePolicy",
                                         "Deletion policy flags must be booleans");
                parsed.removeEffects = bool(policy.getProperty("removeEffects", false));
                parsed.removeAutomation = bool(policy.getProperty("removeAutomation", false));
                parsed.removeGroupMembership = bool(policy.getProperty("removeGroupMembership", false));
                a.mixerDelete = parsed;
            }
            if (values.hasProperty("mixerGroup"))
            {
                if (!values["mixerGroup"].isString() || !isMixerGroupCommand(commandName))
                    throw McpFailure("mixer.group.invalidArguments", "Group UUID applies only to group commands");
                a.mixerGroup = values["mixerGroup"].toString();
            }
            if (values.hasProperty("automationControl"))
            {
                a.automationControl = parseMixerAutomationControl(values["automationControl"]);
                if (!a.automationControl || !isMixerAutomationTargetCommand(commandName))
                    throw McpFailure("automation.invalidArguments", "Unsupported typed Mixer automation control");
            }
            if (values.hasProperty("automationSendTarget"))
            {
                if (!isMixerAutomationTargetCommand(commandName) ||
                    a.automationControl != MixerAutomationControl::send)
                    throw McpFailure("automation.invalidArguments",
                                     "Send destination belongs only to send automation");
                a.automationSendTarget = integer(values, "automationSendTarget", -1, 0, 2147483647);
            }
            if (a.automationControl == MixerAutomationControl::send && !a.automationSendTarget)
                throw McpFailure("automation.invalidArguments",
                                 "Send automation requires its exact destination");
            if (values.hasProperty("text"))
            {
                if (!values["text"].isString() || values["text"].toString().length() > 65536)
                    throw std::runtime_error("Command text must be a string of at most 65536 characters");
                a.text = values["text"].toString();
            }
            a.pattern = identity(values, "pattern", selectedPattern);
            if (commandName == "steps.compose")
                if (auto result = parseStepComposeRecipes(values["recipes"], a.composition); result.failed())
                    throw McpFailure("stepCompose.invalidArguments", result.getErrorMessage());
            // Project controller links own a fixed Channel in their typed payload.
            // Never inject the currently selected performance recipient into that metadata.
            a.channel =
                isSamplerControllerCommand(commandName) ? 0 : identity(values, "channel", selectedChannel);
            const bool mixerRouting = commandName == "mixer.route" || commandName == "mixer.routeOnly" ||
                                      commandName == "mixer.send";
            a.target =
                mixerRouting ? integer(values, "target", 0, 0, 2147483647) : identity(values, "target", 0);
            a.beat = commandName == "automation.sampler.range" ? number(values, "minimum", 0)
                                                               : number(values, "beat", 0);
            a.length = commandName == "automation.sampler.range"
                           ? number(values, "maximum", 0)
                           : number(values, "length",
                                    (commandName == "notes.move" || commandName == "clip.move") ? 0 : .25);
            a.pitch = integer(values, "pitch", commandName == "notes.move" ? 0 : 60, -127, 127);
            const auto *expressionDefault =
                commandName == "steps.modX"   ? stepExpressionInfo(StepExpression::modX)
                : commandName == "steps.modY" ? stepExpressionInfo(StepExpression::modY)
                                              : nullptr;
            const double numericDefault = expressionDefault ? expressionDefault->defaultValue : 0.;
            a.value = number(values, "value", numericDefault);
            a.interval = integer(values, "interval", 4, 2, 8);
            a.offset = number(values, "offset", numericDefault);
            a.strength = number(values, "strength", 1);
            a.sensitivity = number(values, "sensitivity", 1);
            a.durationStrength = number(values, "durationStrength", 1);
            a.velocityStrength = number(values, "velocityStrength", 0);
            if (values.hasProperty("zeroVelocityDeletes"))
            {
                if (!values["zeroVelocityDeletes"].isBool())
                    throw McpFailure("invalid_arguments", "zeroVelocityDeletes must be boolean");
                a.zeroVelocityDeletes = bool(values["zeroVelocityDeletes"]);
            }
            if (values.hasProperty("groove"))
            {
                GrooveTemplate groove;
                if (auto valid = parseGroove(values["groove"], groove); valid.failed())
                    throw McpFailure("invalid_groove", valid.getErrorMessage());
                a.groove = std::move(groove);
            }
            a.generation = values["generation"].toString();
            if (const auto *notes = values["recordedNotes"].getArray())
                for (const auto &note : *notes)
                    a.recordedNotes.push_back(
                        {identity(note, "id", 0), identity(note, "channel", 0), number(note, "beat", 0),
                         number(note, "length", .25), integer(note, "pitch", 60, 0, 127),
                         float(number(note, "velocity", .8)), bool(note["step"]), bool(note["muted"]),
                         number(note, "pan", 0), number(note, "fineCents", 0),
                         number(note, "shiftPercent", 0), integer(note, "repeatMode", 0, 0, 14),
                         number(note, "releaseVelocity", 0), number(note, "modX", .5),
                         number(note, "modY", .5), bool(note.getProperty("slide", false)),
                         bool(note.getProperty("portamento", false)),
                         integer(note, "colourGroup", 0, 0, 15)});
            if (const auto *range = values["range"].getArray())
            {
                if (commandName != "audio.comp" || range->size() != 2 || !(double((*range)[1]) > double((*range)[0])))
                    throw McpFailure("invalid_arguments", "range: audio.comp takes [fromBeat, toBeat] with to > from");
                a.compRange = std::array<double, 2>{double((*range)[0]), double((*range)[1])};
            }
            if (const auto *points = values["slicePoints"].getArray())
                for (const auto &point : *points)
                    a.slicePoints.push_back(double(point));
            if (const auto *cents = values["tuning"].getArray())
            {
                if (commandName != "channel.tuning" || cents->size() != 12)
                    throw McpFailure("invalid_arguments", "tuning: channel.tuning takes 12 numbers (C..B)");
                std::array<double, 12> table{};
                for (int i = 0; i < 12; ++i)
                    table[size_t(i)] = double((*cents)[i]);
                a.tuning = table;
            }
            if (values.hasProperty("scale"))
            {
                const auto *degrees = values["scale"].getArray();
                if (commandName != "channel.scale" || !degrees || degrees->size() > 128)
                    throw McpFailure("invalid_arguments", "scale: channel.scale takes up to 128 degree cents");
                std::vector<double> list;
                for (const auto &degree : *degrees)
                    list.push_back(double(degree));
                a.scale = list;
                if (!values.hasProperty("pitch"))
                    a.pitch = 60;
            }
            if (values.hasProperty("seed"))
                a.seed = std::uint32_t(integer(values, "seed", 1, 0, 2147483647));
            if (values.hasProperty("fadeIn"))
                a.fadeIn = number(values, "fadeIn", 0);
            if (values.hasProperty("fadeOut"))
                a.fadeOut = number(values, "fadeOut", 0);
            if (const auto *takes = values["takes"].getArray())
            {
                if (commandName != "audio.takes" || takes->size() > 64)
                    throw McpFailure("invalid_arguments", "takes: audio.takes only, at most 64 windows");
                for (const auto &take : *takes)
                {
                    AudioPlacement window;
                    window.beat = number(take, "beat", 0);
                    window.offset = number(take, "offset", 0);
                    window.duration = number(take, "duration", 0);
                    window.lane = integer(take, "lane", 0, 0, 127);
                    window.muted = bool(take.getProperty("muted", false));
                    window.channel = Id(integer(take, "source", 0, 0, 15));
                    a.audioTakes.push_back(window);
                }
            }
            if (const auto *files = values["files"].getArray())
            {
                if (commandName != "audio.takes" || files->isEmpty() || files->size() > 16)
                    throw McpFailure("invalid_arguments", "files: audio.takes only, 1..16 files");
                for (const auto &file : *files)
                    a.takeFiles.push_back(file.toString());
            }
            if (values.hasProperty("slice"))
            {
                NoteSliceLine line;
                if (auto r = parseSliceLine(values["slice"], line); r.failed())
                    throw McpFailure("invalid_arguments", r.getErrorMessage());
                a.slice = line;
            }
            if (values.hasProperty("binding"))
            {
                const auto binding = values["binding"];
                a.binding = PluginBinding{std::uint32_t(number(binding, "id", 0)),
                                          std::uint32_t(number(binding, "flags", 1)),
                                          integer(binding, "steps", 0, 0, 2147483647),
                                          float(number(binding, "manual", 0)),
                                          binding["name"].toString(),
                                          binding["label"].toString()};
            }
            a.mixer = integer(values, "mixer", 0, 0,
                              commandName == "channel.mixer" || commandName.startsWith("mixer.") ||
                                      isMixerAutomationTargetCommand(commandName)
                                  ? 2147483647 : 127);
            if (values.hasProperty("mixerPosition"))
                a.mixerPosition = integer(values, "mixerPosition", -1, 0, 128);
            a.lane = integer(values, "lane", 0, commandName == "clip.move" ? -127 : 0,
                             commandName == "pattern.move" ? 1023 : 127);
            a.numerator = integer(values, "numerator", 4, 1, 32);
            a.denominator = integer(values, "denominator", 4, 2, 16);
            for (auto field :
                 {"enabled", "step", "restore", "grouping", "retainSteps", "slide", "portamento"})
                if (values.hasProperty(field) && !values[field].isBool())
                    throw std::runtime_error("Flag must be boolean");
            if (values.hasProperty("enabled"))
                a.enabled = bool(values["enabled"]);
            if (values.hasProperty("grouping"))
                a.grouping = bool(values["grouping"]);
            if (values.hasProperty("retainSteps"))
            {
                if (!isStepRetentionCommand(commandName))
                    throw std::runtime_error(
                        "stepMemory.invalidArguments: Retention is not supported by this command");
                a.retainSteps = bool(values["retainSteps"]);
            }
            if (values.hasProperty("restore"))
                a.restore = bool(values["restore"]);
            if (values.hasProperty("step"))
                a.step = bool(values["step"]);
            if (values.hasProperty("ids"))
            {
                auto *ids = values["ids"].getArray();
                const int limit = commandName == "notes.mute" || commandName == "notes.unmute" ||
                                          commandName == "notes.slide" || commandName == "notes.portamento" ||
                                          commandName == "notes.colourGroup" ||
                                          commandName == "notes.properties" || commandName == "notes.slice" ||
                                          commandName == "notes.stretch" || commandName.startsWith("clip.") ||
                                          commandName == "steps.velocity" || commandName == "steps.pitch" ||
                                          commandName == "steps.pan" || commandName == "steps.fine" ||
                                          commandName == "steps.modX" || commandName == "steps.modY" ||
                                          commandName == "steps.shift" || commandName == "steps.repeat" ||
                                          commandName == "steps.release" || commandName == "notes.quantize" ||
                                          commandName == "notes.quickQuantize" ||
                                          commandName == "notes.quantizeStarts"
                                      ? 100000
                                      : 1024;
                if (!ids || ids->size() > limit)
                    throw McpFailure("invalid_arguments", "Too many target identities");
                for (auto id : *ids)
                {
                    auto item = obj();
                    set(item, "id", id);
                    a.ids.push_back(isMixerGroupCommand(commandName)
                        ? Id(integer(item, "id", -1, commandName.startsWith("mixer.selection.") ? 0 : 1, 2147483647))
                        : identity(item, "id", 0));
                }
            }
            const auto orderBefore = isMixerInsertOrderCommand(commandName)
                ? mixerInsertPosition(controller.project, a.mixer) : std::optional<int>{};
            const bool boundedStructural = requiresBoundedMixerStructural(commandName, a);
            const auto revisionBefore = controller.revision;
            const auto sourceCountBefore = controller.project.automation.sources.size();
            command(commandName, a, mcpBatchOwner);
            if (lastStructuralOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastStructuralOperation));
            if (lastLifecycleOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastLifecycleOperation));
            if (lastMixerEffectSidechainOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastMixerEffectSidechainOperation));
            if (lastMixerEffectReplacementOperation.isNotEmpty())
                return textResult(mixerChainReadback(lastMixerEffectReplacementOperation));
            if (boundedStructural && status != "Ready" && lastStructuralOperation.isEmpty())
                throw McpFailure(status.containsChar(':') ? status.upToFirstOccurrenceOf(":", false, false)
                                                          : juce::String("routing.structuralRefused"),
                                 status);
            if ((commandName == "plugin.effect.replace" || commandName == "plugin.effect.sidechain" ||
                 isMixerInsertLifecycleCommand(commandName)) &&
                status != "Ready")
                throw McpFailure(status.containsChar(':')
                                     ? status.upToFirstOccurrenceOf(":", false, false)
                                     : juce::String(isMixerInsertLifecycleCommand(commandName)
                                                        ? "mixer.operationRefused"
                                                        : "effects.replacementRefused"),
                                 status);
            if (lastRoutingDispatchOperation)
                return textResult(channelRoutingState(lastRoutingDispatchOperation));
            if (status != "Ready")
                throw McpFailure(
                    (commandName.startsWith("automation.") && status.startsWith("automation.")) ||
                            status.startsWith("routing.") || status.startsWith("channelLoop.") ||
                            status.startsWith("samplerController.") || status.startsWith("controller.") ||
                            status.startsWith("samplerVoice.") || status.startsWith("noteMode.") ||
                            status.startsWith("noteModes.") || status.startsWith("patternExtent.") ||
                            status.startsWith("stepCompose.") ||
                            status.startsWith("recording.patternExtentBusy:") ||
                            status.startsWith("channel.solo.") || status.startsWith("channel.mute.locked:") ||
                            (isMixerGroupCommand(commandName) && status.startsWith("mixer.")) ||
                            (isMixerInsertOrderCommand(commandName) &&
                             (status.startsWith("mixer.") || status.startsWith("history.") ||
                              status.startsWith("device.") || status.startsWith("application.")))
                        ? status.upToFirstOccurrenceOf(":", false, false)
                    : hasPendingMidiTake() ? "take_pending"
                                           : "validation_failed",
                    status);
            if (isSamplerControllerCommand(commandName))
            {
                syncSamplerControllerBindings();
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState",
                    samplerControllerProjectionError.isEmpty() ? "completed" : "failed");
                set(response, "completionScope", "projectControllerLink");
                set(response, "documentChanged", revisionBefore != controller.revision);
                set(response, "projectUndoable", revisionBefore != controller.revision);
                set(response, "effectiveApplied", samplerControllerProjectionError.isEmpty());
                set(response, "saved", false);
                if (samplerControllerProjectionError.isNotEmpty())
                {
                    auto error = obj();
                    set(error, "code", "controller.projectionFailed");
                    set(error, "message", samplerControllerProjectionError);
                    set(response, "error", error);
                }
                return textResult(response);
            }
            if (commandName == "automation.source.clone")
            {
                if (controller.revision == revisionBefore ||
                    controller.project.automation.sources.size() != sourceCountBefore + 1)
                    throw McpFailure("automation.cloneNotCommitted", "Source clone did not commit");
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope", "canonicalAutomationSource");
                set(response, "createdSource", juce::int64(controller.project.automation.sources.back().id));
                set(response, "linkPolicy", a.text.isEmpty() ? "clear" : a.text);
                set(response, "changed", true);
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (commandName == "pattern.channelLoop" || commandName == "pattern.burnChannelLoop" ||
                commandName == "pattern.extent" || commandName == "pattern.meter")
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope",
                    commandName == "pattern.extent" || commandName == "pattern.meter"
                        ? "canonicalPatternExtent"
                        : "canonicalPatternChannelLoop");
                set(response, "changed", revisionBefore != controller.revision);
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (commandName == "notes.slide" || commandName == "notes.portamento" ||
                commandName == "notes.colourGroup" || commandName == "notes.properties")
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope", "canonicalNoteMetadata");
                set(response, "changed", revisionBefore != controller.revision);
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (isSamplerVoiceModeCommand(commandName))
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope", "canonicalSamplerVoice");
                set(response, "changed", revisionBefore != controller.revision);
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (isChannelAppearanceCommand(commandName) || commandName == "channel.solo" ||
                commandName == "channel.unsolo" || commandName == "channel.muteLock" ||
                commandName == "rack.muteLock" || commandName == "channel.mute" ||
                commandName == "rack.muteSelected" || commandName == "rack.unmuteSelected")
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope",
                    isChannelAppearanceCommand(commandName) ? "canonicalChannelAppearance"
                                                            : "canonicalChannelMute");
                set(response, "changed", revisionBefore != controller.revision);
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (isMixerGroupCommand(commandName))
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope", "canonicalMixerOrganisation");
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (isMixerAutomationTargetCommand(commandName) || isSamplerAutomationTargetCommand(commandName))
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope", "canonicalAutomationTarget");
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (commandName == "mixer.stereoSeparation" || commandName == "mixer.phaseInvert" || commandName == "mixer.stereoSwap")
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope", "canonicalMixerStereoTarget");
                set(response, "dspSettlingAcknowledged", false);
                return textResult(response);
            }
            if (isMixerInsertOrderCommand(commandName))
            {
                auto response = mcpCall("vibera_state", obj())["structuredContent"];
                set(response, "completionState", "completed");
                set(response, "completionScope", "canonicalProjectOrder");
                set(response, "changed", orderBefore != mixerInsertPosition(controller.project, a.mixer));
                return textResult(response);
            }
        }
    }
    else if (name == "vibera_select")
    {
        if (args.hasProperty("plugin"))
        {
            if (args.hasProperty("pattern") || args.hasProperty("channel") || args.hasProperty("scope"))
                throw std::runtime_error("Plugin UI and Pattern/Channel selection must be separate requests");
            const auto id = identity(args, "plugin", 0);
            const auto host = audio.preparedPluginHost(controller.project, id);
            if (!controller.project.plugin(id) || !host || !host->alive())
                throw std::runtime_error("Unknown or unavailable project plugin worker");
            if (args.hasProperty("instance") &&
                (!args["instance"].isString() || args["instance"].toString() != host->runtimeIdentity()))
                throw std::runtime_error("Plugin worker instance changed; read state and retry");
            const auto view = args.getProperty("pluginView", "controls").toString();
            std::optional<std::uint32_t> parameter;
            if (args.hasProperty("parameter"))
            {
                const auto value = number(args, "parameter", -1);
                if (value < 0 || value > 4294967295. || std::floor(value) != value)
                    throw std::runtime_error("Invalid native parameter ID");
                parameter = std::uint32_t(value);
            }
            if (args.hasProperty("lastTweaked") && !args["lastTweaked"].isBool())
                throw std::runtime_error("lastTweaked must be boolean");
            const bool last = bool(args.getProperty("lastTweaked", false));
            if ((parameter && last) || (view != "controls" && (parameter || last)))
                throw std::runtime_error("Choose one parameter action on the controls view");
            if (view == "native" || view == "closeNative")
            {
                juce::String operation, code;
                const auto admitted =
                    requestPluginEditor(id, view == "native", host->runtimeIdentity(), operation, code);
                if (admitted.failed())
                    throw McpFailure(code, admitted.getErrorMessage());
                auto response = obj();
                set(response, "plugin", juce::int64(id));
                set(response, "instance", host->runtimeIdentity());
                set(response, "operation", operation);
                set(response, "completionState", "pending");
                set(response, "completionObservable", true);
                set(response, "poll", "vibera_plugin action=job with this operation token");
                return textResult(response);
            }
            else if (view == "controls")
            {
                const auto result = selectPluginParameter(id, parameter, last);
                if (result.failed())
                    throw std::runtime_error(result.getErrorMessage().toStdString());
            }
            else
                throw std::runtime_error("Unknown plugin view");
            auto response = mcpCall("vibera_state", obj())["structuredContent"];
            set(response, "completionState", "completed");
            auto request = obj();
            set(request, "plugin", juce::int64(id));
            set(request, "view", view);
            set(request, "completionObservable", view == "controls");
            set(request, "limit", "Controls selection completed; native widgets are not exposed");
            set(response, "pluginUIRequest", request);
            return textResult(response);
        }
        if (args.hasProperty("pluginView") || args.hasProperty("parameter") ||
            args.hasProperty("lastTweaked") || args.hasProperty("instance"))
            throw std::runtime_error("Plugin UI selection requires a project plugin ID");
        auto pattern = identity(args, "pattern", selectedPattern),
             channel = identity(args, "channel", selectedChannel);
        if (!controller.project.pattern(pattern) || !controller.project.channel(channel))
            throw std::runtime_error("Unknown Pattern or Channel");
        if (pattern != selectedPattern || channel != selectedChannel)
            requireFinishedTake();
        if (channel != selectedChannel)
            releaseTypingNotes();
        selectedPattern = pattern;
        selectedChannel = channel;
        focus = Scope(integer(args, "scope", int(focus), 0, int(Scope::sampler)));
        refresh();
        needsRebuild = true;
    }
    else if (name == "vibera_editor")
    {
        auto index = integer(args, "editor", -1, 0, 4);
        auto action = args["action"].toString();
        // 4 is the Browser window: only its window placement is shared with the editors.
        if (index == 4 && action != "detach" && action != "dock" && action != "move" && action != "layout" &&
            action != "browserFolder" && action != "browserForget" && action != "browserTagFilter" &&
            action != "browserSetTags" && action != "browserPreviewSeek" && action != "browserTagSearch")
            throw McpFailure("invalid_arguments", "editor 4 (Browser) supports detach, dock, move, layout, browserFolder, "
                                                  "browserForget, browserTagFilter, browserSetTags, browserTagSearch and browserPreviewSeek only");
        if (action.startsWith("browser") && index != 4)
            throw McpFailure("invalid_arguments", "browser actions use editor 4 (Browser)");
        if (args.hasProperty("enabled") && action != "grouping")
            throw McpFailure("invalid_arguments", "enabled is only valid for action grouping");
        if (args.hasProperty("chord") != (action == "stamp"))
            throw McpFailure("invalid_arguments", "chord is required for, and only valid with, action stamp");
        if (args.hasProperty("target") != (action == "laneTarget"))
            throw McpFailure("invalid_arguments", "target is required for, and only valid with, action laneTarget");
        if (action == "laneTarget")
        {
            const int target = noteLaneTargets().indexOf(args["target"].toString());
            if (index != 1 || target < 0)
                throw McpFailure("invalid_arguments", "laneTarget applies to editor 1 with a known target");
            pianoLaneTarget = target;
            refresh();
            return textResult(mcpCall("vibera_state", obj())["structuredContent"]);
        }
        if (action == "guide")
        {
            // Piano roll scale / maqam guide (view state) and whether incoming MIDI snaps to it.
            const auto scale = args["scale"].toString();
            const bool maqam = bool(args.getProperty("maqam", false));
            const int root = integer(args, "root", 0, 0, 11);
            if (scale.isNotEmpty())
            {
                const bool known = maqam ? std::any_of(score::maqamat().begin(), score::maqamat().end(),
                                                       [&](const auto &m) { return scale == m.name; })
                                         : std::any_of(score::scaleShapes().begin(), score::scaleShapes().end(),
                                                       [&](const auto &s) { return scale == s.name; });
                if (!known)
                    throw McpFailure("invalid_arguments", "Unknown scale or maqam: " + scale);
            }
            pianoGuide = scale.isEmpty() ? PianoGuide{} : PianoGuide{scale, root, maqam};
            if (args.hasProperty("snapIncoming"))
                snapIncomingMidi = bool(args["snapIncoming"]);
            updateMidiScaleSnap();
            refresh();
            return textResult(mcpCall("vibera_state", obj())["structuredContent"]);
        }
        if (action == "stamp")
        {
            const auto chord = args["chord"].toString();
            const auto &shapes = score::chordShapes();
            if (chord.isNotEmpty() && std::none_of(shapes.begin(), shapes.end(),
                                                   [&](const auto &shape) { return chord == shape.name; }))
                throw McpFailure("invalid_arguments", "Unknown chord shape: " + chord);
            pianoStamp = chord;
            refresh();
            return textResult(mcpCall("vibera_state", obj())["structuredContent"]);
        }
        if ((args.hasProperty("line") || args.hasProperty("markers")) && action != "snap")
            throw McpFailure("invalid_arguments", "line and markers are only valid with action snap");
        const bool lineMode = action == "snap" && bool(args.getProperty("line", false));
        const bool markerMode = action == "snap" && bool(args.getProperty("markers", false));
        if (lineMode || markerMode)
        {
            if (args.hasProperty("snap") || (lineMode && markerMode))
                throw McpFailure("invalid_arguments", "Give one of snap, line or markers");
            snapLine = lineMode;
            snapMarkers = markerMode;
            refresh();
        }
        else if (args.hasProperty("snap") != (action == "snap"))
            throw McpFailure("invalid_arguments", "snap is required for, and only valid with, action snap");
        else if (action == "snap")
        {
            const auto value = double(args["snap"]);
            if (!std::isfinite(value) || value < 0 || value > 32 || (value > 0 && value < 1. / 64))
                throw McpFailure("invalid_arguments", "snap is 0 (none) or 1/64..32 beats");
            snapLine = snapMarkers = false;
            snap = value;
            refresh();
        }
        else if (action == "grouping")
        {
            if (index != 2 || (args.hasProperty("enabled") && !args["enabled"].isBool()))
                throw McpFailure("invalid_arguments", "Grouping requires editor2 and a boolean enabled");
            if (auto result = setPlaylistGrouping(bool(args.getProperty("enabled", true))); result.failed())
                throw McpFailure("busy", result.getErrorMessage());
        }
        else if (action == "show")
            showEditor(index);
        else if (action == "hide")
        {
            if (detachedWindows[size_t(index)] ? detachedWindows[size_t(index)]->isVisible()
                                               : windows[size_t(index)]->isVisible())
                toggleEditor(index);
        }
        else if (action == "scroll")
        {
            auto *editor = static_cast<Editor *>(windows[size_t(index)]->getContentComponent());
            const auto scrolled = editor->scrollView(bool(args.getProperty("vertical", false)), number(args, "start", -1),
                                                     number(args, "span", 0));
            if (scrolled.failed())
                throw McpFailure("invalid_arguments", scrolled.getErrorMessage());
        }
        else if (action == "browserTagSearch")
            browser->filesForTest().searchTags(args["text"].toString());
        else if (action == "browserTagFilter" || action == "browserSetTags" || action == "browserPreviewSeek")
        {
            // The same Browser calls as clicking a tag, the Edit tags dialog and Shift+click on the preview.
            auto &files = browser->filesForTest();
            juce::Result result = juce::Result::ok();
            if (action == "browserTagFilter")
                result = files.tagFilter(args["tag"].toString());
            else if (action == "browserSetTags")
            {
                const auto path = args["path"].toString();
                if (!juce::File::isAbsolutePath(path) || !juce::File(path).existsAsFile())
                    throw McpFailure("invalid_arguments", "path must be an absolute existing file");
                juce::StringArray tags;
                if (const auto *list = args["tags"].getArray())
                    for (const auto &tag : *list)
                        tags.add(tag.toString());
                result = files.setTags(juce::File(path), tags);
            }
            else
                result = audio.seekAudition(float(number(args, "position", 0)));
            if (result.failed())
                throw McpFailure("invalid_target", result.getErrorMessage());
        }
        else if (action == "browserFolder" || action == "browserForget")
        {
            const auto path = args["path"].toString();
            if (!juce::File::isAbsolutePath(path))
                throw McpFailure("invalid_arguments", "path must be an absolute folder");
            auto &files = browser->filesForTest();
            const auto result = action == "browserFolder"
                                    ? files.openFolder(juce::File(path), !args.hasProperty("remember") || bool(args["remember"]))
                                    : files.forgetFolder(juce::File(path).getFullPathName());
            if (result.failed())
                throw McpFailure("invalid_target", result.getErrorMessage());
        }
        else if (action == "move")
        {
            // Same as dragging the window header: the window floats at these workspace bounds.
            const auto *b = args["bounds"].getArray();
            if (!b || b->size() != 4)
                throw McpFailure("invalid_arguments", "bounds is [x, y, width, height]");
            const juce::Rectangle<int> bounds{int((*b)[0]), int((*b)[1]), int((*b)[2]), int((*b)[3])};
            if (bounds.getWidth() < 100 || bounds.getHeight() < 60 || bounds.getWidth() > 16384 || bounds.getHeight() > 16384)
                throw McpFailure("invalid_arguments", "width/height are 100/60..16384");
            if (detachedWindows[size_t(index)])
                throw McpFailure("invalid_state", "Dock the detached window before moving it in the workspace");
            moveWorkspaceWindow(index, bounds);
        }
        else if (action == "layout")
        {
            // Same limits as the workspace splitters.
            auto take = [&](const char *key, float &field, float low, float high)
            {
                if (args.hasProperty(key))
                {
                    const double value = double(args[key]);
                    if (!std::isfinite(value))
                        throw McpFailure("invalid_arguments", juce::String(key) + " must be a number");
                    field = juce::jlimit(low, high, float(value));
                }
            };
            take("browserWidth", browserWidth, 180.f, 360.f);
            take("playlistRatio", playlistRatio, .12f, .8f);
            take("rackWidth", rackWidth, 330.f, 900.f);
            take("mixerHeight", mixerHeight, 168.f, 360.f);
            take("coProducerWidth", coProducerWidth, 340.f, 760.f);
            resized();
            repaint();
        }
        else if (action == "detach")
            detachWorkspaceWindow(index);
        else if (action == "dock")
            dockWorkspaceWindow(index);
        else if (action == "maximize")
            maximiseEditor(index);
        else
            throw std::runtime_error("Unknown editor action");
    }
    else if (name == "vibera_transport")
    {
        auto action = args["action"].toString();
        if (action == "play")
        {
            if (hasPendingMidiTake())
                throw McpFailure("take_pending", "Retry/cancel the retained MIDI take before Play");
            if (!audio.playing())
                play.onClick();
            if (devicePreparationBusy())
            {
                auto response = obj(); set(response, "completionState", "pending");
                set(response, "devicePreparation", devicePreparationState()); return textResult(response);
            }
            if (!audio.playing())
                throw McpFailure(
                    "playback_unavailable",
                    status.isEmpty() ? "Configure an audio device and wait for project preparation" : status);
        }
        else if (action == "previewNote")
        {
            const auto *target = controller.project.channel(Id(juce::int64(args["channel"])));
            if (!target || target->audioClip)
                throw McpFailure("invalid_target", "channel must be an existing score Channel");
            if ((audio.playing() && (!previewDuringPlayback || recordArmed)) || midiTaking || hasPendingMidiTake())
                throw McpFailure("busy", "Stop playback/recording before a note preview (or enable view.previewDuringPlayback)");
            const int pitch = args.hasProperty("pitch") ? int(args["pitch"])
                                                        : (target->sampler.root < 0 ? 60 : target->sampler.root);
            if (pitch < 0 || pitch > 127)
                throw McpFailure("invalid_arguments", "pitch must be 0..127");
            const double seconds = args.hasProperty("seconds") ? double(args["seconds"]) : 1.5;
            if (!std::isfinite(seconds) || seconds < .05 || seconds > 4)
                throw McpFailure("invalid_arguments", "seconds must be 0.05..4");
            selectedChannel = target->id;
            refresh();
            previewNote(-1);
            previewNote(pitch);
            auto safe = juce::Component::SafePointer<Studio>(this);
            juce::Timer::callAfterDelay(int(seconds * 1000), [safe, pitch]
            {
                if (safe && safe->previewPitch == pitch)
                    safe->previewNote(-1);
            });
        }
        else if (action == "loop")
        {
            if (hasPendingMidiTake())
                throw McpFailure("take_pending", "Retry/cancel the retained MIDI take before changing Loop");
            if (needsRebuild || !audio.projectionDiagnostics().edit)
                throw McpFailure("playback_unavailable",
                                 "Wait for project audio preparation before changing loop mode");
            if (args.hasProperty("range"))
            {
                const auto range = args["range"];
                if (range.isVoid() || range.isUndefined())
                    playlistRange.reset();
                else if (const auto *pair = range.getArray(); pair && pair->size() == 2 &&
                                                              double((*pair)[0]) >= 0 &&
                                                              double((*pair)[1]) > double((*pair)[0]) + 1.e-8)
                    playlistRange = std::pair{double((*pair)[0]), double((*pair)[1])};
                else
                    throw McpFailure("invalid_arguments", "range is [startBeat, endBeat] with end > start, or null");
                syncSongLoop();
                repaint();
            }
            const bool enabled = !args.hasProperty("enabled") || bool(args["enabled"]);
            if (enabled != audio.looping() && !performAction("transport.loop"))
                throw McpFailure(midiTaking ? "busy" : "playback_unavailable", status);
        }
        else if (action == "stop")
        {
            stop.onClick();
            if (hasPendingMidiTake())
                throw McpFailure("take_pending", status);
        }
        else if (action == "guide")
        {
            // Same engine setters as the Settings > Count-in page (local preferences, not Undo).
            if (args.hasProperty("countInBars"))
                audio.setCountInBars(integer(args, "countInBars", 0, 0, 2));
            if (args.hasProperty("sound"))
                audio.setGuideSound(integer(args, "sound", 0, 0, 2));
            if (args.hasProperty("gain"))
            {
                const double gain = number(args, "gain", -1);
                if (!(gain >= 0 && gain <= 1))
                    throw McpFailure("invalid_arguments", "gain is 0..1");
                audio.setGuideGain(float(gain));
            }
            if (args.hasProperty("metronome"))
                audio.setMetronome(bool(args["metronome"]));
            refresh();
        }
        else if (action == "typing")
        {
            // The Typing keys right-click menu: held typing notes are released first, then the choice is saved.
            releaseTypingNotes();
            if (args.hasProperty("typingRoot"))
                typingKeys.root = integer(args, "typingRoot", 5, 2, 7) * 12;
            if (args.hasProperty("typingVelocity"))
                typingKeys.velocity = integer(args, "typingVelocity", 100, 1, 127);
            saveTypingPreferences();
        }
        else if (action == "drumPad")
        {
            const bool on = !args.hasProperty("on") || bool(args["on"]);
            const auto played = drumPadNote(integer(args, "pad", on ? 0 : 1, 1, 16) - 1,
                                            float(args.hasProperty("velocity") ? double(args["velocity"]) : .85), on);
            if (played.failed())
                throw McpFailure("invalid_target", played.getErrorMessage());
        }
        else if (action == "previewSample")
        {
            // The Channel settings Preview button: audition the Channel's sample as it is on disk.
            const auto *c = controller.project.channel(Id(integer(args, "channel", 0, 1, std::numeric_limits<int>::max())));
            if (!c || c->instrument || c->source.isEmpty())
                throw McpFailure("invalid_target", "channel.noSample: channel is a sampler Channel with a sample");
            audio.requestAudition(c->source);
        }
        else if (action == "midiInput")
        {
            // Same router calls as the Settings > MIDI page; the channel filter change sends All Notes Off.
            requireFinishedTake();
            midiInputDevices = juce::MidiInput::getAvailableDevices(); // Rescan: this action is the Settings > MIDI page.
            if (args.hasProperty("device"))
            {
                const auto wanted = args["device"].toString();
                bool known = wanted.isEmpty();
                for (const auto &device : midiInputDevices)
                    known = known || device.identifier == wanted;
                if (!known)
                    throw McpFailure("invalid_target", "midi.unknownDevice: not in state.midiInput.devices");
                if (const auto opened = audio.midiInput.setDevice(wanted); opened.failed())
                    throw McpFailure("device_unavailable", opened.getErrorMessage());
            }
            if (args.hasProperty("midiChannel"))
                audio.midiInput.setChannel(integer(args, "midiChannel", 0, 0, 16));
            if (args.hasProperty("quantizeStart"))
                quantizeRecordingStart = bool(args["quantizeStart"]);
            if (args.hasProperty("quantizeEnd"))
                quantizeRecordingEnd = bool(args["quantizeEnd"]);
            audio.midiInput.setRecordingQuantize(quantizeRecordingStart, quantizeRecordingEnd);
        }
        else if (action == "jumpQuantize")
        {
            const int value = integer(args, "quantize", 0, -1, 8);
            if (value != 0 && value != -1 && value != 1 && value != 2 && value != 4 && value != 8)
                throw McpFailure("invalid_arguments", "quantize is 0 off, -1 beat, or 1/2/4/8 bars");
            songJumpQuantize = value;
            pendingSongJump.reset();
        }
        else if (action == "jump")
        {
            auto beat = number(args, "beat", -1);
            if (beat < 0 || beat > 1000000)
                throw std::runtime_error("Invalid jump position");
            requireFinishedTake();
            songJump(beat);
        }
        else if (action == "seek")
        {
            auto beat = number(args, "beat", -1);
            if (beat < 0 || beat > 1000000)
                throw std::runtime_error("Invalid seek position");
            requireFinishedTake();
            audio.seekBeat(beat);
        }
        else if (action == "pattern" || action == "song")
        {
            if (patternMode != (action == "pattern"))
            {
                requireFinishedTake();
                mode.onClick();
                if (patternMode != (action == "pattern"))
                    throw McpFailure("take_pending", status);
            }
        }
        else
            throw std::runtime_error("Unknown transport action");
    }
    else if (name == "vibera_key")
    {
        lastRoutingDispatchOperation = 0;
        lastMixerChainHistoryOperation.clear();
        lastLifecycleOperation.clear();
        lastStructuralOperation.clear();
        auto code = integer(args, "key", 0, 1, 65535);
        auto flags = modifiers(args);
        if (typingKeys.enabled && flags == 0)
            throw std::runtime_error("Disable typing keyboard before unmodified MCP shortcuts");
        juce::KeyPress key(code, juce::ModifierKeys(flags), 0);
        auto action = customization.resolve(key, focus);
        static const juce::StringArray safe{
            "selection.all", "selection.none", "selection.cancel", "tool.draw",  "tool.paint",
            "view.piano",    "view.rack",      "view.playlist",    "view.mixer", "midi.typing"};
        const juce::StringArray playlistViews{"tool.slice",      "tool.slip",
                                              "tool.mute",       "tool.select",
                                              "tool.delete",     "tool.zoom",
                                              "playlist.zoomIn", "playlist.zoomOut",
                                              "playlist.zoom1",  "playlist.zoom2",
                                              "playlist.zoom3",  "playlist.fit",
                                              "playlist.picker", "playlist.zoomSelection"};
        if (action == "pattern.delete" || action == "pattern.rename" || action == "pattern.properties" ||
            action == "pattern.transpose" || action == "pattern.group")
            throw std::runtime_error("Dialog shortcut is not exposed through MCP; use canonical command");
        if (!allowed(action) && !safe.contains(action) && !playlistViews.contains(action) &&
            !action.startsWith("piano.") && !action.startsWith("notes.nudge") &&
            !action.startsWith("notes.transpose") && !action.startsWith("notes.octave") &&
            action != "pattern.moveUp" && action != "pattern.moveDown")
            throw std::runtime_error("Shortcut is not exposed through MCP");
        if (!keyPressed(key))
            throw std::runtime_error("Shortcut handler did not accept key");
        if (lastRoutingDispatchOperation) return textResult(channelRoutingState(lastRoutingDispatchOperation));
        if (lastStructuralOperation.isNotEmpty())
            return textResult(mixerChainReadback(lastStructuralOperation));
        if (lastLifecycleOperation.isNotEmpty())
            return textResult(mixerChainReadback(lastLifecycleOperation));
        if (lastMixerChainHistoryOperation.isNotEmpty())
            return textResult(mixerChainReadback(lastMixerChainHistoryOperation));
    }
    else if (name == "vibera_pointer")
    {
        auto index = integer(args, "editor", -1, 0, 3);
        auto *editor = static_cast<Editor *>(windows[size_t(index)]->getContentComponent());
        auto phase = args["phase"].toString();
        if (phase == "cancel")
        {
            if (!mcpPointerTarget || mcpPointerEditor != index)
                throw std::runtime_error("No owned gesture in this editor");
            auto *target = mcpPointerTarget.getComponent();
            auto now = juce::Time::getCurrentTime();
            target->mouseExit(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {-1, -1},
                                               {}, 1, 0, 0, 0, 0, target, target, now, mcpPointerOrigin, now,
                                               1, false));
            editor->cancelPointerGesture();
            mcpPointerTarget = nullptr;
            mcpPointerEditor = -1;
        }
        else if (phase == "wheel")
        {
            if (!windows[size_t(index)]->isVisible())
                throw std::runtime_error("Show editor before pointer input");
            if (mcpPointerTarget)
                throw std::runtime_error("Finish the active pointer gesture first");
            const auto point = juce::Point<float>{float(number(args, "x", -1)), float(number(args, "y", -1))};
            if (!editor->getLocalBounds().toFloat().contains(point))
                throw std::runtime_error("Wheel position must be inside editor");
            auto *target = editor->getComponentAt(point.toInt());
            if (!target)
                throw std::runtime_error("No pointer target at coordinates");
            const auto local = target->getLocalPoint(editor, point);
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), local,
                                   juce::ModifierKeys(modifiers(args)), 1, 0, 0, 0, 0, target, target, now, local, now,
                                   1, false);
            juce::MouseWheelDetails wheel{};
            wheel.deltaY = float(number(args, "deltaY", 0));
            target->mouseWheelMove(event, wheel);
        }
        else
        {
            if (!windows[size_t(index)]->isVisible())
                throw std::runtime_error("Show editor before pointer input");
            auto point = juce::Point<float>{float(number(args, "x", -1)), float(number(args, "y", -1))};
            if (std::abs(point.x) > editor->getWidth() * 2 || std::abs(point.y) > editor->getHeight() * 2)
                throw std::runtime_error("Pointer coordinates exceed editor bounds");
            int clicks = integer(args, "clicks", 1, 1, 3);
            int flags = modifiers(args);
            if (phase == "down")
            {
                if (mcpPointerTarget || controller.hasGesture())
                    throw std::runtime_error("Another gesture is active");
                for (auto &window : windows)
                    if (static_cast<Editor *>(window->getContentComponent())->pointerGestureActive())
                        throw std::runtime_error("An editor gesture is already active");
                if (!editor->getLocalBounds().toFloat().contains(point))
                    throw std::runtime_error("Pointer down must be inside editor");
                auto button = args["button"].toString();
                if (button.isEmpty() || button == "left")
                    flags |= juce::ModifierKeys::leftButtonModifier;
                else if (button == "right")
                    flags |= juce::ModifierKeys::rightButtonModifier;
                else if (button == "middle")
                    flags |= juce::ModifierKeys::middleButtonModifier;
                else if (button == "x1")
                    flags |= juce::ModifierKeys::backButtonModifier;
                else
                    throw std::runtime_error("Unknown pointer button");
                mcpPointerTarget = editor->getComponentAt(point.toInt());
                if (!mcpPointerTarget)
                    throw std::runtime_error("No pointer target at coordinates");
                mcpPointerOrigin = mcpPointerTarget->getLocalPoint(editor, point);
                mcpPointerEditor = index;
                mcpPointerFlags = flags;
                mcpPointerProject = controller.projectIdentity;
            }
            else
            {
                if ((phase != "drag" && phase != "up") || !mcpPointerTarget || mcpPointerEditor != index)
                    throw std::runtime_error("Pointer drag/up requires its paired down in this editor");
                if (mcpPointerProject != controller.projectIdentity)
                {
                    mcpPointerTarget = nullptr;
                    mcpPointerEditor = -1;
                    throw std::runtime_error("Project replaced during gesture; start a new gesture");
                }
                flags |= mcpPointerFlags & juce::ModifierKeys::allMouseButtonModifiers;
                if (!args.hasProperty("ctrl") && !args.hasProperty("shift") && !args.hasProperty("alt"))
                    flags = mcpPointerFlags;
            }
            auto *target = mcpPointerTarget.getComponent();
            auto local = target->getLocalPoint(editor, point);
            auto now = juce::Time::getCurrentTime();
            juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), local,
                                   juce::ModifierKeys(flags), 1, 0, 0, 0, 0, target, target, now,
                                   mcpPointerOrigin, now, clicks, local != mcpPointerOrigin);
            if (phase == "down")
            {
                target->mouseDown(event);
                if (clicks > 1)
                    target->mouseDoubleClick(event);
            }
            else if (phase == "drag")
                target->mouseDrag(event);
            else
            {
                target->mouseUp(event);
                mcpPointerTarget = nullptr;
                mcpPointerEditor = -1;
            }
        }
    }
    else if (name != "vibera_state")
        throw std::runtime_error("Unknown tool");
    auto state = obj();
    set(state, "project", controller.project.name);
    set(state, "revision", juce::int64(controller.revision));
    set(state, "projectIdentity", controller.projectIdentity);
    set(state, "dirty", controller.dirty);
    set(state, "undoAvailable", controller.undo.canUndo());
    set(state, "redoAvailable", controller.undo.canRedo());
    set(state, "gestureActive", controller.hasGesture());
    auto notifications = obj();
    set(notifications, "controllerPending", controller.hasPendingControllerNotification());
    set(notifications, "undoRequestPending", controller.hasPendingUndoNotification());
    set(notifications, "undoObserverDeliveryAcknowledged", false);
    set(state, "historyNotifications", notifications);
    set(state, "selectedInsert", selectedInsert);
    set(state, "mixerOrder", mixerInsertOrderState(controller.project, selectedInsert));
    set(state, "mixerEffectsBrowser", mixerEffectBrowserState());
    set(state, "mixerEffectsPane",
        static_cast<Editor *>(windows[3]->getContentComponent())->effectSlotsState());
    set(state, "mixerRouting", mixerRoutingState());
    set(state, "mixerOrganisation", mixerOrganisationState());
    set(state, "clipGroupingEnabled", controller.clipGroupingEnabled);
    juce::Array<juce::var> selectedClipIds, effectiveClipIds, clipGroupRows;
    for (auto id : selectedClips)
        selectedClipIds.add(juce::int64(id));
    const auto effectiveClips = effectiveClipSelection();
    const std::unordered_set<Id> effectiveClipSet(effectiveClips.begin(), effectiveClips.end());
    for (auto id : effectiveClips)
        effectiveClipIds.add(juce::int64(id));
    std::unordered_map<Id, Id> clipGroupByMember;
    for (const auto &group : controller.project.clipGroups)
    {
        for (auto id : group.members)
            clipGroupByMember.emplace(id, group.id);
        if (clipGroupRows.size() >= 128)
            continue;
        auto row = obj();
        set(row, "id", juce::int64(group.id));
        set(row, "memberCount", int(group.members.size()));
        juce::Array<juce::var> members;
        for (auto id : group.members)
        {
            if (members.size() >= 512)
                break;
            members.add(juce::int64(id));
        }
        set(row, "members", members);
        clipGroupRows.add(row);
    }
    set(state, "selectedClips", selectedClipIds);
    set(state, "effectiveSelectedClips", effectiveClipIds);
    set(state, "clipGroups", clipGroupRows);
    set(state, "clipGroupCount", int(controller.project.clipGroups.size()));
    set(state, "quantizerPreview", quantizerPreviewState);
    set(state, "stepCompose", stepComposeState());
    if (quantizerWindow)
    {
        auto window = obj(), controls = obj();
        std::function<void(juce::Component *)> inspect = [&](juce::Component *parent)
        {
            for (auto *child : parent->getChildren())
            {
                const auto id = child->getComponentID();
                if (id.startsWith("quantizer."))
                {
                    const auto bounds = quantizerWindow->getLocalArea(child, child->getLocalBounds());
                    auto rectangle = obj();
                    set(rectangle, "x", bounds.getX());
                    set(rectangle, "y", bounds.getY());
                    set(rectangle, "width", bounds.getWidth());
                    set(rectangle, "height", bounds.getHeight());
                    set(rectangle, "enabled", child->isEnabled());
                    set(controls, id.toRawUTF8(), rectangle);
                }
                inspect(child);
            }
        };
        inspect(quantizerWindow.getComponent());
        set(window, "controls", controls);
        set(window, "width", quantizerWindow->getWidth());
        set(window, "height", quantizerWindow->getHeight());
        set(state, "quantizerWindow", window);
    }
    set(state, "quantizerPreviewActive", controller.hasQuantizerPreview());
    if (quantizerGuide)
    {
        auto guide = obj();
        set(guide, "name", quantizerGuide->name);
        set(guide, "cycleBeats", quantizerGuide->cycle);
        set(guide, "pointCount", int(quantizerGuide->points.size()));
        set(guide, "pattern", juce::int64(quantizerGuidePattern));
        set(guide, "displayLimit", 2048);
        set(state, "quantizerGuide", guide);
    }
    set(state, "gestureOwner", controller.gestureOwner().isEmpty() ? "local-or-none" : "mcp");
    set(state, "controllerProfile", midiController.state());
    set(state, "samplerControllers", samplerControllerState());
    if (browser)
        set(state, "browser", browser->filesForTest().mcpState()); // Readback for browser.* actions.
    set(state, "controllerLearnIndex", midiController.learnIndex);
    set(state, "selectedPattern", juce::int64(selectedPattern));
    set(state, "selectedChannel", juce::int64(selectedChannel));
    const auto currentRack = rackState();
    set(state, "rack", currentRack);
    for (const auto *key : {"soloChannel", "soloRestoreMuted", "soloScope", "soloGroup", "soloAffected"})
        set(state, key, currentRack[key]);
    set(state, "scope", int(focus));
    set(state, "testKeyDiagnosticsAvailable", controllerTestMode);
    if (controllerTestMode)
        set(state, "testKeyDispatches", getProperties()["testKeyDispatches"]);
    set(state, "autoTileMode", autoTileMode);
    set(state, "tilePrimaryEditor", tilePrimaryEditor);
    set(state, "playing", audio.playing());
    set(state, "patternMode", patternMode);
    set(state, "playlistFollowPlayhead", followPlayhead);
    set(state, "pianoFollowPlayhead", pianoFollowPlayhead);
    set(state, "followCenter", followCenter);
    set(state, "rackFocusSelected", rackFocusSelected);
    set(state, "previewDuringPlayback", previewDuringPlayback);
    set(state, "previewPitch", previewPitch);
    {
        juce::Array<juce::var> armed;
        for (int insert : renderArmedInserts)
            armed.add(insert);
        set(state, "mixerRenderArmed", armed);
    }
    set(state, "pianoBackgroundWaveform", pianoBackgroundWaveform);
    set(state, "rackButtonWidth", rackButtonWidth == 0 ? "narrow" : rackButtonWidth == 1 ? "normal" : "wide");
    set(state, "snap", snap);
    set(state, "snapLine", snapLine);
    set(state, "snapMarkers", snapMarkers);
    set(state, "audioStretchMode", audioStretchMode);
    set(state, "pianoStamp", pianoStamp);
    set(state, "songJumpQuantize", songJumpQuantize);
    if (pendingSongJump)
    {
        auto jump = obj();
        set(jump, "target", pendingSongJump->first);
        set(jump, "at", pendingSongJump->second);
        set(state, "pendingSongJump", jump);
    }
    {
        auto guide = obj();
        set(guide, "scale", pianoGuide.name);
        set(guide, "root", pianoGuide.root);
        set(guide, "maqam", pianoGuide.maqam);
        set(guide, "snapIncoming", snapIncomingMidi);
        juce::Array<juce::var> snapped; // What each incoming key 48..59 plays and records as.
        const auto mask = audio.midiInput.scaleSnap.load();
        for (int key = 48; key < 60; ++key)
            snapped.add(MidiInputRouter::snapToScale(key, mask));
        set(guide, "snappedKeys48to59", snapped);
        set(state, "pianoGuide", guide);
    }
    for (const auto &[editor, key] : {std::pair{1, "pianoScrollBeat"}, std::pair{2, "playlistScrollBeat"}})
        if (windows[size_t(editor)])
            if (const auto *view = dynamic_cast<const Editor *>(windows[size_t(editor)]->getContentComponent()))
                set(state, key, view->scrollBeat());
    set(state, "pianoLaneTarget", noteLaneTargets()[pianoLaneTarget]);
    set(state, "mixerMeterWave", mixerMeterWave);
    set(state, "pianoGridHighlights", pianoGridHighlights);
    set(state, "pianoKeyboardLabels", pianoKeyboardLabels == 0 ? "none" : pianoKeyboardLabels == 1 ? "root" : "white");
    {
        juce::Array<juce::var> tracks;
        for (const auto lane : selectedTracks)
            tracks.add(lane);
        set(state, "selectedTracks", tracks);
    }
    {
        auto labels = obj();
        if (const auto *labelled = controller.project.channel(selectedChannel))
            for (const auto &[keyPitch, text] : labelled->keyLabels)
                set(labels, juce::String(keyPitch).toRawUTF8(), text);
        set(state, "keyLabels", labels);
    }
    {
        juce::Array<juce::var> spares;
        for (const auto &[id, xml] : spareStates)
            spares.add(juce::int64(id));
        set(state, "spareStateChannels", spares);
        juce::Array<juce::var> effectSpareInserts;
        for (const auto insert : effectSpares)
            effectSpareInserts.add(insert);
        set(state, "spareStateInserts", effectSpareInserts);
        set(state, "spareStateBusy", effectSpareJob.has_value());
    }
    {
        juce::Array<juce::var> zipped;
        for (auto id : zippedChannels)
            zipped.add(juce::int64(id));
        set(state, "zippedChannels", zipped);
    }
    {
        // Playlist tracks with non-default settings (lanes without an entry use the defaults).
        juce::Array<juce::var> tracks;
        for (const auto &track : controller.project.tracks)
        {
            auto row = obj();
            set(row, "lane", track.lane);
            set(row, "name", track.name);
            set(row, "colour", juce::String::toHexString(int(track.colour)));
            set(row, "muted", track.muted);
            set(row, "groupedAbove", track.groupedAbove);
            set(row, "collapsed", track.collapsed);
            set(row, "locked", track.locked);
            set(row, "height", track.height);
            set(row, "channel", juce::int64(track.channel));
            set(row, "audio", track.audio);
            tracks.add(row);
        }
        set(state, "playlistTracks", tracks);
    }
    set(state, "recordArmed", recordArmed);
    set(state, "recordingTake", midiTaking);
    set(state, "recording", midiTakeState());
    set(state, "metronome", audio.metronome());
    set(state, "countInBars", audio.countInBars());
    set(state, "guideSound", audio.guideSound());
    set(state, "browserShown", browserShown);
    {
        auto layout = obj();
        set(layout, "browserWidth", double(browserWidth));
        set(layout, "playlistRatio", double(playlistRatio));
        set(layout, "rackWidth", double(rackWidth));
        set(layout, "mixerHeight", double(mixerHeight));
        set(layout, "coProducerWidth", double(coProducerWidth));
        set(state, "workspaceLayout", layout);
        juce::Array<juce::var> bounds;
        for (int i = 0; i < 5; ++i)
        {
            const auto *component = i == 4 ? static_cast<juce::Component *>(browser.get())
                                           : static_cast<juce::Component *>(windows[size_t(i)].get());
            const auto r = component ? component->getBounds() : juce::Rectangle<int>{};
            bounds.add(juce::Array<juce::var>{r.getX(), r.getY(), r.getWidth(), r.getHeight()});
        }
        set(state, "editorBounds", bounds);
        juce::Array<juce::var> views;
        for (int editor = 0; editor < 4; ++editor)
            views.add(static_cast<Editor *>(windows[size_t(editor)]->getContentComponent())->viewState());
        set(state, "editorViews", views);
    }
    set(state, "coProducerShown", coProducerShown);
    set(state, "guideGain", double(audio.guideGain()));
    {
        auto input = obj();
        set(input, "channel", audio.midiInput.channel());
        set(input, "quantizeStart", quantizeRecordingStart);
        set(input, "quantizeEnd", quantizeRecordingEnd);
        set(input, "device", audio.midiInput.identifier());
        set(input, "deviceStatus", audio.midiInput.status());
        juce::Array<juce::var> devices; // From the last midiInput call (a rescan), never enumerated here.
        for (const auto &device : midiInputDevices)
        {
            auto entry = obj();
            set(entry, "name", device.name);
            set(entry, "identifier", device.identifier);
            devices.add(entry);
        }
        set(input, "devices", devices);
        auto pads = obj();
        juce::Array<juce::var> padChannels;
        for (const auto id : drumPadChannels())
            padChannels.add(juce::int64(id));
        set(pads, "channels", padChannels);
        set(pads, "held", drumPadIndex + 1); // 0 = no pad held.
        set(state, "drumPads", pads);
        set(state, "midiRecordReplace", midiRecordReplace);
        set(state, "performanceShown", performanceWindow != nullptr && performanceWindow->isVisible());
        auto folders = obj();
        set(folders, "templates", templatesFolder().getFullPathName());
        set(folders, "backups", projectBackupsFolder().getFullPathName());
        set(state, "folders", folders);
        // Waveform view of the selected sampler Channel's open settings window (sampler.view.* actions).
        if (const auto window = instrumentWindows.find(selectedChannel);
            window != instrumentWindows.end() && window->second && window->second->isVisible())
            if (auto *range = dynamic_cast<SamplerRangeView *>(
                    window->second->controlsComponent()->findChildWithID("instrument.sampler.waveform")))
            {
                auto view = obj();
                set(view, "channel", juce::int64(selectedChannel));
                set(view, "start", range->viewStart());
                set(view, "span", range->viewLength());
                if (auto *viewport = range->findParentComponentOfClass<juce::Viewport>())
                    set(view, "windowScrollY", viewport->getViewPositionY());
                set(state, "samplerView", view);
            }
        auto typing = obj();
        set(typing, "enabled", typingKeys.enabled);
        set(typing, "root", typingKeys.root);
        set(typing, "velocity", typingKeys.velocity);
        set(state, "typingKeys", typing);
        set(state, "midiInput", input);
    }
    set(state, "positionBeat", audio.positionBeat());
    set(state, "playlistRange", playlistRange ? juce::var(juce::Array<juce::var>{playlistRange->first, playlistRange->second}) : juce::var());
    set(state, "tempo", controller.project.tempo);
    set(state, "masterPitch", controller.project.masterPitch);
    set(state, "waitForInput", waitForInput);
    set(state, "rememberSeekTime", rememberSeekTime);
    set(state, "disarmOnStop", disarmOnStop);
    set(state, "recordStartsPlayback", recordStartsPlayback);
    set(state, "timeDisplay", timeDisplaySeconds ? "seconds" : "beats");
    set(state, "positionSeconds", positionSeconds());
    set(state, "recordNotes", recordNotes);
    set(state, "exportOutputChannels", juce::StringArray{"stereo", "mono", "left", "right"}[audio.exportOutputChannels]);
    set(state, "exportDither", audio.exportDither);
    set(state, "audioLogger", audioLoggerState());
    set(state, "waitingForInput", waitingForInput);
    set(state, "pianoNoteModes",
        static_cast<Editor *>(windows[1]->getContentComponent())
            ->noteModes(juce::JSON::parse("{\"action\":\"state\"}")));
    set(state, "swing", controller.project.swing);
    set(state, "numerator", controller.project.numerator);
    set(state, "denominator", controller.project.denominator);
    set(state, "status", status);
    set(state, "automationRecording", automationRecordingState());
    set(state, "nativeScalarOperation", lastNativeScalarOperation);
    set(state, "nativeScalarPending", bool(nativeScalar));
    auto diagnostics = audio.projectionDiagnostics();
    set(state, "pendingAudioUpdate",
        mixerChainApplying() || devicePreparationBusy() || needsRebuild || routingPending() ||
            !audio.deferredWorkerPreparationIdle() || diagnostics.pluginSourceState == "pending");
    set(state, "devicePreparation", devicePreparationState());
    set(state, "channelRouting", channelRoutingState());
    auto instrumentOpening = obj();
    const bool waitingInstrument =
        pendingInstrument && pendingInstrument->matches(controller.projectIdentity, controller.project);
    set(instrumentOpening, "phase", waitingInstrument ? "waiting" : "idle");
    set(instrumentOpening, "channel", juce::int64(waitingInstrument ? pendingInstrument->channel : 0));
    set(instrumentOpening, "plugin", juce::int64(waitingInstrument ? pendingInstrument->plugin : 0));
    set(instrumentOpening, "projectionReady",
        waitingInstrument && audio.pluginProjectionReady(controller.project, pendingInstrument->plugin));
    set(state, "instrumentOpening", instrumentOpening);
    auto audioState = obj();
    set(audioState, "device", audio.deviceDescription());
    set(audioState, "graphCPUsConfigured", audio.engine.getEngineBehaviour().getNumberOfCPUsToUseForAudio());
    set(audioState, "graphWorkersConfigured",
        audio.engine.getEngineBehaviour().getNumberOfCPUsToUseForAudio() - 1);
    set(audioState, "replacements", juce::int64(diagnostics.replacements));
    set(audioState, "updates", juce::int64(diagnostics.updates));
    set(audioState, "samplerCreations", juce::int64(diagnostics.samplerCreations));
    set(audioState, "pluginSourceState", diagnostics.pluginSourceState);
    const auto sendAutomationHealth = audio.mixerSendAutomationHealthy();
    set(audioState, "sendAutomationHealthy",
        sendAutomationHealth ? juce::var(*sendAutomationHealth) : juce::var{});
    set(audioState, "pluginTransportPackets", diagnostics.pluginTransportPackets);
    set(audioState, "maximumPhysicalFrames", diagnostics.maximumPhysicalFrames);
    set(audioState, "maximumSourceFrames", diagnostics.maximumSourceFrames);
    set(audioState, "pluginDeviceEpoch", juce::int64(diagnostics.pluginDeviceEpoch));
    set(audioState, "headPlaying", diagnostics.headPlaying);
    auto loopBounds = obj();
    set(loopBounds, "enabled", diagnostics.loopEnabled);
    set(loopBounds, "startBeat", diagnostics.loopStartBeat);
    set(loopBounds, "endBeat", diagnostics.loopEndBeat);
    if (const auto selection = audio.songLoopBeats())
        set(loopBounds, "timeSelection", juce::Array<juce::var>{selection->first, selection->second});
    else
        set(loopBounds, "timeSelection", juce::var());
    set(loopBounds, "appliedAvailable", diagnostics.headLoopAvailable);
    set(loopBounds, "appliedEnabled",
        diagnostics.headLoopAvailable ? juce::var(diagnostics.headLoopEnabled) : juce::var{});
    set(loopBounds, "appliedStartBeat",
        diagnostics.headLoopAvailable ? juce::var(diagnostics.headLoopStartBeat) : juce::var{});
    set(loopBounds, "appliedEndBeat",
        diagnostics.headLoopAvailable ? juce::var(diagnostics.headLoopEndBeat) : juce::var{});
    set(audioState, "loopBounds", loopBounds);
    juce::Array<juce::var> audioChannels;
    for (auto &channel : diagnostics.channels)
    {
        auto item = obj();
        set(item, "channel", juce::int64(channel.channel));
        set(item, "graphInitialisations", juce::int64(channel.graphInitialisations));
        set(item, "renderCalls", juce::int64(channel.renderCalls));
        set(item, "samplerAutomationFailures", juce::int64(channel.samplerAutomationFailures));
        set(item, "samplerAutomationFailureReason", int(channel.samplerAutomationFailureReason));
        set(item, "samplerAutomationFailureReasonCodes",
            "0 idle; 1 ready; 2 invalidBlock; 3 invalidCurve; 4 staleView. Retained after recovery.");
        set(item, "scheduledNotes", channel.midiNotes);
        set(item, "heldInputKeys", channel.heldInputKeys);
        set(item, "lastPlaying", channel.lastPlaying);
        set(item, "lastTime", channel.lastTime);
        audioChannels.add(item);
    }
    set(audioState, "channels", audioChannels);
    juce::Array<juce::var> pluginWorkers;
    for (const auto &plugin : audio.pluginDiagnostics())
    {
        auto item = obj();
        set(item, "plugin", juce::int64(plugin.id));
        set(item, "pid", juce::int64(plugin.processId));
        if (auto host = audio.parameterHost(plugin.id))
        {
            set(item, "instance", host->runtimeIdentity());
            set(item, "auxiliaryInputBus", host->busLayout().auxiliaryInputBus);
            set(item, "auxiliaryInputChannels", host->busLayout().auxiliaryChannels);
            set(item, "bypassTransitionSamples", host->bypassTransitionSamples());
            const auto blend = host->bypassBlend();
            set(item, "bypassBlend", blend ? juce::var(*blend) : juce::var{});
            const auto wetBlend = host->wetBlend();
            set(item, "wetBlend", wetBlend ? juce::var(*wetBlend) : juce::var{});
            const auto declared = host->declaredLatency();
            auto declaration = obj();
            set(declaration, "nativeSamples", declared.nativeSamples);
            set(declaration, "generation", juce::int64(declared.generation));
            set(item, "latencyDeclared", declaration);
            const auto consumed = host->consumedLatency();
            juce::var consumption;
            if (consumed)
            {
                consumption = obj();
                set(consumption, "nativeSamples", consumed->stamp.nativeSamples);
                set(consumption, "generation", juce::int64(consumed->stamp.generation));
                set(consumption, "sequence", juce::int64(consumed->sequence));
            }
            set(item, "latencyConsumed", consumption);
        }
        set(item, "alive", plugin.alive);
        set(item, "latencySamples", plugin.latency);
        set(item, "submitted", juce::int64(plugin.packets.submitted));
        set(item, "processed", juce::int64(plugin.packets.processed));
        set(item, "missed", juce::int64(plugin.packets.missed));
        set(item, "dropped", juce::int64(plugin.packets.dropped));
        set(item, "midiOverflow", juce::int64(plugin.packets.midiOverflow));
        set(item, "parametersApplied", juce::int64(plugin.packets.parametersApplied));
        set(item, "parametersRejected", juce::int64(plugin.packets.parametersRejected));
        set(item, "auxiliaryRejected", juce::int64(plugin.packets.auxiliaryRejected));
        set(item, "phase", ipc::dspPhaseName(plugin.packets.workerPhase));
        set(item, "heartbeat", juce::int64(plugin.packets.workerHeartbeat));
        pluginWorkers.add(item);
    }
    set(audioState, "pluginWorkers", pluginWorkers);
    set(audioState, "pluginControls", pluginControlSnapshot());
    auto lastTweak = juce::var{};
    if (const auto target = lastPluginTweak())
    {
        lastTweak = obj();
        set(lastTweak, "plugin", juce::int64(target->plugin));
        set(lastTweak, "instance", target->instance);
        set(lastTweak, "parameter", juce::int64(target->gesture.id));
        set(lastTweak, "generation", juce::int64(target->gesture.generation));
        set(lastTweak, "projectIdentity", target->project);
        set(lastTweak, "serial", juce::int64(target->gesture.serial));
        set(lastTweak, "completedTicks", juce::int64(target->gesture.completedTicks));
        set(lastTweak, "gestureValue", target->gesture.after);
    }
    set(audioState, "lastTweakedPlugin", lastTweak);
    set(state, "audio", audioState);
    juce::Array<juce::var> channels, patterns, notes, editors, clips;
    for (auto &channel : controller.project.channels)
    {
        auto c = obj();
        set(c, "id", juce::int64(channel.id));
        set(c, "name", channel.name);
        set(c, "colour", juce::int64(channel.colour));
        set(c, "icon", channel.icon);
        set(c, "audioClip", channel.audioClip);
        set(c, "muted", channel.muted);
        set(c, "muteLocked", channel.muteLocked);
        set(c, "group", juce::int64(channel.group));
        set(c, "mixer", channel.mixer);
        set(c, "gain", channel.gain);
        set(c, "pan", channel.pan);
        set(c, "swingMix", channel.swingMix);
        set(c, "truncateSwing", channel.truncateSwing);
        auto sampler = obj();
        set(sampler, "available", !channel.instrument && !channel.audioClip);
        set(sampler, "root", channel.sampler.root);
        set(sampler, "cents", channel.sampler.cents);
        set(sampler, "start", channel.sampler.start);
        set(sampler, "end", channel.sampler.end);
        set(sampler, "reverse", channel.sampler.reverse);
        set(sampler, "normalize", channel.sampler.normalize);
        set(sampler, "filterType", channel.sampler.filterType);
        juce::Array<juce::var> modulation;
        for (size_t target = 0; target < samplerModulationTargets.size(); ++target)
        {
            auto row = obj();
            set(row, "target", juce::String(samplerModulationTargets[target].data()));
            for (const auto &parameter : samplerModulationParameters)
            {
                const auto value = samplerModulationValue(channel.sampler.modulation[target], parameter);
                set(row, parameter.id.data(),
                    parameter.boolean ? juce::var(value != 0) : juce::var(value));
            }
            set(row, "envelopeAvailable", target != 1 && !channel.instrument && !channel.audioClip);
            set(row, "lfoAvailable", !channel.instrument && !channel.audioClip);
            modulation.add(row);
        }
        set(sampler, "modulation", modulation);
        auto modulationAutomation = obj();
        const bool samplerAutomationAvailable = !channel.instrument && !channel.audioClip;
        set(modulationAutomation, "available", samplerAutomationAvailable);
        set(modulationAutomation, "reason",
            samplerAutomationAvailable ? "localSampler" : "dormantUnsupportedChannelRole");
        set(modulationAutomation, "discovery",
            "vibera_commands command=automation.sampler.create includeSchemas=true");
        set(modulationAutomation, "readback",
            "Paginated automationLinks/automationInitialValues with kind7 and owner=Channel ID; parameter "
            "uses stable sampler code.");
        set(modulationAutomation, "dspAcknowledged", juce::var{});
        set(sampler, "modulationAutomation", modulationAutomation);
        set(sampler, "modulationAcknowledged", juce::var{});
        set(sampler, "modulationScope",
            "Original per-voice D-A-H-D-S-R plus five LFOs. Tempo units are quarter-note beats; otherwise "
            "seconds. Global shares oscillator phase only; per-note delay/fade. Read requested parameters "
            "and pendingAudioUpdate separately; no active voice acknowledgement.");
        set(sampler, "filterTypeName", samplerFilterTypeName(channel.sampler.filterType));
        set(sampler, "filterCutoff", channel.sampler.filterCutoff);
        set(sampler, "filterResonance", channel.sampler.filterResonance);
        const bool filterAvailable = !channel.instrument && !channel.audioClip;
        auto *filterDevice = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        const double filterRate = filterDevice ? filterDevice->getCurrentSampleRate() : 0.;
        set(sampler, "filterAvailable", filterAvailable);
        set(sampler, "filterSampleRate",
            filterAvailable && filterRate > 0 ? juce::var(filterRate) : juce::var{});
        set(sampler, "filterCutoffHz",
            filterAvailable && filterRate > 0
                ? juce::var(samplerFilterCutoffHz(channel.sampler.filterCutoff, filterRate))
                : juce::var{});
        set(sampler, "filterQ",
            filterAvailable ? juce::var(samplerFilterQ(channel.sampler.filterResonance)) : juce::var{});
        set(sampler, "filterBypassScope",
            "Global settings at neutral Mod X/Y only. Per-note modulation can activate a full-open LP; "
            "Filter Off always bypasses. This is not an active-voice acknowledgement.");
        set(sampler, "filterBypassed",
            filterAvailable
                ? juce::var(channel.sampler.filterType == 0 ||
                            ((channel.sampler.filterType == 1 || channel.sampler.filterType == 2) &&
                             channel.sampler.filterCutoff == 1 && channel.sampler.filterResonance == 0))
                : juce::var{});
        const auto prepared =
            !channel.instrument && !channel.audioClip ? audio.channelSample(channel.id) : nullptr;
        set(sampler, "normalizationAvailable", bool(prepared));
        set(sampler, "sourcePeak", prepared ? juce::var(prepared->sourcePeak) : juce::var{});
        set(sampler, "normalizationGain", prepared ? juce::var(prepared->normalizationGain) : juce::var{});
        set(sampler, "loop", channel.sampler.loop);
        set(sampler, "pingPong", channel.sampler.pingPong);
        set(sampler, "fadeIn", channel.sampler.fadeIn);
        set(sampler, "fadeOut", channel.sampler.fadeOut);
        set(sampler, "loopCrossfade", channel.sampler.loopCrossfade);
        set(sampler, "fileLoopStart",
            prepared && prepared->fileLoopStart >= 0 ? juce::var(prepared->fileLoopStart) : juce::var{});
        set(sampler, "fileLoopEnd",
            prepared && prepared->fileLoopEnd >= 0 ? juce::var(prepared->fileLoopEnd) : juce::var{});
        set(sampler, "stretch", channel.sampler.stretch);
        set(sampler, "hqInterpolation", channel.sampler.hqInterpolation);
        set(sampler, "stretchPitch", channel.sampler.stretchPitch);
        set(sampler, "timeMul", channel.sampler.timeMul);
        set(sampler, "timeBeats", channel.sampler.timeBeats);
        set(sampler, "loopStart", channel.sampler.loopStart);
        set(sampler, "loopEnd", channel.sampler.loopEnd);
        set(sampler, "envelope", channel.sampler.envelope);
        set(sampler, "attack", channel.sampler.attack);
        set(sampler, "decay", channel.sampler.decay);
        set(sampler, "sustain", channel.sampler.sustain);
        set(sampler, "release", channel.sampler.release);
        set(sampler, "sourceTempo", channel.sampler.sourceTempo);
        set(sampler, "fitTempo", channel.sampler.fitTempo);
        set(sampler, "cut", channel.sampler.cut);
        set(sampler, "cutBy", channel.sampler.cutBy);
        set(sampler, "maxPoly", channel.sampler.maxPoly);
        set(sampler, "mono", channel.sampler.mono);
        set(sampler, "portamento", channel.sampler.portamento);
        set(sampler, "fullPortamento", channel.sampler.fullPortamento);
        set(sampler, "slideTime", channel.sampler.slideTime);
        set(sampler, "gateBeats", channel.sampler.gateBeats);
        set(sampler, "voiceModeAcknowledged", juce::var{}); // Requested model values, not a voice/DSP ACK.
        set(sampler, "voiceCeiling",
            channel.instrument || channel.audioClip ? juce::var{} : juce::var(int(VoiceBank::capacity)));
        set(sampler, "voiceControlsAvailable", !channel.instrument && !channel.audioClip);
        set(c, "sampler", sampler);
        set(c, "instrument", channel.instrument ? channel.instrument->name : sourceName(channel.source));
        if (channel.instrument)
        {
            juce::Array<juce::var> routes;
            for (int target : channel.instrument->extraOutputMixers)
                routes.add(target);
            set(c, "extraOutputMixers", routes);
        }
        channels.add(c);
    }
    juce::Array<juce::var> samplerWindows;
    for (auto &[channel, window] : instrumentWindows)
    {
        if (!window || samplerWindows.size() >= 128)
            continue;
        auto item = obj();
        set(item, "channel", juce::int64(channel));
        set(item, "visible", window->isVisible());
        set(item, "modulationTarget",
            juce::String(samplerModulationTargets[size_t(window->currentModulationTarget())].data()));
        set(item, "instance",
            juce::String::toHexString(juce::int64(reinterpret_cast<std::uintptr_t>(window.get()))));
        auto rectangle = [](juce::Rectangle<int> r)
        {
            auto value = obj();
            set(value, "x", r.getX());
            set(value, "y", r.getY());
            set(value, "width", r.getWidth());
            set(value, "height", r.getHeight());
            return value;
        };
        set(item, "bounds", rectangle(window->getLocalBounds()));
        auto controls = obj();
        for (auto id :
             {"instrument.sampler.showKeyboard", "instrument.sampler.waveform", "instrument.sampler.keyboard",
              "instrument.sampler.cut", "instrument.sampler.cutBy", "instrument.sampler.maxPoly",
              "instrument.sampler.cutItself", "instrument.sampler.mono", "instrument.sampler.portamento",
              "instrument.sampler.fullPortamento", "instrument.sampler.slideTime",
              "instrument.sampler.gateBeats", "instrument.sampler.normalize", "instrument.sampler.filterType",
              "instrument.sampler.filterCutoff", "instrument.sampler.filterResonance"})
            if (auto *component = window->controlsComponent()->findChildWithID(id))
                set(controls, id, rectangle(window->getLocalArea(component, component->getLocalBounds())));
        if (auto *waveform = dynamic_cast<SamplerRangeView *>(
                window->controlsComponent()->findChildWithID("instrument.sampler.waveform")))
        {
            set(item, "waveformViewStart", waveform->viewStart());
            set(item, "waveformViewLength", waveform->viewLength());
        }
        if (auto *keyboard = dynamic_cast<SamplerKeyboard *>(
                window->controlsComponent()->findChildWithID("instrument.sampler.keyboard")))
        {
            juce::Array<juce::var> keys;
            auto &component = keyboard->keyComponent();
            for (int pitch = 0; pitch <= 127; ++pitch)
            {
                const auto local = component.getRectangleForKey(pitch).toNearestInt();
                if (!component.getLocalBounds().contains(local.getCentre()))
                    continue;
                auto key = obj();
                set(key, "pitch", pitch);
                set(key, "bounds", rectangle(window->getLocalArea(&component, local)));
                keys.add(key);
            }
            set(item, "keys", keys);
        }
        set(item, "controls", controls);
        samplerWindows.add(item);
    }
    set(state, "samplerWindows", samplerWindows);
    for (auto &pattern : controller.project.patterns)
    {
        auto p = obj();
        set(p, "id", juce::int64(pattern.id));
        set(p, "name", pattern.name);
        set(p, "group", pattern.group);
        set(p, "colour", pattern.colour ? juce::Colour(pattern.colour).toString() : juce::String("auto"));
        set(p, "length", pattern.length);
        set(p, "autoLength", pattern.autoLength);
        set(p, "numerator", pattern.numerator);
        set(p, "denominator", pattern.denominator);
        set(p, "effectiveNumerator", patternNumerator(controller.project, pattern));
        set(p, "effectiveDenominator", patternDenominator(controller.project, pattern));
        set(p, "barBeats", patternBarBeats(controller.project, pattern));
        set(p, "notes", int(pattern.notes.size()));
        juce::Array<juce::var> loops;
        for (const auto &loop : pattern.channelLoops)
        {
            auto item = obj();
            set(item, "channel", juce::int64(loop.channel));
            set(item, "mode", channelLoopModeName(loop.mode));
            set(item, "length", loop.length);
            set(item, "resolvedLength", channelLoopLength(controller.project, pattern, loop.channel));
            loops.add(item);
        }
        set(p, "channelLoops", loops);
        patterns.add(p);
        if (patterns.size() >= 128)
            break;
    }
    if (auto *pattern = controller.project.pattern(selectedPattern))
        for (auto &note : pattern->notes)
        {
            if (notes.size() >= 512)
                break;
            auto n = obj();
            set(n, "id", juce::int64(note.id));
            set(n, "channel", juce::int64(note.channel));
            set(n, "beat", note.beat);
            set(n, "length", note.length);
            set(n, "pitch", note.pitch);
            set(n, "velocity", note.velocity);
            set(n, "muted", note.muted);
            set(n, "slide", note.slide);
            set(n, "portamento", note.portamento);
            set(n, "colourGroup", note.colourGroup);
            set(n, "midiChannel", note.colourGroup + 1);
            set(n, "transitionSupported",
                controller.project.channel(note.channel) &&
                    !controller.project.channel(note.channel)->instrument &&
                    !controller.project.channel(note.channel)->audioClip);
            set(n, "transitionAcknowledged", juce::var{});
            set(n, "shiftPercent", note.shiftPercent);
            set(n, "repeatMode", note.repeatMode);
            set(n, "repeatRate", noteRepeatRate(note.repeatMode)->label);
            set(n, "repeatIntervalBeats", noteRepeatInterval(note.repeatMode));
            set(n, "releaseVelocity", note.releaseVelocity);
            set(n, "modX", note.modX);
            set(n, "modY", note.modY);
            set(n, "releaseMidiVelocity", int(noteReleaseMidiVelocity(note.releaseVelocity)));
            const auto *releaseChannel = controller.project.channel(note.channel);
            set(n, "releaseVelocityAudioResponse",
                releaseChannel ? noteReleaseAudioResponse(*releaseChannel) : "unavailable");
            set(n, "shiftDelayBeats", noteShiftDelay(note.shiftPercent));
            const auto *channel = controller.project.channel(note.channel);
            const bool modulationSupported = channel && !channel->instrument && !channel->audioClip;
            set(n, "noteModulationSupported", modulationSupported);
            set(n, "noteModulationScope",
                modulationSupported
                    ? "Original local sampler composition: clamp(global + 2*(note-0.5),0,1). Mod X "
                      "offsets cutoff; Mod Y offsets resonance. Filter Off is a no-op. Source metadata, "
                      "not a voice/DSP ACK."
                    : "Mod X/Y stored only; native/VST3 and Audio Clip per-note sampler modulation "
                      "unsupported.");
            set(n, "noteModulationAcknowledged", juce::var{});
            const auto delay = channel ? swingDelay(note.beat, controller.project.swing, *channel) : 0;
            set(n, "swingDelayBeats", delay);
            set(n, "playbackBeat", note.beat + delay + noteShiftDelay(note.shiftPercent));
            set(n, "playbackLength",
                channel && channel->truncateSwing ? std::max(.000001, note.length - delay) : note.length);

            auto bounds = static_cast<Editor *>(windows[1]->getContentComponent())->noteBounds(note);
            auto rectangle = obj();
            set(rectangle, "x", bounds.getX());
            set(rectangle, "y", bounds.getY());
            set(rectangle, "width", bounds.getWidth());
            set(rectangle, "height", bounds.getHeight());
            set(n, "pianoBounds", rectangle);
            notes.add(n);
        }
    auto appendClip = [&](Id id, const char *kind, Id source, int lane, double beat, double length,
                          double offset, bool muted)
    {
        if (clips.size() >= 512)
            return;
        auto clip = obj();
        set(clip, "id", juce::int64(id));
        set(clip, "kind", kind);
        const auto member = clipGroupByMember.find(id);
        set(clip, "groupId", juce::int64(member == clipGroupByMember.end() ? 0 : member->second));
        set(clip, "selectedEffective", effectiveClipSet.contains(id));
        set(clip, "sourceId", juce::int64(source));
        set(clip, "lane", lane);
        set(clip, "beat", beat);
        set(clip, "lengthBeats", length);
        set(clip, "offset", offset);
        set(clip, "offsetUnit", juce::String(kind) == "audio" ? "seconds" : "beats");
        set(clip, "muted", muted);
        auto bounds = static_cast<Editor *>(windows[2]->getContentComponent())->playlistClipBounds(id);
        auto rectangle = obj();
        set(rectangle, "x", bounds.getX());
        set(rectangle, "y", bounds.getY());
        set(rectangle, "width", bounds.getWidth());
        set(rectangle, "height", bounds.getHeight());
        set(clip, "playlistBounds", rectangle);
        clips.add(clip);
    };
    for (const auto &clip : controller.project.placements)
        appendClip(clip.id, "pattern", clip.pattern, clip.lane, clip.beat, clip.length, clip.offset,
                   clip.muted);
    for (const auto &clip : controller.project.audioPlacements)
        appendClip(clip.id, "audio", clip.channel, clip.lane, clip.beat,
                   controller.project.audioEndBeat(clip.beat, clip.duration) - clip.beat, clip.offset, clip.muted);
    for (const auto &clip : controller.project.automation.placements)
        appendClip(clip.id, "automation", clip.source, clip.lane, clip.beat, clip.length, clip.offset,
                   clip.muted);
    set(state, "clips", clips);
    juce::Array<juce::var> automationSources;
    for (const auto &source : controller.project.automation.sources)
    {
        if (automationSources.size() >= 128)
            break;
        auto item = obj();
        set(item, "id", juce::int64(source.id));
        set(item, "name", juce::String::fromUTF8(source.name.c_str()));
        set(item, "length", source.length);
        juce::Array<juce::var> points, links;
        for (const auto &point : source.points)
        {
            if (points.size() >= 512)
                break;
            auto p = obj();
            set(p, "id", juce::int64(point.id));
            set(p, "beat", point.beat);
            set(p, "value", point.value);
            set(p, "shape", int(point.incoming));
            set(p, "tension", point.tension);
            points.add(p);
        }
        for (const auto &link : source.links)
        {
            if (links.size() >= 128)
                break;
            auto l = obj();
            set(l, "mixer", juce::int64(link.key.owner)); // Compatibility alias; inspect actual kind below.
            set(l, "kind", int(link.key.kind));
            set(l, "owner", juce::int64(link.key.owner));
            set(l, "parameter", juce::int64(link.key.parameter));
            set(l, "minimum", link.minimum);
            set(l, "maximum", link.maximum);
            links.add(l);
        }
        set(item, "points", points);
        set(item, "links", links);
        set(item, "linksTruncated", source.links.size() > 128);
        set(item, "pointsTruncated", source.points.size() > 512);
        automationSources.add(item);
    }
    set(state, "automationSources", automationSources);
    set(state, "automationSourcesTruncated", controller.project.automation.sources.size() > 128);
    juce::Array<juce::var> automationInitialValues;
    for (const auto &initial : controller.project.automation.initialValues)
    {
        auto value = obj();
        set(value, "kind", int(initial.key.kind)); set(value, "owner", juce::int64(initial.key.owner));
        set(value, "parameter", juce::int64(initial.key.parameter)); set(value, "value", initial.value);
        automationInitialValues.add(value);
    }
    set(state, "automationInitialValues", automationInitialValues);
    set(state, "projectVersion", controller.project.version);
    for (int i = 0; i < 4; ++i)
    {
        auto e = obj();
        auto *component = windows[size_t(i)]->getContentComponent();
        set(e, "index", i);
        if (i == 1 || i == 2)
            if (auto *timeline = dynamic_cast<Editor *>(component))
                set(e, "timeline", timeline->timelineGeometry());
        set(e, "visible",
            detachedWindows[size_t(i)] ? detachedWindows[size_t(i)]->isVisible()
                                       : windows[size_t(i)]->isVisible());
        set(e, "width", component->getWidth());
        set(e, "height", component->getHeight());
        const auto contentBounds = getLocalArea(component, component->getLocalBounds());
        set(e, "contentBounds",
            juce::Array<juce::var>{contentBounds.getX(), contentBounds.getY(), contentBounds.getWidth(),
                                   contentBounds.getHeight()});
        juce::Array<juce::var> controls;
        for (int child = 0; child < component->getNumChildComponents(); ++child)
            if (auto *button = dynamic_cast<juce::TextButton *>(component->getChildComponent(child)))
            {
                auto control = obj();
                auto bounds = button->getBounds();
                set(control, "name", button->getButtonText());
                set(control, "x", bounds.getX());
                set(control, "y", bounds.getY());
                set(control, "width", bounds.getWidth());
                set(control, "height", bounds.getHeight());
                set(control, "enabled", button->isEnabled());
                set(control, "toggled", button->getToggleState());
                controls.add(control);
            }
        set(e, "controls", controls);
        if (i == 1)
        {
            const auto r = static_cast<Editor *>(component)->pianoNoteViewport();
            set(e, "noteViewportBounds",
                juce::Array<juce::var>{r.getX(), r.getY(), r.getWidth(), r.getHeight()});
        }
        set(e, "floating", floatingWindows[size_t(i)]);
        set(e, "detached", detachedWindows[size_t(i)] != nullptr);
        if (auto *window = detachedWindows[size_t(i)].get())
        {
            const auto r = window->getBounds();
            set(e, "desktopBounds", juce::Array<juce::var>{r.getX(), r.getY(), r.getWidth(), r.getHeight()});
            set(e, "desktopScale", window->getPeer() ? window->getPeer()->getPlatformScaleFactor() : 1.);
            if (auto *button = window->getMinimiseButton())
                set(e, "minimiseBounds",
                    juce::Array<juce::var>{button->getX(), button->getY(), button->getWidth(),
                                           button->getHeight()});
        }
        editors.add(e);
    }
    set(state, "channels", channels);
    set(state, "patterns", patterns);
    set(state, "notes", notes);
    set(state, "editors", editors);
    juce::Array<juce::var> selection;
    for (auto id : selectedNotes)
    {
        if (selection.size() >= 1000)
            break;
        selection.add(juce::int64(id));
    }
    set(state, "selectedNotes", selection);
    auto piano = obj();
    auto *pianoEditor = static_cast<Editor *>(windows[1]->getContentComponent());
    set(piano, "visibleBeats", pianoEditor->visibleBeatCount());
    set(piano, "repeatInterval", pianoEditor->repeatInterval());
    set(piano, "tool",
        pianoEditor->noteSliceToolActive()  ? "slice"
        : pianoEditor->noteMuteToolActive() ? "mute"
        : pianoEditor->painting()           ? "paint"
                                            : "draw");
    set(piano, "slicePreview", noteSliceState());
    set(piano, "stretchPreview", noteStretchState());
    const auto stretchHandle = pianoEditor->stretchHandleBounds();
    set(piano, "stretchHandleBounds",
        juce::Array<juce::var>{stretchHandle.getX(), stretchHandle.getY(), stretchHandle.getWidth(),
                               stretchHandle.getHeight()});
    set(state, "piano", piano);
    auto playlist = obj();
    auto *playlistEditor = static_cast<Editor *>(windows[2]->getContentComponent());
    set(playlist, "visibleBeats", playlistEditor->visibleBeatCount());
    set(state, "playlist", playlist);
    set(state, "pointerGestureActive", mcpPointerTarget != nullptr);
    set(state, "notesTruncated",
        controller.project.pattern(selectedPattern) &&
            controller.project.pattern(selectedPattern)->notes.size() > 512);
    set(state, "patternsTruncated", controller.project.patterns.size() > 128);
    set(state, "inputEvidence", "Component state/render; no physical input/listening qualification");
    if ((name == "vibera_transport" && args["action"].toString() == "loop") ||
        (name == "vibera_command" && args["command"].toString() == "transport.loop"))
    {
        set(state, "completionState", "completed");
        set(state, "completionScope",
            "Transport configuration; applied playhead state is not audible output");
    }
    return textResult(state);
}
void Studio::completeMcpOperation(const juce::String &id, juce::Result result)
{
    if (id.isEmpty())
        return;
    for (auto &operation : mcpOperations)
        if (operation.id == id)
        {
            operation.state = result.wasOk()                                          ? "completed"
                              : result.getErrorMessage().startsWith("export.cancelled") ? "cancelled"
                                                                                       : "failed";
            operation.message = result.wasOk() ? "Operation finished" : result.getErrorMessage();
            return;
        }
}
juce::var Studio::mcpCatalogueOperation(const juce::var &args)
{
    const auto action = args["action"].toString();
    auto snapshot = plugins.snapshot();
    auto result = obj();
    const auto strings = [](const juce::StringArray &items)
    {
        juce::Array<juce::var> values;
        for (const auto &item : items)
            values.add(item);
        return juce::var(values);
    };
    if (action == "scan")
    {
        auto folders = snapshot.folders;
        if (args.hasProperty("customFolders"))
        {
            if (!args["customFolders"].isArray())
                throw McpFailure("invalid_arguments", "customFolders must be an array of absolute paths");
            folders.clear();
            for (const auto &item : *args["customFolders"].getArray())
                folders.add(item.toString());
        }
        juce::String operation;
        const auto started = plugins.scan(
            std::move(folders), bool(args.getProperty("retryBlacklisted", false)),
            args.hasProperty("standardLocations") ? std::optional<bool>(bool(args["standardLocations"]))
                                                  : std::nullopt,
            &operation);
        if (started.failed())
            throw McpFailure("scan_refused", started.getErrorMessage());
        set(result, "operation", operation);
        set(result, "completionState", "pending");
        set(result, "documentChanged", false);
        return textResult(result);
    }
    if (action == "job" || action == "cancel")
    {
        const auto id = args["operation"].toString();
        if (!plugins.operation(id, snapshot))
            throw McpFailure("unknown_operation", "Unknown/expired VST3 scan operation");
        if (action == "cancel" && snapshot.completionState == "pending")
        {
            const auto cancelled = plugins.cancel(id);
            if (cancelled.failed())
                throw McpFailure("unsupported_cancel", cancelled.getErrorMessage());
            plugins.operation(id, snapshot);
        }
        set(result, "completionState", snapshot.completionState);
    }
    set(result, "operation", snapshot.operation);
    set(result, "scanState", snapshot.completionState);
    set(result, "scanning", snapshot.scanning);
    set(result, "cancelRequested", snapshot.cancelRequested);
    set(result, "persisted", snapshot.persisted);
    set(result, "progress", snapshot.progress);
    set(result, "completed", snapshot.completed);
    set(result, "total", snapshot.total);
    set(result, "pluginCount", int(snapshot.plugins.size()));
    set(result, "blacklistCount", snapshot.blacklist.size());
    set(result, "failureCount", snapshot.failures.size());
    set(result, "settingsPath", plugins.settingsFile().getFullPathName());
    set(result, "disposableSettings", bool(pluginTestSettings));
    set(result, "standardLocations", snapshot.standardLocations);
    set(result, "standardFolders", strings(PluginCatalog::standardFolders()));
    set(result, "customFolders", strings(snapshot.folders));
    set(result, "requestedFolders", strings(snapshot.requestedFolders));
    set(result, "requestedStandardLocations", snapshot.requestedStandardLocations);
    set(result, "documentChanged", false);
    if (snapshot.completionState == "failed")
    {
        auto error = obj();
        set(error, "code", snapshot.errorCode);
        set(error, "message", snapshot.error);
        set(error, "effectMayHaveOccurred", snapshot.errorCode == "persistence_failed");
        set(error, "retryable", false);
        set(result, action == "job" || action == "cancel" ? "error" : "scanError", error);
    }
    if (action == "list" || action == "descriptor")
    {
        const auto section = args.getProperty("section", "plugins").toString();
        const auto kind = args.getProperty("kind", "all").toString();
        juce::Array<juce::var> items, page;
        int matching = 0;
        if (section == "plugins" || action == "descriptor")
            for (const auto &type : snapshot.plugins)
            {
                const auto id = type.createIdentifierString();
                if (action == "descriptor")
                {
                    if (id != args["id"].toString())
                        continue;
                    ++matching;
                    set(result, "id", id);
                    set(result, "descriptor", type.createXml()->toString());
                    set(result, "instrument", type.isInstrument);
                }
                else if (kind == "all" || (kind == "instruments" && type.isInstrument) ||
                         (kind == "effects" && !type.isInstrument))
                {
                    auto item = obj();
                    set(item, "id", id);
                    set(item, "name", type.name);
                    set(item, "manufacturer", type.manufacturerName);
                    set(item, "version", type.version);
                    set(item, "format", type.pluginFormatName);
                    set(item, "path", type.fileOrIdentifier);
                    set(item, "instrument", type.isInstrument);
                    set(item, "inputChannels", type.numInputChannels);
                    set(item, "outputChannels", type.numOutputChannels);
                    items.add(item);
                }
            }
        else
            for (const auto &item : section == "blacklist" ? snapshot.blacklist : snapshot.failures)
                items.add(item);
        if (action == "descriptor")
        {
            if (matching != 1)
                throw McpFailure(matching ? "ambiguous_target" : "unavailable_target",
                                 "Rediscover a unique current catalogue class ID");
        }
        else
        {
            const auto offset = integer(args, "offset", 0, 0, 2147483647);
            const auto limit = integer(args, "limit", 64, 1, 128);
            for (int i = offset; i < items.size() && page.size() < limit; ++i)
                page.add(items[i]);
            set(result, "items", page);
            set(result, "itemCount", items.size());
            set(result, "nextOffset",
                offset + page.size() < items.size() ? juce::var(offset + page.size()) : juce::var{});
        }
    }
    return textResult(result);
}
juce::var Studio::mcpPluginOperation(const juce::var &args, juce::String *admittedOperation,
                                     const PluginGesture *lastTweakWitness)
{
    const auto action = args["action"].toString();
    if (action != "audioIo" && args.hasProperty("pluginIo"))
        throw McpFailure("invalid_arguments", "pluginIo applies only to audioIo");
    for (const auto *field : {"chainAction", "path", "directory", "overwrite", "detector"})
        if (action != "chain" && args.hasProperty(field))
            throw McpFailure("invalid_arguments", "Containing-chain fields apply only to chain");
    if (action != "wrapper" && args.hasProperty("view"))
        throw McpFailure("invalid_arguments", "view applies only to wrapper navigation");
    auto result = obj();
    const auto stamp = [&]
    {
        set(result, "projectIdentity", controller.projectIdentity);
        set(result, "revision", juce::int64(controller.revision));
    };
    stamp();
    if (action == "job" || action == "cancel")
    {
        const auto found = std::find_if(pluginOperations.begin(), pluginOperations.end(), [&](const auto &job)
                                        { return job.id == args["operation"].toString(); });
        if (found == pluginOperations.end())
            throw McpFailure("unknown_operation",
                             "Unknown/expired plugin operation; inspect actual runtime state");
        auto &job = *found;
        if (action == "cancel" && job.state == "pending")
        {
            if (job.kind == "chain" && job.childOperation.isEmpty())
            {
                job.cancelRequested = true;
                if (job.choosing)
                    cancelPluginChainUi(job.id);
                pollPluginOperations(); // Native close/choice ownership must actually drain first.
            }
            else if (job.kind == "audioIo" || job.kind == "chain")
            {
                auto cancel = obj();
                set(cancel, "action", "cancel");
                set(cancel, "operation", job.childOperation);
                const auto result = mixerChainOperation(cancel);
                if (result["completionState"].toString() == "failed")
                    throw McpFailure(result["code"].toString(), result["message"].toString());
                pollPluginOperations(); // Cancellation drains/restores before the parent becomes terminal.
            }
            else if (job.kind == "set")
            {
                const auto cancelled = cancelNativeScalar(job.id);
                if (cancelled.failed()) throw McpFailure("too_late", cancelled.getErrorMessage());
            }
            else if (job.kind == "editor")
            {
                const auto cancelled = job.host->cancelEditorRequest(job.instance, job.serial);
                if (cancelled == PluginEditorCancellation::tooLate)
                    throw McpFailure("too_late", "Native editor work began; poll its actual outcome");
                if (cancelled == PluginEditorCancellation::unknownRequest ||
                    cancelled == PluginEditorCancellation::instanceChanged)
                    throw McpFailure("stale_operation", "Native editor request ownership changed");
                pollPluginOperations();
            }
            else
            {
                if (job.kind != "refresh" && job.kind != "automate")
                    throw McpFailure("unsupported_cancel",
                                     "This action may already have effects; observe its actual completion");
                job.state = "cancelled";
                job.message =
                    job.kind == "automate"
                        ? "Automation preparation cancelled before document mutation"
                        : "Refresh observation cancelled; the read-only worker refresh may still finish";
                job.completedRevision = controller.revision;
                if (job.lastTweakWitness && job.project == controller.projectIdentity)
                    status = job.message;
                job.host.reset();
            }
        }
        set(result, "operation", job.id);
        set(result, "kind", job.kind);
        set(result, "plugin", juce::int64(job.plugin));
        set(result, "requestedInstance", job.instance);
        set(result, "sourceProjectIdentity", job.project);
        set(result, "completionState", job.state);
        set(result, "message", job.message);
        set(result, "catalogueSerial", juce::int64(job.completedSerial));
        set(result, "generation", juce::int64(job.completedGeneration));
        set(result, "revisionAtCompletion", juce::int64(job.completedRevision));
        if (job.kind == "audioIo" || job.kind == "chain")
        {
            if (job.kind == "audioIo")
            {
                auto requested = obj();
                set(requested, "inputBus", job.io.inputBus);
                set(requested, "outputBus", job.io.outputBus);
                set(requested, "inputChannels", job.io.inputChannels);
                set(requested, "monoSource", job.io.monoSource);
                set(result, "requestedIo", requested);
            }
            else
            {
                set(result, "chainAction", job.action);
                set(result, "phase", job.phase);
                set(result, "mixer", job.mixer);
                set(result, "slot", job.slot);
                set(result, "path", job.chainOptions["path"].toString());
                set(result, "configuredDirectory", mixerChainReadback()["state"]["directory"]);
                set(result, "nativeDispatched", job.nativeDispatched);
                juce::Array<juce::var> closures;
                for (const auto &close : job.closes)
                    if (close.serial)
                    {
                        auto value = obj();
                        set(value, "plugin", juce::int64(close.plugin));
                        set(value, "instance", close.instance);
                        set(value, "serial", juce::int64(close.serial));
                        set(value, "completionState",
                            !close.result                                             ? "pending"
                            : close.result->succeeded()                               ? "completed"
                            : close.result->outcome == PluginEditorOutcome::cancelled ? "cancelled"
                                                                                      : "failed");
                        if (close.result)
                        {
                            set(value, "outcome", pluginEditorOutcomeName(close.result->outcome));
                            set(value, "dispatched", close.result->dispatched);
                            set(value, "nativeCompletionKnown", close.result->completionKnown);
                            set(value, "windowExists", close.result->window.exists);
                        }
                        closures.add(value);
                    }
                set(result, "editorClosures", closures);
            }
            set(result, "canonicalOperation", job.childOperation);
            const auto child =
                job.childOperation.isNotEmpty() ? mixerChainReadback(job.childOperation) : obj();
            set(result, "structuralOperation", child);
            set(result, "historyCommitted", job.historyCommitted);
            set(result, "documentChanged", job.historyCommitted);
            set(result, "undoable", bool(child["undoable"]));
        }
        if (job.kind == "automate")
        {
            set(result, "parameter", juce::int64(job.parameter));
            set(result, "requestedGeneration", juce::int64(job.generation));
            set(result, "automationAction", job.action);
            set(result, "historyCommitted", job.historyCommitted);
            set(result, "documentChanged", job.historyCommitted);
            set(result, "undoable", job.historyCommitted);
            if (job.lastTweakWitness)
                set(result, "sourceGestureSerial", juce::int64(job.lastTweakWitness->serial));
        }
        if (job.kind == "set")
        {
            set(result, "parameter", juce::int64(job.parameter));
            set(result, "nativeApplied", job.nativeApplied);
            set(result, "nativeDispatched", job.nativeDispatched);
            set(result, "historyCommitted", job.historyCommitted);
            set(result, "undoable", job.historyCommitted);
            set(result, "documentChanged", job.nativeApplied || job.historyCommitted);
            set(result, "beforeKnown", job.scalarBeforeKnown);
            set(result, "before", job.scalarBeforeKnown ? juce::var(job.scalarBefore) : juce::var{});
            set(result, "after", job.nativeApplied ? juce::var(job.scalarAfter) : juce::var{});
            const bool recordingWrite = job.action == "manualRecording";
            set(result, "completionScope",
                recordingWrite ? "nativeScalarAckDuringRecording" : "nativeScalarAckAndCanonicalHistory");
            set(result, "recordingWriteOnly", recordingWrite);
            set(result, "historyUnavailableReason", recordingWrite ? "recordingCaptureProtected" : "");
            juce::Array<juce::var> edits;
            for (const auto &e : job.scalarEdits)
            {
                auto row = obj();
                set(row, "parameter", juce::int64(e.parameter));
                set(row, "before", e.before);
                set(row, "after", e.after);
                edits.add(row);
            }
            set(result, "edits", edits);
        }
        if (job.kind == "editor")
        {
            set(result, "editorAction", job.action);
            set(result, "editorState", pluginEditorState(job.plugin));
            set(result, "documentChanged", false);
            set(result, "undoable", false);
            if (job.editorResult)
            {
                const auto &editor = *job.editorResult;
                set(result, "outcome", pluginEditorOutcomeName(editor.outcome));
                set(result, "dispatched", editor.dispatched);
                set(result, "nativeCompletionKnown", editor.completionKnown);
                set(result, "nativeOutcome",
                    editor.completionKnown ? juce::var(pluginEditorOutcomeName(editor.nativeOutcome))
                                           : juce::var{});
                set(result, "elapsedMs",
                    editor.completionKnown ? juce::var(juce::int64(editor.completedAt - editor.admittedAt))
                                           : juce::var{});
                auto observed = obj();
                set(observed, "known", editor.window.known);
                set(observed, "exists", editor.window.exists);
                set(observed, "visible", editor.window.visible);
                set(observed, "minimised", editor.window.minimised);
                set(observed, "revision", juce::int64(editor.window.revision));
                set(result, "windowAtCompletion", observed);
            }
        }
        if (job.state == "failed")
        {
            auto error = obj();
            set(error, "code", job.code);
            set(error, "message", job.message);
            set(error, "effectMayHaveOccurred",
                job.kind == "recover" || job.nativeDispatched || job.nativeApplied || job.historyCommitted ||
                    ((job.kind == "audioIo" || job.kind == "chain") && job.childOperation.isNotEmpty()) ||
                    (job.editorResult && job.editorResult->dispatched));
            set(error, "retryable", false);
            set(result, "error", error);
        }
        return textResult(result);
    }
    const int offset = integer(args, "offset", 0, 0, 2147483647);
    const int limit = integer(args, "limit", 64, 1, 512);
    if (action == "list")
    {
        juce::Array<juce::var> all, page;
        const auto add = [&](const PluginSpec &plugin, Id channel, int mixer, int slot)
        {
            auto row = obj();
            const auto host = audio.preparedPluginHost(controller.project, plugin.id);
            set(row, "id", juce::int64(plugin.id));
            set(row, "name", plugin.name);
            set(row, "instrument", plugin.instrument);
            set(row, "bypassed", plugin.bypass);
            set(row, "wet", plugin.wet);
            set(row, "mainInputBus", plugin.mainInputBus);
            set(row, "mainOutputBus", plugin.mainOutputBus);
            set(row, "mainInputChannels", plugin.mainInputChannels);
            set(row, "mainInputMonoSource", plugin.mainInputMonoSource);
            {
                juce::Array<juce::var> routes;
                for (int target : plugin.extraOutputMixers)
                    routes.add(target);
                set(row, "extraOutputMixers", routes);
                set(row, "extraOutputBusesAvailable", host ? juce::var(host->extraOutputBuses()) : juce::var{});
            }
            if (!plugin.instrument)
                set(row, "detector", pluginDetectorState(controller.project, plugin.id));
            const auto *chain = channel == 0 ? mixerInsert(controller.project, mixer) : nullptr;
            set(row, "chainBypassed", chain && chain->effectsBypassed);
            set(row, "effectiveBypass", plugin.bypass || (chain && chain->effectsBypassed));
            set(row, "channel", juce::int64(channel));
            set(row, "mixer", mixer);
            set(row, "slot", slot);
            set(row, "available", host && host->alive());
            set(row, "instance", host ? host->runtimeIdentity() : juce::String{});
            set(row, "status", audio.pluginStatus(plugin.id));
            set(row, "nativeEditor", pluginEditorState(plugin.id));
            const auto peaks = host ? host->nativeOutputMeter() : PluginNativeMeterReading{};
            const bool meterReady = peaks.status == PluginNativeMeterStatus::ready;
            auto meter = obj();
            set(meter, "status", pluginNativeMeterStatusName(peaks.status));
            set(meter, "available", meterReady);
            set(meter, "leftPeak", meterReady ? juce::var(peaks.leftPeak) : juce::var{});
            set(meter, "rightPeak", meterReady ? juce::var(peaks.rightPeak) : juce::var{});
            set(meter, "units", "linear sample peak");
            set(meter, "scope",
                "Latest retained native processor block, before Host wet/dry/bypass; not physical output or "
                "packet-coherent freshness");
            set(row, "nativeOutputMeter", meter);
            if(peaks.synthState) set(row,"synthesizer",synth::synthTelemetryJson(peaks.synthState));
            if (host)
            {
                const auto startup = host->startupDiagnostics();
                auto timings = obj();
                set(timings, "throughLaunchMs", startup.launchMs);
                set(timings, "workerReadyWaitMs", startup.readyMs);
                set(timings, "initialCatalogueMs", startup.catalogueMs);
                set(timings, "totalMs", startup.totalMs);
                set(row, "startup", timings);
            }
            set(row, "latencySamples", host ? host->latencySamples() : 0);
            all.add(row);
        };
        for (const auto &channel : controller.project.channels)
            if (channel.instrument)
                add(*channel.instrument, channel.id, channel.mixer, -1);
        for (const auto &insert : controller.project.mixer)
            for (size_t i = 0; i < insert.effects.size(); ++i)
                add(insert.effects[i], 0, insert.index, int(i));
        for (int i = offset; i < all.size() && page.size() < limit; ++i)
            page.add(all[i]);
        set(result, "items", page);
        set(result, "total", all.size());
        set(result, "nextOffset",
            offset + page.size() < all.size() ? juce::var(offset + page.size()) : juce::var{});
        set(result, "nativeWidgetControl", false);
        set(result, "manualParameterUndo", false);
        set(result, "scalarParameterUndo", true);
        set(result, "manualHistoryScope", "supported scalar only; arbitrary native UI and hidden/program state excluded");
        return textResult(result);
    }
    const auto id = identity(args, "plugin", 0);
    if (!controller.project.plugin(id))
        throw McpFailure("unavailable_target", "Unknown project plugin ID");
    const auto host = audio.preparedPluginHost(controller.project, id);
    const auto instance = host ? host->runtimeIdentity() : juce::String{};
    if (args.hasProperty("instance") && args["instance"].toString() != instance)
        throw McpFailure("stale_instance", "Plugin runtime changed; rediscover this target");
    if (action != "parameters" && !args.hasProperty("instance"))
        throw McpFailure("invalid_arguments", "Provide the exact instance from plugin list");
    if (args.hasProperty("revision") && number(args, "revision", -1) != double(controller.revision))
        throw McpFailure("stale_revision", "Project revision changed before plugin action");
    set(result, "plugin", juce::int64(id));
    set(result, "instance", instance);
    if (action == "buses")
        return textResult(pluginIoState(id));
    if (action == "audioIo")
    {
        if (!args.hasProperty("pluginIo"))
            throw McpFailure("invalid_arguments", "audioIo requires a complete typed pluginIo selection");
        const auto selection = args["pluginIo"];
        validateMcpSchema(commandArgumentSchema("plugin.io")["properties"]["pluginIo"], selection,
                          "pluginIo");
        const PluginMainIo io{
            integer(selection, "inputBus", -1, 0, 31), integer(selection, "outputBus", -1, 0, 31),
            integer(selection, "inputChannels", -1, 1, 2), integer(selection, "monoSource", -1, 0, 3)};
        juce::String operation, code;
        const auto admitted = requestPluginAudioIo(id, instance, io, operation, code);
        if (admitted.failed())
            throw McpFailure(code, admitted.getErrorMessage());
        if (admittedOperation)
            *admittedOperation = operation;
        set(result, "operation", operation);
        set(result, "kind", "audioIo");
        set(result, "completionState", "pending");
        return textResult(result);
    }
    if (action == "chain")
    {
        const auto task = args["chainAction"].toString();
        if (task.isEmpty())
            throw McpFailure("invalid_arguments", "chainAction is required");
        for (const auto *field : {"path", "directory", "overwrite", "detector"})
        {
            const bool allowed = juce::String(field) == "path"        ? task == "save" || task == "loadApply"
                                 : juce::String(field) == "directory" ? task == "configureDirectory"
                                 : juce::String(field) == "overwrite" ? task == "save"
                                                                      : task == "sidechain";
            if (args.hasProperty(field) && !allowed)
                throw McpFailure("invalid_arguments", "Field is not valid for this containing-chain action");
        }
        if ((task == "save" || task == "loadApply") && !args.hasProperty("path"))
            throw McpFailure("invalid_arguments", "Exact absolute chain path is required");
        if (task == "configureDirectory" && !args.hasProperty("directory"))
            throw McpFailure("invalid_arguments", "Explicit existing local directory is required");
        if (task == "sidechain")
        {
            if (!args.hasProperty("detector"))
                throw McpFailure("invalid_arguments", "Explicit detector object/null is required");
            validateMcpSchema(commandArgumentSchema("plugin.effect.sidechain")["properties"]["detector"],
                              args["detector"], "detector");
            std::optional<PluginDetectorAssignment> assignment;
            if (args["detector"].isObject())
                assignment =
                    PluginDetectorAssignment{integer(args["detector"], "sourceMixer", -1, 1, 2147483647),
                                             integer(args["detector"], "inputBus", -1, 1, 31),
                                             integer(args["detector"], "channels", -1, 1, 2)};
            auto candidate = controller.project;
            if (auto valid = setPluginDetector(candidate, id, assignment); valid.failed())
                throw McpFailure("sidechain.invalidArguments", valid.getErrorMessage());
        }
        juce::String operation, code;
        const auto admitted = requestPluginChain(id, instance, task, args, false, operation, code);
        if (admitted.failed())
            throw McpFailure(code, admitted.getErrorMessage());
        if (admittedOperation)
            *admittedOperation = operation;
        set(result, "operation", operation);
        set(result, "kind", "chain");
        set(result, "completionState", "pending");
        return textResult(result);
    }
    if (action == "set")
    {
        if (!args.hasProperty("parameter") || !args.hasProperty("generation") || !args.hasProperty("value"))
            throw McpFailure("invalid_arguments", "set requires exact native parameter/generation and normalized value");
        juce::String operation = args["operation"].toString();
        if (operation.isNotEmpty() && (!nativeScalar || nativeScalar->operation != operation))
            throw McpFailure("stale_operation", "Gesture operation is no longer pending; inspect its actual outcome");
        const auto parameter = number(args, "parameter", -1), generation = number(args, "generation", 0);
        const auto value = number(args, "value", -1);
        if (!std::isfinite(parameter) || parameter < 0 || parameter > 4294967295. ||
            std::floor(parameter) != parameter || !std::isfinite(generation) || generation < 1 ||
            generation > 4294967295. || std::floor(generation) != generation || !std::isfinite(value) ||
            value < 0 || value > 1)
            throw McpFailure("invalid_arguments",
                             "Exact uint32 parameter/generation and finite normalized value required");
        const auto admitted =
            requestNativeScalar(id, std::uint32_t(parameter), unsigned(generation), float(value), instance,
                                bool(args.getProperty("gestureOpen", false)), operation);
        if (admitted.failed())
            throw McpFailure("parameterHistory.refused", admitted.getErrorMessage());
        const auto job = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                      [&](const auto &item) { return item.id == operation; });
        set(result, "operation", operation);
        set(result, "kind", "set");
        set(result, "completionState", "pending");
        // These are aggregate witnesses for this exact operation. An earlier
        // value may already be ACKed while its latest desired value is queued.
        set(result, "nativeApplied", job != pluginOperations.end() && job->nativeApplied);
        set(result, "nativeDispatched", job != pluginOperations.end() && job->nativeDispatched);
        set(result, "historyCommitted", job != pluginOperations.end() && job->historyCommitted);
        set(result, "nativeStatusScope",
            "Operation aggregate; not acknowledgement of this latest desired value");
        return textResult(result);
    }
    if (action == "editorState")
    {
        set(result, "nativeEditor", pluginEditorState(id));
        return textResult(result);
    }
    if (action == "openEditor" || action == "closeEditor" || action == "wrapper")
    {
        const auto name = args.getProperty("view", "plugin").toString();
        if (name != "plugin" && name != "settings" && name != "parameters")
            throw McpFailure("invalid_arguments", "Wrapper view must be plugin, settings or parameters");
        const auto view = name == "settings"     ? PluginWrapperView::settings
                          : name == "parameters" ? PluginWrapperView::parameters
                                                 : PluginWrapperView::plugin;
        if (action != "wrapper" &&
            (args.hasProperty("view") || args.hasProperty("parameter") || args.hasProperty("generation")))
            throw McpFailure("invalid_arguments", "Use wrapper to choose a view or parameter");
        std::uint64_t selection = 0;
        if (args.hasProperty("parameter") || args.hasProperty("generation"))
        {
            if (view != PluginWrapperView::parameters || !args.hasProperty("parameter") ||
                !args.hasProperty("generation"))
                throw McpFailure(
                    "invalid_arguments",
                    "Parameter selection needs Parameters view and exact parameter/generation pair");
            const auto parameter = number(args, "parameter", -1), generation = number(args, "generation", 0);
            if (!std::isfinite(parameter) || parameter < 0 || parameter > 4294967295. ||
                std::floor(parameter) != parameter || !std::isfinite(generation) || generation < 1 ||
                generation > 4294967295. || std::floor(generation) != generation)
                throw McpFailure("invalid_arguments",
                                 "Exact uint32 parameter/generation required for wrapper selection");
            selection = (std::uint64_t(generation) << 32) | std::uint32_t(parameter);
        }
        juce::String operation, code;
        const auto admitted =
            requestPluginEditor(id, action != "closeEditor", instance, operation, code, view, selection);
        if (admitted.failed())
            throw McpFailure(code, admitted.getErrorMessage());
        set(result, "operation", operation);
        set(result, "kind", "editor");
        set(result, "completionState", "pending");
        set(result, "documentChanged", false);
        set(result, "undoable", false);
        return textResult(result);
    }
    if (action == "recover")
    {
        // Recovery also supports an unavailable/missing-runtime placeholder. Its
        // exact empty instance is still guarded by the current project identity.
    }
    else if (action != "closeControls" && (!host || !host->alive()))
        throw McpFailure("unavailable_target", "Plugin worker is unavailable; inspect/recover saved state");
    if (action == "parameters")
    {
        PluginParameterCatalogue catalogue, initial;
        if (!readPluginCatalogue(id, instance, catalogue) || catalogue.error)
            throw McpFailure(
                "catalogue_unavailable",
                "Complete native metadata is not available; request refresh and inspect its job");
        const bool hasInitial = readPluginCatalogue(id, instance, initial);
        juce::Array<juce::var> page;
        for (int i = offset; i < int(catalogue.parameters.size()) && page.size() < limit; ++i)
        {
            const auto &parameter = catalogue.parameters[size_t(i)];
            auto row = obj();
            set(row, "id", juce::int64(parameter.id));
            set(row, "name", parameter.name);
            set(row, "label", parameter.label);
            set(row, "unit", parameter.unit);
            set(row, "steps", parameter.steps);
            set(row, "flags", juce::int64(parameter.flags));
            set(row, "writable", parameter.writable());
            set(row, "automatable", parameter.automatable());
            const PluginBinding declaration{parameter.id,    parameter.flags, parameter.steps,
                                            parameter.value, parameter.name,  parameter.label};
            set(row, "automationSupported",
                parameter.automatable() && validPluginBindings({&declaration, 1}));
            set(row, "hidden", (parameter.flags & (1u << 4)) != 0);
            set(row, "normalized", (parameter.availability & 1u) ? juce::var(parameter.value) : juce::var{});
            set(row, "defaultNormalized",
                parameter.hasDefault() ? juce::var(parameter.defaultValue) : juce::var{});
            auto original = hasInitial ? std::find_if(initial.parameters.begin(), initial.parameters.end(),
                                                      [&](const auto &p) { return p.id == parameter.id; })
                                       : initial.parameters.end();
            set(row, "initialNormalized",
                original != initial.parameters.end() && (original->availability & 1u)
                    ? juce::var(original->value)
                    : juce::var{});
            page.add(row);
        }
        set(result, "items", page);
        set(result, "total", int(catalogue.parameters.size()));
        set(result, "generation", juce::int64(catalogue.generation));
        set(result, "catalogueSerial", juce::int64(catalogue.serial));
        set(result, "nextOffset",
            offset + page.size() < int(catalogue.parameters.size()) ? juce::var(offset + page.size())
                                                                    : juce::var{});
        set(result, "notifications", host->parameterNotificationsEnabled());
        set(result, "initialSnapshotAvailable", hasInitial);
        set(result, "valueAuthority",
            "Native controller catalogue snapshot; not instantaneous DSP or canonical manual fallback");
        set(result, "initialSnapshotAuthority",
            "Post-restore/pre-DSP; not a current manual fallback or complete Undo baseline");
        set(result, "manualWriteSupported", true);
        set(result, "manualHistoryScope", "supported scalar only; fresh state capture; program/hidden state excluded");
        return textResult(result);
    }
    if (action == "notifications")
    {
        if (!args.hasProperty("enabled"))
            throw McpFailure("invalid_arguments", "notifications requires enabled");
        const auto applied = setPluginNotifications(id, instance, bool(args["enabled"]));
        if (applied.failed())
            throw McpFailure("configuration_failed", applied.getErrorMessage());
        set(result, "enabled", host->parameterNotificationsEnabled());
        set(result, "documentChanged", false);
        return textResult(result);
    }
    if (action == "closeControls")
    {
        if (auto closed = closePluginParameters(id, instance); closed.failed())
            throw McpFailure("busy", closed.getErrorMessage());
        const auto after = pluginControlSnapshot();
        set(result, "closed", juce::int64(after["plugin"]) != id || !bool(after["visible"]));
        set(result, "completionState", "completed");
        return textResult(result);
    }
    if (action == "controls")
    {
        const auto selected = selectPluginParameter(
            id,
            args.hasProperty("parameter")
                ? std::optional<std::uint32_t>(std::uint32_t(number(args, "parameter", 0)))
                : std::nullopt,
            false);
        if (selected.failed())
            throw McpFailure("unavailable_control", selected.getErrorMessage());
        const auto state = pluginControlSnapshot();
        if (!bool(state["visible"]))
            throw McpFailure("unavailable_control", "Shared Controls window is not visible");
        set(result, "controls", state);
        return textResult(result);
    }
    if (action != "recover" && action != "refresh" && action != "automate")
        throw McpFailure("unsupported_operation", "Unknown plugin operation");
    if (pluginOperations.size() >= 64)
    {
        const auto terminal = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                           [](const auto &job) { return job.state != "pending"; });
        if (terminal == pluginOperations.end())
            throw McpFailure("capacity_exceeded", "All plugin operation slots are in use");
        pluginOperations.erase(terminal);
    }
    if (std::any_of(pluginOperations.begin(), pluginOperations.end(),
                    [&](const auto &job) { return job.state == "pending" && job.plugin == id; }))
        throw McpFailure("busy", "A plugin operation for this target is already pending");
    // Prepare ownership storage before a physical catalogue/recovery request.
    // Internal wrapper callers retain the admitted token even if response
    // construction subsequently throws; accepted work must still be observed.
    pluginOperations.reserve(pluginOperations.size() + 1);
    PluginOperation job;
    job.id = juce::Uuid().toString();
    job.plugin = id;
    job.project = controller.projectIdentity;
    job.instance = instance;
    job.started = juce::Time::getMillisecondCounter();
    job.kind = action;
    if (args.hasProperty("revision"))
        job.revision = controller.revision;
    if (action == "recover")
    {
        const auto admitted = recoverPlugin(id);
        if (admitted.failed())
            throw McpFailure("recovery_refused", admitted.getErrorMessage());
    }
    else
    {
        if (action == "automate")
        {
            if (!args.hasProperty("generation") || !args.hasProperty("parameter") ||
                !args.hasProperty("automation"))
                throw McpFailure("invalid_arguments",
                                 "automate requires generation, parameter and automation action");
            PluginParameterCatalogue catalogue;
            if (!readPluginCatalogue(id, instance, catalogue) || catalogue.error ||
                catalogue.generation != unsigned(number(args, "generation", 0)))
                throw McpFailure("stale_catalogue",
                                 "Read current parameter capabilities before preparing automation");
            job.generation = catalogue.generation;
            job.parameter = std::uint32_t(number(args, "parameter", 0));
            if (lastTweakWitness)
                job.lastTweakWitness = *lastTweakWitness;
            const auto parameter = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                                [&](const auto &p) { return p.id == job.parameter; });
            if (parameter == catalogue.parameters.end() || !parameter->automatable())
                throw McpFailure("unavailable_parameter", "Native parameter is not available for automation");
            const auto requested = args["automation"].toString();
            job.action = requested == "create" ? "automation.plugin.create"
                         : requested == "init" ? "automation.plugin.init"
                                               : "automation.plugin.init.remove";
        }
        job.host = host;
        job.serial = refreshPluginCatalogue(id, instance);
        if (!job.serial)
            throw McpFailure(host->editorBusy() ? "busy" : "unavailable_target",
                             "Parameter refresh request was not admitted");
    }
    pluginOperations.push_back(std::move(job));
    if (admittedOperation)
        *admittedOperation = pluginOperations.back().id;
    set(result, "operation", pluginOperations.back().id);
    set(result, "completionState", "pending");
    set(result, "kind", action);
    return textResult(result);
}

juce::var Studio::mcpProjectOperation(const juce::var &args)
{
    const auto action = args["action"].toString();
    if (action == "envelopePresets" || action == "saveEnvelopePreset" || action == "loadEnvelopePreset")
    {
        if (action != "envelopePresets")
        {
            const Id channel = identity(args, "channel", 0);
            const auto name = args["name"].toString();
            const auto r = action == "saveEnvelopePreset" ? saveEnvelopePreset(channel, name) : loadEnvelopePreset(channel, name);
            if (r.failed())
                throw McpFailure(r.getErrorMessage().upToFirstOccurrenceOf(":", false, false), r.getErrorMessage());
        }
        auto result = obj();
        juce::Array<juce::var> names;
        for (const auto &n : envelopePresetNames())
            names.add(n);
        set(result, "envelopePresets", names);
        return result;
    }
    if (action == "audioLogger" || action == "audioLoggerSave")
    {
        // Same calls as Tools > Audio logger. Local preference; the save is a plain file write.
        if (action == "audioLogger")
        {
            const double minutes = args.hasProperty("minutes") ? double(args["minutes"]) : 2.;
            if (auto r = setAudioLogger(bool(args.getProperty("enabled", true)), minutes); r.failed())
                throw McpFailure(r.getErrorMessage().upToFirstOccurrenceOf(":", false, false), r.getErrorMessage());
        }
        else
        {
            const auto path = args["path"].toString();
            if (!juce::File::isAbsolutePath(path) || !path.endsWithIgnoreCase(".wav"))
                throw McpFailure("invalid_arguments", "audioLoggerSave needs an absolute .wav path");
            const juce::File file(path);
            if (file.exists() && !bool(args["overwrite"]))
                throw McpFailure("exists", "File exists; pass overwrite:true");
            double saved = 0;
            if (auto r = AudioLogger::instance().save(file, double(args.getProperty("seconds", 0.)), saved); r.failed())
                throw McpFailure(r.getErrorMessage().upToFirstOccurrenceOf(":", false, false), r.getErrorMessage());
            auto result = audioLoggerState();
            result.getDynamicObject()->setProperty("savedSeconds", saved);
            result.getDynamicObject()->setProperty("path", file.getFullPathName());
            return result;
        }
        return audioLoggerState();
    }
    auto describe = [&](const McpOperation &operation)
    {
        auto data = obj();
        set(data, "operation", operation.id);
        set(data, "kind", operation.kind);
        set(data, "completionState", operation.state);
        set(data, "message", operation.message);
        set(data, "path", operation.path);
        set(data, "projectIdentity", operation.project);
        set(data, "sourceRevision", juce::int64(operation.revision));
        if (operation.kind == "exportMidi")
        {
            set(data, "noteExpressionEncoding",
                "Pan/Fine/Mod X/Y and Porta omitted from MIDI1 score; silent Slide skipped; color "
                "encoded as MIDI channel; Shift/Repeat encoded in event ticks; use project/WAV to "
                "preserve all");
            set(data, "midiExpressionNotesOmitted",
                operation.state == "completed" && operation.midiExpressionNotesOmitted >= 0
                    ? juce::var(operation.midiExpressionNotesOmitted)
                    : juce::var{});
            set(data, "midiSlideNotesOmitted",
                operation.state == "completed" && operation.midiSlideNotesOmitted >= 0
                    ? juce::var(operation.midiSlideNotesOmitted)
                    : juce::var{});
            set(data, "midiPortamentoNotesOmitted",
                operation.state == "completed" && operation.midiPortamentoNotesOmitted >= 0
                    ? juce::var(operation.midiPortamentoNotesOmitted)
                    : juce::var{});
            set(data, "expressionNotesOmitted",
                operation.state == "completed" && operation.expressionNotesOmitted >= 0
                    ? juce::var(operation.expressionNotesOmitted)
                    : juce::var{});
        }
        if (operation.kind == "new" || operation.kind == "open")
            set(data, "documentChanged", operation.documentChanged);
        if (operation.state == "failed")
        {
            auto detail = obj();
            set(detail, "code", "operation_failed");
            set(detail, "message", operation.message);
            if (operation.kind == "new" || operation.kind == "open")
            {
                set(detail, "effectMayHaveOccurred", operation.documentChanged);
                set(detail, "effectScope", "document_replacement");
            }
            set(data, "error", detail);
        }
        if (operation.path.isNotEmpty())
        {
            const juce::File file(operation.path);
            set(data, "fileExists", file.existsAsFile());
            set(data, "fileBytes", file.getSize());
        }
        return data;
    };
    if (action == "state")
    {
        juce::String transitionCode;
        const auto transition = refreshProjectTransitionState(transitionCode);
        auto data = obj();
        set(data, "projectIdentity", controller.projectIdentity);
        set(data, "revision", juce::int64(controller.revision));
        set(data, "dirty", controller.dirty);
        set(data, "path", projectFile.getFullPathName());
        set(data, "busy",
            transition.failed() || devicePreparationBusy() || afterPluginState != nullptr || pendingExport.has_value() || audio.rendering() || midiFileBusy ||
                noteSlicePending() || noteStretchPending() || midiTaking || hasPendingMidiTake());
        set(data, "busyReason", transition.failed() ? transition.getErrorMessage() : juce::String{});
        juce::Array<juce::var> operations;
        for (const auto &operation : mcpOperations)
            operations.add(describe(operation));
        set(data, "operations", operations);
        return data;
    }
    if (action == "job")
    {
        for (const auto &operation : mcpOperations)
            if (operation.id == args["operation"].toString())
                return describe(operation);
        throw McpFailure("unknown_operation",
                         "Job does not belong to this application session, or has expired");
    }
    if (action == "savePreset" || action == "loadPreset")
    {
        const juce::File presetFile(args["path"].toString());
        const auto channel = Id(juce::int64(args["channel"]));
        if (!juce::File::isAbsolutePath(args["path"].toString()) || !presetFile.hasFileExtension("vibch"))
            throw McpFailure("invalid_arguments", "An absolute .vibch preset path is required");
        if (!controller.project.channel(channel))
            throw McpFailure("invalid_target", "channel must be an existing Channel ID");
        if (afterPluginState || pendingExport || audio.rendering())
            throw McpFailure("busy", "Finish the active save/export operation first");
        if (action == "savePreset" && presetFile.exists() && !bool(args["overwrite"]))
            throw McpFailure("output_exists", "Output already exists; overwrite=true is required");
        while (mcpOperations.size() >= 64)
            mcpOperations.erase(mcpOperations.begin());
        McpOperation operation;
        operation.id = juce::Uuid().toString();
        operation.kind = action;
        operation.path = presetFile.getFullPathName();
        operation.project = controller.projectIdentity;
        operation.revision = controller.revision;
        const auto id = operation.id;
        mcpOperations.push_back(operation);
        if (action == "loadPreset")
            completeMcpOperation(id, applyChannelPresetFile(channel, presetFile));
        else
        {
            auto safe = juce::Component::SafePointer<Studio>(this);
            captureChannelPreset(channel,
                                 [safe, id, presetFile](juce::Result result, juce::String xml)
                                 {
                                     if (!safe)
                                         return;
                                     if (result.wasOk() && !presetFile.replaceWithText(xml))
                                         result = juce::Result::fail("channelPreset.writeFailed: Could not write " +
                                                                     presetFile.getFullPathName());
                                     safe->completeMcpOperation(id, result);
                                 });
        }
        for (const auto &current : mcpOperations)
            if (current.id == id)
                return describe(current);
        throw McpFailure("unknown_operation", "Preset operation was not retained");
    }
    if (action == "perform")
    {
        if (args.hasProperty("pattern"))
            if (auto r = launchPattern(identity(args, "pattern", 0), bool(args.getProperty("on", true))); r.failed())
                throw McpFailure("invalid_arguments", r.getErrorMessage());
        return performanceState();
    }
    if (action == "cancelExport")
    {
        auto response = obj();
        set(response, "cancelling", cancelExport());
        set(response, "completionState", "completed");
        return response;
    }
    if (action == "vocalPitch" || action == "vocalPitchApply")
    {
        const auto clip = identity(args, "clip", 0);
        vocal::Analysis analysis;
        if (auto analysed = analyzeVocalClip(clip, analysis); analysed.failed())
            throw McpFailure(analysed.getErrorMessage().upToFirstOccurrenceOf(":", false, false).containsChar(' ')
                                 ? juce::String("invalid_arguments")
                                 : analysed.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                             analysed.getErrorMessage());
        if (action == "vocalPitch")
        {
            if (bool(args.getProperty("editor", false)))
                if (auto opened = openVocalPitchEditor(clip); opened.failed())
                    throw McpFailure("invalid_arguments", opened.getErrorMessage());
            auto result = obj();
            juce::Array<juce::var> notes, curve;
            for (const auto &n : analysis.notes)
            {
                auto row = obj();
                set(row, "note", n.index);
                set(row, "start", n.start);
                set(row, "end", n.end);
                set(row, "pitch", n.pitch);
                set(row, "name", vocal::noteName(n.pitch));
                set(row, "cents", int(std::lround((n.pitch - std::round(n.pitch)) * 100)));
                notes.add(row);
            }
            for (const auto &f : analysis.curve)
                curve.add(f.midi == 0 ? juce::var() : juce::var(std::round(f.midi * 100) / 100));
            set(result, "notes", notes);
            set(result, "curveHopSeconds", analysis.hop);
            set(result, "curve", curve);
            set(result, "completionState", "completed");
            return result;
        }
        std::vector<double> moves(analysis.notes.size(), 0.);
        const double snap = double(args.getProperty("snap", 0.));
        if (!std::isfinite(snap) || snap < 0 || snap > 1)
            throw McpFailure("invalid_arguments", "snap is 0..1");
        for (size_t i = 0; i < moves.size(); ++i)
            moves[i] = (std::round(analysis.notes[i].pitch) - analysis.notes[i].pitch) * snap;
        std::vector<double> formants(moves.size(), 0.), times(moves.size(), 0.);
        if (const auto *list = args["shifts"].getArray())
            for (const auto &item : *list)
            {
                const auto note = int(item["note"]);
                const double semitones = double(item.getProperty("semitones", 0.)),
                             formant = double(item.getProperty("formant", 0.));
                if (note < 0 || note >= int(moves.size()) || !std::isfinite(semitones) || std::abs(semitones) > 24 ||
                    !std::isfinite(formant) || std::abs(formant) > 12)
                    throw McpFailure("invalid_arguments",
                                     "Shift a note index from vocalPitch by -24..24 semitones (formant -12..12)");
                if (item.hasProperty("semitones"))
                    moves[size_t(note)] = semitones;
                formants[size_t(note)] = formant;
                const double time = double(item.getProperty("time", 0.));
                if (!std::isfinite(time) || std::abs(time) > 1)
                    throw McpFailure("invalid_arguments", "Note time moves are -1..1 seconds");
                times[size_t(note)] = time;
            }
        std::vector<vocal::Shift> shifts;
        for (size_t i = 0; i < moves.size(); ++i)
            shifts.push_back({analysis.notes[i].start, analysis.notes[i].end, moves[i], formants[i], times[i]});
        while (mcpOperations.size() >= 64)
            mcpOperations.erase(mcpOperations.begin());
        McpOperation operation;
        operation.id = juce::Uuid().toString();
        operation.kind = action;
        operation.project = controller.projectIdentity;
        operation.revision = controller.revision;
        mcpOperations.push_back(operation);
        if (auto started = applyVocalPitch(clip, shifts, operation.id, bool(args.getProperty("preserveFormants", true)));
            started.failed())
        {
            mcpOperations.pop_back();
            throw McpFailure(started.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                             started.getErrorMessage());
        }
        mcpOperations.back().path = stretchJob ? stretchJob->target.getFullPathName() : juce::String{};
        return describe(mcpOperations.back());
    }
    if (action == "alignClip")
    {
        const auto revision = controller.revision;
        if (auto aligned = alignClipToGrid(identity(args, "clip", 0), double(args.getProperty("beats", .25)));
            aligned.failed())
            throw McpFailure("invalid_arguments", aligned.getErrorMessage());
        auto response = obj();
        set(response, "message", status);
        set(response, "changed", controller.revision != revision);
        set(response, "completionState", "completed");
        return response;
    }
    if (action == "missingMedia" || action == "relinkMedia")
    {
        if (action == "relinkMedia")
        {
            const juce::File folder(args["path"].toString());
            if (!args["path"].isString() || !juce::File::isAbsolutePath(args["path"].toString()) ||
                !folder.isDirectory())
                throw McpFailure("invalid_arguments", "relinkMedia needs an absolute folder path");
            if (auto relinked = relinkMissingMedia(folder); relinked.failed())
                throw McpFailure("not_found", relinked.getErrorMessage());
        }
        auto response = obj();
        juce::Array<juce::var> rows;
        for (const auto id : missingMedia(controller.project))
        {
            auto row = obj();
            set(row, "channel", juce::int64(id));
            set(row, "name", controller.project.channel(id)->name);
            set(row, "source", controller.project.channel(id)->source);
            rows.add(row);
        }
        set(response, "missing", rows);
        set(response, "completionState", "completed");
        return response;
    }
    if (action == "importMidi")
    {
        // Same reader, filters and canonical notes.import step as the Import MIDI dialog.
        const juce::File file(args["path"].toString());
        if (!args["path"].isString() || !juce::File::isAbsolutePath(args["path"].toString()) || !file.existsAsFile())
            throw McpFailure("invalid_arguments", "importMidi needs an absolute path to an existing .mid file");
        const Id pattern = args.hasProperty("pattern") ? identity(args, "pattern", 0) : selectedPattern;
        const Id channel = args.hasProperty("channel") ? identity(args, "channel", 0) : selectedChannel;
        MidiScoreOptions options;
        options.track = args.hasProperty("track") ? integer(args, "track", 0, 1, 256) - 1 : -1;
        options.realign = bool(args.getProperty("realign", false));
        if (const auto *channels = args["midiChannels"].getArray())
        {
            // Same subset as the dialog's MIDI channel tiles (1..16).
            std::uint32_t mask = 0;
            for (const auto &value : *channels)
            {
                const int ch = int(value);
                if (ch < 1 || ch > 16)
                    throw McpFailure("invalid_arguments", "midiChannels are 1..16");
                mask |= 1u << (ch - 1);
            }
            if (!mask)
                throw McpFailure("invalid_arguments", "midiChannels needs at least one channel");
            options.channelMask = mask;
        }
        juce::MemoryBlock bytes;
        MidiScore score;
        if (!file.loadFileAsData(bytes))
            throw McpFailure("invalid_arguments", "Cannot read the MIDI file");
        if (auto read = readMidiScore(bytes, options, score); read.failed())
            throw McpFailure("invalid_arguments", read.getErrorMessage());
        if (auto applied = commitMidiImport(score, bool(args.getProperty("blend", false)), controller.projectIdentity,
                                            controller.revision, pattern, channel,
                                            bool(args.getProperty("useTempo", false)),
                                            bool(args.getProperty("split", false)));
            applied.failed())
            throw McpFailure("invalid_arguments", applied.getErrorMessage());
        auto response = obj();
        set(response, "notes", int(score.notes.size()));
        set(response, "controllers", int(score.controllers.size()));
        set(response, "fileTempos", int(score.tempos.size()));
        set(response, "fileMeter", score.numerator ? juce::var(juce::String(score.numerator) + "/" +
                                                                juce::String(score.denominator))
                                                    : juce::var());
        set(response, "tempo", controller.project.tempo);
        set(response, "tempoChanges", int(controller.project.tempoChanges.size()));
        set(response, "ignoredEvents", score.ignoredEvents);
        set(response, "completionState", "completed");
        return response;
    }
    if (action == "video")
    {
        if (bool(args["close"]))
            closeVideo();
        else if (args.hasProperty("path"))
        {
            if (auto opened = openVideo(juce::File(args["path"].toString())); opened.failed())
                throw McpFailure("invalid_arguments", opened.getErrorMessage());
        }
        if (args.hasProperty("offset") && videoWindow)
            videoWindow->offset = double(args["offset"]);
        return videoState();
    }
    if (action == "sliceChannel")
    {
        if (auto sliced = sliceChannel(identity(args, "channel", 0), double(args.getProperty("sensitivity", .5)));
            sliced.failed())
            throw McpFailure("invalid_arguments", sliced.getErrorMessage());
        return mcpCall("vibera_state", obj())["structuredContent"];
    }
    if (action == "editClip")
    {
        while (mcpOperations.size() >= 64)
            mcpOperations.erase(mcpOperations.begin());
        McpOperation operation;
        operation.id = juce::Uuid().toString();
        operation.kind = action;
        operation.project = controller.projectIdentity;
        operation.revision = controller.revision;
        mcpOperations.push_back(operation);
        if (auto started = editAudioClip(identity(args, "clip", 0), args["edit"].toString(),
                                         double(args.getProperty("value", 0.)), operation.id);
            started.failed())
        {
            mcpOperations.pop_back();
            throw McpFailure("invalid_arguments", started.getErrorMessage());
        }
        mcpOperations.back().path = stretchJob ? stretchJob->target.getFullPathName() : juce::String{};
        return describe(mcpOperations.back());
    }
    if (action == "warpClip")
    {
        while (mcpOperations.size() >= 64)
            mcpOperations.erase(mcpOperations.begin());
        McpOperation operation;
        operation.id = juce::Uuid().toString();
        operation.kind = action;
        operation.project = controller.projectIdentity;
        operation.revision = controller.revision;
        mcpOperations.push_back(operation);
        const double bpm = args.hasProperty("bpm") ? double(args["bpm"]) : controller.project.tempo;
        if (auto started = warpAudioClip(identity(args, "clip", 0), bpm, operation.id); started.failed())
        {
            mcpOperations.pop_back();
            throw McpFailure("invalid_arguments", started.getErrorMessage());
        }
        mcpOperations.back().path = stretchJob ? stretchJob->target.getFullPathName() : juce::String{};
        return describe(mcpOperations.back());
    }
    if (action == "stretchClip")
    {
        while (mcpOperations.size() >= 64)
            mcpOperations.erase(mcpOperations.begin());
        McpOperation operation;
        operation.id = juce::Uuid().toString();
        operation.kind = action;
        operation.project = controller.projectIdentity;
        operation.revision = controller.revision;
        mcpOperations.push_back(operation);
        const double ratio = args.hasProperty("beats")
                                 ? [&]
                                   {
                                       const auto clip = identity(args, "clip", 0);
                                       for (const auto &p : controller.project.audioPlacements)
                                           if (p.id == clip && p.duration > 0)
                                               return (controller.project.songSeconds(p.beat + double(args["beats"])) -
                                                       controller.project.songSeconds(p.beat)) /
                                                      p.duration;
                                       return 0.;
                                   }()
                                 : double(args["ratio"]);
        if (auto started = stretchAudioClip(identity(args, "clip", 0), ratio, operation.id); started.failed())
        {
            mcpOperations.pop_back();
            throw McpFailure("invalid_arguments", started.getErrorMessage());
        }
        mcpOperations.back().path = stretchJob ? stretchJob->target.getFullPathName() : juce::String{};
        return describe(mcpOperations.back());
    }
    if (action == "loudness")
    {
        const auto path = args["path"].toString();
        if (!juce::File::isAbsolutePath(path))
            throw McpFailure("invalid_arguments", "loudness requires an absolute audio file path");
        loudness::Result measured;
        if (auto analysed = analyzeLoudnessFile(juce::File(path), measured); analysed.failed())
            throw McpFailure(analysed.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                             analysed.getErrorMessage().fromFirstOccurrenceOf(": ", false, false));
        auto number = [](double v) { return std::isfinite(v) ? juce::var(v) : juce::var(); };
        auto result = obj();
        set(result, "completionState", "completed");
        set(result, "integratedLufs", number(measured.integratedLufs));
        set(result, "shortTermMaxLufs", number(measured.shortTermMaxLufs));
        set(result, "momentaryMaxLufs", number(measured.momentaryMaxLufs));
        set(result, "loudnessRangeLu", number(measured.rangeLu));
        set(result, "truePeakDbtp", number(measured.truePeakDbtp));
        set(result, "samplePeakDbfs", number(measured.samplePeakDbfs));
        set(result, "seconds", measured.seconds);
        return result;
    }
    if (action == "diagnostics")
    {
        const auto path = args["path"].toString();
        if (!juce::File::isAbsolutePath(path))
            throw McpFailure("invalid_arguments", "diagnostics requires an absolute .txt path");
        const juce::File file(path);
        if (auto written = writeDiagnostics(file, bool(args["overwrite"])); written.failed())
            throw McpFailure(written.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                             written.getErrorMessage().fromFirstOccurrenceOf(": ", false, false));
        auto result = obj();
        set(result, "completionState", "completed");
        set(result, "path", file.getFullPathName());
        set(result, "report", file.loadFileAsString());
        return result;
    }
    if (action != "new" && action != "open" && action != "save" && action != "export" &&
        action != "templates" && action != "saveTemplate" && action != "effectImpulse" &&
        action != "effectSmartDisable" &&
        action != "saveTrackState" && action != "openTrackState" && action != "exportTracks" && action != "exportTrackGroups" &&
        action != "exportMidi")
        throw McpFailure("unsupported_operation", "Unknown project operation");
    if (devicePreparationBusy() || afterPluginState || pendingExport || audio.rendering() || midiFileBusy ||
        exportBusy())
        throw McpFailure("busy", "Finish the active save/export operation first");
    if (controller.hasGesture() || midiTaking || hasPendingMidiTake())
        throw McpFailure("busy", "Finish the edit or finish/retry/cancel the retained MIDI take first");
    if (action == "templates")
    {
        auto data = obj();
        juce::Array<juce::var> names;
        for (const auto &file : projectTemplates())
            names.add(file.getFileNameWithoutExtension());
        set(data, "templates", names);
        set(data, "folder", templatesFolder().getFullPathName());
        return data;
    }
    if (action == "new" && args.hasProperty("template") && args["template"].toString() != "empty" &&
        args["template"].toString() != "demo" &&
        !templatesFolder().getChildFile(args["template"].toString().trim() + ".vibra").existsAsFile())
        throw McpFailure("unavailable_target", "No project template named " + args["template"].toString());
    if (action == "new" || action == "open")
    {
        juce::String code;
        if (auto ready = admitProjectTransition(bool(args["discardChanges"]), code); ready.failed())
            throw McpFailure(code, ready.getErrorMessage()); // Before allocating an accepted operation.
    }
    juce::File file;
    if (action == "exportTracks" || action == "exportTrackGroups")
    {
        const auto path = args["path"].toString();
        if (!juce::File::isAbsolutePath(path))
            throw McpFailure("invalid_arguments", "exportTracks requires an absolute folder path");
        file = juce::File(path);
        if (file.existsAsFile())
            throw McpFailure("invalid_arguments", "exportTracks needs a folder, not a file");
    }
    else if (action == "saveTrackState" || action == "openTrackState")
    {
        const auto path = args["path"].toString();
        if (!juce::File::isAbsolutePath(path) || !args.hasProperty("mixer"))
            throw McpFailure("invalid_arguments", action + " requires mixer and an absolute .vibfx path");
        file = juce::File(path);
    }
    else if (action == "effectImpulse")
    {
        const auto path = args["path"].toString();
        if (!juce::File::isAbsolutePath(path) || !args.hasProperty("plugin"))
            throw McpFailure("invalid_arguments", "effectImpulse requires plugin and an absolute path");
        file = juce::File(path);
    }
    else if (action == "saveTemplate")
    {
        juce::String legal;
        if (auto valid = templateFileName(args["template"].toString(), legal); valid.failed())
            throw McpFailure("invalid_arguments", valid.getErrorMessage());
        file = templatesFolder().getChildFile(legal + ".vibra");
    }
    else if (action != "new" && action != "effectSmartDisable" && !(action == "export" && bool(args["placeClip"])))
    {
        const auto path = args["path"].toString();
        if (!juce::File::isAbsolutePath(path))
            throw McpFailure("invalid_arguments", "An absolute project/output path is required");
        file = juce::File(path);
        const bool stems = action == "export" && bool(args["stems"]);
        if ((action == "save" && !file.hasFileExtension("vibra")) ||
            (action == "export" && !stems && !file.hasFileExtension("wav;flac;ogg;mp3")) ||
            (action == "exportMidi" && !file.hasFileExtension("mid;midi")))
            throw McpFailure("invalid_arguments", "Save requires .vibra; export requires .wav/.flac/.ogg (or "
                                                  "stems=true with a folder); exportMidi requires .mid/.midi");
        if (stems)
        {
            if (file.existsAsFile())
                throw McpFailure("invalid_arguments", "stems export needs a folder path");
            const bool armedOnly = bool(args["armedOnly"]);
            if (stemTargets(controller.project, file, armedOnly).empty())
                throw McpFailure("invalid_state", armedOnly ? "No Mixer track is armed for rendering"
                                                            : "No Mixer insert is fed by a Channel: nothing to export as stems");
            for (const auto &[insert, stem] : stemTargets(controller.project, file, armedOnly))
                if (stem.exists() && !bool(args["overwrite"]))
                    throw McpFailure("output_exists", stem.getFileName() + " exists; overwrite=true is required");
        }
        else if ((action == "save" || action == "export" || action == "exportMidi") && file.exists() &&
                 !bool(args["overwrite"]))
            throw McpFailure("output_exists", "Output already exists; overwrite=true is required");
    }
    if (action == "exportMidi")
    {
        const auto snapshot = midiExportSnapshot();
        if (!snapshot)
            throw McpFailure("invalid_target", "Select an existing Pattern and Channel first");
        juce::String operation;
        const auto result = startMidiExport(file, *snapshot, bool(args["overwrite"]), operation);
        if (result.failed())
            throw McpFailure("invalid_state", result.getErrorMessage());
        return describe(mcpOperations.back());
    }
    Id exportPattern = 0;
    if (args.hasProperty("pattern"))
    {
        const auto requested = args["pattern"];
        if (action != "export" || (!requested.isInt64() && !requested.isInt()) ||
            !controller.project.pattern(Id(juce::int64(requested))))
            throw McpFailure("invalid_target", "pattern applies to export and must be an existing Pattern ID");
        exportPattern = Id(juce::int64(requested));
    }
    if (bool(args["placeClip"]))
    {
        if (action != "export" || exportPattern == 0)
            throw McpFailure("invalid_arguments", "placeClip requires export with pattern");
        while (mcpOperations.size() >= 64)
            mcpOperations.erase(mcpOperations.begin());
        mcpOperations.push_back({});
        auto &operation = mcpOperations.back();
        operation.id = juce::Uuid().toString();
        operation.kind = action;
        operation.project = controller.projectIdentity;
        operation.revision = controller.revision;
        mcpExportOperation = operation.id;
        if (auto started = renderPatternToClip(exportPattern); started.failed())
        {
            mcpOperations.pop_back();
            mcpExportOperation.clear();
            throw McpFailure("invalid_state", started.getErrorMessage());
        }
        mcpOperations.back().path = renderClip ? renderClip->file.getFullPathName() : juce::String{};
        return describe(mcpOperations.back());
    }
    while (mcpOperations.size() >= 64)
        mcpOperations.erase(
            mcpOperations.begin()); // All older operations are terminal: one writer is admitted.
    McpOperation operation;
    operation.id = juce::Uuid().toString();
    operation.kind = action;
    operation.path = action == "new" ? juce::String{} : file.getFullPathName();
    operation.project = controller.projectIdentity;
    operation.revision = controller.revision;
    const auto id = operation.id;
    mcpOperations.push_back(operation);
    if (action == "exportTracks" || action == "exportTrackGroups")
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        auto run = action == "exportTracks" ? &Studio::exportPlaylistTracks : &Studio::exportPlaylistTrackGroups;
        if (auto started = (this->*run)(file,
                                                [safe, id](juce::Result result)
                                                {
                                                    if (safe)
                                                        safe->completeMcpOperation(id, result);
                                                });
            started.failed())
            completeMcpOperation(id, started);
    }
    else if (action == "saveTrackState" || action == "openTrackState")
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        const auto report = [safe, id](juce::Result result)
        {
            if (safe)
                safe->completeMcpOperation(id, result);
        };
        const int insert = int(args["mixer"]);
        if (auto started = action == "saveTrackState" ? saveTrackState(insert, file, report)
                                                      : openTrackState(insert, file, report);
            started.failed())
            completeMcpOperation(id, started);
    }
    else if (action == "effectSmartDisable")
    {
        if (!args.hasProperty("plugin"))
            throw McpFailure("invalid_arguments", "effectSmartDisable requires plugin and enabled");
        auto safe = juce::Component::SafePointer<Studio>(this);
        if (auto started = setEffectSmartDisable(Id(juce::int64(args["plugin"])), bool(args.getProperty("enabled", true)),
                                                 [safe, id](juce::Result result)
                                                 {
                                                     if (safe)
                                                         safe->completeMcpOperation(id, result);
                                                 });
            started.failed())
            completeMcpOperation(id, started);
    }
    else if (action == "effectImpulse")
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        if (auto started = loadEffectImpulse(Id(juce::int64(args["plugin"])), file,
                                             [safe, id](juce::Result result)
                                             {
                                                 if (safe)
                                                     safe->completeMcpOperation(id, result);
                                             });
            started.failed())
            completeMcpOperation(id, started);
    }
    else if (action == "saveTemplate")
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        if (auto started = saveAsTemplate(args["template"].toString(),
                                          [safe, id](juce::Result result)
                                          {
                                              if (safe)
                                                  safe->completeMcpOperation(id, result);
                                          });
            started.failed())
            completeMcpOperation(id, started);
    }
    else if (action == "save")
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        saveTo(file,
               [safe, id](juce::Result result)
               {
                   if (safe)
                       safe->completeMcpOperation(id, result);
               });
    }
    else if (action == "export")
    {
        if (args.hasProperty("sampleRate"))
            exportRate = double(args["sampleRate"]);
        if (args.hasProperty("bitDepth"))
            exportBits = int(args["bitDepth"]);
        if (args.hasProperty("tail"))
            exportTail = args["tail"].toString() == "leave" ? 1 : args["tail"].toString() == "wrap" ? 2 : 0;
        if (args.hasProperty("outputChannels"))
        {
            const auto mode = args["outputChannels"].toString();
            const int index = juce::StringArray{"stereo", "mono", "left", "right"}.indexOf(mode);
            if (index < 0)
                throw McpFailure("invalid_arguments", "outputChannels is stereo, mono, left or right");
            audio.exportOutputChannels = index;
        }
        if (args.hasProperty("dither"))
            audio.exportDither = bool(args["dither"]);
        mcpExportOperation = id;
        stemsArmedOnly = bool(args["stems"]) && bool(args["armedOnly"]);
        exportTo(file, exportPattern, bool(args["stems"]));
    }
    else
    {
        const auto sourceProject = controller.projectIdentity;
        auto outcome = juce::Result::fail("Project transition did not complete");
        try
        {
            const auto templateName = args["template"].toString();
            outcome = action == "open" ? openProjectFile(file, bool(args["discardChanges"]))
                : templateName.isNotEmpty() && templateName != "empty" && templateName != "demo"
                    ? newFromTemplate(templateName, bool(args["discardChanges"]))
                    : newProject(templateName == "demo", bool(args["discardChanges"]));
        }
        catch (const std::exception &error)
        { outcome = juce::Result::fail(juce::String("Project transition failed: ") + error.what()); }
        catch (...)
        { outcome = juce::Result::fail("Project transition failed; inspect current project and audio state"); }
        const bool changed = sourceProject != controller.projectIdentity;
        for (auto &accepted : mcpOperations)
            if (accepted.id == id)
                accepted.documentChanged = changed;
        if (outcome.failed() && changed)
            outcome = juce::Result::fail("Project replacement committed but subsequent publication failed: " +
                                        outcome.getErrorMessage());
        completeMcpOperation(id, outcome); // Synchronous refusal/exception is terminal, not stranded pending.
    }
    return describe(mcpOperations.back());
}
#include "StudioMixerMacros.inc"
#include "StudioPluginIoAcceptance.inc"
#include "StudioSynth.inc"
} // namespace vibera
