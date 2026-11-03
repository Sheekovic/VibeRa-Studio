"""Actual sampler Mod X/Y score, graph, history, files and independent WAV workflow."""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import time
import uuid
import wave
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
app = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("modulation_output", ROOT / "scripts/verify-plugin-io-mcp.py")
paths = importlib.util.module_from_spec(spec)
spec.loader.exec_module(paths)
output_root = paths.owned_output_path(sys.argv[2], app)
paths.ordinary(app, False)
output_root.mkdir(parents=True, exist_ok=True)
output = output_root / ("attempt-" + uuid.uuid4().hex)
output.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location("neutral", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=[], output=str(output),
               applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               nativeInput=False, listening=False, hardware=False, flRuntimeObserved=False)
began = time.monotonic()
client = None


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    receipt["checks"].append(label)


try:
    source = output / "original-modulation-tone.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((2, 2, 48000, 48000, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<hh", value, -value // 2)
            for frame in range(48000)
            for value in [round(4500 * sum(math.sin(2 * math.pi * hz * frame / 48000)
                                          for hz in (220, 6000)))]))
    client = module.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    initial = state()
    pattern = initial["selectedPattern"]
    channel, other = [c["id"] for c in initial["channels"][:2]]
    command("channel.source", channel=channel, text=str(source))
    command("pattern.length", pattern=pattern, length=4)
    command("notes.add", pattern=pattern, channel=channel, beat=0, length=3, pitch=60, value=.7, step=True)
    command("clip.add", pattern=pattern, beat=0, length=4)
    data("vibera_rack", dict(action="select", ids=[channel], activeChannel=channel))
    notes = lambda: data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"]
    note = notes()[0]["id"]
    target = dict(pattern=pattern, channel=channel, ids=[note])
    check(notes()[0]["modX"] == notes()[0]["modY"] == .5, "New notes have neutral Mod X/Y")

    def render(name):
        path = output / name
        accepted = data("vibera_project", dict(action="export", path=str(path), overwrite=True))
        check(accepted["completionState"] == "pending", "Export acceptance remains pending")
        terminal = client.wait_job(accepted)
        check(terminal["completionState"] == "completed", "Export reaches actual terminal success")
        with wave.open(str(path), "rb") as wav:
            check((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000),
                  "Actual stereo24/48k render")
            return wav.readframes(wav.getnframes())

    dry = render("neutral-dry.wav")
    for field, manual in (("modX", "filterCutoff"), ("modY", "filterResonance")):
        operation = "steps." + field
        contract = data("vibera_commands", dict(command=operation, includeSchemas=True))["operations"][0]
        schema = contract["arguments"]
        check(contract["supported"] and schema["additionalProperties"] is False and
              all(schema["properties"][key]["minimum"] == 0 and
                  schema["properties"][key]["maximum"] == 1 and
                  schema["properties"][key]["default"] == .5 for key in ("value", "offset")),
              "Closed normalized discovery with neutral midpoint: " + field)
        graph = data("vibera_rack", dict(action="configure", graphVisible=True, graphProperty=field))["graph"]
        check(graph["property"] == field and graph["supported"] and graph["defaultValue"] == .5,
              "Actual Rack Graph exposes matching supported property: " + field)
        before = notes()
        for bad in (-.01, 1.01, True, "0.5"):
            refused = client.call("vibera_command", dict(command=operation,
                                  args=dict(target, value=bad)), allow_failure=True)
            check(refused.get("isError") and refused["structuredContent"].get("error") and notes() == before,
                  "Invalid modulation refuses without score mutation")
        refused = client.call("vibera_command", dict(command=operation,
                              args=dict(target, channel=other, value=.25)), allow_failure=True)
        check(refused.get("isError") and notes() == before, "Foreign score target refuses atomically")
        command(operation, **target, value=.25)
        command(operation, **target)
        check(notes()[0][field] == .5, "Omitted value applies the declared neutral default")
        command("edit.undo")
        check(notes()[0][field] == .25, "Defaulted edit has exact Undo")
        command("edit.undo")
        check(notes() == before, "Default-input exercise restores every source field")
        command(operation, **target, value=.25)
        command(operation, **target, text="ramp", beat=0, length=0, value=.25)
        check(notes()[0][field] == .5, "Omitted ramp endpoint applies its declared neutral default")
        command("edit.undo")
        command("edit.undo")
        check(notes() == before, "Defaulted ramp preserves exact two-action history")
        token = data("vibera_gesture", dict(action="begin", domain="stepGraph"))["operation"]
        for value in (.1, .8, .25):
            pending = data("vibera_gesture", dict(action="update", operation=token,
                command=operation, args=dict(target, value=value)))
            check(pending["completionState"] == "pending" and notes()[0][field] == value,
                  "Owned preview reports actual model value and pending state")
        cancelled = data("vibera_gesture", dict(action="cancel", operation=token))
        check(cancelled["completionState"] == "cancelled" and notes() == before,
              "Cancel restores every original note field")
        value = .25 if field == "modX" else .75
        command(operation, **target, value=value)
        assigned = notes()
        check(assigned[0]["noteModulationSupported"] and assigned[0]["noteModulationAcknowledged"] is None,
              "Local modulation availability is explicit and never a fabricated voice acknowledgement")
        command("edit.undo")
        check(notes() == before, "One exact score Undo")
        command("edit.redo")
        check(notes() == assigned, "Exact Redo retains source identity and modulation")
        modulated = render(field + ".wav")
        command(operation, **target, text="reset")
        check(notes()[0][field] == .5, "Reset uses neutral .5 rather than zero")
        command("channel.sampler." + manual, channel=channel, value=.5)
        reference = render(field + "-manual-reference.wav")
        check(modulated == reference, "Independent manual-target WAV is byte exact: " + field)
        command("channel.sampler." + manual, channel=channel, value=1 if field == "modX" else 0)
    check(render("restored-dry.wav") == dry, "Neutral restoration is byte-exact dry")
    command("steps.repeat", **target, value=10)
    command("steps.modX", **target, value=.25)
    command("steps.modY", **target, value=.75)
    repeated = render("repeat-modulation.wav")
    command("steps.modX", **target, text="reset")
    command("steps.modY", **target, text="reset")
    command("channel.sampler.filterCutoff", channel=channel, value=.5)
    command("channel.sampler.filterResonance", channel=channel, value=.5)
    check(render("repeat-manual-reference.wav") == repeated,
          "Repeated sub-notes preserve both modulation fields and match literal manual targets")
    command("channel.sampler.filterCutoff", channel=channel, value=1)
    command("channel.sampler.filterResonance", channel=channel, value=0)
    command("steps.repeat", **target, value=0)
    profile = data("vibera_macros", dict(action="state"))
    recipe = dict(id="macro.acceptance-note-modulation", name="Disposable Mod X/Y", steps=[
        dict(action="steps.modX", currentContext=False, parameters=dict(target, value=.25)),
        dict(action="steps.modY", currentContext=False, parameters=dict(target, value=.75))])
    before = notes()
    created = data("vibera_macros", dict(action="create", generation=profile["generation"], macro=recipe))
    check(created["profileMatchesMemory"] and created["configurationCommitted"],
          "Numeric modulation macro is actually saved through shared validation")
    ran = data("vibera_macros", dict(action="run", generation=created["generation"], id=recipe["id"]))
    check(ran["completionState"] == "completed" and
          (notes()[0]["modX"], notes()[0]["modY"]) == (.25, .75),
          "Saved macro executes both canonical step commands")
    assigned = notes()
    command("edit.undo")
    check(notes() == before, "Two-step modulation macro has one exact Undo")
    command("edit.redo")
    check(notes() == assigned, "Modulation macro Redo retains all source fields")
    command("edit.undo")
    current = data("vibera_macros", dict(action="state"))
    data("vibera_macros", dict(action="remove", generation=current["generation"], id=recipe["id"]))

    source_note = dict(id=987654321, channel=channel, beat=1.5, length=.375,
                       pitch=67, velocity=.5, step=True, muted=True, modX=.25, modY=.75)
    recipe = dict(id="macro.acceptance-import-modulation", name="Disposable imported Mod X/Y", steps=[
        dict(action="notes.import", currentContext=False, parameters=dict(
            pattern=pattern, channel=other, enabled=True, recordedNotes=[source_note]))])
    current = data("vibera_macros", dict(action="state"))
    created = data("vibera_macros", dict(action="create", generation=current["generation"], macro=recipe))
    check(created["profileVersion"] == 16 and created["profileMatchesMemory"] and created["configurationCommitted"],
          "Nonneutral imported modulation uses actual persisted profile16")
    stored = data("vibera_macros", dict(action="state", id=recipe["id"]))
    payload = stored["macros"][0]["steps"][0]["parameters"]["recordedNotes"][0]
    check((payload["modX"], payload["modY"]) == (.25, .75), "Saved macro readback retains note modulation")
    before = notes()
    ran = data("vibera_macros", dict(action="run", generation=stored["generation"], id=recipe["id"]))
    imported = [n for n in notes() if n["channel"] == other]
    check(ran["completionState"] == "completed" and len(imported) == 1 and
          imported[0]["id"] != source_note["id"] and
          all(imported[0][key] == source_note[key] for key in ("beat", "length", "pitch", "velocity", "muted", "modX", "modY")),
          "Actual imported macro allocates fresh note identity and preserves modulation and score data")
    assigned = notes()
    command("edit.undo")
    check(notes() == before, "Imported modulation macro has exact Undo")
    command("edit.redo")
    check(notes() == assigned, "Imported modulation macro has exact Redo")
    command("edit.undo")
    current = data("vibera_macros", dict(action="state"))
    removed = data("vibera_macros", dict(action="remove", generation=current["generation"], id=recipe["id"]))
    check(removed["profileVersion"] == profile["profileVersion"], "Removing disposable macros restores compatibility level")
    command("steps.modX", **target, value=.25)
    command("steps.modY", **target, value=.75)
    saved = output / "modulation.vibra"
    client.wait_job(data("vibera_project", dict(action="save", path=str(saved), overwrite=True)))
    tree = ET.parse(saved)
    row = next(n for n in tree.iter("NOTE") if int(n.get("id")) == note)
    check(float(row.get("modX")) == .25 and float(row.get("modY")) == .75,
          "Project retains exact nonneutral note modulation")
    before = notes()
    expected = render("before-reopen.wav")
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    client.wait_job(data("vibera_project", dict(action="open", path=str(saved), discardChanges=True)))
    check(notes() == before and render("after-reopen.wav") == expected,
          "Actual save/new/reopen restores exact score and filtered WAV")
    data("vibera_select", dict(pattern=pattern, channel=channel, scope=2))
    accepted = data("vibera_project", dict(action="exportMidi", path=str(output / "score.mid"), overwrite=True))
    check(accepted["completionState"] == "pending" and accepted["expressionNotesOmitted"] is None,
          "MIDI export snapshots the explicit Piano part and leaves pending omission count unknown")
    midi = client.wait_job(accepted)
    check(midi["completionState"] == "completed" and midi["expressionNotesOmitted"] == 1 and
          (output / "score.mid").read_bytes()[:4] == b"MThd",
          "MIDI1 truthfully reports omitted per-note modulation")
    before = notes()
    revision = state()["revision"]
    row.set("modY", "1.1")
    invalid_project = output / "invalid-modulation.vibra"
    tree.write(invalid_project, encoding="utf-8", xml_declaration=True)
    response = client.call("vibera_project", dict(action="open", path=str(invalid_project), discardChanges=True), allow_failure=True)
    job = response["structuredContent"]
    receipt["invalidProjectAdmissionState"] = job["completionState"]
    deadline = time.monotonic() + 20
    while job["completionState"] == "pending" and time.monotonic() < deadline:
        time.sleep(.02)
        job = client.call("vibera_project", dict(action="job", operation=job["operation"]), allow_failure=True)["structuredContent"]
    check(job["completionState"] == "failed" and job.get("error") and
          "noteModulation.invalidValue" in json.dumps(job) and notes() == before and state()["revision"] == revision,
          "Actual malformed-modulation open reaches typed failure without changing score or revision")
    receipt["passed"] = True
except Exception as error:
    receipt["error"] = str(error)
    raise
finally:
    if client:
        try:
            client.close()
            receipt["exitCode"] = client.process.returncode
        except Exception as error:
            receipt["passed"] = False
            receipt["closeError"] = str(error)
    receipt["finished"] = True
    receipt["seconds"] = time.monotonic() - began
    for path in (output / "verification.json", output_root / "verification.json"):
        path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps(dict(passed=receipt["passed"], checks=len(receipt["checks"]), output=str(output))))
raise SystemExit(0 if receipt["passed"] else 1)
