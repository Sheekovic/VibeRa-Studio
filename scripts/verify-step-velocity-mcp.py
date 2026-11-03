"""Actual semantic velocity/pitch, sampler pan/fine, score Shift/Repeat and MIDI note-off Release workflow.

The unreleased pitch increment intentionally extends the supported graph property
contract to velocity/pitch, sampler-only pan/fine, score Shift/Repeat and note-off Release. Existing velocity checks remain;
this is virtual/offline software evidence, not native input or listening.
"""
import base64
from dev_support import project_schema
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import wave
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
app, output = [Path(x).resolve() for x in sys.argv[1:3]]
output.mkdir(parents=True, exist_ok=True)
client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
if not client_path.exists(): client_path = root / "skills/vibera-studio/scripts/stdio_client.py"
spec = importlib.util.spec_from_file_location("neutral", client_path)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=0, applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(), nativeInput=False, physicalMidi=False, listening=False)
receipt_path = output / "verification.json"
receipt_path.write_text(json.dumps(receipt,indent=2),encoding="utf-8")
client = None
started = time.monotonic()
def check(ok, message):
    if not ok: raise AssertionError(message)
    receipt["checks"] += 1
def error(tool, args, expected=None):
    result = client.call(tool,args,allow_failure=True)
    check(result.get("isError") and (expected is None or result["structuredContent"]["error"]["code"]==expected), "Actual structured refusal: "+str(result))
def midi_note_events(raw, check):
    """Decode actual exported note event order; shared by Shift and Repeat cases."""
    events = []
    pos = 14
    def vlq():
        nonlocal pos
        value = 0
        for _ in range(4):
            byte = raw[pos]; pos += 1; value = (value << 7) | (byte & 127)
            if byte < 128: return value
        raise AssertionError("Malformed exported MIDI variable-length quantity")
    while pos < len(raw):
        check(raw[pos:pos+4] == b"MTrk", "Actual exported MIDI track marker")
        end = pos + 8 + int.from_bytes(raw[pos+4:pos+8], "big"); pos += 8
        tick = 0; running = 0
        while pos < end:
            tick += vlq(); status = raw[pos]
            if status >= 128: pos += 1
            else: status = running
            if status == 255:
                pos += 1; size = vlq(); pos += size; running = 0
            elif status in (240, 247):
                size = vlq(); pos += size; running = 0
            else:
                running = status; size = 1 if status >> 4 in (12, 13) else 2
                payload = raw[pos:pos+size]; pos += size
                if status >> 4 in (8, 9): events.append((tick, status >> 4, payload[0], payload[1]))
        check(pos == end, "Exported MIDI parses exactly through each track")
    return events


def verify_step_expression(client, output):
    output = Path(output).resolve()
    admitted = (root / "build/windows/step-expression-mcp").resolve()
    if output.parent != admitted or not output.name.startswith("attempt-"):
        raise ValueError("Fresh owned expression attempt required")
    output.mkdir(parents=True, exist_ok=False)
    checks = []
    def check(ok, label):
        if not ok: raise AssertionError(label)
        checks.append(label)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", {"command": name, "args": args})
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    current = state(); pattern = current["selectedPattern"]
    channel, other = [c["id"] for c in current["channels"][:2]]
    data("vibera_rack", {"action": "select", "ids": [channel], "activeChannel": channel})
    notes = lambda: data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]
    for beat in [0, .25, .5, .75]:
        command("steps.set", pattern=pattern, channel=channel, beat=beat, enabled=True)
    command("pattern.length", pattern=pattern, length=4)
    command("clip.add", pattern=pattern, beat=0, length=4)
    ids = [n["id"] for n in notes()]
    check(len(ids) == 4, "Expression fixture has four genuine source steps")
    for prop, field, limit in [("pan", "pan", 1), ("fine", "fineCents", 100)]:
        name = "steps." + prop
        contract = data("vibera_commands", {"command": name, "includeSchemas": True})["operations"][0]
        schema = contract["arguments"]
        check(contract["supported"] and schema["additionalProperties"] is False and
              schema["properties"]["value"]["minimum"] == -limit and
              schema["properties"]["value"]["maximum"] == limit and
              schema["properties"]["value"]["default"] == 0,
              "Actual closed signed expression discovery: " + prop)
        graph = data("vibera_rack", {"action": "configure", "graphVisible": True, "graphProperty": prop})["graph"]
        check(graph["property"] == prop and graph["defaultValue"] == 0 and graph["supported"] is True and
              graph["localSamplerTarget"] is True and graph["applicationAcknowledged"] is None,
              "Graph capability readback does not pretend runtime adoption: " + prop)
        before = notes()
        command(name, pattern=pattern, channel=channel, ids=ids, text="ramp", beat=0, length=.75,
                value=-limit, offset=limit)
        ramp = notes()
        expected = [-limit, -limit/3, limit/3, limit]
        check(all(abs(n[field]-v) < 1.e-12 for n, v in zip(ramp, expected)) and
              [{k:v for k,v in n.items() if k != field} for n in ramp] ==
              [{k:v for k,v in n.items() if k != field} for n in before],
              "Literal signed ramp preserves every other source field: " + prop)
        command("edit.undo"); check(notes() == before, "Expression one exact Undo: " + prop)
        command("edit.redo"); check(notes() == ramp, "Expression exact Redo: " + prop)
        command("edit.undo")
        token = data("vibera_gesture", {"action": "begin", "domain": "stepGraph"})["operation"]
        changed = data("vibera_gesture", {"action": "update", "operation": token, "command": name,
                     "args": {"pattern": pattern, "channel": channel, "text": "set", "value": limit/2}})
        check(changed["completionState"] == "pending" and all(n[field] == limit/2 for n in notes()),
              "Actual continuous expression preview remains pending: " + prop)
        refused = client.call("vibera_gesture", {"action": "update", "operation": token, "command": name,
                     "args": {"pattern": pattern, "channel": other, "value": 0}}, allow_failure=True)
        check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain",
              "Foreign Channel cannot retarget expression lease: " + prop)
        cancelled = data("vibera_gesture", {"action": "cancel", "operation": token})
        check(cancelled["completionState"] == "cancelled" and notes() == before,
              "Expression preview cancellation restores exact source: " + prop)
        for invalid in [{"value": limit + 1}, {"value": True}, {"value": 0, "ids": [ids[0], ids[0]]},
                        {"value": 0, "ids": [999999999]}]:
            baseline = notes(); revision = state()["revision"]
            result = client.call("vibera_command", {"command": name,
                "args": {"pattern": pattern, "channel": channel, **invalid}}, allow_failure=True)
            check(result.get("isError") and result["structuredContent"]["error"]["code"] and
                  notes() == baseline and state()["revision"] == revision,
                  "Invalid expression input refuses without data/history mutation: " + prop)
    def render(name):
        path = output / name
        client.wait_job(data("vibera_project", {"action": "export", "path": str(path), "overwrite": False}))
        with wave.open(str(path), "rb") as wav:
            check((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000),
                  "Actual signed-expression WAV format")
            pcm = wav.readframes(wav.getnframes())
        return [int.from_bytes(pcm[i:i+3], "little", signed=True) for i in range(0, len(pcm), 3)]
    baseline = render("neutral.wav")
    command("steps.pan", pattern=pattern, channel=channel, value=1)
    right = render("right.wav")
    check(max(abs(v) for v in right[::2]) == 0 and max(abs(v) for v in right[1::2]) > 1000,
          "Actual WAV applies per-note hard-right balance without fake vendor controls")
    command("edit.undo")
    command("steps.fine", pattern=pattern, channel=channel, value=100)
    fine = render("fine.wav")
    check(fine != baseline and max(abs(v) for v in fine) > 1000,
          "Actual WAV reflects nonneutral fine pitch with otherwise identical source state")
    saved = notes(); project = output / "expression.vibra"
    client.wait_job(data("vibera_project", {"action": "save", "path": str(project), "overwrite": False}))
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    client.wait_job(data("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    check(notes() == saved and render("reopened.wav") == fine,
          "Real project round trip retains expression source IDs and exact WAV")
    data("vibera_select", {"pattern": pattern, "channel": channel, "scope": 2})
    midi = data("vibera_project", {"action": "exportMidi", "path": str(output/"expression.mid"), "overwrite": False})
    check(midi["completionState"] == "pending" and midi["expressionNotesOmitted"] is None,
          "Accepted MIDI export reports unknown omission count until actual completion")
    done = client.wait_job(midi)
    check(done["completionState"] == "completed" and done["expressionNotesOmitted"] == 4 and
          (output/"expression.mid").read_bytes().startswith(b"MThd"),
          "MIDI1 export truthfully reports actual four-note expression omission at completion")
    data("vibera_select", {"pattern": pattern, "channel": other, "scope": 2})
    failed_path = output / "failed-expression.mid"
    failed_path.write_bytes(b"original-disposable-midi-output")
    before_failure = notes(); revision = state()["revision"]
    failed = data("vibera_project", {"action": "exportMidi", "path": str(failed_path), "overwrite": True})
    check(failed["completionState"] == "pending" and failed["expressionNotesOmitted"] is None,
          "Empty-part MIDI failure is initially an accepted pending job")
    deadline = time.monotonic() + 20
    while failed["completionState"] == "pending" and time.monotonic() < deadline:
        time.sleep(.03)
        failed = client.call("vibera_project", {"action": "job", "operation": failed["operation"]},
                             allow_failure=True)["structuredContent"]
    check(failed["completionState"] == "failed" and failed["error"]["code"] == "operation_failed" and
          failed["expressionNotesOmitted"] is None and failed["fileExists"] is True and
          failed_path.read_bytes() == b"original-disposable-midi-output" and
          notes() == before_failure and state()["revision"] == revision,
          "Actual MIDI encoding failure preserves output/model and never claims a completed omission count")
    # Real saved-recipe service: retain the full imported payload instead of
    # silently losing it at profile save/load. Core tests also reopen a file.
    source_note = {"id": 987654321, "channel": channel, "beat": 1.5, "length": .375,
                   "pitch": 67, "velocity": .5, "step": True, "muted": True,
                   "pan": -.375, "fineCents": 37.5}
    import_schema = data("vibera_commands", {"command": "notes.import", "includeSchemas": True})["operations"][0]["arguments"]
    payload_schema = import_schema["properties"]["recordedNotes"]
    check(payload_schema["minItems"] == 1 and
          payload_schema["items"]["properties"]["channel"]["maximum"] == 1000000000,
          "Imported note discovery describes the actual nonempty and exact identity bounds")
    before_invalid = notes(); invalid_revision = state()["revision"]
    for payload in ([], [dict(source_note, channel=1000000001)]):
        refused = client.call("vibera_command", {"command": "notes.import", "args": {
            "pattern": pattern, "channel": other, "recordedNotes": payload}}, allow_failure=True)
        check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "invalid_arguments" and
              notes() == before_invalid and state()["revision"] == invalid_revision,
              "Invalid import payload is refused by the same typed bounds without model/history changes")
    recipe = {"id": "macro.expression-import-acceptance", "name": "Import preserved expression",
              "steps": [{"action": "notes.import", "currentContext": False,
                         "parameters": {"pattern": pattern, "channel": other, "enabled": True,
                                        "recordedNotes": [source_note]}}]}
    macro_before = data("vibera_macros", {"action": "state"})
    created = data("vibera_macros", {"action": "create", "generation": macro_before["generation"],
                                   "macro": recipe})
    check(created["completionState"] == "completed" and created["profileVersion"] == 12 and
          created["profileMatchesMemory"] and created["configurationCommitted"],
          "Imported-expression macro is actually persisted in compatible profile12")
    readback = data("vibera_macros", {"action": "state", "id": recipe["id"]})
    stored = readback["macros"][0]
    check(stored["steps"][0]["parameters"]["recordedNotes"] == [source_note],
          "Saved macro readback preserves all source note fields and units")
    unchanged = {k: stored[k] for k in ("id", "name", "steps")}
    updated = data("vibera_macros", {"action": "update", "generation": readback["generation"],
                                   "macro": unchanged})
    check(updated["completionState"] == "completed" and updated["profileMatchesMemory"],
          "Full source metadata readback can be submitted unchanged through canonical validation")
    before_import = notes()
    current = data("vibera_macros", {"action": "state"})
    ran = data("vibera_macros", {"action": "run", "generation": current["generation"], "id": recipe["id"]})
    imported = [n for n in notes() if n["channel"] == other]
    check(ran["completionState"] == "completed" and len(imported) == 1 and
          all(imported[0][key] == source_note[key] for key in
              ("beat", "length", "pitch", "velocity", "muted", "pan", "fineCents")) and
          imported[0]["id"] != source_note["id"] and not imported[0]["step"],
          "Saved import macro allocates a fresh target note and preserves expression/activation")
    after_import = notes()
    command("edit.undo"); check(notes() == before_import, "Import macro has one exact Undo")
    command("edit.redo"); check(notes() == after_import, "Import macro has exact Redo/source identity")
    command("edit.undo")
    current = data("vibera_macros", {"action": "state"})
    removed = data("vibera_macros", {"action": "remove", "generation": current["generation"], "id": recipe["id"]})
    check(removed["profileVersion"] == macro_before["profileVersion"] and
          [m["id"] for m in removed["macros"]] == [m["id"] for m in macro_before["macros"]],
          "Disposable import macro removal restores the previous profile compatibility level")
    return {"checks": checks, "project": str(project), "nativeInput": False, "listening": False,
            "realPluginSupported": False, "pcmExported": True}

