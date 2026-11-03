"""Real semantic fill/conversion/selection/history/long-pattern/save/export acceptance."""
import hashlib
import importlib.util
import json
import pathlib
import sys
import time
import wave

sys.dont_write_bytecode = True
root = pathlib.Path(__file__).resolve().parents[1]
app, output = (pathlib.Path(p).resolve() for p in sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
if not client_path.exists():
    client_path = root / "skills/vibera-studio/scripts/stdio_client.py"
spec = importlib.util.spec_from_file_location("vibera_neutral_client", client_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = {"passed": False, "checks": 0, "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
           "clientSha256": hashlib.sha256(client_path.read_bytes()).hexdigest(),
           "nativeInput": False, "listening": False, "hardware": False}
began, client = time.monotonic(), None


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    receipt["checks"] += 1


try:
    client = module.VibeRaClient(app)
    receipt["version"] = client.server["version"]
    command = lambda name, **args: client.data("vibera_command", {"command": name, "args": args})
    initial = client.data("vibera_state")
    pattern = initial["selectedPattern"]
    kick, ghost, hat, bass = (c["id"] for c in initial["channels"][:4])
    notes = lambda: client.data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]
    discovery = client.data("vibera_commands", {"command": "steps.fill", "includeSchemas": True})
    check("sixteenth" in json.dumps(discovery).lower() and "floor" in json.dumps(discovery).lower(),
          "Fill semantics/units discoverable from the actual registry")
    command("pattern.length", pattern=pattern, length=24)
    client.data("vibera_select", {"pattern": pattern, "channel": hat, "scope": 1})
    for channel, beat, length, pitch, velocity in [(hat, .125, 2, 67, .45), (hat, .25, 1, 62, .2),
                                                  (hat, 25, 1, 50, .7), (ghost, .25, 2, 65, .3)]:
        command("notes.add", pattern=pattern, channel=channel, beat=beat, length=length, pitch=pitch, value=velocity)
    command("pattern.length", pattern=pattern, length=24)
    before = notes()
    kept, removed, outside, untouched = before
    client.data("vibera_selection", {"action": "set", "target": "notes", "ids": [kept["id"], removed["id"]]})
    command("steps.fill2", pattern=pattern, channel=hat)
    after = notes()
    by_id = {n["id"]: n for n in after}
    check(len(after) == 50 and max(n["beat"] for n in after if n["channel"] == hat and n["step"]) == 23.5,
          "Fill reaches all96 Pattern cells beyond the old64-step display cap")
    survivor = by_id[kept["id"]]
    check(survivor["beat"] == 0 and survivor["length"] == .25 and survivor["step"] and
          survivor["pitch"] == kept["pitch"] and survivor["velocity"] == kept["velocity"],
          "Conversion preserves original ID/pitch/velocity and uses the step grid")
    check(removed["id"] not in by_id and by_id[outside["id"]] == outside and by_id[untouched["id"]] == untouched,
          "Off-mask removed; outside-loop and unrelated Channel remain exact")
    modes = {r["id"]: r["committedSequenceMode"] for r in client.data("vibera_rack", {"action": "state"})["rows"]}
    check(modes[hat] == "steps" and modes[ghost] == "pianoRoll",
          "Shared committed grid readback ignores dormant outside-loop melody without hiding active melody")
    check(client.data("vibera_selection", {"action": "state", "target": "notes"})["ids"] == [kept["id"]],
          "Shared selection prunes removed note IDs")
    revision = client.data("vibera_state")["revision"]
    command("steps.fill2", pattern=pattern, channel=hat)
    check(client.data("vibera_state")["revision"] == revision and notes() == after, "Idempotent fill retains identities/history")
    command("edit.undo")
    check(notes() == before, "One exact conversion Undo")
    command("edit.redo")
    check(notes() == after, "Exact conversion Redo")
    for args in [{"pattern": pattern, "channel": hat, "interval": 3},
                 {"pattern": pattern, "channel": hat, "interval": 2.5},
                 {"pattern": pattern, "ids": [hat, 999999], "interval": 4},
                 {"pattern": pattern, "ids": [hat, hat], "interval": 4},
                 {"pattern": 999999, "channel": hat, "interval": 4}]:
        failed = client.call("vibera_command", {"command": "steps.fill", "args": args}, allow_failure=True)
        check(failed.get("isError") and failed["structuredContent"]["error"]["code"] and notes() == after,
              "Structured atomic rejection: " + json.dumps(args))
    command("rack.group.assign", ids=[kick, hat], text="Drums")
    client.data("vibera_select", {"channel": bass, "scope": 1})
    selected = client.data("vibera_rack", {"action": "select", "ids": [kick, hat]})
    command("steps.fill4", pattern=pattern, ids=selected["selectedChannels"])
    bulk = notes()
    check(sum(n["step"] for n in bulk if n["channel"] == kick) == 24 and
          sum(n["step"] for n in bulk if n["channel"] == hat) == 24,
          "Shared bulk selection fills each intended Channel")
    rack = client.data("vibera_rack", {"action": "scroll", "firstStep": 4000000})
    check(rack["viewport"]["totalSteps"] == 96 and rack["viewport"]["firstStep"] == rack["viewport"]["maximumFirstStep"] > 64,
          "Semantic Rack scroll reaches the full Pattern and reports real bounds")
    check(rack["activeChannel"] == bass and rack["fillIntervals"] == [2, 4, 8] and all(r["canFillSteps"] for r in rack["rows"]),
          "Fill availability/readback and hidden active MIDI recipient preserved")
    command("edit.undo")
    check(notes() == after, "Bulk fill one exact Undo")
    command("edit.redo")
    check(notes() == bulk, "Bulk fill Redo")
    # The generic operation defaults to4 through the advertised parameter contract.
    command("steps.fill", pattern=pattern, channel=kick)
    check(notes() == bulk, "Generic interval default matches Fill each4")
    for beat in [0, 24]:
        command("clip.add", pattern=pattern, beat=beat, length=24)
    project = output / "filled-beat.vibra"
    job = client.data("vibera_project", {"action": "save", "path": str(project), "overwrite": True})
    check(job["completionState"] == "pending", "Save acceptance is not completion")
    client.wait_job(job)
    client.data("vibera_project", {"action": "open", "path": str(project), "discardChanges": True})
    check(notes() == bulk and len(client.data("vibera_state")["clips"]) == 2,
          "Actual file reopen preserves fill events, properties and linked arrangement")
    wav_path = output / "filled-beat.wav"
    job = client.data("vibera_project", {"action": "export", "path": str(wav_path), "overwrite": True})
    check(job["completionState"] == "pending", "Export acceptance is not completion")
    client.wait_job(job)
    with wave.open(str(wav_path), "rb") as wav:
        check(wav.getframerate() == 48000 and wav.getnframes() == 1152000 and wav.getsampwidth() == 3 and wav.getnchannels() == 2,
              "Actual two-placement WAV duration/format")
        data = wav.readframes(wav.getnframes())
        peak = max(abs(int.from_bytes(data[i:i+3], "little", signed=True)) for i in range(0, len(data), 3)) / 8388608
        check(peak > .01, "Actual filled-score PCM above silence/dither")
    receipt.update(project=str(project), wav=str(wav_path), peak=peak, before=before, after=after, bulk=bulk)
    client.close()
    receipt.update(passed=True, exitCode=client.process.returncode)
except Exception as error:
    receipt["error"] = str(error)
    raise
finally:
    if client and client.process.poll() is None:
        client.close()
    receipt["elapsedSeconds"] = time.monotonic() - began
    (output / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps({k: receipt[k] for k in ["passed", "checks", "elapsedSeconds", "applicationSha256"]}))
