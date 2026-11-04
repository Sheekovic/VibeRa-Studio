"""Actual stdio MCP/application check; no OS input, listening or hardware claim."""
import base64
import hashlib
import json
import pathlib
import queue
import subprocess
import sys
import threading
import time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
from dev_support import project_schema

app = pathlib.Path(sys.argv[1]).resolve()
output = pathlib.Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
application_sha = hashlib.sha256(app.read_bytes()).hexdigest()
(output / "verification.json").write_text(json.dumps({"finished":False,"passed":False,
    "checks":0,"applicationSha256":application_sha},indent=2),encoding="utf-8")
proc = subprocess.Popen([str(app), "--mcp-test"], stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                        encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
messages = queue.Queue()
errors = []
def receive():
    for line in proc.stdout:
        try:
            messages.put(json.loads(line))
        except Exception as exc:
            messages.put(exc)
    messages.put(RuntimeError("MCP stdout closed"))
threading.Thread(target=receive, daemon=True).start()
threading.Thread(target=lambda: errors.extend(proc.stderr), daemon=True).start()
serial = 0
checks = 0
def check(condition, message):
    global checks
    if not condition:
        raise AssertionError(message)
    checks += 1
def request(method, params=None, test_id=None):
    global serial
    serial += 1
    response_id = serial if test_id is None else test_id
    item = {"jsonrpc": "2.0", "id": response_id, "method": method}
    if params is not None:
        item["params"] = params
    proc.stdin.write(json.dumps(item) + "\n")
    proc.stdin.flush()
    reply = messages.get(timeout=15)
    if isinstance(reply, Exception):
        raise reply
    check(reply.get("jsonrpc") == "2.0" and reply.get("id") == response_id,
          "Response identity or stdio framing corrupted")
    return reply
def call(name, arguments=None):
    reply = request("tools/call", {"name": name, "arguments": arguments or {}})
    check("result" in reply, "Tool protocol error: " + str(reply))
    return reply["result"]
try:
    check("error" in request("tools/list"), "Uninitialized tools accepted")
    hello = request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                   "clientInfo": {"name": "VibeRa acceptance", "version": "1"}})
    check(hello["result"]["protocolVersion"] == "2025-11-25", "Protocol version mismatch")
    if len(sys.argv) > 3:
        check(hello["result"]["serverInfo"]["version"] == sys.argv[3], "Application/MCP version mismatch")
    check("error" in request("tools/list"), "Initialized notification not required")
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    proc.stdin.flush()
    tools = request("tools/list")["result"]["tools"]
    tool_names = [tool["name"] for tool in tools]
    runtime_schema = next(t for t in tools if t["name"] == "vibera_state")["outputSchema"]["properties"]["audio"]["properties"]["pluginWorkers"]["items"]["properties"]
    check(runtime_schema["auxiliaryInputChannels"]["enum"] == [0, 1, 2] and
          runtime_schema["bypassBlend"]["type"] == ["number", "null"] and
          runtime_schema["bypassBlend"]["minimum"] == 0 and runtime_schema["bypassBlend"]["maximum"] == 1,
          "Actual auxiliary/bypass readback discovery missing or invalid")
    # Deliberate compatibility assertion: preserve all28 released tools and add
    # the approved sampler-tempo, Advanced Fill, Synth, Note modes and AI workflows.
    # Never derive this expected set
    # from the candidate being tested; removal/replacement must still fail.
    expected_tools = {
        "vibera_state", "vibera_commands", "vibera_command", "vibera_select", "vibera_transport",
        "vibera_editor", "vibera_snapshot", "vibera_key", "vibera_pointer", "vibera_controller",
        "vibera_macros", "vibera_automationRecording", "vibera_effects", "vibera_plugin",
        "vibera_catalogue", "vibera_stretch", "vibera_slice", "vibera_selection", "vibera_gesture",
        "vibera_picker", "vibera_project", "vibera_batch", "vibera_mixer", "vibera_inspect",
        "vibera_rack", "vibera_groove", "vibera_recording", "vibera_mixerchain", "vibera_samplerTempo",
        "vibera_stepCompose", "vibera_synth", "vibera_note_modes", "vibera_ai", "vibera_audioRecording",
        "vibera_update"
    }
    check(len(tools) == 35 and set(tool_names) == expected_tools,
          "Expected all28 released tools plus sampler tempo, Advanced Fill, Synth, Note modes, AI and audio recording, without duplicates")
    effects_tool = next(t for t in tools if t["name"] == "vibera_effects")
    effects_schema = effects_tool["inputSchema"]
    check(effects_tool["annotations"]["readOnlyHint"] is True and
          effects_schema["additionalProperties"] is False and
          effects_schema["required"] == ["action"] and
          effects_schema["properties"]["action"]["enum"] == ["list", "descriptor"],
          "Built-in effects discovery exposes a typed immutable contract")
    builtins = call("vibera_effects", {"action":"list"})["structuredContent"]["effects"]
    check(len(builtins) == 25 and len({row["type"] for row in builtins}) == 25 and
          all(row["builtIn"] and row["format"] == "VibeRa" for row in builtins),
          "Independent built-in effects are available without scanning")
    select_schema = next(t for t in tools if t["name"] == "vibera_select")["inputSchema"]
    check(select_schema["properties"]["scope"]["maximum"] == 7, "Scope schema drifted from the canonical enum")
    for index in range(8):
        identity = "unicode-\u0627\u0644\u0645\u0648\u0633\u064a\u0642\u0649-" + str(index) + "-" + "x" * 32000
        check("result" in request("ping", test_id=identity), "Large UTF-8 response/lifetime corrupted")
    initial = call("vibera_state")["structuredContent"]
    check(initial["projectVersion"] == project_schema(pathlib.Path(__file__).resolve().parents[1]) and initial["clips"] == [] and initial["automationSources"] == [], "Project version/clip state missing")
    check(initial["audio"]["pluginWorkers"] == [], "Worker diagnostics should be empty before hosting")
    notification_schema = next(t for t in tools if t["name"] == "vibera_state")["outputSchema"]["properties"]["historyNotifications"]
    check(set(notification_schema["required"]) == {"controllerPending", "undoRequestPending", "undoObserverDeliveryAcknowledged"}
          and all(type(value) is bool for value in initial["historyNotifications"].values())
          and initial["historyNotifications"]["undoObserverDeliveryAcknowledged"] is False,
          "Typed truthful notification-request readback")
    for direction in ("edit.undo", "edit.redo"):
        refused_history = call("vibera_command", {"command":direction})
        check(refused_history.get("isError") is True and
              refused_history["structuredContent"]["error"]["code"] == "history_refused",
              "Unavailable history must return a structured failure, never fake completion")
    after_empty_history = call("vibera_state")["structuredContent"]
    check(all(after_empty_history[k] == initial[k] for k in
              ("revision", "dirty", "channels", "undoAvailable", "redoAvailable", "gestureActive")),
          "Refused history preserves canonical model and cursor")
    state_tool = next(t for t in tools if t["name"] == "vibera_state")
    preview_schema = state_tool["outputSchema"]["properties"]["quantizerPreview"]
    check(preview_schema["properties"]["error"]["properties"]["code"]["const"] == "history_refused",
          "Refused local Quantizer completion exposes typed pending readback")
    recording_tool = next(t for t in tools if t["name"] == "vibera_recording")
    token_schema = recording_tool["inputSchema"]["properties"]["serial"]
    check(token_schema["minimum"] == 1 and token_schema["maximum"] == 9007199254740991 and
          set(recording_tool["inputSchema"]["properties"]["action"]["enum"]) == {"state", "finish", "retry", "cancel"},
          "Recording discovery must expose exact bounded take tokens and actual shared operations")
    for action in ("finish", "retry", "cancel"):
        declared = call("vibera_commands", {"command":"recording." + action, "includeSchemas":True})["structuredContent"]
        check(declared["total"] == 1 and len(declared["operations"]) == 1,
              "Stable recording command ID must be discoverable exactly once")
        contract = declared["operations"][0]
        check(contract["supported"] and contract["mapping"] == "vibera_recording" and
              contract["invocation"] == {"tool":"vibera_recording", "action":action} and
              contract["macroAvailable"] is False and contract["arguments"]["properties"]["action"]["enum"] == [action] and
              ("serial" in contract["arguments"]["required"]) == (action != "finish") and
              "canonical" in contract["prerequisitesDetail"] and "recording" in contract["resultSchema"]["properties"],
              "Recording registry must expose its authoritative typed tool/target prerequisites and real outcome schema")
    idle_take = call("vibera_recording", {"action":"state", "offset":0, "limit":1})["structuredContent"]
    check(idle_take["completionState"] == "completed" and idle_take["recording"]["phase"] == "idle" and
          idle_take["recording"]["serial"] == 0 and not idle_take["recording"]["capturing"] and
          not idle_take["recording"]["pending"] and idle_take["notes"] == [] and not idle_take["notesPartial"],
          "Recording query completion must not claim capture or note commit")
    for bad_take in ({"action":"retry"}, {"action":"cancel"}, {"action":"retry", "serial":0},
                     {"action":"cancel", "serial":-1}, {"action":"retry", "serial":1.5},
                     {"action":"cancel", "serial":9007199254740992}, {"action":"retry", "serial":True},
                     {"action":"state", "serial":1}, {"action":"finish", "serial":1},
                     {"action":"finish", "limit":2}, {"action":"state", "limit":513}):
        rejected = call("vibera_recording", bad_take)
        check(rejected.get("isError") is True and
              rejected["structuredContent"]["error"]["code"] == "invalid_arguments",
              "Malformed take token/action must refuse before changing ownership: " + str(bad_take))
    for action in ("retry", "cancel"):
        stale_take = call("vibera_recording", {"action":action, "serial":1})
        check(stale_take.get("isError") is True and stale_take["structuredContent"]["completionState"] == "failed" and
              stale_take["structuredContent"]["error"]["code"] == "take.stale" and
              stale_take["structuredContent"]["recording"]["serial"] == 0,
              "Unknown take token cannot create, append or cancel an invented capture")
    no_take = call("vibera_recording", {"action":"finish"})["structuredContent"]
    check(no_take["completionState"] == "completed" and not no_take["documentChanged"] and
          no_take["recording"]["phase"] == "idle", "Idle finish is a truthful no-op")
    armed_take = call("vibera_command", {"command":"transport.record"})["structuredContent"]
    armed_query = call("vibera_recording", {"action":"state"})["structuredContent"]
    check(armed_take["recordArmed"] and armed_query["recordArmed"] and
          not armed_query["recording"]["capturing"] and armed_query["recording"]["phase"] == "idle",
          "Canonical Record arms while stopped; arm is not capture, hardware input or note commit")
    armed_finish = call("vibera_recording", {"action":"finish"})["structuredContent"]
    check(armed_finish["completionState"] == "completed" and armed_finish["recordArmed"] and
          not armed_finish["documentChanged"] and armed_finish["recording"]["serial"] == 0,
          "Finish cannot invent or replace a take merely because Record is armed")
    disarmed_take = call("vibera_command", {"command":"transport.record"})["structuredContent"]
    check(not disarmed_take["recordArmed"] and disarmed_take["revision"] == initial["revision"] and
          disarmed_take["dirty"] == initial["dirty"] and disarmed_take["notes"] == initial["notes"] and
          disarmed_take["undoAvailable"] == initial["undoAvailable"],
          "Record arm/disarm must preserve canonical musical state and history")
    (output / "recording-semantic-state.json").write_text(json.dumps({"idle":idle_take,
        "armed":armed_query,"idleFinish":no_take,"armedFinish":armed_finish}, indent=2), encoding="utf-8")
    check(initial["audio"]["graphCPUsConfigured"] == 1 and initial["audio"]["graphWorkersConfigured"] == 0,
          "Application graph policy must avoid auxiliary graph-worker completion waits")
    empty_plugins = call("vibera_plugin", {"action":"list"})["structuredContent"]
    check(empty_plugins["items"] == [] and empty_plugins["manualParameterUndo"] is False,
          "Plugin discovery must identify current targets and incomplete manual Undo honestly")
    check(empty_plugins["scalarParameterUndo"] is True and
          "supported scalar" in empty_plugins["manualHistoryScope"],
          "Supported scalar history is discoverable without promising arbitrary native UI Undo")
    recorder = call("vibera_automationRecording", {"action":"state"})["structuredContent"]
    check(recorder["completionState"] == "completed" and not recorder["active"] and
          not recorder["nativeParameterRecording"], "Real idle automation recorder reports its scope")
    before_arm = call("vibera_state")["structuredContent"]
    no_clock = call("vibera_automationRecording", {"action":"arm", "insert":1,
        "projectIdentity":before_arm["projectIdentity"], "revision":before_arm["revision"]})
    check(no_clock.get("isError") is True and
          no_clock["structuredContent"]["error"]["code"] == "automationRecording.clockUnavailable" and
          call("vibera_state")["structuredContent"]["revision"] == before_arm["revision"],
          "Pattern-mode arm (now supported) refuses without a real input clock and leaves the Project unchanged")
    catalogue = call("vibera_catalogue", {"action":"state"})["structuredContent"]
    check(catalogue["disposableSettings"] and catalogue["scanState"] == "idle" and
          catalogue["pluginCount"] == 0 and catalogue["standardLocations"],
          "Test catalogue must be isolated from user preferences and standard discovery defaults retained")
    for bad_catalogue in [{"action":"descriptor","id":"unknown"}, {"action":"job","operation":"unknown"},
                          {"action":"scan","customFolders":["relative/folder"]},
                          {"action":"scan","standardLocations":"no"}, {"action":"list","limit":129}]:
        check(call("vibera_catalogue", bad_catalogue).get("isError"), "Invalid catalogue request accepted")
    for bad_plugin in [{"action":"parameters","plugin":999999}, {"action":"job","operation":"unknown"},
                       {"action":"notifications","plugin":999999,"enabled":"yes"}]:
        check(call("vibera_plugin", bad_plugin).get("isError"), "Invalid plugin operation was accepted")
    channel_id = initial["selectedChannel"]
    check(initial["channels"][0]["sampler"]["available"], "Sampler state missing")
    check(initial["samplerWindows"] == [], "Sampler window state should be empty before opening")
    for invalid_plugin_selection in [
        {"plugin": 999, "pluginView": "controls"},
        {"parameter": 0}, {"lastTweaked": True}, {"pluginView": "native"},
        {"instance": "stale-worker"}, {"plugin": 999, "channel": channel_id},
        {"plugin": 999, "parameter": 4294967296},
    ]:
        refused = call("vibera_select", invalid_plugin_selection)
        check(refused.get("isError") is True, "Invalid/unloaded plugin UI selection accepted")
    after_plugin_refusals = call("vibera_state")["structuredContent"]
    check(after_plugin_refusals["revision"] == initial["revision"] and
          after_plugin_refusals["channels"] == initial["channels"] and
          after_plugin_refusals["audio"]["pluginWorkers"] == [],
          "Rejected plugin UI request changed the project or launched a worker")
    loop_bounds = call("vibera_command", {"command": "channel.sampler.loopRange", "args": {"channel": channel_id, "beat": .25, "length": .75}})["structuredContent"]
    loop_sampler = next(c for c in loop_bounds["channels"] if c["id"] == channel_id)["sampler"]
    check(loop_sampler["loopStart"] == .25 and loop_sampler["loopEnd"] == .75, "Sampler MCP loop range failed")
    for action in ("loop", "pingPong"):
        loop_state = call("vibera_command", {"command": "channel.sampler." + action, "args": {"channel": channel_id, "enabled": True}})["structuredContent"]
        check(next(c for c in loop_state["channels"] if c["id"] == channel_id)["sampler"][action], "Sampler MCP loop mode failed")
    for _ in range(3):
        loop_undo = call("vibera_command", {"command": "edit.undo"})["structuredContent"]
    check(next(c for c in loop_undo["channels"] if c["id"] == channel_id)["sampler"] == initial["channels"][0]["sampler"], "Sampler MCP loop exact Undo failed")
    ranged = call("vibera_command", {"command": "channel.sampler.range", "args": {"channel": channel_id, "beat": .2, "length": .7}})["structuredContent"]
    current_range = next(c for c in ranged["channels"] if c["id"] == channel_id)["sampler"]
    check(current_range["start"] == .2 and current_range["end"] == .7, "Atomic sampler range command failed")
    range_undo = call("vibera_command", {"command": "edit.undo"})["structuredContent"]
    check(next(c for c in range_undo["channels"] if c["id"] == channel_id)["sampler"]["start"] == 0, "Atomic sampler range Undo failed")
    tuned = call("vibera_command", {"command": "channel.sampler.cents", "args": {"channel": channel_id, "value": -300}})["structuredContent"]
    check(next(c for c in tuned["channels"] if c["id"] == channel_id)["sampler"]["cents"] == -300, "Sampler MCP tuning failed")
    undone = call("vibera_command", {"command": "edit.undo"})["structuredContent"]
    check(next(c for c in undone["channels"] if c["id"] == channel_id)["sampler"]["cents"] == 0, "Sampler MCP undo failed")
    placed = call("vibera_command", {"command": "clip.add", "args": {"pattern": initial["selectedPattern"], "beat": 0, "length": 2, "lane": 0}})["structuredContent"]
    clip = placed["clips"][0]
    check(clip["kind"] == "pattern" and clip["offsetUnit"] == "beats" and not clip["muted"] and clip["playlistBounds"]["width"] > 0,
          "Playlist state lacks source units/mute/real bounds")
    muted = call("vibera_command", {"command": "clip.mute", "args": {"ids": [clip["id"]], "enabled": True}})["structuredContent"]
    check(muted["clips"][0]["muted"] and all(not c["muted"] for c in muted["channels"]), "Clip mute leaked into Channels")
    restored_clip = call("vibera_command", {"command": "edit.undo"})["structuredContent"]
    check(not restored_clip["clips"][0]["muted"], "Clip mute undo lost state")
    restored_empty = call("vibera_command", {"command": "edit.undo"})["structuredContent"]
    check(restored_empty["clips"] == [], "Placement undo failed")
    initial = call("vibera_state")["structuredContent"]

    command_list = call("vibera_commands")["structuredContent"]["commands"]
    check("notes.add" in command_list and "project.save" not in command_list,
          "Canonical commands or filesystem boundary incorrect")
    check("automation.create" in command_list and "automation.point.move" in command_list,
          "Automation canonical commands are missing")
    automation = call("vibera_command", {"command": "automation.create", "args": {"mixer": 17, "beat": 2, "length": 4}})["structuredContent"]
    curve = automation["automationSources"][0]
    check(automation["clips"][0]["kind"] == "automation" and curve["links"][0]["mixer"] == 17,
          "Automation source/placement/target readback differs from command")
    pointed = call("vibera_command", {"command": "automation.point.add", "args": {"target": curve["id"], "beat": 1.5, "value": .25}})["structuredContent"]
    point = pointed["automationSources"][0]["points"][1]
    check(point["beat"] == 1.5 and point["value"] == .25, "Automation normalized point readback incorrect")
    invalid_point = call("vibera_command", {"command": "automation.point.move", "args": {"target": point["id"], "beat": 2, "value": 2}})
    unchanged = call("vibera_state")["structuredContent"]
    check(invalid_point.get("isError") is True and unchanged["automationSources"] == pointed["automationSources"] and unchanged["revision"] == pointed["revision"],
          "Malformed MCP automation edit was not atomic")
    call("vibera_command", {"command": "edit.undo"})
    clean_automation = call("vibera_command", {"command": "edit.undo"})["structuredContent"]
    check(clean_automation["automationSources"] == [] and clean_automation["clips"] == [], "Automation Undo did not restore document")
    initial = call("vibera_state")["structuredContent"]
    revision = initial["revision"]
    stale = call("vibera_command", {"command": "notes.add", "revision": revision + 100})
    check(stale.get("isError") is True, "Stale edit accepted")
    after = call("vibera_command", {"command": "notes.add", "revision": revision,
                  "args": {"beat": 1, "length": 0.5, "pitch": 64, "value": 0.8}})
    check(not after.get("isError"), "Canonical edit failed: " + str(after))
    state = after["structuredContent"]
    check(len(state["notes"]) == len(initial["notes"]) + 1, "Edit did not reach actual project")
    note = state["notes"][-1]
    check(note["pitch"] == 64 and note["beat"] == 1 and note["length"] == 0.5,
          "Edited note differs from request")
    undone = call("vibera_command", {"command": "edit.undo"})["structuredContent"]
    check(undone["notes"] == initial["notes"], "Undo did not restore actual project")
    redone = call("vibera_command", {"command": "edit.redo"})["structuredContent"]
    check(redone["notes"] == state["notes"], "Redo did not restore exact notes")
    bad = call("vibera_command", {"command": "project.save"})
    check(bad.get("isError") is True, "Unexposed file operation accepted")
    invalid = call("vibera_command", {"command": "notes.move", "args": {"beat": "oops"}})
    check(invalid.get("isError") is True, "Invalid numeric argument coerced")
    unsupported = call("vibera_command", {"command": "notes.add", "args": {"duration": 2}})
    check(unsupported.get("isError") is True, "Unsupported field silently ignored")
    check(call("vibera_state")["structuredContent"]["notes"] == redone["notes"],
          "Rejected arguments mutated the project")
    unknown = request("tools/call", {"name": "unknown", "arguments": {}})
    check("error" in unknown, "Unknown tool accepted")
    editor = call("vibera_editor", {"editor": 1, "action": "maximize"})
    check(not editor.get("isError"), "Editor handler failed")
    def musical(notes):
        return [{key: value for key, value in note.items() if key != "pianoBounds"} for note in notes]
    def pointer(phase, x=None, y=None, **kwargs):
        arguments = {"editor": 1, "phase": phase, **kwargs}
        if x is not None:
            arguments.update(x=x, y=y)
        return call("vibera_pointer", arguments)
    def state_of(result):
        check(not result.get("isError"), "Input handler failed: " + str(result))
        return result["structuredContent"]
    baseline = state_of(call("vibera_state"))
    rectangle = baseline["notes"][0]["pianoBounds"]
    x, y = rectangle["x"] + rectangle["width"] * 0.5, rectangle["y"] + rectangle["height"] * 0.5
    state_of(pointer("down", x, y))
    check(call("vibera_command", {"command": "edit.undo"}).get("isError") is True,
          "Edit interleaved with owned gesture")
    moved = state_of(pointer("drag", x + 80, y))
    check(moved["notes"][0]["beat"] != baseline["notes"][0]["beat"], "Real note-drag handler did not move note")
    state_of(pointer("up", x + 80, y))
    restored = state_of(call("vibera_command", {"command": "edit.undo"}))
    check(musical(restored["notes"]) == musical(baseline["notes"]), "Mouse gesture did not produce one undo")
    rectangle = restored["notes"][0]["pianoBounds"]
    x, y = rectangle["x"] + rectangle["width"] * 0.5, rectangle["y"] + rectangle["height"] * 0.5
    state_of(pointer("down", x, y))
    state_of(pointer("drag", x + 80, y))
    cancelled = state_of(pointer("cancel"))
    check(musical(cancelled["notes"]) == musical(baseline["notes"]) and not cancelled["pointerGestureActive"],
          "Gesture cancellation did not restore actual document")
    check(pointer("up", x, y).get("isError") is True, "Unpaired pointer release accepted")
    select_all = state_of(call("vibera_key", {"key": ord("A"), "ctrl": True}))
    check(select_all["selectedNotes"] == [baseline["notes"][0]["id"]], "Contextual Ctrl+A not routed to Piano")
    duplicated = state_of(call("vibera_key", {"key": ord("B"), "ctrl": True}))
    check(len(duplicated["notes"]) == 2, "Actual Ctrl+B handler did not duplicate")
    state_of(call("vibera_command", {"command": "edit.undo"}))
    check(call("vibera_key", {"key": ord("S"), "ctrl": True}).get("isError") is True,
          "Filesystem shortcut bypassed MCP boundary")
    zoom_before = state_of(call("vibera_state"))["piano"]["visibleBeats"]
    state_of(call("vibera_key", {"key": ord("3"), "shift": True}))
    zoom_after = state_of(call("vibera_state"))["piano"]["visibleBeats"]
    check(zoom_after != zoom_before, "Piano shortcut zoom did not reach actual view")
    controls = state_of(call("vibera_state"))["editors"][1]["controls"]
    ghost = next(control for control in controls if control["name"] == "Ghosts")
    gx, gy = ghost["x"] + ghost["width"] / 2, ghost["y"] + ghost["height"] / 2
    state_of(pointer("down", gx, gy))
    child_clicked = state_of(pointer("up", gx, gy))
    check(next(c for c in child_clicked["editors"][1]["controls"] if c["name"] == "Ghosts")["toggled"] != ghost["toggled"],
          "Child button click did not invoke its actual callback")
    final_state = state_of(call("vibera_state"))
    check(musical(final_state["notes"]) == musical(baseline["notes"]), "View/input checks changed musical document")
    playlist = state_of(call("vibera_editor", {"editor": 2, "action": "maximize"}))
    for key, shift, name in [(ord("E"), False, "Select"), (ord("D"), False, "Delete"), (ord("Z"), True, "Zoom")]:
        selected_tool = state_of(call("vibera_key", {"key": key, "shift": shift}))
        check(next(c for c in selected_tool["editors"][2]["controls"] if c["name"] == name)["toggled"], "Playlist tool shortcut did not update actual toolbar")
    visible_before = selected_tool["playlist"]["visibleBeats"]
    zoomed = state_of(call("vibera_key", {"key": ord("1"), "shift": True}))
    check(zoomed["playlist"]["visibleBeats"] != visible_before, "Playlist quick zoom did not update actual viewport")
    restored_view = state_of(call("vibera_key", {"key": ord("4"), "shift": True}))
    check(abs(restored_view["playlist"]["visibleBeats"] - visible_before) < 1e-8, "Playlist quick zoom restore lost original viewport")
    check(musical(restored_view["notes"]) == musical(baseline["notes"]), "Playlist view tools changed musical document")
    for index in range(4):
        detached = state_of(call("vibera_editor", {"editor": index, "action": "detach"}))
        check(detached["editors"][index]["detached"] and detached["editors"][index]["visible"],
              "Editor detach did not expose independent native host")
        hidden = state_of(call("vibera_editor", {"editor": index, "action": "hide"}))
        check(not hidden["editors"][index]["visible"], "Detached editor hide failed")
        shown = state_of(call("vibera_editor", {"editor": index, "action": "show"}))
        check(shown["editors"][index]["visible"], "Detached editor restore failed")
        docked = state_of(call("vibera_editor", {"editor": index, "action": "dock"}))
        check(not docked["editors"][index]["detached"] and docked["editors"][index]["visible"],
              "Editor did not return to main workspace")
        check(musical(docked["notes"]) == musical(baseline["notes"]), "Native host change edited music")
    layout_before = state_of(call("vibera_state"))
    for name, mode in [("tileBalanced", 1), ("tileFocused", 2), ("free", 0), ("docked", 0)]:
        arranged = state_of(call("vibera_command", {"command": "workspace." + name}))
        check(arranged["autoTileMode"] == mode, "Workspace layout command not dispatched")
        check(arranged["revision"] == layout_before["revision"] and
              musical(arranged["notes"]) == musical(layout_before["notes"]),
              "Workspace command edited music/history")
    state_of(call("vibera_command", {"command": "workspace.tileDetached"}))
    pattern_before = state_of(call("vibera_state"))
    source_id = pattern_before["patterns"][0]["id"]
    split_channel = next(c["id"] for c in pattern_before["channels"]
                         if c["id"] != pattern_before["selectedChannel"])
    split_fixture = state_of(call("vibera_command", {"command":"notes.add", "args":
        {"pattern":source_id,"channel":split_channel,"beat":1,"length":.5,"pitch":67,"value":.7}}))
    split_channels = state_of(call("vibera_command", {"command":"pattern.splitChannels", "args":
        {"pattern":source_id}}))
    check(len(split_channels["patterns"]) > len(pattern_before["patterns"]) and
          split_channels["patterns"][0]["id"] == source_id,
          "Split by channel did not reach the actual model with stable source identity")
    restored_split = state_of(call("vibera_command", {"command":"edit.undo"}))
    check(restored_split["patterns"] == split_fixture["patterns"] and
          musical(restored_split["notes"]) == musical(split_fixture["notes"]),
          "Split by channel Undo did not restore actual score/pattern list")
    split_cleanup = state_of(call("vibera_command", {"command":"edit.undo"}))
    check(split_cleanup["patterns"] == pattern_before["patterns"] and
          musical(split_cleanup["notes"]) == musical(pattern_before["notes"]),
          "Split fixture cleanup did not restore the earlier MCP workflow")
    transpose_before = split_cleanup
    transposed = state_of(call("vibera_command", {"command":"pattern.transpose", "args":
        {"pattern":source_id, "channel":transpose_before["selectedChannel"], "value":12}}))
    check([n["pitch"] for n in transposed["notes"]] ==
          [n["pitch"] + 12 for n in transpose_before["notes"]],
          "Pattern transpose did not shift the selected Channel")
    transpose_undo = state_of(call("vibera_command", {"command":"edit.undo"}))
    check(musical(transpose_undo["notes"]) == musical(transpose_before["notes"]),
          "Pattern transpose Undo did not restore score")
    for value in [1.5, 128, -128, 127]:
        refused = call("vibera_command", {"command":"pattern.transpose", "args":
            {"pattern":source_id, "channel":0, "value":value}})
        check(refused.get("isError") is True, "Invalid/overflowing Pattern transpose accepted")
    check(musical(state_of(call("vibera_state"))["notes"]) == musical(transpose_before["notes"]),
          "Invalid Pattern transpose changed score")
    named = state_of(call("vibera_command", {"command":"pattern.properties", "args":
        {"pattern":source_id,"text":"Verse <bass> & lead", "value":0xff5c93d1}}))
    check(named["patterns"][0]["name"] == "Verse <bass> & lead" and named["patterns"][0]["colour"] == "ff5c93d1",
          "Atomic Pattern properties did not reach actual model")
    check(musical(named["notes"]) == musical(pattern_before["notes"]), "Pattern metadata changed score")
    invalid_colour = call("vibera_command", {"command":"pattern.properties", "args":
        {"pattern":source_id,"text":"must not apply","value":12}})
    check(invalid_colour.get("isError") is True, "Invalid Pattern colour accepted")
    check(state_of(call("vibera_state"))["patterns"] == named["patterns"], "Invalid colour partially renamed")
    inserted = state_of(call("vibera_command", {"command":"pattern.insert", "args":{"pattern":source_id}}))
    check(inserted["patterns"][1]["id"] == source_id and inserted["patterns"][0]["notes"] == 0,
          "Insert did not precede selected Pattern")
    insert_id = inserted["patterns"][0]["id"]
    removed = state_of(call("vibera_command", {"command":"pattern.delete", "args":{"pattern":insert_id}}))
    check(removed["patterns"] == named["patterns"], "Empty Pattern delete changed unrelated sources")
    restored_insert = state_of(call("vibera_command", {"command":"edit.undo"}))
    check(restored_insert["patterns"] == inserted["patterns"], "Pattern delete Undo not exact")
    reordered = state_of(call("vibera_command", {"command":"pattern.move", "args":
        {"pattern":source_id,"lane":0}}))
    check(reordered["patterns"][0]["id"] == source_id, "Pattern reorder did not preserve source identity")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    state_of(call("vibera_command", {"command":"edit.undo"}))  # inserted empty Pattern
    state_of(call("vibera_command", {"command":"edit.undo"}))  # properties
    check(state_of(call("vibera_state"))["patterns"] == pattern_before["patterns"], "Pattern command Undo stack not exact")
    state_of(call("vibera_select", {"pattern":source_id,"scope":6}))
    check(call("vibera_key", {"key":127}).get("isError") is True, "Pattern Delete opened a dialog through MCP")
    state_of(call("vibera_select", {"scope":2}))
    controller_profile = state_of(call("vibera_controller", {"action":"preset"}))["controllerProfile"]
    check(len(controller_profile["bindings"]) >= 50, "Launchkey native mapping preset missing")
    base_revision = state_of(call("vibera_state"))["revision"]
    fader = state_of(call("vibera_controller", {"action":"inject","kind":0,"channel":16,"number":53,"value":0}))
    check(fader["revision"] == base_revision, "Pickup jumped to remote fader")
    caught = state_of(call("vibera_controller", {"action":"inject","kind":0,"channel":16,"number":53,"value":127}))
    check(caught["revision"] > base_revision, "Pickup failed to catch current value")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    learned = state_of(call("vibera_controller", {"action":"learn","index":0}))
    check(learned["controllerLearnIndex"] == 0, "MIDI Learn did not arm")
    captured = state_of(call("vibera_controller", {"action":"inject","kind":0,"channel":16,"number":89,"value":127}))
    check(captured["controllerLearnIndex"] == -1 and captured["controllerProfile"]["bindings"][0]["number"] == 89,
          "MIDI Learn did not capture actual inbox event")
    check(call("vibera_controller", {"action":"inject","port":1,"kind":1,"channel":1,"number":119,"value":127}).get("isError") is not True,
          "Modifier pad hold failed")
    check(call("vibera_controller", {"action":"inject","port":1,"kind":1,"channel":1,"number":96,"value":127}).get("isError") is True,
          "Mapped Save bypassed MCP filesystem boundary")
    state_of(call("vibera_controller", {"action":"inject","port":1,"kind":1,"channel":1,"number":119,"value":0}))
    snapshot = call("vibera_snapshot")
    content = snapshot["content"][0]
    check(content["type"] == "image" and content["mimeType"] == "image/png", "Snapshot is not MCP PNG")
    png = base64.b64decode(content["data"], validate=True)
    check(png[:8] == b"\x89PNG\r\n\x1a\n", "Snapshot bytes invalid")
    (output / "mcp-project.png").write_bytes(png)
    (output / "mcp-state.json").write_text(json.dumps(final_state, indent=2), encoding="utf-8")
    # Semantic product operations: no vibera_pointer or vibera_key in this workflow.
    registry = state_of(call("vibera_commands"))
    check(registry["coverageComplete"] is False, "Incomplete coverage was falsely advertised as complete")
    check("notes.record" in registry["commands"] and "plugin.instrument.create" in registry["commands"],
          "Canonical document operations still hidden behind the former whitelist")
    contracts = []
    offset = 0
    while True:
        page = state_of(call("vibera_commands", {"offset":offset,"limit":32,"includeSchemas":True}))
        contracts.extend(page["operations"])
        if page["nextOffset"] is None:
            break
        offset = page["nextOffset"]
    check(len(contracts) == registry["total"] and len({c["id"] for c in contracts}) == len(contracts),
          "Registry discovery omitted or duplicated operations")
    (output / "command-contracts.json").write_text(json.dumps(contracts, indent=2), encoding="utf-8")
    (output / "tools.json").write_text(json.dumps(tools, indent=2), encoding="utf-8")
    group_contract = next(c for c in contracts if c["id"] == "pattern.group")
    check(group_contract["arguments"]["properties"]["text"]["maxLength"] == 128,
          "Pattern group range missing from discovery")
    bad_schema = call("vibera_command", {"command":"mixer.gain", "args":{"mixer":0,"value":3}})
    check(bad_schema.get("isError") is True and
          bad_schema["structuredContent"]["error"]["code"] == "invalid_arguments" and
          bad_schema["structuredContent"]["completionState"] == "failed", "Structured schema failure missing")
    check(call("vibera_picker", {"action":"state","invented":True}).get("isError") is True,
          "Unknown root argument silently ignored")
    state_of(call("vibera_transport", {"action":"song"}))
    demo_job = state_of(call("vibera_project", {"action":"new","template":"demo","discardChanges":True}))
    demo_state = state_of(call("vibera_state"))
    check(demo_job["completionState"] == "completed" and demo_state["patternMode"] is False and
          demo_state["selectedChannel"] == demo_state["channels"][-1]["id"] and len(demo_state["notes"]) > 0,
          "Shared Demo action lost the original mode/selection behavior")
    fresh = state_of(call("vibera_project", {"action":"new","discardChanges":True}))
    check(fresh["completionState"] == "completed", "New project did not complete")
    current = state_of(call("vibera_state"))
    check(current["patternMode"] is True and current["selectedChannel"] == current["channels"][0]["id"] and
          current["notes"] == [], "Shared New project action lost the original empty/Pattern/first-Channel defaults")
    stale = call("vibera_command", {"command":"project.tempo", "projectIdentity":"old-session", "args":{"value":130}})
    check(stale.get("isError") is True and stale["structuredContent"]["error"]["code"] == "stale_project",
          "Project identity guard missing")
    invalid_source = call("vibera_command", {"command":"channel.add", "args":{"text":"relative-missing.wav"}})
    check(invalid_source.get("isError") is True and
          len(state_of(call("vibera_state"))["channels"]) == len(current["channels"]),
          "MCP bypassed shared UI sample validation")
    pattern, channel = current["selectedPattern"], current["channels"][0]["id"]
    steps = [{"command":"steps.set","args":{"pattern":pattern,"channel":channel,"beat":b,"enabled":True}}
             for b in [0,1,2,3]]
    steps += [{"command":"notes.add","args":{"pattern":pattern,"channel":current["channels"][-1]["id"],
                "beat":.5,"length":1,"pitch":48,"value":.75}}]
    state_of(call("vibera_batch", {"revision":current["revision"],"commands":steps}))
    score = state_of(call("vibera_inspect", {"entity":"notes","pattern":pattern}))["items"]
    check(len(score) == 5 and any(n["pitch"] == 48 for n in score), "Semantic beat/melody batch failed")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    check(state_of(call("vibera_inspect", {"entity":"notes"}))["items"] == [], "Batch was not one Undo")
    state_of(call("vibera_command", {"command":"edit.redo"}))
    check(state_of(call("vibera_inspect", {"entity":"notes"}))["items"] == score, "Batch Redo was not exact")
    bad_batch = call("vibera_batch", {"commands":[
        {"command":"notes.add","args":{"pattern":pattern,"channel":channel,"beat":.25,"value":.5}},
        {"command":"notes.velocity","args":{"pattern":pattern,"ids":[score[0]["id"]],"value":2}}]})
    check(bad_batch.get("isError") is True and
          state_of(call("vibera_inspect", {"entity":"notes"}))["items"] == score,
          "Failed semantic batch left partial edits")
    # Semantic selections target stable IDs, not editor pixels or list rows.
    kick_ids = [n["id"] for n in score if n["channel"] == channel]
    bass_id = next(n["id"] for n in score if n["channel"] != channel)
    selection_revision = state_of(call("vibera_state"))["revision"]
    picked_notes = state_of(call("vibera_selection", {"action":"range","target":"notes",
                           "start":1,"end":3,"low":0,"high":127}))
    check(picked_notes["ids"] == kick_ids[1:3], "Musical-time note selection failed")
    state_of(call("vibera_selection", {"action":"set","target":"notes","ids":[kick_ids[0]],"mode":"add"}))
    toggled = state_of(call("vibera_selection", {"action":"set","target":"notes","ids":[kick_ids[1]],"mode":"toggle"}))
    check(toggled["ids"] == [kick_ids[2],kick_ids[0]], "Selection add/toggle differs from model")
    refused_selection = call("vibera_selection", {"action":"set","target":"notes","ids":[bass_id,999999]})
    check(refused_selection.get("isError") and
          state_of(call("vibera_selection", {"action":"state","target":"notes"}))["ids"] == toggled["ids"],
          "Invalid selection changed the previous selection")
    all_notes = state_of(call("vibera_selection", {"action":"all","target":"notes","allChannels":True}))
    check(len(all_notes["ids"]) == len(score) and all_notes["revision"] == selection_revision,
          "Selection changed document history or missed multi-Channel notes")
    check(call("vibera_selection", {"action":"range","target":"notes","start":3,"end":2}).get("isError"),
          "Inverted musical selection accepted")
    state_of(call("vibera_selection", {"action":"clear","target":"notes"}))
    gesture = state_of(call("vibera_gesture", {"action":"begin","label":"Melody velocity sweep"}))
    token = gesture["operation"]
    check(gesture["completionState"] == "pending", "Gesture begin falsely reported committed")
    check(call("vibera_command", {"command":"edit.undo"}).get("isError") and
          call("vibera_gesture", {"action":"cancel","operation":"foreign"}).get("isError"),
          "Gesture ownership failed to guard unrelated mutation or wrong token")
    for value in [.2,.4,.6,.9]:
        preview = state_of(call("vibera_gesture", {"action":"update","operation":token,
            "command":"notes.velocity","args":{"pattern":pattern,"ids":[bass_id],"value":value}}))
        check(preview["completionState"] == "pending", "Live update falsely reported committed")
    bad_update = call("vibera_gesture", {"action":"update","operation":token,
                     "command":"notes.velocity","args":{"pattern":pattern,"ids":[bass_id],"value":2}})
    check(bad_update.get("isError"), "Invalid gesture value accepted")
    preview_score = state_of(call("vibera_inspect", {"entity":"notes"}))["items"]
    check(abs(next(n["velocity"] for n in preview_score if n["id"] == bass_id)-.9) < 1e-6,
          "Invalid update changed earlier valid preview")
    refused_commit = call("vibera_gesture", {"action":"commit","operation":token,"revision":0})
    check(refused_commit.get("isError") and refused_commit["structuredContent"]["error"]["code"] == "stale_revision" and
          state_of(call("vibera_gesture", {"action":"state","operation":token}))["completionState"] == "pending",
          "Stale commit bypassed revision guard or discarded the preview")
    committed = state_of(call("vibera_gesture", {"action":"commit","operation":token}))
    check(committed["completionState"] == "completed", "Gesture commit did not complete")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    check(state_of(call("vibera_inspect", {"entity":"notes"}))["items"] == score, "Continuous edits were not one exact Undo")
    state_of(call("vibera_command", {"command":"edit.redo"}))
    check(state_of(call("vibera_inspect", {"entity":"notes"}))["items"] == preview_score, "Continuous Redo differs")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    dirty_before = state_of(call("vibera_state"))["dirty"]
    cancelled = state_of(call("vibera_gesture", {"action":"begin","label":"Cancelled note move"}))
    state_of(call("vibera_gesture", {"action":"update","operation":cancelled["operation"],
        "command":"notes.move","args":{"pattern":pattern,"ids":[bass_id],"beat":.25,"pitch":2}}))
    terminal = state_of(call("vibera_gesture", {"action":"cancel","operation":cancelled["operation"]}))
    check(terminal["completionState"] == "cancelled" and
          state_of(call("vibera_inspect", {"entity":"notes"}))["items"] == score and
          state_of(call("vibera_state"))["dirty"] == dirty_before, "Cancellation did not restore baseline")
    expired = state_of(call("vibera_gesture", {"action":"begin","leaseMs":250}))
    state_of(call("vibera_gesture", {"action":"update","operation":expired["operation"],
        "command":"notes.velocity","args":{"pattern":pattern,"ids":[bass_id],"value":.1}}))
    deadline = time.monotonic()+3
    while time.monotonic() < deadline:
        expiry = state_of(call("vibera_gesture", {"action":"state","operation":expired["operation"]}))
        if expiry["completionState"] != "pending": break
        time.sleep(.03)
    check(expiry["completionState"] == "cancelled" and
          state_of(call("vibera_inspect", {"entity":"notes"}))["items"] == score, "Idle expiry retained preview edits")
    clone = state_of(call("vibera_command", {"command":"pattern.clone","args":{"pattern":pattern}}))
    second = next(p["id"] for p in clone["patterns"] if p["id"] != pattern)
    state_of(call("vibera_command", {"command":"pattern.group", "args":{"ids":[pattern,second],"text":"MCP beat"}}))
    state_of(call("vibera_picker", {"action":"type","kind":"pattern"}))
    picked = state_of(call("vibera_picker", {"action":"groupFilter","text":"MCP beat"}))
    check(picked["sourceIds"] == [pattern,second], "Semantic group filter failed")
    filtered = state_of(call("vibera_picker", {"action":"filter","text":"copy"}))
    check(filtered["sourceIds"] == [second], "Semantic Picker text filter failed")
    check(call("vibera_picker", {"action":"select","ids":[pattern]}).get("isError") is True,
          "Filtered-out Picker target accepted")
    check(call("vibera_picker", {"action":"groupFilter","text":"unknown-group"}).get("isError") is True,
          "Unknown group silently changed filter")
    state_of(call("vibera_picker", {"action":"filter","text":""}))
    scrolled = state_of(call("vibera_picker", {"action":"scroll","scrollY":0}))
    check(scrolled["scrollY"] == 0 and scrolled["maximumScrollY"] >= 0, "Semantic Picker scroll readback failed")
    state_of(call("vibera_picker", {"action":"select","ids":[pattern,second]}))
    state_of(call("vibera_picker", {"action":"place","beat":4,"lane":1}))
    clips = state_of(call("vibera_inspect", {"entity":"clips"}))["items"]
    check(len(clips) == 2 and [c["source"] for c in clips] == [pattern,second] and
          [c["lane"] for c in clips] == [1,2], "Semantic Picker placement lost links or lane alignment")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    check(state_of(call("vibera_inspect", {"entity":"clips"}))["items"] == [], "Picker placement Undo failed")
    state_of(call("vibera_command", {"command":"edit.redo"}))
    state_of(call("vibera_command", {"command":"mixer.gain","args":{"mixer":0,"value":.65}}))
    mixer = state_of(call("vibera_inspect", {"entity":"mixer","offset":0,"limit":1}))
    check(abs(mixer["items"][0]["gain"]-.65) < 1e-6 and mixer["nextOffset"] == 1,
          "Mixer value/readback pagination failed")
    ui_revision = state_of(call("vibera_state"))["revision"]
    resized_picker = state_of(call("vibera_picker", {"action":"width","width":210}))
    check(resized_picker["width"] == 210, "Semantic Picker resize did not change actual layout")
    hidden = state_of(call("vibera_picker", {"action":"visible","visible":False}))
    check(hidden["visible"] is False, "Semantic picker hide failed")
    state_of(call("vibera_picker", {"action":"visible","visible":True}))
    check(state_of(call("vibera_state"))["revision"] == ui_revision, "Picker navigation changed musical history")
    bad_page = call("vibera_inspect", {"entity":"notes","pattern":999999})
    check(bad_page.get("isError") is True, "Unknown inspect target accepted")
    def wait_job(requested, expected="completed"):
        check(requested["completionState"] == "pending", "Async request falsely reported completion")
        until = time.monotonic()+35
        while time.monotonic() < until:
            result = call("vibera_project", {"action":"job","operation":requested["operation"]})
            job = result["structuredContent"]
            if job["completionState"] != "pending":
                check(job["completionState"] == expected, "Unexpected job outcome: "+str(job))
                return job
            time.sleep(.02)
        raise AssertionError("Job did not reach a terminal state")
    project_path = output / "semantic-session.vibra"
    saved = wait_job(state_of(call("vibera_project", {"action":"save","path":str(project_path),"overwrite":True})))
    check(saved["fileExists"] and saved["fileBytes"] > 0, "Save job finished without a project")
    check(call("vibera_project", {"action":"save","path":str(project_path)}).get("isError") is True,
          "Output overwrite guard missing")
    state_of(call("vibera_command", {"command":"pattern.group","args":{"ids":[pattern],"text":"Changed"}}))
    check(call("vibera_project", {"action":"open","path":str(project_path)}).get("isError") is True,
          "Dirty replacement guard missing")
    reopened = state_of(call("vibera_project", {"action":"open","path":str(project_path),"discardChanges":True}))
    check(reopened["completionState"] == "completed" and
          all(p["group"] == "MCP beat" for p in state_of(call("vibera_inspect", {"entity":"patterns"}))["items"]),
          "Project open did not restore saved groups")
    wav_path = output / "semantic-song.wav"
    wait_job(state_of(call("vibera_project", {"action":"export","path":str(wav_path),"overwrite":True})))
    import wave
    with wave.open(str(wav_path), "rb") as wav:
        check(wav.getframerate() == 48000 and wav.getsampwidth() == 3 and wav.getnframes() > 0,
              "Semantic WAV format/duration invalid")
        check(any(wav.readframes(wav.getnframes())), "Semantic beat export is all-zero PCM")
    before_sample = state_of(call("vibera_inspect", {"entity":"channels"}))["items"]
    sample = state_of(call("vibera_command", {"command":"channel.add","args":{"text":str(wav_path)}}))
    check(len(sample["channels"]) == len(before_sample)+1, "Shared sample validation rejected valid generated WAV")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    check(state_of(call("vibera_inspect", {"entity":"channels"}))["items"] == before_sample,
          "Sample-channel creation Undo not exact")
    # Product increment: actual semantic score tools, not legacy-coverage work.
    fresh = state_of(call("vibera_project", {"action":"new","discardChanges":True}))
    current = state_of(call("vibera_state"))
    pattern, channel = current["selectedPattern"], current["channels"][-1]["id"]
    state_of(call("vibera_select", {"pattern":pattern,"channel":channel,"scope":2}))
    for beat, length, pitch in [(0,.25,60),(2,1.125,67)]:
        state_of(call("vibera_command", {"command":"notes.add","args":{
            "pattern":pattern,"channel":channel,"beat":beat,"length":length,"pitch":pitch,"value":.8}}))
    def score_rows():
        return state_of(call("vibera_inspect", {"entity":"notes","pattern":pattern}))["items"]
    original_score = score_rows()
    contract = state_of(call("vibera_commands", {"command":"notes.chop","includeSchemas":True}))
    check("0.03125" in json.dumps(contract), "Chop range missing from live discovery")
    state_of(call("vibera_command", {"command":"notes.legato","args":{"pattern":pattern,"channel":channel}}))
    connected = score_rows()
    check(connected[0]["length"] == 2 and connected[1]["length"] == 1.125, "Semantic legato lengths incorrect")
    state_of(call("vibera_command", {"command":"notes.chop","args":{
        "pattern":pattern,"ids":[original_score[1]["id"]],"length":.25}}))
    chopped_score = score_rows()
    check(len(chopped_score) == 6 and chopped_score[-1]["length"] == .125 and
          chopped_score[-1]["beat"] == 3 and chopped_score[-1]["pitch"] == 67,
          "Semantic chop lost the exact tail or note properties")
    selected_score = state_of(call("vibera_state"))["selectedNotes"]
    check(len(selected_score) == 5, "UI/MCP chop selection readback differs")
    for bad in [{"length":0}, {"length":65}, {"ids":[999999],"length":.25}]:
        bad.update(pattern=pattern, channel=channel)
        failed = call("vibera_command", {"command":"notes.chop","args":bad})
        check(failed.get("isError") and "error" in failed["structuredContent"] and score_rows() == chopped_score,
              "Invalid score tool failed without structured atomic refusal")
    state_of(call("vibera_command", {"command":"notes.glue","args":{
        "pattern":pattern,"ids":selected_score}}))
    glued_score = score_rows()
    check(glued_score == connected and
          state_of(call("vibera_selection", {"action":"state","target":"notes"}))["ids"] == [original_score[1]["id"]],
          "Semantic Glue did not restore exact held note or retain surviving selection")
    stale_glue = call("vibera_command", {"command":"notes.glue","args":{
        "pattern":pattern,"ids":selected_score}})
    check(stale_glue.get("isError") and "error" in stale_glue["structuredContent"] and score_rows() == glued_score,
          "Stale Glue selection must refuse atomically with structured error")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    check(score_rows() == chopped_score, "Glue must be one exact Undo")
    state_of(call("vibera_command", {"command":"edit.redo"}))
    check(score_rows() == glued_score, "Glue Redo must retain original onset/ID/velocity")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    state_of(call("vibera_command", {"command":"edit.undo"}))
    check(score_rows() == connected, "Semantic chop must be one exact Undo")
    state_of(call("vibera_command", {"command":"edit.undo"}))
    check(score_rows() == original_score, "Semantic legato must be one exact Undo")
    state_of(call("vibera_command", {"command":"edit.redo"}))
    state_of(call("vibera_command", {"command":"edit.redo"}))
    check(score_rows() == chopped_score, "Semantic tool Redo must restore IDs and tail")
    state_of(call("vibera_command", {"command":"edit.redo"}))
    check(score_rows() == glued_score, "Final Glue Redo before persistence")
    state_of(call("vibera_command", {"command":"clip.add","args":{"pattern":pattern,"length":4}}))
    score_path = output / "score-tools.vibra"
    wait_job(state_of(call("vibera_project", {"action":"save","path":str(score_path),"overwrite":True})))
    state_of(call("vibera_project", {"action":"open","path":str(score_path),"discardChanges":True}))
    check(score_rows() == glued_score, "Semantic Glue project round trip differs")
    score_wav = output / "score-tools.wav"
    wait_job(state_of(call("vibera_project", {"action":"export","path":str(score_wav),"overwrite":True})))
    with wave.open(str(score_wav), "rb") as wav:
        check(wav.getnframes() > 0 and any(wav.readframes(wav.getnframes())), "Transformed score WAV is empty/silent")
    (output / "score-tools-result.json").write_text(json.dumps({"before":original_score,"chopped":chopped_score,"after":glued_score,
        "saved":str(score_path),"wav":str(score_wav),"passed":True}, indent=2), encoding="utf-8")
    blocker = output / "blocked-parent"
    blocker.write_text("owned failure fixture", encoding="utf-8")
    failure = wait_job(state_of(call("vibera_project", {"action":"save","path":str(blocker/"failed.vibra")})), "failed")
    check(failure["error"]["code"] == "operation_failed" and blocker.read_text() == "owned failure fixture",
          "Asynchronous save failure damaged the existing fixture or lost structured error")
    missing_job = call("vibera_project", {"action":"job","operation":"other-session"})
    check(missing_job.get("isError") is True, "Foreign/expired job accepted")
    (output / "semantic-state.json").write_text(json.dumps(state_of(call("vibera_state")), indent=2), encoding="utf-8")
    proc.stdin.close()
    check(proc.wait(timeout=10) == 0, "EOF did not cleanly close owned server")
    result = {"finished": True, "passed": True, "checks": checks, "exitCode": proc.returncode,
              "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
              "evidence": "Actual application stdio MCP/JSON-RPC, canonical edits/undo/redo and component PNG; no OS input/audio/listening"}
    (output / "verification.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result))
finally:
    if proc.poll() is None:
        if not proc.stdin.closed:
            proc.stdin.close()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.terminate()  # Only this retained subprocess, after graceful EOF failed.
            proc.wait(timeout=5)
    (output / "stderr.log").write_text("".join(errors), encoding="utf-8")
    if sys.exc_info()[1] is not None:
        (output / "verification.json").write_text(json.dumps({"finished":True,"passed":False,
            "checks":checks,"serverExitCode":proc.returncode,"applicationSha256":application_sha,
            "error":str(sys.exc_info()[1])},indent=2),encoding="utf-8")