def verify_step_shift(client, output):
    """Actual semantic Shift workflow; no FL-runtime or hardware/listening claim."""
    import xml.etree.ElementTree as ET
    output = Path(output).resolve()
    admitted = (root / "build/windows/step-shift-mcp").resolve()
    if output.parent != admitted or not output.name.startswith("attempt-"):
        raise ValueError("Fresh owned Shift attempt required")
    output.mkdir(parents=True, exist_ok=False)
    checks = []
    def check(ok, label):
        if not ok: raise AssertionError(label)
        checks.append(label)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", {"command": name, "args": args})
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    current = state(); pattern = current["selectedPattern"]
    channel, other = [c["id"] for c in current["channels"][:2]]
    data("vibera_rack", {"action": "select", "ids": [channel], "activeChannel": channel})
    notes = lambda: data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]
    command("pattern.length", pattern=pattern, length=4)
    command("clip.add", pattern=pattern, beat=0, length=4)
    for beat in [0, .25, .5, .75]:
        command("steps.set", pattern=pattern, channel=channel, beat=beat, enabled=True)
    ordinary_ids = [n["id"] for n in notes()]
    command("notes.add", pattern=pattern, channel=channel, beat=.25, length=.5, pitch=67, value=0, step=True)
    chord = next(n["id"] for n in notes() if n["id"] not in ordinary_ids)
    command("notes.mute", pattern=pattern, channel=channel, ids=[chord])
    ids = ordinary_ids + [chord]
    for note_id, value in zip(ids, [0, 25, 50, 100, 25]):
        command("steps.shift", pattern=pattern, channel=channel, ids=[note_id], value=value)
    baseline = notes()
    baseline_fields = [{k: v for k, v in n.items() if k not in {"shiftPercent", "shiftDelayBeats", "playbackBeat"}} for n in baseline]
    def literal(values):
        actual = {n["id"]: n for n in notes()}
        check(len(actual) == len(ids) and all(abs(actual[i]["shiftPercent"] - v) < 1.e-10 for i, v in zip(ids, values)) and
              [{k: v for k, v in actual[i].items() if k not in {"shiftPercent", "shiftDelayBeats", "playbackBeat"}} for i in ids] == baseline_fields and
              all(abs(actual[i]["playbackBeat"] - b) < 1.e-12 for i, b in zip(ids, [n["beat"] + v * .002475 for n, v in zip(baseline, values)])),
              "Literal Shift percentages retain raw beat/ID/chord/muted/zero and all unrelated source fields")
    contract = data("vibera_commands", {"command": "steps.shift", "includeSchemas": True})["operations"][0]
    properties = contract["arguments"]["properties"]
    check(contract["supported"] and contract["arguments"]["additionalProperties"] is False and
          properties["value"]["minimum"] == 0 and properties["value"]["maximum"] == 100 and
          properties["value"]["default"] == 0 and properties["offset"]["maximum"] == 100 and
          properties["text"]["enum"] == ["set", "reset", "scale", "ramp"],
          "Actual Shift discovery uses displayed percent0..100 and the existing four canonical modes")
    graph = data("vibera_rack", {"action": "configure", "graphVisible": True, "graphProperty": "shift"})["graph"]
    by_id = {n["id"]: n for n in graph["notes"]}
    check(graph["property"] == "shift" and graph["units"] == "percent" and graph["minimum"] == 0 and
          graph["maximum"] == 100 and graph["defaultValue"] == 0 and graph["supported"] and
          graph["applicationAcknowledged"] is None and
          all(abs(by_id[i]["shiftDelayBeats"] - d) < 1.e-12 for i, d in zip(ids, [0, .061875, .12375, .2475, .061875])),
          "Actual graph readback distinguishes displayed Shift from literal physical delay and adoption")
    main_rows = {n["id"]: n for n in state()["notes"]}
    check(all(main_rows[i]["shiftPercent"] == by_id[i]["shiftPercent"] and
              abs(main_rows[i]["shiftDelayBeats"] - by_id[i]["shiftDelayBeats"]) < 1.e-12 and
              abs(main_rows[i]["playbackBeat"] - next(n for n in baseline if n["id"] == i)["playbackBeat"]) < 1.e-12
              for i in ids), "Main state and inspector agree on source Shift and projected onset")
    def begin(domain="stepGraph"):
        return data("vibera_gesture", {"action": "begin", "domain": domain})["operation"]
    def update(token, **args):
        return data("vibera_gesture", {"action": "update", "operation": token, "command": "steps.shift",
                    "args": {"pattern": pattern, "channel": channel, **args}})
    def cancel(token):
        check(data("vibera_gesture", {"action": "cancel", "operation": token})["completionState"] == "cancelled" and
              notes() == baseline, "Shift lease Cancel restores the exact full source baseline")
    token = begin()
    check(update(token, text="scale", target=ids[0], value=25)["completionState"] == "pending", "Shift scale preview remains pending")
    literal([25, 50, 75, 100, 50])
    update(token, text="scale", target=ids[0], value=50); literal([50, 75, 100, 100, 75]); cancel(token)
    token = begin()
    update(token, text="scale", target=ids[1], value=50); literal([0, 50, 100, 100, 50])
    update(token, text="scale", target=ids[1], value=12.5); literal([0, 12.5, 25, 50, 12.5]); cancel(token)
    token = begin()
    update(token, text="ramp", beat=.75, length=-.75, value=100, offset=0); literal([0, 100/3, 200/3, 100, 100/3])
    update(token, text="ramp", beat=.75, length=-.25, value=100, offset=25); literal([0, 25, 25, 100, 25])
    refused = client.call("vibera_gesture", {"action": "update", "operation": token, "command": "steps.shift",
        "args": {"pattern": pattern, "channel": other, "value": 0}}, allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain", "Shift lease refuses foreign Channel")
    cancel(token)
    command("steps.shift", pattern=pattern, channel=channel, text="reset"); literal([0, 0, 0, 0, 0])
    command("edit.undo"); check(notes() == baseline, "Shift reset one exact Undo")
    command("edit.redo"); literal([0, 0, 0, 0, 0]); command("edit.undo")
    token = begin(); update(token, value=50)
    committed = data("vibera_gesture", {"action": "commit", "operation": token})
    check(committed["completionState"] == "completed", "Shift gesture Commit produces actual completed history")
    command("edit.undo"); check(notes() == baseline, "Committed Shift lease has one exact Undo")
    command("edit.redo"); literal([50, 50, 50, 50, 50]); command("edit.undo")
    for invalid in [{"value": -1}, {"value": 101}, {"value": True}, {"value": 50, "ids": [ids[0], ids[0]]},
                    {"value": 50, "ids": [999999999]}, {"text": "ramp", "beat": .1, "length": .25, "value": 0, "offset": 100}]:
        prior = state()
        refused = client.call("vibera_command", {"command": "steps.shift", "args": {"pattern": pattern, "channel": channel, **invalid}}, allow_failure=True)
        after = state()
        check(refused.get("isError") and refused["structuredContent"]["error"]["code"] and notes() == baseline and
              all(after[k] == prior[k] for k in ["revision", "undoAvailable", "redoAvailable"]), "Invalid Shift leaves score/revision/history exact")
    token = begin("stepVelocity")
    refused = client.call("vibera_gesture", {"action": "update", "operation": token, "command": "steps.shift",
        "args": {"pattern": pattern, "channel": channel, "value": 50}}, allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain", "Legacy stepVelocity lease remains velocity-only")
    cancel(token)
    command("transport.record"); token = begin(); update(token, value=50)
    check(state()["recordArmed"], "Actual stepGraph Shift preview preserves recording arm")
    cancel(token); command("transport.record")
    check(not state()["recordArmed"], "Shift armed cancellation releases no invented physical take")
    saved_path = output / "shift.vibra"
    client.wait_job(data("vibera_project", {"action": "save", "path": str(saved_path), "overwrite": False}))
    tree = ET.parse(saved_path)
    note_attributes = [n.attrib for n in tree.findall("./PATTERNS/PATTERN/NOTE")]
    check(int(tree.getroot().get("version")) == project_schema(root) and len(note_attributes) == 5 and
          "shiftPercent" not in next(n for n in note_attributes if int(n["id"]) == ids[0]) and
          all("shiftPercent" in n for n in note_attributes if int(n["id"]) != ids[0]), "Current schema retains sparse neutral Shift semantics")
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    client.wait_job(data("vibera_project", {"action": "open", "path": str(saved_path), "discardChanges": True}))
    check(notes() == baseline, "Actual save/New/reopen preserves all raw Shift source fields and IDs")
    data("vibera_rack", {"action": "select", "ids": [channel], "activeChannel": channel})
    # Independent melodic timing reference: no swing, tails or window-boundary claim.
    command("notes.delete", pattern=pattern, ids=ids)
    for beat, value in [(0, 0), (.5, 50), (1, 100), (1.5, 25)]:
        command("steps.set", pattern=pattern, channel=channel, beat=beat, enabled=True)
        note_id = next(n["id"] for n in notes() if n["beat"] == beat)
        command("steps.shift", pattern=pattern, channel=channel, ids=[note_id], value=value)
    def render(name):
        path = output / name
        check(not path.exists(), "Fresh owned Shift WAV target")
        job = client.wait_job(data("vibera_project", {"action": "export", "path": str(path), "overwrite": False}))
        with wave.open(str(path), "rb") as wav:
            check(job["completionState"] == "completed" and (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000), "Actual Shift WAV reaches terminal and has declared format")
            return wav.readframes(wav.getnframes())
    shifted = render("shifted.wav")
    shifted_notes = notes()
    command("notes.delete", pattern=pattern, ids=[n["id"] for n in shifted_notes])
    for beat in [0, .62375, 1.2475, 1.561875]:
        command("notes.add", pattern=pattern, channel=channel, beat=beat, length=.25, pitch=60, value=.8)
    check(render("literal-melodic.wav") == shifted and any(shifted), "Actual Shift WAV equals independently authored literal melodic onsets with unchanged durations")
    for _ in range(5): command("edit.undo")
    check(notes() == shifted_notes, "Independent PCM construction restores original raw grid/IDs/Shift exactly")
    command("notes.delete", pattern=pattern, ids=[n["id"] for n in shifted_notes])
    command("steps.set", pattern=pattern, channel=channel, beat=0, enabled=True)
    command("steps.shift", pattern=pattern, channel=channel, value=50)
    data("vibera_select", {"pattern": pattern, "channel": channel, "scope": 2})
    midi_path = output / "shift-50.mid"
    midi = client.wait_job(data("vibera_project", {"action": "exportMidi", "path": str(midi_path), "overwrite": False}))
    raw = midi_path.read_bytes()
    check(midi["completionState"] == "completed" and midi["expressionNotesOmitted"] == 0 and
          raw[:4] == b"MThd" and int.from_bytes(raw[12:14], "big") == 960, "Actual MIDI export encodes Shift timing at960PPQ without an expression-omission claim")
    events = midi_note_events(raw, check)
    check(events == [(119, 9, 60, 102), (359, 8, 60, 0)], "Decoded Shift50 MIDI has independent literal119-on/359-off ticks and unchanged pitch/velocity")
    # A real worker-side encoding failure must retain existing task-owned bytes.
    data("vibera_select", {"pattern": pattern, "channel": other, "scope": 2})
    failed_path = output / "failed-shift.mid"; marker = b"owned-shift-output-before-failure"
    check(not failed_path.exists(), "Fresh owned failed Shift MIDI destination")
    failed_path.write_bytes(marker); before_failure = notes(); before_revision = state()["revision"]
    result = client.call("vibera_project", {"action": "exportMidi", "path": str(failed_path), "overwrite": True}, allow_failure=True)
    job = result.get("structuredContent", {})
    check(not result.get("isError") and job.get("completionState") == "pending", "Empty-part Shift MIDI export initially reports actual pending admission")
    deadline = time.monotonic() + 20
    while job.get("completionState") == "pending" and time.monotonic() < deadline:
        time.sleep(.03)
        result = client.call("vibera_project", {"action": "job", "operation": job["operation"]}, allow_failure=True)
        job = result.get("structuredContent", {})
    check(job.get("completionState") == "failed" and job.get("error") and job.get("expressionNotesOmitted") is None and
          failed_path.read_bytes() == marker and notes() == before_failure and state()["revision"] == before_revision,
          "Actual asynchronous failed MIDI export preserves original bytes/model and never claims success")
    source = {"id": 987654321, "channel": channel, "beat": 2, "length": .25, "pitch": 67, "velocity": .5,
              "step": True, "muted": True, "pan": 0, "fineCents": 0, "shiftPercent": 50}
    recipe = {"id": "macro.shift-import-acceptance", "name": "Import retained Shift", "steps": [{"action": "notes.import", "currentContext": False,
        "parameters": {"pattern": pattern, "channel": other, "enabled": True, "recordedNotes": [source]}}]}
    previous = data("vibera_macros", {"action": "state"})
    saved = data("vibera_macros", {"action": "create", "generation": previous["generation"], "macro": recipe})
    check(saved["profileVersion"] == 13 and saved["configurationCommitted"] and saved["profileMatchesMemory"], "Nonzero imported Shift uses genuinely saved profile13")
    readback = data("vibera_macros", {"action": "state", "id": recipe["id"]})
    check(readback["macros"][0]["steps"][0]["parameters"]["recordedNotes"] == [source], "Saved profile13 preserves the complete Shift source payload")
    ran = data("vibera_macros", {"action": "run", "generation": readback["generation"], "id": recipe["id"]})
    imported = [n for n in notes() if n["channel"] == other]
    check(ran["completionState"] == "completed" and len(imported) == 1 and imported[0]["shiftPercent"] == 50 and
          imported[0]["id"] != source["id"] and imported[0]["muted"] and not imported[0]["step"], "Actual saved macro imports Shift after melodic conversion with a fresh runtime ID")
    command("edit.undo"); check(notes() == before_failure, "Shift import macro is one exact Undo")
    recipe["steps"][0]["parameters"]["recordedNotes"][0]["shiftPercent"] = 0
    current = data("vibera_macros", {"action": "state"})
    neutral = data("vibera_macros", {"action": "update", "generation": current["generation"], "macro": recipe})
    neutral_recipe = data("vibera_macros", {"action": "state", "id": recipe["id"]})
    stored = neutral_recipe["macros"][0]["steps"][0]["parameters"]["recordedNotes"][0]
    check(neutral["profileVersion"] == 12 and "shiftPercent" not in stored and len(stored) == 10, "Neutral imported Shift preserves old profile12 and its ten source fields")
    current = data("vibera_macros", {"action": "state"})
    removed = data("vibera_macros", {"action": "remove", "generation": current["generation"], "id": recipe["id"]})
    check(removed["profileVersion"] == previous["profileVersion"] and
          [m["id"] for m in removed["macros"]] == [m["id"] for m in previous["macros"]], "Removing disposable Shift recipe restores prior profile compatibility")
    native_creation = data("vibera_synth", {"action": "create"})
    native = native_creation["channel"]
    deadline = time.monotonic() + 20
    native_rows = []
    while time.monotonic() < deadline:
        native_rows = [row for row in data("vibera_plugin", {"action": "list"})["items"] if row["id"] == native_creation["plugin"]]
        if not state()["pendingAudioUpdate"] and len(native_rows) == 1 and native_rows[0]["available"]: break
        time.sleep(.03)
    check(not state()["pendingAudioUpdate"] and len(native_rows) == 1 and native_rows[0]["available"], "Actual native Shift fixture reaches its existing prepared-worker availability boundary")
    data("vibera_rack", {"action": "select", "ids": [native], "activeChannel": native})
    command("steps.set", pattern=pattern, channel=native, beat=0, enabled=True)
    command("steps.shift", pattern=pattern, channel=native, value=50)
    graph = data("vibera_rack", {"action": "configure", "graphProperty": "shift", "graphVisible": True})["graph"]
    check(graph["supported"] and graph["editable"] and not graph["localSamplerTarget"] and
          "shift" in graph["supportedProperties"] and "pan" not in graph["supportedProperties"] and
          any(n["channel"] == native and n["shiftPercent"] == 50 for n in notes()), "Actual native instrument Shift is supported timing without advertising native per-note pan")
    return {"checks": checks, "project": str(saved_path), "independentPcmExact": True, "midiTicks": [119, 359],
            "nativeInput": False, "physicalMidi": False, "listening": False, "flRuntimePolicyObserved": False}


def verify_step_repeat(client, output):
    """Actual bounded Repeat workflow; original duration policy, not FL parity."""
    import xml.etree.ElementTree as ET
    output = Path(output).resolve()
    admitted = (root / "build/windows/step-repeat-mcp").resolve()
    if output.parent != admitted or not output.name.startswith("attempt-"):
        raise ValueError("Fresh owned Repeat attempt required")
    output.mkdir(parents=True, exist_ok=False)
    checks = []
    def check(ok, label):
        if not ok: raise AssertionError(label)
        checks.append(label)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", {"command": name, "args": args})
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    current = state(); pattern = current["selectedPattern"]
    channel, other = [c["id"] for c in current["channels"][:2]]
    data("vibera_rack", {"action": "select", "ids": [channel], "activeChannel": channel})
    notes = lambda: data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]
    command("pattern.length", pattern=pattern, length=4)
    command("clip.add", pattern=pattern, beat=0, length=4)
    for beat in [0, .25, .5, .75]:
        command("steps.set", pattern=pattern, channel=channel, beat=beat, enabled=True)
    ids = [n["id"] for n in notes()]
    command("notes.add", pattern=pattern, channel=channel, beat=.25, length=.5, pitch=67, value=0, step=True)
    chord = next(n["id"] for n in notes() if n["id"] not in ids); ids.append(chord)
    command("notes.mute", pattern=pattern, channel=channel, ids=[chord])
    command("steps.set", pattern=pattern, channel=other, beat=0, enabled=True)
    foreign = next(n["id"] for n in notes() if n["channel"] == other)
    for note_id, value in zip(ids, [0, 4, 10, 14, 4]):
        command("steps.repeat", pattern=pattern, channel=channel, ids=[note_id], value=value)
    baseline = notes()
    ignored = {"repeatMode", "repeatRate", "repeatIntervalBeats"}
    stable_fields = [{k: v for k, v in n.items() if k not in ignored} for n in baseline]
    def literal(values):
        actual = notes(); by_id = {n["id"]: n for n in actual}
        check(len(actual) == len(baseline) and all(by_id[i]["repeatMode"] == v for i, v in zip(ids, values)) and
              by_id[foreign]["repeatMode"] == 0 and
              [{k: v for k, v in n.items() if k not in ignored} for n in actual] == stable_fields,
              "Literal Repeat indices retain source count/geometry/IDs/chords/inactive and foreign fields")
    contract = data("vibera_commands", {"command": "steps.repeat", "includeSchemas": True})["operations"][0]
    props = contract["arguments"]["properties"]
    check(contract["supported"] and contract["arguments"]["additionalProperties"] is False and
          all(props[k]["type"] == "integer" and props[k]["minimum"] == 0 and props[k]["maximum"] == 14 for k in ["value", "offset"]) and
          props["value"]["default"] == 0 and props["text"]["enum"] == ["set", "reset", "scale", "ramp"], "Repeat discovery uses rate indices0..14, not stored trigger count")
    graph = data("vibera_rack", {"action": "configure", "graphVisible": True, "graphProperty": "repeat"})["graph"]
    rows = {n["id"]: n for n in graph["notes"]}
    check(graph["property"] == "repeat" and graph["minimum"] == 0 and graph["maximum"] == 14 and graph["defaultValue"] == 0 and
          graph["supported"] and graph["applicationAcknowledged"] is None and
          all(rows[i]["repeatMode"] == m and rows[i]["repeatRate"] == label and abs(rows[i]["repeatIntervalBeats"] - interval) < 1.e-12
              for i, m, label, interval in zip(ids, [0, 4, 10, 14, 4], ["Off", "1/8", "1/32", "1/64 triplet", "1/8"], [0, .5, .125, 1/24, .5])),
          "Actual Repeat graph exposes source mode/rhythmic label/approximate triplet interval without DSP ACK")
    def begin(domain="stepGraph"):
        return data("vibera_gesture", {"action": "begin", "domain": domain})["operation"]
    def update(token, **args):
        return data("vibera_gesture", {"action": "update", "operation": token, "command": "steps.repeat",
                    "args": {"pattern": pattern, "channel": channel, **args}})
    def cancel(token):
        check(data("vibera_gesture", {"action": "cancel", "operation": token})["completionState"] == "cancelled" and
              notes() == baseline, "Repeat Cancel restores exact source instead of retaining synthetic copies")
    token = begin()
    check(update(token, text="scale", target=ids[1], value=6)["completionState"] == "pending", "Repeat additive scale remains an actual pending preview")
    literal([2, 6, 12, 14, 6])
    update(token, text="scale", target=ids[1], value=2); literal([0, 2, 8, 12, 2]); cancel(token)
    token = begin(); update(token, text="scale", target=ids[0], value=3); literal([3, 7, 13, 14, 7])
    update(token, text="scale", target=ids[0], value=0); literal([0, 4, 10, 14, 4]); cancel(token)
    token = begin()
    update(token, text="ramp", beat=.75, length=-.75, value=14, offset=0); literal([0, 5, 9, 14, 5])
    update(token, text="ramp", beat=.75, length=-.25, value=14, offset=7); literal([0, 4, 7, 14, 4])
    refused = client.call("vibera_gesture", {"action": "update", "operation": token, "command": "steps.repeat",
        "args": {"pattern": pattern, "channel": other, "value": 10}}, allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain", "Repeat lease refuses retargeting a foreign Channel")
    cancel(token)
    command("steps.repeat", pattern=pattern, channel=channel, text="reset"); literal([0, 0, 0, 0, 0])
    command("edit.undo"); check(notes() == baseline, "Repeat reset is one exact Undo")
    command("edit.redo"); literal([0, 0, 0, 0, 0]); command("edit.undo")
    token = begin(); update(token, value=10)
    check(data("vibera_gesture", {"action": "commit", "operation": token})["completionState"] == "completed", "Repeat Commit produces real completed history")
    command("edit.undo"); check(notes() == baseline, "Repeat lease is one exact Undo")
    command("edit.redo"); literal([10, 10, 10, 10, 10]); command("edit.undo")
    for invalid in [{"value": -1}, {"value": 15}, {"value": 10.5}, {"value": True}, {"text": "ramp", "value": 0, "offset": 1.5},
                    {"value": 10, "ids": [ids[0], ids[0]]}, {"value": 10, "ids": [foreign]}]:
        prior = state()
        refused = client.call("vibera_command", {"command": "steps.repeat", "args": {"pattern": pattern, "channel": channel, **invalid}}, allow_failure=True)
        after = state()
        check(refused.get("isError") and refused["structuredContent"]["error"]["code"] and notes() == baseline and
              all(after[k] == prior[k] for k in ["revision", "undoAvailable", "redoAvailable"]), "Invalid Repeat mode/fraction/foreign ID refuses without source/history mutation")
    token = begin("stepVelocity")
    refused = client.call("vibera_gesture", {"action": "update", "operation": token, "command": "steps.repeat",
        "args": {"pattern": pattern, "channel": channel, "value": 10}}, allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain", "Legacy stepVelocity domain does not admit Repeat")
    cancel(token)
    command("transport.record"); token = begin(); update(token, value=10)
    check(state()["recordArmed"], "Actual Repeat stepGraph preview preserves recording arm")
    cancel(token); command("transport.record")
    check(not state()["recordArmed"] and notes() == baseline, "Repeat armed cancellation restores source without inventing a physical take")
    saved_path = output / "repeat.vibra"
    client.wait_job(data("vibera_project", {"action": "save", "path": str(saved_path), "overwrite": False}))
    tree = ET.parse(saved_path); attributes = tree.findall("./PATTERNS/PATTERN/NOTE")
    check(int(tree.getroot().get("version")) == project_schema(root) and len(attributes) == 6 and
          all(("repeatMode" in n.attrib) == (int(n.get("id")) in ids[1:]) for n in attributes), "Actual schema stores Repeat sparsely without expanding persisted notes")
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    client.wait_job(data("vibera_project", {"action": "open", "path": str(saved_path), "discardChanges": True}))
    check(notes() == baseline, "Actual Repeat save/New/reopen retains all source data and identities")
    data("vibera_rack", {"action": "select", "ids": [channel], "activeChannel": channel})
    def terminal(args):
        response = client.call("vibera_project", args, allow_failure=True)
        job = response.get("structuredContent", {}); admission = job.get("completionState")
        deadline = time.monotonic() + 20
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.03)
            response = client.call("vibera_project", {"action": "job", "operation": job["operation"]}, allow_failure=True)
            job = response.get("structuredContent", {})
        return admission, job, bool(response.get("isError"))
    for n in attributes: n.attrib.pop("repeatMode", None)
    for sampler in tree.getroot().iter("SAMPLER"):
        for key in ("filterType", "filterCutoff", "filterResonance"):
            sampler.attrib.pop(key, None)
        modulation = sampler.find("MODULATION")
        if modulation is not None:
            sampler.remove(modulation)
    attributes[0].set("repeatMode", "0"); tree.getroot().set("version", "32")
    smuggled = output / "old-schema-neutral-repeat.vibra"
    tree.write(smuggled, encoding="utf-8", xml_declaration=True)
    admission, failed, is_error = terminal({"action": "open", "path": str(smuggled), "discardChanges": True})
    check((failed.get("completionState") == "failed" or is_error) and failed.get("error") and notes() == baseline,
          "Older schema cannot smuggle even neutral Repeat or replace the current musical source")
    command("notes.delete", pattern=pattern, ids=[n["id"] for n in baseline])
    command("steps.set", pattern=pattern, channel=channel, beat=0, enabled=True)
    def render(name):
        path = output / name
        check(not path.exists(), "Fresh owned Repeat WAV destination")
        job = client.wait_job(data("vibera_project", {"action": "export", "path": str(path), "overwrite": False}))
        with wave.open(str(path), "rb") as wav:
            check(job["completionState"] == "completed" and (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000), "Actual Repeat WAV terminal outcome and format")
            return wav.readframes(wav.getnframes())
    # Literal melodic references use existing one-shot/choke behavior on both sides;
    # no swing, clipping, third-party, or FL duration-policy equivalence is asserted.
    for mode, beats, length in [(10, [0, .125], .125), (13, [0, .0625, .125, .1875], .0625),
                                (14, [0, 1/24, 1/12, 1/8, 1/6, 5/24], 1/24)]:
        command("steps.repeat", pattern=pattern, channel=channel, value=mode)
        source = notes()
        actual = render("repeat-mode-" + str(mode) + ".wav")
        command("notes.delete", pattern=pattern, ids=[source[0]["id"]])
        for beat in beats:
            command("notes.add", pattern=pattern, channel=channel, beat=beat, length=length, pitch=60, value=.8)
        check(render("literal-mode-" + str(mode) + ".wav") == actual and any(actual), "Actual Repeat mode" + str(mode) + " WAV equals the independently authored2/4/6-trigger melodic score")
        for _ in range(len(beats) + 1): command("edit.undo")
        check(notes() == source, "Literal Repeat reference construction restores the one original raw source")
    data("vibera_select", {"pattern": pattern, "channel": channel, "scope": 2})
    midi_path = output / "repeat-mode-14.mid"
    midi = client.wait_job(data("vibera_project", {"action": "exportMidi", "path": str(midi_path), "overwrite": False}))
    raw = midi_path.read_bytes()
    check(midi["completionState"] == "completed" and midi["expressionNotesOmitted"] == 0 and raw[:4] == b"MThd" and
          int.from_bytes(raw[12:14], "big") == 960, "Repeat MIDI export encodes timing without an unsupported-expression omission claim")
    expected = [(0, 9, 60, 102)]
    for tick in [40, 80, 120, 160, 200]: expected.extend([(tick, 8, 60, 0), (tick, 9, 60, 102)])
    expected.append((240, 8, 60, 0))
    check(midi_note_events(raw, check) == expected, "Decoded960PPQ Repeat14 starts0/40/80/120/160/200, ends240 and orders shared-tick offs before ons")
    data("vibera_select", {"pattern": pattern, "channel": other, "scope": 2})
    blocked = output / "failed-repeat.mid"; marker = b"owned-repeat-output-before-failure"
    check(not blocked.exists(), "Fresh owned failed Repeat export fixture")
    blocked.write_bytes(marker); before_failure = notes(); revision = state()["revision"]
    admission, failed, is_error = terminal({"action": "exportMidi", "path": str(blocked), "overwrite": True})
    check(admission == "pending" and failed.get("completionState") == "failed" and failed.get("error") and
          failed.get("expressionNotesOmitted") is None and blocked.read_bytes() == marker and
          notes() == before_failure and state()["revision"] == revision, "Actual asynchronous failed Repeat export retains old bytes/source and never reports successful encoding")
    payload = {"id": 987654321, "channel": channel, "beat": 2, "length": .25, "pitch": 67, "velocity": .5,
               "step": True, "muted": True, "pan": 0, "fineCents": 0, "shiftPercent": 50, "repeatMode": 14}
    recipe = {"id": "macro.repeat-import-acceptance", "name": "Import retained Repeat", "steps": [{"action": "notes.import", "currentContext": False,
        "parameters": {"pattern": pattern, "channel": other, "enabled": True, "recordedNotes": [payload]}}]}
    previous = data("vibera_macros", {"action": "state"})
    created = data("vibera_macros", {"action": "create", "generation": previous["generation"], "macro": recipe})
    check(created["profileVersion"] == 14 and created["configurationCommitted"] and created["profileMatchesMemory"], "Nonzero imported Repeat reaches actual saved profile14")
    recipe_state = data("vibera_macros", {"action": "state", "id": recipe["id"]})
    check(recipe_state["macros"][0]["steps"][0]["parameters"]["recordedNotes"] == [payload], "Saved profile14 retains every source property and inactive Repeat/Shift")
    ran = data("vibera_macros", {"action": "run", "generation": recipe_state["generation"], "id": recipe["id"]})
    imported = [n for n in notes() if n["channel"] == other]
    check(ran["completionState"] == "completed" and len(imported) == 1 and imported[0]["repeatMode"] == 14 and
          imported[0]["shiftPercent"] == 50 and imported[0]["id"] != payload["id"] and imported[0]["muted"] and
          not imported[0]["step"] and len(notes()) == len(before_failure) + 1, "Real import macro allocates one fresh melodic source, retaining Repeat rather than persisting generated copies")
    command("edit.undo"); check(notes() == before_failure, "Repeat import macro one exact Undo")
    for shift, version, count in [(50, 13, 11), (0, 12, 10)]:
        payload["repeatMode"] = 0; payload["shiftPercent"] = shift
        current = data("vibera_macros", {"action": "state"})
        updated = data("vibera_macros", {"action": "update", "generation": current["generation"], "macro": recipe})
        recipe_state = data("vibera_macros", {"action": "state", "id": recipe["id"]})
        stored = recipe_state["macros"][0]["steps"][0]["parameters"]["recordedNotes"][0]
        check(updated["profileVersion"] == version and len(stored) == count and "repeatMode" not in stored and
              (stored.get("shiftPercent", 0) == shift), "Neutral imported Repeat restores compatible profile" + str(version) + " without newer neutral payload fields")
    current = data("vibera_macros", {"action": "state"})
    removed = data("vibera_macros", {"action": "remove", "generation": current["generation"], "id": recipe["id"]})
    check(removed["profileVersion"] == previous["profileVersion"] and
          [m["id"] for m in removed["macros"]] == [m["id"] for m in previous["macros"]], "Disposable Repeat recipe removal restores original saved macro compatibility")
    native_creation = data("vibera_synth", {"action": "create"}); native = native_creation["channel"]
    deadline = time.monotonic() + 20; native_rows = []
    while time.monotonic() < deadline:
        native_rows = [n for n in data("vibera_plugin", {"action": "list"})["items"] if n["id"] == native_creation["plugin"]]
        if not state()["pendingAudioUpdate"] and len(native_rows) == 1 and native_rows[0]["available"]: break
        time.sleep(.03)
    check(not state()["pendingAudioUpdate"] and len(native_rows) == 1 and native_rows[0]["available"], "Actual native Repeat fixture reaches prepared worker availability")
    data("vibera_rack", {"action": "select", "ids": [native], "activeChannel": native})
    command("steps.set", pattern=pattern, channel=native, beat=0, enabled=True)
    command("steps.repeat", pattern=pattern, channel=native, value=14)
    graph = data("vibera_rack", {"action": "configure", "graphProperty": "repeat", "graphVisible": True})["graph"]
    check(graph["supported"] and graph["editable"] and not graph["localSamplerTarget"] and graph["applicationAcknowledged"] is None and
          "repeat" in graph["supportedProperties"] and "pan" not in graph["supportedProperties"] and
          any(n["channel"] == native and n["repeatMode"] == 14 for n in notes()), "Actual native score Repeat admission does not advertise vendor per-note expression compatibility")
    # The actual native channel must refuse a new over-capacity score atomically.
    # Neutral chords remain legacy-compatible; only the changed Repeat admission is tested.
    for begin, end in [(0, 64), (64, 99)]:
        data("vibera_batch", {"commands": [{"command": "notes.add", "args": {
            "pattern": pattern, "channel": native, "beat": 0, "length": .25,
            "pitch": 20 + index, "value": .5, "step": True}} for index in range(begin, end)]})
    before_dense, dense_state = notes(), state()
    refused = client.call("vibera_command", {"command": "steps.repeat", "args": {
        "pattern": pattern, "channel": native, "value": 13}}, allow_failure=True)
    after_dense = state()
    (output / "dense-native-admission.json").write_text(json.dumps({
        "response": refused, "sourceUnchanged": notes() == before_dense,
        "before": {k: dense_state[k] for k in ["revision", "undoAvailable", "redoAvailable"]},
        "after": {k: after_dense[k] for k in ["revision", "undoAvailable", "redoAvailable"]}},
        indent=2) + "\n", encoding="utf-8")
    check(refused.get("isError") and "repeat.midiCapacity" in json.dumps(refused) and
          notes() == before_dense and all(after_dense[k] == dense_state[k] for k in
              ["revision", "undoAvailable", "redoAvailable"]),
          "Actual native dense Repeat refuses before source/history changes, without packet dropping or buffer changes")
    return {"checks": checks, "project": str(saved_path), "midiTicks": [0, 40, 80, 120, 160, 200], "midiLastOff": 240,
            "independentPcmModes": [10, 13, 14], "nativeInput": False, "listening": False, "flRuntimePolicyObserved": False}


def verify_step_release(client, output):
    """Actual Release source/export workflow; no universal audible response claim."""
    import xml.etree.ElementTree as ET
    output = Path(output).resolve()
    admitted = (root / "build/windows/step-release-mcp").resolve()
    if output.parent != admitted or not output.name.startswith("attempt-"):
        raise ValueError("Fresh owned Release attempt required")
    output.mkdir(parents=True, exist_ok=False)
    checks = []
    def check(ok, label):
        if not ok: raise AssertionError(label)
        checks.append(label)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", {"command": name, "args": args})
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    current = state(); pattern = current["selectedPattern"]
    channel, other = [c["id"] for c in current["channels"][:2]]
    data("vibera_rack", {"action": "select", "ids": [channel], "activeChannel": channel})
    notes = lambda: data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]
    command("pattern.length", pattern=pattern, length=4)
    command("clip.add", pattern=pattern, beat=0, length=4)
    for beat in [0, .25, .5, .75]:
        command("steps.set", pattern=pattern, channel=channel, beat=beat, enabled=True)
    ids = [n["id"] for n in notes()]
    command("notes.add", pattern=pattern, channel=channel, beat=.25, length=.5, pitch=67, value=0, step=True)
    chord = next(n["id"] for n in notes() if n["id"] not in ids); ids.append(chord)
    command("notes.mute", pattern=pattern, channel=channel, ids=[chord])
    command("steps.set", pattern=pattern, channel=other, beat=0, enabled=True)
    foreign = next(n["id"] for n in notes() if n["channel"] == other)
    for note_id, value in zip(ids, [0, .25, .5, 1, .25]):
        command("steps.release", pattern=pattern, channel=channel, ids=[note_id], value=value)
    baseline = notes()
    ignored = {"releaseVelocity", "releaseMidiVelocity"}
    stable_fields = [{k: v for k, v in n.items() if k not in ignored} for n in baseline]
    def literal(values):
        actual = notes(); by_id = {n["id"]: n for n in actual}
        check(len(actual) == len(baseline) and
              all(abs(by_id[i]["releaseVelocity"] - v) < 1.e-10 for i, v in zip(ids, values)) and
              by_id[foreign]["releaseVelocity"] == 0 and
              [{k: v for k, v in n.items() if k not in ignored} for n in actual] == stable_fields,
              "Literal Release values retain source count/IDs/chord/muted-zero and every sibling field")
    contract = data("vibera_commands", {"command": "steps.release", "includeSchemas": True})["operations"][0]
    props = contract["arguments"]["properties"]
    check(contract["supported"] and contract["arguments"]["additionalProperties"] is False and
          all(props[k]["type"] == "number" and props[k]["minimum"] == 0 and props[k]["maximum"] == 1
              for k in ["value", "offset"]) and props["value"]["default"] == 0 and
          props["text"]["enum"] == ["set", "reset", "scale", "ramp"],
          "Release discovery declares finite normalized note-off data and canonical edit modes")
    graph = data("vibera_rack", {"action": "configure", "graphVisible": True, "graphProperty": "release"})["graph"]
    graph_rows = {n["id"]: n for n in graph["notes"]}
    check(graph["property"] == "release" and graph["units"] == "normalized" and graph["minimum"] == 0 and
          graph["maximum"] == 1 and graph["defaultValue"] == 0 and graph["supported"] and graph["editable"] and
          graph["releaseVelocityAudioResponse"] == "ignored" and graph["midiExportEligible"] and
          not graph["hostedMidiEligible"] and not graph["appliedEligible"] and
          graph["applicationAcknowledged"] is None and graph["midiDeliveryAcknowledged"] is None and
          "Stored/export-only" in graph["applicationScope"] and
          [graph_rows[i]["releaseMidiVelocity"] for i in ids] == [0, 32, 64, 127, 32],
          "Actual local Graph exposes normalized/byte source values and honest ignored-audio/export-only support")
    main_rows = {n["id"]: n for n in state()["notes"]}
    check(all(main_rows[i]["releaseVelocity"] == graph_rows[i]["releaseVelocity"] and
              main_rows[i]["releaseMidiVelocity"] == graph_rows[i]["releaseMidiVelocity"] for i in ids),
          "Main state/inspector/Graph agree on Release source and rounded note-off values")
    def begin(domain="stepGraph"):
        return data("vibera_gesture", {"action": "begin", "domain": domain})["operation"]
    def update(token, **args):
        return data("vibera_gesture", {"action": "update", "operation": token, "command": "steps.release",
                    "args": {"pattern": pattern, "channel": channel, **args}})
    def cancel(token):
        check(data("vibera_gesture", {"action": "cancel", "operation": token})["completionState"] == "cancelled" and
              notes() == baseline, "Release Cancel restores the exact full raw source")
    token = begin()
    check(update(token, text="scale", target=ids[1], value=.5)["completionState"] == "pending",
          "Release proportional scaling is an actual pending preview")
    literal([0, .5, 1, 1, .5])
    update(token, text="scale", target=ids[1], value=.125); literal([0, .125, .25, .5, .125]); cancel(token)
    token = begin(); update(token, text="scale", target=ids[0], value=.2); literal([.2, .45, .7, 1, .45])
    update(token, text="scale", target=ids[0], value=0); literal([0, .25, .5, 1, .25]); cancel(token)
    token = begin()
    update(token, text="ramp", beat=.75, length=-.75, value=1, offset=0); literal([0, 1/3, 2/3, 1, 1/3])
    update(token, text="ramp", beat=.75, length=-.25, value=.8, offset=.2); literal([0, .25, .2, .8, .25])
    refused = client.call("vibera_gesture", {"action": "update", "operation": token, "command": "steps.release",
        "args": {"pattern": pattern, "channel": other, "value": .5}}, allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain",
          "Release lease cannot silently retarget another Channel")
    cancel(token)
    command("steps.release", pattern=pattern, channel=channel, text="ramp", beat=.25, length=0, value=.1, offset=.7)
    literal([0, .7, .5, 1, .7]); command("edit.undo"); check(notes() == baseline, "Release zero-span ramp exact Undo")
    command("steps.release", pattern=pattern, channel=channel, text="reset"); literal([0, 0, 0, 0, 0])
    command("edit.undo"); check(notes() == baseline, "Release reset one exact Undo")
    command("edit.redo"); literal([0, 0, 0, 0, 0]); command("edit.undo")
    token = begin(); update(token, value=.5)
    check(data("vibera_gesture", {"action": "commit", "operation": token})["completionState"] == "completed",
          "Release Commit creates real completed canonical history")
    command("edit.undo"); check(notes() == baseline, "Release gesture is one exact Undo")
    command("edit.redo"); literal([.5, .5, .5, .5, .5]); command("edit.undo")
    command("steps.release", pattern=pattern, channel=channel, ids=[ids[1]])
    literal([0, 0, .5, 1, .25]); command("edit.undo"); check(notes() == baseline, "Omitted Release value uses neutral0")
    for invalid in [{"value": -.1}, {"value": 1.1}, {"value": True}, {"value": "0.5"},
                    {"text": "ramp", "beat": 0, "length": .25, "value": .5, "offset": 1.1},
                    {"value": .5, "ids": [ids[0], ids[0]]}, {"value": .5, "ids": [foreign]},
                    {"value": .5, "ids": [999999999]}, {"value": .5, "unexpected": 1}]:
        prior = state()
        refused = client.call("vibera_command", {"command": "steps.release",
            "args": {"pattern": pattern, "channel": channel, **invalid}}, allow_failure=True)
        after = state()
        check(refused.get("isError") and refused["structuredContent"]["error"]["code"] and notes() == baseline and
              all(after[k] == prior[k] for k in ["revision", "undoAvailable", "redoAvailable"]),
              "Invalid Release value/ID/extra field refuses atomically with exact source/history")
    token = begin("stepVelocity")
    refused = client.call("vibera_gesture", {"action": "update", "operation": token, "command": "steps.release",
        "args": {"pattern": pattern, "channel": channel, "value": .5}}, allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain",
          "Legacy stepVelocity lease stays velocity-only with Release added")
    cancel(token)
    command("transport.record"); token = begin(); update(token, value=.5)
    check(state()["recordArmed"], "Actual Release source preview preserves recording arm")
    cancel(token); command("transport.record")
    check(not state()["recordArmed"] and notes() == baseline, "Release armed Cancel preserves source without a physical-take claim")
    saved_path = output / "release.vibra"
    client.wait_job(data("vibera_project", {"action": "save", "path": str(saved_path), "overwrite": False}))
    tree = ET.parse(saved_path); attributes = tree.findall("./PATTERNS/PATTERN/NOTE")
    check(int(tree.getroot().get("version")) == project_schema(root) and len(attributes) == 6 and
          all(("releaseVelocity" in n.attrib) == (int(n.get("id")) in ids[1:]) for n in attributes),
          "Actual current schema stores nonneutral Release sparsely without duplicating raw notes")
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    client.wait_job(data("vibera_project", {"action": "open", "path": str(saved_path), "discardChanges": True}))
    check(notes() == baseline, "Release save/New/reopen preserves all source fields and stable IDs")
    data("vibera_rack", {"action": "select", "ids": [channel], "activeChannel": channel})
    def terminal(args):
        response = client.call("vibera_project", args, allow_failure=True)
        job = response.get("structuredContent", {}); admission = job.get("completionState")
        deadline = time.monotonic() + 20
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.03)
            response = client.call("vibera_project", {"action": "job", "operation": job["operation"]}, allow_failure=True)
            job = response.get("structuredContent", {})
        return admission, job, bool(response.get("isError"))
    for n in attributes: n.attrib.pop("releaseVelocity", None)
    for sampler in tree.getroot().iter("SAMPLER"):
        for key in ("filterType", "filterCutoff", "filterResonance"):
            sampler.attrib.pop(key, None)
        modulation = sampler.find("MODULATION")
        if modulation is not None:
            sampler.remove(modulation)
    attributes[0].set("releaseVelocity", "0"); tree.getroot().set("version", "33")
    smuggled = output / "old-schema-neutral-release.vibra"
    tree.write(smuggled, encoding="utf-8", xml_declaration=True)
    admission, failed, is_error = terminal({"action": "open", "path": str(smuggled), "discardChanges": True})
    check((failed.get("completionState") == "failed" or is_error) and failed.get("error") and notes() == baseline,
          "Older schema33 cannot smuggle neutral Release or replace current source")
    command("notes.delete", pattern=pattern, ids=[n["id"] for n in baseline])
    command("steps.set", pattern=pattern, channel=channel, beat=0, enabled=True)
    def render(name):
        path = output / name
        job = client.wait_job(data("vibera_project", {"action": "export", "path": str(path), "overwrite": False}))
        with wave.open(str(path), "rb") as wav:
            check(job["completionState"] == "completed" and (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000),
                  "Actual local Release PCM export completes with the original format")
            return wav.readframes(wav.getnframes())
    neutral_pcm = render("release-local-neutral.wav")
    command("steps.release", pattern=pattern, channel=channel, value=.5)
    check(render("release-local-nonzero.wav") == neutral_pcm and any(neutral_pcm),
          "Actual local sampler audio remains identical for MIDI-only Release source data")
    data("vibera_select", {"pattern": pattern, "channel": channel, "scope": 2})
    def export_midi(name):
        path = output / name
        admitted_job = data("vibera_project", {"action": "exportMidi", "path": str(path), "overwrite": False})
        check(admitted_job["completionState"] == "pending" and admitted_job["expressionNotesOmitted"] is None,
              "MIDI export admission is pending, never successful encoding")
        done = client.wait_job(admitted_job); raw = path.read_bytes()
        check(done["completionState"] == "completed" and done["expressionNotesOmitted"] == 0 and raw[:4] == b"MThd" and
              int.from_bytes(raw[12:14], "big") == 960, "Release MIDI export terminal960PPQ encoding without expression omission")
        return midi_note_events(raw, check)
    check(export_midi("release-half.mid") == [(0, 9, 60, 102), (240, 8, 60, 64)],
          "Actual decoded MIDI note-off carries rounded Release .5 byte64")
    command("steps.repeat", pattern=pattern, channel=channel, value=13)
    expected = [(0, 9, 60, 102)]
    for tick in [60, 120, 180]: expected.extend([(tick, 8, 60, 64), (tick, 9, 60, 102)])
    expected.append((240, 8, 60, 64))
    check(export_midi("release-repeat-13.mid") == expected,
          "Actual Repeat13 exported off bytes all retain64 before same-tick next note-on")
    command("steps.repeat", pattern=pattern, channel=channel, text="reset")
    command("steps.release", pattern=pattern, channel=channel, text="reset")
    check(export_midi("release-neutral.mid") == [(0, 9, 60, 102), (240, 8, 60, 0)],
          "Legacy-neutral Release remains note-off byte0")
    data("vibera_select", {"pattern": pattern, "channel": other, "scope": 2})
    blocked = output / "failed-release.mid"; marker = b"owned-release-output-before-failure"
    check(not blocked.exists(), "Fresh owned failed Release MIDI destination")
    blocked.write_bytes(marker); before_failure = notes(); revision = state()["revision"]
    admission, failed, is_error = terminal({"action": "exportMidi", "path": str(blocked), "overwrite": True})
    failure_proof = {"admission": admission, "terminal": failed, "isError": is_error,
                     "targetSha256": hashlib.sha256(blocked.read_bytes()).hexdigest(),
                     "sourceUnchanged": notes() == before_failure, "revisionUnchanged": state()["revision"] == revision}
    (output / "failed-midi-export.json").write_text(json.dumps(failure_proof, indent=2) + "\n", encoding="utf-8")
    check(admission == "pending" and failed.get("completionState") == "failed" and failed.get("error") and
          failed.get("expressionNotesOmitted") is None and blocked.read_bytes() == marker and
          notes() == before_failure and state()["revision"] == revision,
          "Actual async failed Release MIDI export retains existing target/source and never reports successful encoding")
    payload = {"id": 987654321, "channel": channel, "beat": 2, "length": .25, "pitch": 67, "velocity": .5,
               "step": True, "muted": True, "pan": 0, "fineCents": 0, "shiftPercent": 50,
               "repeatMode": 14, "releaseVelocity": .5}
    recipe = {"id": "macro.release-import-acceptance", "name": "Import retained Release", "steps": [{"action": "notes.import", "currentContext": False,
        "parameters": {"pattern": pattern, "channel": other, "enabled": True, "recordedNotes": [payload]}}]}
    previous = data("vibera_macros", {"action": "state"})
    created = data("vibera_macros", {"action": "create", "generation": previous["generation"], "macro": recipe})
    check(created["profileVersion"] == 15 and created["configurationCommitted"] and created["profileMatchesMemory"],
          "Nonzero imported Release creates actually saved compatible profile15")
    recipe_state = data("vibera_macros", {"action": "state", "id": recipe["id"]})
    check(recipe_state["macros"][0]["steps"][0]["parameters"]["recordedNotes"] == [payload],
          "Saved profile15 retains exact Release/Repeat/Shift source payload")
    ran = data("vibera_macros", {"action": "run", "generation": recipe_state["generation"], "id": recipe["id"]})
    imported = [n for n in notes() if n["channel"] == other]
    check(ran["completionState"] == "completed" and len(imported) == 1 and imported[0]["releaseVelocity"] == .5 and
          imported[0]["releaseMidiVelocity"] == 64 and imported[0]["repeatMode"] == 14 and imported[0]["shiftPercent"] == 50 and
          imported[0]["id"] != payload["id"] and imported[0]["muted"] and not imported[0]["step"] and
          len(notes()) == len(before_failure) + 1, "Actual imported Release allocates one fresh melodic source and preserves retained properties")
    command("edit.undo"); check(notes() == before_failure, "Release import macro has one exact Undo")
    for repeat, shift, version, count in [(14, 50, 14, 12), (0, 50, 13, 11), (0, 0, 12, 10)]:
        payload["releaseVelocity"] = 0; payload["repeatMode"] = repeat; payload["shiftPercent"] = shift
        current = data("vibera_macros", {"action": "state"})
        updated = data("vibera_macros", {"action": "update", "generation": current["generation"], "macro": recipe})
        recipe_state = data("vibera_macros", {"action": "state", "id": recipe["id"]})
        stored = recipe_state["macros"][0]["steps"][0]["parameters"]["recordedNotes"][0]
        check(updated["profileVersion"] == version and len(stored) == count and "releaseVelocity" not in stored and
              stored.get("repeatMode", 0) == repeat and stored.get("shiftPercent", 0) == shift,
              "Neutral imported Release restores exact compatible profile" + str(version) + " without newer neutral fields")
    current = data("vibera_macros", {"action": "state"})
    removed = data("vibera_macros", {"action": "remove", "generation": current["generation"], "id": recipe["id"]})
    check(removed["profileVersion"] == previous["profileVersion"] and
          [m["id"] for m in removed["macros"]] == [m["id"] for m in previous["macros"]],
          "Disposable Release recipe removal restores original saved macro compatibility")
    native_creation = data("vibera_synth", {"action": "create"}); native = native_creation["channel"]
    deadline = time.monotonic() + 20; native_rows = []
    while time.monotonic() < deadline:
        native_rows = [n for n in data("vibera_plugin", {"action": "list"})["items"] if n["id"] == native_creation["plugin"]]
        if not state()["pendingAudioUpdate"] and len(native_rows) == 1 and native_rows[0]["available"]: break
        time.sleep(.03)
    check(not state()["pendingAudioUpdate"] and len(native_rows) == 1 and native_rows[0]["available"],
          "Actual owned Synth Release fixture reaches worker availability")
    data("vibera_rack", {"action": "select", "ids": [native], "activeChannel": native})
    command("steps.set", pattern=pattern, channel=native, beat=0, enabled=True)
    command("steps.release", pattern=pattern, channel=native, value=.5)
    graph = data("vibera_rack", {"action": "configure", "graphProperty": "release", "graphVisible": True})["graph"]
    check(graph["supported"] and graph["editable"] and graph["hostedMidiEligible"] and graph["midiExportEligible"] and
          graph["releaseVelocityAudioResponse"] == "ignored" and not graph["appliedEligible"] and
          graph["applicationAcknowledged"] is None and graph["midiDeliveryAcknowledged"] is None and
          "release" in graph["supportedProperties"] and "pan" not in graph["supportedProperties"],
          "Actual native Synth source/export is supported while its audio-ignore and unacknowledged delivery remain explicit")
    data("vibera_select", {"pattern": pattern, "channel": native, "scope": 2})
    check(export_midi("release-native-score.mid") == [(0, 9, 60, 102), (240, 8, 60, 64)],
          "Native-owned score MIDI export carries byte64 without claiming worker delivery or audible response")
    return {"checks": checks, "project": str(saved_path), "releaseMidiVelocity": 64, "repeatedOffVelocity": 64,
            "localAudioIgnoredPcmExact": True, "nativeSynthSourceWorkerAvailable": True,
            "hostedMidiDeliveryObserved": False, "thirdPartyAudioResponseObserved": False,
            "nativeInput": False, "physicalMidi": False, "hardware": False, "listening": False}


def verify_step_memory(client, output):
    root = Path(__file__).resolve().parents[1]
    output = Path(output).resolve()
    admitted_parent = (root / "build" / "windows" / "step-memory-mcp").resolve()
    if output.parent != admitted_parent or not output.name.startswith("attempt-") or not output.name[8:].isdigit():
        raise ValueError("Provide a fresh step-memory-mcp/attempt-<numeric identity> directory")
    assert isinstance(client.process.args, list) and client.process.args[1:] == ["--mcp-test"], "Owned disposable client required"
    # Refuse existing attempts, including an empty directory: never merge into
    # another run's fixtures or overwrite retained failure evidence.
    output.mkdir(parents=True, exist_ok=False)
    checks = []

    def check(ok, label):
        if not ok:
            raise AssertionError(label)
        checks.append(label)

    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", {"command": name, "args": args})
    rack = lambda: data("vibera_rack", {"action": "state"})
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    initial = state()
    check(initial["tempo"] == 120, "Independent four-second render oracle requires the empty project's120BPM default")
    pattern = initial["selectedPattern"]
    kick, snare = [row["id"] for row in initial["channels"][:2]]
    notes = lambda: data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]
    check(not notes(), "Disposable project really starts with an empty score")
    schema = data("vibera_commands", {"command": "steps.set", "includeSchemas": True})["operations"][0]
    check(schema["arguments"]["properties"]["retainSteps"]["type"] == "boolean", "Actual typed activation discovery")
    configured = data("vibera_rack", {"action": "configure", "muteRemovedSteps": True})
    check(configured["muteRemovedSteps"] and state()["revision"] == initial["revision"], "Saved preference is not a musical edit")
    macro_state = data("vibera_macros", {"action": "state"})
    check(macro_state["profileVersion"] == 10, "Actual macro readback accepts preference profile10")

    for beat in (0, .5):
        command("steps.set", pattern=pattern, channel=kick, beat=beat, enabled=True)
    command("notes.add", pattern=pattern, channel=kick, beat=.5, length=.75, pitch=67, value=.4, step=True)
    command("notes.add", pattern=pattern, channel=kick, beat=.5, length=.5, pitch=72, value=0, step=True)
    command("steps.set", pattern=pattern, channel=snare, beat=1, enabled=True)
    command("clip.add", pattern=pattern, beat=0, length=4)
    command("clip.add", pattern=pattern, beat=4, length=4)
    baseline = notes()
    remembered = [n for n in baseline if n["channel"] == kick and n["beat"] == .5]
    check(len(remembered) == 3, "Real mixed-property source chord exists")

    def render(name):
        path = output / name
        check(not path.exists(), "Fresh disposable WAV target: " + name)
        job = data("vibera_project", {"action": "export", "path": str(path), "overwrite": False})
        check(job["completionState"] == "pending", "Export acceptance is pending, never fabricated completion")
        finished = client.wait_job(job)
        check(finished["completionState"] == "completed" and path.is_file(), "Actual asynchronous export terminal")
        with wave.open(str(path), "rb") as wav:
            metadata = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes())
            pcm = wav.readframes(wav.getnframes())
        check(metadata == (2, 3, 48000, 192000), "Literal linked eight-beat Song: stereo24bit/48kHz/four seconds")
        check(any(pcm), "Genuine nonsilent PCM")
        return metadata, pcm

    before_pcm = render("step-memory-before.wav")
    # Exercise real semantic continuous ownership, not merely one-shot commands.
    # No device is opened by this stdio fixture; actual held-MIDI/audio coverage
    # remains a separate required application case.
    history_before = state()
    all_muted = [dict(n, muted=True) if n["channel"] == kick else n for n in baseline]

    def paint_two_cells():
        lease = data("vibera_gesture", {"action": "begin", "domain": "stepActivation", "label": "Retained step stroke"})
        check(lease["completionState"] == "pending", "Actual semantic stroke begins pending")
        for beat in (.5, 0):
            preview = data("vibera_gesture", {"action": "update", "operation": lease["operation"],
                "command": "steps.set", "args": {"pattern": pattern, "channel": kick,
                "beat": beat, "enabled": False}})
            check(preview["completionState"] == "pending", "Retained step preview is not fabricated completion")
        check(notes() == all_muted, "Successive actual stroke updates accumulate on both source cells")
        return lease["operation"]

    operation = paint_two_cells()
    cancelled = data("vibera_gesture", {"action": "cancel", "operation": operation})
    after_cancel = state()
    check(cancelled["completionState"] == "cancelled" and notes() == baseline and
          all(after_cancel[key] == history_before[key] for key in ("dirty", "undoAvailable", "redoAvailable")),
          "Semantic stroke cancellation restores exact source, dirty state and history availability")
    operation = paint_two_cells()
    committed = data("vibera_gesture", {"action": "commit", "operation": operation})
    check(committed["completionState"] == "completed" and notes() == all_muted,
          "Actual semantic retained stroke reports completion only after commit")
    command("edit.undo")
    check(notes() == baseline, "Entire two-cell retained stroke has one musical Undo")
    command("edit.redo")
    check(notes() == all_muted, "Entire retained stroke has exact Redo")
    command("edit.undo")
    check(notes() == baseline, "Gesture fixture restores original score before one-shot tests")
    command("transport.record")
    check(state()["recordArmed"], "Actual recording arm is active for the scoped activation lease")
    refused = client.call("vibera_gesture", {"action": "begin", "domain": "document"}, allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "busy",
          "General document leases still refuse armed recording")
    operation = paint_two_cells()
    for name, args in (("steps.set", {"pattern": pattern, "channel": snare, "beat": 1, "enabled": False}),
                       ("steps.pitch", {"pattern": pattern, "channel": kick, "pitch": 72})):
        result = client.call("vibera_gesture", {"action": "update", "operation": operation,
                            "command": name, "args": args}, allow_failure=True)
        check(result.get("isError") and result["structuredContent"]["error"]["code"] == "gesture_domain",
              "Activation lease refuses a different Channel or a different command")
        check(notes() == all_muted and state()["recordArmed"],
              "Rejected activation update preserves earlier valid previews and recording arm")
    cancelled = data("vibera_gesture", {"action": "cancel", "operation": operation})
    check(cancelled["completionState"] == "cancelled" and notes() == baseline and state()["recordArmed"],
          "Scoped activation cancel restores score without disarming recording")
    command("transport.record")
    check(not state()["recordArmed"] and notes() == baseline, "Owned armed fixture ends without a take or score change")
    command("steps.set", pattern=pattern, channel=kick, beat=.5, enabled=False)
    retained = notes()
    expected = [dict(n, muted=True) if n in remembered else n for n in baseline]
    check(retained == expected, "Removal changes only actual source chord activation, preserving IDs and zero velocity")
    revision = state()["revision"]
    command("steps.set", pattern=pattern, channel=kick, beat=.5, enabled=False)
    check(notes() == retained and state()["revision"] == revision, "Repeated removal is an actual no-op")
    command("edit.undo")
    check(notes() == baseline, "Retained chord is one exact Undo")
    command("edit.redo")
    check(notes() == retained, "Retained chord exact Redo")
    data("vibera_rack", {"action": "select", "ids": [kick], "mode": "replace", "activeChannel": kick})
    data("vibera_rack", {"action": "configure", "graphVisible": True})
    data("vibera_rack", {"action": "scroll", "firstStep": 0})
    graph = rack()["graph"]
    check(graph["visible"] and graph["channel"] == kick, "Actual graph shows the explicit target Channel")
    check(graph["inactivePropertyEditing"] and graph["retainedInactiveSourceNotes"] == 3, "Truthful inactive graph readback")
    visible = {row["id"]: row for row in graph["notes"]}
    check(all(n["id"] in visible and visible[n["id"]]["muted"] is True for n in remembered),
          "Every actual retained chord member is present in visible graph readback with muted=true")
    active = next(n for n in baseline if n["channel"] == kick and n["beat"] == 0)
    check(active["id"] in visible and visible[active["id"]]["muted"] is False,
          "Unaffected active step remains explicitly active in graph readback")
    data("vibera_editor", {"editor": 0, "action": "show"})
    snapshot = client.call("vibera_snapshot", {})
    image = next(item for item in snapshot["content"] if item.get("type") == "image")
    pixels = base64.b64decode(image["data"], validate=True)
    check(image["mimeType"] == "image/png" and pixels.startswith(b"\x89PNG\r\n\x1a\n") and
          pixels[12:16] == b"IHDR", "Actual inactive-source component PNG captured through MCP")
    image_path = output / "retained-step-graph.png"
    check(not image_path.exists(), "Fresh owned component capture path")
    image_path.write_bytes(pixels)
    command("steps.pitch", pattern=pattern, channel=kick, ids=[remembered[0]["id"]], pitch=76)
    edited = next(n for n in notes() if n["id"] == remembered[0]["id"])
    check(edited["pitch"] == 76 and edited["muted"], "Existing canonical graph edits inactive properties without activation")
    command("edit.undo")
    check(notes() == retained, "Inactive graph property history is exact")
    command("steps.set", pattern=pattern, channel=kick, beat=.5, enabled=True)
    check(notes() == baseline, "Reactivation recalls the complete chord rather than new defaults")
    check(render("step-memory-after.wav") == before_pcm, "Restored source yields byte-exact original linked-Song PCM")

    for arguments in ({"retainSteps": 1}, {"retainSteps": "true"}):
        before = state()
        result = client.call("vibera_command", {"command": "steps.set", "args": {
            "pattern": pattern, "channel": kick, "beat": .5, "enabled": False, **arguments}}, allow_failure=True)
        check(result.get("isError") and result["structuredContent"]["error"]["code"], "Actual malformed flag structured failure")
        check(notes() == baseline and state()["revision"] == before["revision"], "Invalid input leaves score/history revision exact")

    # This branch catches profile derivation downgrading10 to5..8 during actual
    # macro configuration, rather than merely parsing a schema in isolation.
    macro = {"id": "macro.retained-step-acceptance", "name": "Remember chord", "steps": [{
        "action": "steps.set", "currentContext": False,
        "parameters": {"pattern": pattern, "channel": kick, "beat": .5, "enabled": False}}]}
    current = data("vibera_macros", {"action": "state"})
    saved = data("vibera_macros", {"action": "create", "generation": current["generation"], "macro": macro})
    check(saved["profileVersion"] == 10 and saved["completionState"] == "completed", "Actual profile10 macro configuration completes")
    current = data("vibera_macros", {"action": "state"})
    ran = data("vibera_macros", {"action": "run", "id": macro["id"], "generation": current["generation"]})
    check(ran["completionState"] == "completed" and notes() == retained, "Real macro inherits shared retained-step policy")
    command("edit.undo")
    check(notes() == baseline, "Macro is one exact musical Undo")
    command("steps.set", pattern=pattern, channel=kick, beat=.5, enabled=False)
    path = output / "retained-step-score.vibra"
    check(not path.exists(), "Fresh disposable project target")
    client.wait_job(data("vibera_project", {"action": "save", "path": str(path), "overwrite": False}))
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    client.wait_job(data("vibera_project", {"action": "open", "path": str(path), "discardChanges": True}))
    check(notes() == retained and rack()["muteRemovedSteps"], "Actual save/New/reopen retains source chord and saved preference")
    check(render("step-memory-muted.wav") != before_pcm, "Muted retained source has actual PCM consequence")

    blocked = output / "blocked-step-memory.wav"
    check(not blocked.exists(), "Fresh owned blocked-output fixture")
    blocked.mkdir()
    result = client.call("vibera_project", {"action": "export", "path": str(blocked), "overwrite": True}, allow_failure=True)
    job = result.get("structuredContent", {})
    blocked_admission = job.get("completionState")
    check(not result.get("isError") and blocked_admission == "pending",
          "Genuine file failure is admitted pending before asynchronous rendering/commit")
    if not result.get("isError"):
        deadline = time.monotonic() + 40
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.03)
            result = client.call("vibera_project", {"action": "job", "operation": job["operation"]}, allow_failure=True)
            job = result.get("structuredContent", {})
    check(job.get("completionState") == "failed" and job.get("error"), "Actual unwritable export reports failure, never completed acceptance")
    check(notes() == retained, "Failed file operation preserves retained musical source")

    command("pattern.channelLoop", pattern=pattern, channel=kick, text="step")

    def auto_period():
        part = next(row for row in state()["patterns"] if row["id"] == pattern)
        loop = next(row for row in part["channelLoops"] if row["channel"] == kick)
        check(loop["mode"] == "step", "Readback retains the actual requested automatic mode")
        return loop["resolvedLength"]

    check(notes() == retained and auto_period() == .25,
          "Automatic empty-tail period is a quarter beat while retained source IDs survive outside it")
    command("steps.set", pattern=pattern, channel=kick, beat=.5, enabled=True)
    check(notes() == baseline and auto_period() == 1.25,
          "Actual source chord is recalled after loop shrink without an aliased default note")
    automatic_pcm = render("step-memory-automatic-before.wav")
    command("edit.undo")
    check(notes() == retained and auto_period() == .25,
          "Reactivation Undo restores exact inactive source and the shrunk automatic period")
    command("edit.redo")
    check(notes() == baseline and auto_period() == 1.25,
          "Reactivation Redo restores exact original properties and computed period")
    check(render("step-memory-automatic-after.wav") == automatic_pcm,
          "Loop-period reactivation history restores byte-exact actual Song PCM")
    auto_path = output / "retained-step-automatic.vibra"
    check(not auto_path.exists(), "Fresh owned automatic-loop project target")
    client.wait_job(data("vibera_project", {"action": "save", "path": str(auto_path), "overwrite": False}))
    client.wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
    client.wait_job(data("vibera_project", {"action": "open", "path": str(auto_path), "discardChanges": True}))
    check(notes() == baseline and auto_period() == 1.25,
          "Automatic source identities and period survive actual save/New/reopen")
    check(render("step-memory-automatic-reopened.wav") == automatic_pcm,
          "Reopened automatic-loop project yields the exact original rendered Song")
    return {"checks": checks, "scope": "actual owned application MCP, files/history/PCM; no native input/listening/hardware",
            "asynchronousFailure": {"admitted": blocked_admission, "terminal": job.get("completionState"),
                                    "error": job.get("error"), "operation": job.get("operation")}}


try:
    client=module.VibeRaClient(app); data=client.data
    receipt["version"]=client.server["version"]
    state=lambda: data("vibera_state")
    command=lambda name, **args: data("vibera_command",dict(command=name,args=args))
    rack=lambda **args: data("vibera_rack",{"action":"state",**args})
    pattern=state()["selectedPattern"]
    channels=[c["id"] for c in state()["channels"]]
    kick,snare=channels[:2]
    notes=lambda: data("vibera_inspect",dict(entity="notes",pattern=pattern))["items"]
    velocities=lambda: [n["velocity"] for n in notes() if n["channel"]==kick and n["step"]]
    discovery=data("vibera_commands",dict(command="steps.velocity",includeSchemas=True))["operations"][0]
    check(discovery["supported"] and discovery["arguments"]["properties"]["text"]["enum"]==["set","reset","scale","ramp"],"Actual typed graph command discovery")
    revision=state()["revision"]
    shown=rack(action="configure",graphVisible=True)
    check(shown["graph"]["visible"] and state()["revision"]==revision and shown["activeChannel"]==kick,"Graph navigation is shared view state, not document Undo")
    for i in range(4): command("steps.set",pattern=pattern,channel=kick,beat=i*.25,enabled=True)
    for i,n in enumerate(notes()): command("notes.velocity",pattern=pattern,ids=[n["id"]],value=(i+1)*.2)
    command("steps.set",pattern=pattern,channel=snare,beat=.5,enabled=True)
    command("clip.add",pattern=pattern,beat=0,length=4)
    command("clip.add",pattern=pattern,beat=4,length=4)
    initial=notes(); anchor=initial[0]["id"]
    def begin(**args): return data("vibera_gesture",{"action":"begin","domain":"stepVelocity",**args})["operation"]
    def update(token,**args): return data("vibera_gesture",dict(action="update",operation=token,command="steps.velocity",args=dict(pattern=pattern,channel=kick,**args)))
    token=begin()
    error("vibera_gesture",dict(action="update",operation=token,domain="document",command="steps.velocity",args=dict(value=.5)),"invalid_arguments")
    check(update(token,text="scale",target=anchor,value=.4)["completionState"]=="pending","Live preview is not committed")
    check(rack()["graph"]["semanticGestureActive"],"Actual semantic graph ownership readback")
    update(token,text="scale",target=anchor,value=.1)
    check(all(abs(a-b)<1.e-6 for a,b in zip(velocities(),[.1,.2,.3,.4])),"Repeated scale uses baseline")
    check(data("vibera_gesture",dict(action="commit",operation=token))["completionState"]=="completed","Actual gesture commit")
    scaled=notes();command("edit.undo");check(notes()==initial,"One-step scale Undo")
    command("edit.redo");check(notes()==scaled,"Exact scale Redo");command("edit.undo")
    token=begin()
    update(token,text="ramp",beat=0,length=.75,value=.1,offset=.7)
    update(token,text="ramp",beat=0,length=.25,value=.1,offset=.7)
    check(abs(velocities()[1]-.7)<1.e-6 and abs(velocities()[2]-.6)<1.e-6,"Shrinking ramp restores baseline outside range")
    check(data("vibera_gesture",dict(action="cancel",operation=token))["completionState"]=="cancelled" and notes()==initial,"Actual graph cancellation restores full canonical notes")
    command("steps.velocity",pattern=pattern,channel=kick,text="ramp",beat=.75,length=-.75,value=.2,offset=.8)
    check(abs(velocities()[0]-.8)<1.e-6 and abs(velocities()[3]-.2)<1.e-6,"Reverse ramp result")
    command("edit.undo");check(notes()==initial,"Reverse ramp Undo")
    for args in [dict(ids=[anchor,anchor],value=.4),dict(ids=[999999],value=.4),dict(ids=[initial[-1]["id"]],value=.4),dict(value=1.01),dict(value="loud"),dict(text="ramp",beat=.1,length=.25,value=.3,offset=.7)]:
        error("vibera_command",dict(command="steps.velocity",args=dict(pattern=pattern,channel=kick,**args)))
        check(notes()==initial,"Refused command retains exact project data")
    command("transport.record")
    check(state()["recordArmed"],"Recording arm actually active")
    error("vibera_gesture",dict(action="begin"),"busy")
    token=begin()
    error("vibera_gesture",dict(action="update",operation=token,command="notes.delete",args=dict(pattern=pattern,ids=[anchor])),"gesture_domain")
    update(token,text="set",value=.45)
    check(state()["recordArmed"],"Narrow velocity lease preserves armed recording")
    data("vibera_gesture",dict(action="cancel",operation=token)); command("transport.record")
    check(notes()==initial and not state()["recordArmed"],"Armed graph cancellation is exact")
    token=begin(leaseMs=250)
    update(token,text="set",value=.1);time.sleep(.45)
    check(data("vibera_gesture",dict(action="state",operation=token))["completionState"]=="cancelled" and notes()==initial,"Real timer lease expiry restores graph preview")
    command("steps.velocity",pattern=pattern,channel=kick,text="reset")
    check(velocities()==[.8]*4 or all(abs(v-.8)<1.e-6 for v in velocities()),"Reset uses step default")
    command("edit.undo")
    command("channel.pan",channel=kick,value=-1)
    command("channel.pan",channel=snare,value=1)
    command("steps.velocity",pattern=pattern,channel=kick,text="set",value=.4)
    def render(name):
        path=output/name
        job=data("vibera_project",dict(action="export",path=str(path),overwrite=True))
        check(job["completionState"]=="pending","Export admission is not completion")
        client.wait_job(job)
        with wave.open(str(path),"rb") as wav:
            check((wav.getframerate(),wav.getnchannels(),wav.getsampwidth(),wav.getnframes())==(48000,2,3,192000),"Actual linked Song WAV format/duration")
            pcm=wav.readframes(wav.getnframes())
        left=[int.from_bytes(pcm[i:i+3],"little",signed=True) for i in range(0,len(pcm),6)]
        right=b"".join(pcm[i:i+3] for i in range(3,len(pcm),6))
        return sum(x*x for x in left),right
    before,right=render("velocity-before.wav")
    command("steps.velocity",pattern=pattern,channel=kick,text="set",value=.2)
    after,right_after=render("velocity-after.wav")
    ratio=(after/before)**.5
    check(before>0 and abs(ratio-(25/51))<.001 and right==right_after,"Actual exported velocity changes left PCM, unaffected right is exact")
    command("steps.velocity",pattern=pattern,channel=kick,text="set",value=0)
    zero,right_zero=render("velocity-zero.wav")
    check(zero==0 and right_zero==right,"Zero velocity retains steps but emits no scored hit; other Channel exact")
    command("steps.velocity",pattern=pattern,channel=kick,text="set",value=.2)
    path=output/"velocity-beat.vibra"
    saved_notes=notes()
    client.wait_job(data("vibera_project",dict(action="save",path=str(path),overwrite=True)))
    client.wait_job(data("vibera_project",dict(action="open",path=str(path),discardChanges=True)))
    check(notes()==saved_notes,"Real project round trip retains graph notes and stable IDs")
    blocked=output/"blocked.wav";blocked.mkdir(exist_ok=True)
    job=data("vibera_project",dict(action="export",path=str(blocked),overwrite=True))
    until=time.monotonic()+20
    while job["completionState"]=="pending" and time.monotonic()<until:
        time.sleep(.03);job=client.call("vibera_project",dict(action="job",operation=job["operation"]),allow_failure=True)["structuredContent"]
    check(job["completionState"]=="failed" and job.get("error"),"Actual asynchronous export failure, never accepted-as-completed")

    # Pitch is an additional shared Graph property; it does not replace the
    # existing velocity-only lease or alter any velocity oracle above.
    tools = {tool["name"]: tool for tool in client.request("tools/list", {})["tools"]}
    graph_schema = tools["vibera_rack"]["inputSchema"]["properties"]["graphProperty"]
    check(graph_schema["enum"] == ["velocity", "pitch", "pan", "fine", "shift", "repeat", "release", "modX", "modY"],
          "Property schema preserves all seven existing properties and adds sampler Mod X/Y")
    pitch_command = data("vibera_commands", dict(command="steps.pitch", includeSchemas=True))["operations"][0]
    pitch_schema = pitch_command["arguments"]["properties"]["pitch"]
    check(pitch_command["supported"] and pitch_schema["type"] == "integer" and
          pitch_schema["minimum"] == 0 and pitch_schema["maximum"] == 127 and
          pitch_schema["default"] == 60 and
          pitch_command["arguments"]["properties"]["text"]["enum"] == ["set", "reset", "scale", "ramp"],
          "Actual typed integral pitch command discovery")
    check("stepGraph" in tools["vibera_gesture"]["inputSchema"]["properties"]["domain"]["enum"],
          "Actual shared stepGraph lease is discoverable without a new tool")
    # Reopening deliberately resets the active Channel independently of explicit
    # command targets. The graph follows that live target, not the last edit.
    selected_pitch_target = rack(action="select", ids=[kick], activeChannel=kick)
    check(selected_pitch_target["activeChannel"] == kick,
          "Explicitly select the intended Pitch graph Channel after project reopen")
    before_view = state()
    pitch_view = rack(action="configure", graphProperty="pitch", graphVisible=True)
    check(pitch_view["graph"]["property"] == "pitch" and pitch_view["graph"]["visible"] and
          pitch_view["graph"]["units"] == "MIDI note number 0-127" and
          pitch_view["graph"]["defaultValue"] == 60 and
          pitch_view["graph"]["supportedProperties"] == ["velocity", "pitch", "pan", "fine", "shift", "repeat", "release", "modX", "modY"] and
          state()["revision"] == before_view["revision"],
          "Pitch navigation/readback is truthful view state, not document history")
    error("vibera_rack", dict(action="configure", graphProperty="pressure"))
    check(state()["revision"] == before_view["revision"] and rack()["graph"]["property"] == "pitch",
          "Unsupported property refusal leaves document and selected property unchanged")

    pitch_ids = [n["id"] for n in notes() if n["channel"] == kick and n["step"]]
    check(len(pitch_ids) == 4, "Pitch fixture retains all four original aligned steps")
    anchor = pitch_ids[0]
    for note_id, pitch in zip(pitch_ids, [0, 60, 100, 127]):
        command("steps.pitch", pattern=pattern, channel=kick, ids=[note_id], pitch=pitch)
    pitch_baseline = notes()

    def pitches():
        return [n["pitch"] for n in notes() if n["channel"] == kick and n["step"]]

    def without_pitch(rows):
        return [{key: value for key, value in note.items() if key != "pitch"} for note in rows]

    def check_pitches(expected, message):
        current = notes()
        check(pitches() == expected and without_pitch(current) == without_pitch(pitch_baseline) and
              [n for n in current if n["channel"] != kick] ==
              [n for n in pitch_baseline if n["channel"] != kick], message)

    def pitch_begin(**args):
        return data("vibera_gesture", {"action": "begin", "domain": "stepGraph", **args})["operation"]

    def pitch_update(token, **args):
        return data("vibera_gesture", dict(action="update", operation=token, command="steps.pitch",
                                          args=dict(pattern=pattern, channel=kick, **args)))

    check_pitches([0, 60, 100, 127], "Literal pitch fixture preserves all non-pitch fields and IDs")
    receipt["pitchGraphReadback"] = rack()["graph"]
    visible = {n["id"]: n for n in receipt["pitchGraphReadback"]["notes"]}
    check(all(visible.get(note_id, {}).get("pitch") == pitch and visible.get(note_id, {}).get("value") == pitch
              for note_id, pitch in zip(pitch_ids, [0, 60, 100, 127])),
          "Actual Graph notes expose matching pitch and selected-property value")
    command("steps.pitch", pattern=pattern, channel=kick, text="set", ids=[anchor], pitch=64)
    check_pitches([64, 60, 100, 127], "Canonical pitch set changes exactly its explicit step target")
    pitch_set = notes()
    command("edit.undo"); check(notes() == pitch_baseline, "Pitch set exact Undo")
    command("edit.redo"); check(notes() == pitch_set, "Pitch set exact Redo")
    command("edit.undo")

    token = pitch_begin()
    check(pitch_update(token, text="scale", target=anchor, pitch=20)["completionState"] == "pending",
          "Pitch scale preview is pending, not committed")
    check_pitches([20, 80, 120, 127], "Pitch scale is a clamped semitone translation")
    pitch_update(token, text="scale", target=anchor, pitch=10)
    check_pitches([10, 70, 110, 127], "Repeated pitch scale translates its opening baseline")
    pitch_update(token, text="scale", target=pitch_ids[2], pitch=0)
    check_pitches([0, 0, 0, 27], "Pitch scale clamps negative translated pitches at zero")
    pitch_update(token, text="scale", target=anchor, pitch=10)
    error("vibera_gesture", dict(action="update", operation=token, command="notes.delete",
                                args=dict(pattern=pattern, ids=[anchor])), "gesture_domain")
    error("vibera_gesture", dict(action="update", operation=token, command="steps.pitch",
                                args=dict(pattern=pattern, channel=snare, pitch=60)), "gesture_domain")
    check_pitches([10, 70, 110, 127], "Refused foreign command/channel retains exact live preview")
    check(data("vibera_gesture", dict(action="commit", operation=token))["completionState"] == "completed",
          "Pitch gesture commits only at real terminal completion")
    pitch_scaled = notes()
    command("edit.undo"); check(notes() == pitch_baseline, "Entire pitch scale is one exact Undo")
    command("edit.redo"); check(notes() == pitch_scaled, "Pitch scale exact Redo")
    command("edit.undo")

    token = pitch_begin()
    pitch_update(token, text="ramp", beat=0, length=.75, pitch=12, offset=96)
    check_pitches([12, 40, 68, 96], "Pitch ramp uses independent literal semitone endpoints")
    pitch_update(token, text="ramp", beat=0, length=.25, pitch=12, offset=96)
    check_pitches([12, 96, 100, 127], "Shrinking pitch ramp restores baseline outside its new range")
    # The shared lease permits velocity too; cancellation restores BOTH fields.
    update(token, text="set", ids=[anchor], value=.6)
    check(data("vibera_gesture", dict(action="cancel", operation=token))["completionState"] == "cancelled" and
          notes() == pitch_baseline, "Mixed pitch/velocity lease cancellation restores all canonical fields")
    command("steps.pitch", pattern=pattern, channel=kick, text="ramp", beat=.75, length=-.75, pitch=0, offset=90)
    check_pitches([90, 60, 30, 0], "Reverse pitch ramp has literal ordered endpoints")
    command("edit.undo"); check(notes() == pitch_baseline, "Reverse pitch ramp exact Undo")
    command("steps.pitch", pattern=pattern, channel=kick, text="ramp", beat=.25, length=0, pitch=12, offset=77)
    check_pitches([0, 77, 100, 127], "Zero-span pitch ramp uses its explicit ending pitch")
    command("edit.undo")
    command("steps.pitch", pattern=pattern, channel=kick, text="reset")
    check_pitches([60, 60, 60, 60], "Pitch reset is middle-C60, preserving velocity and step identity")
    command("edit.undo")
    command("steps.pitch", pattern=pattern, channel=kick, ids=[anchor])
    check_pitches([60, 60, 100, 127], "Omitted canonical pitch uses declared default60")
    command("edit.undo")
    outsider = next(n["id"] for n in notes() if n["channel"] == snare)
    for args in [dict(pitch=-1), dict(pitch=128), dict(pitch=60.5), dict(pitch=True),
                 dict(ids=[outsider], pitch=60), dict(ids=[anchor, anchor], pitch=60),
                 dict(ids=[999999], pitch=60), dict(text="scale", target=outsider, pitch=60),
                 dict(text="ramp", beat=.1, length=.25, pitch=60, offset=72),
                 dict(text="ramp", beat=0, length=.25, pitch=60, offset=72.5)]:
        prior = state()
        error("vibera_command", dict(command="steps.pitch", args=dict(pattern=pattern, channel=kick, **args)))
        check(notes() == pitch_baseline and state()["revision"] == prior["revision"] and
              state()["undoAvailable"] == prior["undoAvailable"] and
              state()["redoAvailable"] == prior["redoAvailable"],
              "Refused pitch edit preserves exact score/revision/history")
    token = begin()
    error("vibera_gesture", dict(action="update", operation=token, command="steps.pitch",
                                args=dict(pattern=pattern, channel=kick, pitch=72)), "gesture_domain")
    data("vibera_gesture", dict(action="cancel", operation=token))
    check(notes() == pitch_baseline, "Legacy stepVelocity domain remains strictly velocity-only")
    command("transport.record")
    token = pitch_begin()
    pitch_update(token, text="set", pitch=72)
    check(state()["recordArmed"], "Narrow pitch lease retains actual armed recording")
    data("vibera_gesture", dict(action="cancel", operation=token))
    command("transport.record")
    check(notes() == pitch_baseline and not state()["recordArmed"],
          "Armed pitch cancellation is exact; no physical MIDI claim")

    command("steps.pitch", pattern=pattern, channel=kick, text="ramp", beat=0, length=.75, pitch=60, offset=72)
    check_pitches([60, 64, 68, 72], "Pitch PCM fixture uses literal four-note score")
    pitch_saved = notes()
    render("pitch-graph.wav")
    # Independently author the same literal score with ordinary melodic notes;
    # this does not call the pitch graph algorithm or infer expected PCM from it.
    command("notes.delete", pattern=pattern, ids=pitch_ids)
    for beat, pitch in [(0, 60), (.25, 64), (.5, 68), (.75, 72)]:
        command("notes.add", pattern=pattern, channel=kick, beat=beat, length=.25, pitch=pitch, value=.2)
    render("pitch-literal-reference.wav")
    def pcm_bytes(name):
        with wave.open(str(output/name), "rb") as wav:
            return wav.readframes(wav.getnframes())
    check(pcm_bytes("pitch-graph.wav") == pcm_bytes("pitch-literal-reference.wav"),
          "Actual graph pitch WAV equals an independently authored literal melodic score")
    for _ in range(5): command("edit.undo")
    check(notes() == pitch_saved, "Independent PCM reference construction restores all original step IDs")
    pitch_path = output/"pitch-beat.vibra"
    client.wait_job(data("vibera_project", dict(action="save", path=str(pitch_path), overwrite=True)))
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    client.wait_job(data("vibera_project", dict(action="open", path=str(pitch_path), discardChanges=True)))
    check(notes() == pitch_saved, "Actual pitch save/New/reopen preserves every note field and ID")
    rack(action="configure", graphProperty="velocity", graphVisible=True)
    check(rack()["graph"]["property"] == "velocity" and rack()["graph"]["units"] == "normalized 0-1",
          "Original velocity Graph remains available after pitch persistence")
    receipt["pitchGraph"] = dict(project=str(pitch_path), literalPitches=[60, 64, 68, 72],
                                independentPcmExact=True, legacyVelocityDomainPreserved=True,
                                installedPlugin=False, unreleasedSourceIncrement=True)
    receipt.update(project=str(path),leftRmsRatio=ratio,unaffectedRightExact=True)
    expression_output = root / "build/windows/step-expression-mcp" / ("attempt-" + str(time.time_ns()))
    expression = verify_step_expression(client, expression_output)
    receipt["stepExpression"] = dict(expression, output=str(expression_output))
    receipt["checks"] += len(expression["checks"])
    memory_output = root / "build/windows/step-memory-mcp" / ("attempt-" + str(time.time_ns()))
    memory = verify_step_memory(client, memory_output)
    receipt["stepMemory"] = dict(memory, output=str(memory_output))
    receipt["checks"] += len(memory["checks"])
    shift_output = root / "build/windows/step-shift-mcp" / ("attempt-" + str(time.time_ns()))
    shift = verify_step_shift(client, shift_output)
    receipt["stepShift"] = dict(shift, output=str(shift_output))
    receipt["checks"] += len(shift["checks"])
    repeat_output = root / "build/windows/step-repeat-mcp" / ("attempt-" + str(time.time_ns()))
    repeated = verify_step_repeat(client, repeat_output)
    receipt["stepRepeat"] = dict(repeated, output=str(repeat_output))
    receipt["checks"] += len(repeated["checks"])
    release_output = root / "build/windows/step-release-mcp" / ("attempt-" + str(time.time_ns()))
    receipt["stepRelease"] = dict(output=str(release_output), passed=False)
    released = verify_step_release(client, release_output)
    receipt["stepRelease"] = dict(released, output=str(release_output), passed=True)
    receipt["checks"] += len(released["checks"])
    client.close();receipt.update(passed=True,exitCode=client.process.returncode)
except Exception as failure:
    receipt["error"]=str(failure);raise
finally:
    if client and client.process.poll() is None:client.close()
    receipt.update(finished=True,elapsedSeconds=time.monotonic()-started)
    receipt_path.write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
print(json.dumps({k:receipt[k] for k in ["passed","checks","elapsedSeconds","applicationSha256"]}))
