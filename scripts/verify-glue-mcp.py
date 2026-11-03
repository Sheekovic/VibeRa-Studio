"""Real semantic Glue edge/persistence/export acceptance using the neutral client.

Owned disposable application only; no pointer input, plugin substitute or listening.
"""
import hashlib
import importlib.util
import json
import pathlib
import sys
import time
import wave
from dev_support import project_schema

sys.dont_write_bytecode = True
root = pathlib.Path(__file__).resolve().parents[1]
app = pathlib.Path(sys.argv[1]).resolve(strict=True)
output = pathlib.Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
if not client_path.exists():
    client_path = root / "skills/vibera-studio/scripts/stdio_client.py"
spec = importlib.util.spec_from_file_location("vibera_neutral_client", client_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = {"passed": False, "checks": 0, "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
           "client": str(client_path), "clientSha256": hashlib.sha256(client_path.read_bytes()).hexdigest(),
           "nativeInput": False, "listening": False, "hardware": False}
receipt_path = output / "verification.json"
receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
began = time.monotonic()
client = None


def check(condition, description):
    if not condition:
        raise AssertionError(description)
    receipt["checks"] += 1


try:
    client = module.VibeRaClient(app)
    receipt["version"] = client.server["version"]
    state = client.data("vibera_state")
    pattern, channel, ghost = state["selectedPattern"], state["channels"][-1]["id"], state["channels"][0]["id"]
    check(state["projectVersion"] == project_schema(root) and state["notes"] == [], "Disposable current-schema project")
    client.data("vibera_select", {"pattern": pattern, "channel": channel, "scope": 2})
    discovery = client.data("vibera_commands", {"command": "notes.glue", "includeSchemas": True})
    check("notes.glue" in json.dumps(discovery) and "surviv" in json.dumps(discovery).lower(),
          "Glue contract/readback semantics are discoverable")
    # Simultaneous onsets, nested/overlapping span, genuine small gap, another
    # pitch and another Channel: independent fixed expectations, not a union implementation.
    for owner, beat, length, pitch, velocity in [
        (channel, 0, 1, 60, .2), (channel, 0, .5, 60, .9), (channel, .75, .5, 60, .8),
        (channel, 1.25000001, .25, 60, .7), (channel, 0, 1, 64, .6), (ghost, 0, .5, 60, .3)]:
        client.data("vibera_command", {"command": "notes.add", "args": {
            "pattern": pattern, "channel": owner, "beat": beat, "length": length, "pitch": pitch, "value": velocity}})

    def notes():
        return client.data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]

    before = notes()
    selected = [n["id"] for n in before if n["channel"] == channel]
    client.data("vibera_selection", {"action": "set", "target": "notes", "ids": selected})
    client.data("vibera_command", {"command": "notes.glue", "args": {"pattern": pattern, "ids": selected}})
    after = notes()
    by_id = {n["id"]: n for n in after}
    check(len(after) == 4, "Duplicate-onset and overlap notes joined")
    check(by_id[before[0]["id"]]["beat"] == 0 and by_id[before[0]["id"]]["length"] == 1.25 and
          abs(by_id[before[0]["id"]]["velocity"] - .2) < 1.e-6,
          "Earliest/lowest identity and velocity survive the union")
    check(by_id[before[3]["id"]] == before[3], "Genuine small gap remains; no snap-sized tolerance")
    check(by_id[before[4]["id"]] == before[4] and by_id[before[5]["id"]] == before[5],
          "Other pitch and ghost Channel remain exact")
    check(set(client.data("vibera_selection", {"action": "state", "target": "notes"})["ids"]) ==
          {before[0]["id"], before[3]["id"], before[4]["id"]}, "Shared selection retains only surviving targets")
    failed = client.call("vibera_command", {"command": "notes.glue", "args": {"pattern": pattern, "ids": selected}},
                         allow_failure=True)
    check(failed.get("isError") and failed["structuredContent"]["error"]["code"] and notes() == after,
          "Stale identities produce structured atomic refusal")
    client.data("vibera_command", {"command": "edit.undo"})
    check(notes() == before, "One exact Glue Undo")
    client.data("vibera_command", {"command": "edit.redo"})
    check(notes() == after, "Exact Glue Redo")
    for beat in [0, 4]:
        client.data("vibera_command", {"command": "clip.add", "args": {"pattern": pattern, "beat": beat, "length": 4}})
    def clip_rows():
        return [{k: v for k, v in c.items() if k != "playlistBounds"} for c in client.data("vibera_state")["clips"]]

    clips = clip_rows()
    check(len(clips) == 2 and all(c["kind"] == "pattern" and c["sourceId"] == pattern for c in clips), "Two linked placements retain same Pattern")
    project = output / "glue-edge-song.vibra"
    job = client.data("vibera_project", {"action": "save", "path": str(project), "overwrite": True})
    check(job["completionState"] == "pending", "Save acceptance is not false completion")
    client.wait_job(job)
    client.data("vibera_project", {"action": "open", "path": str(project), "discardChanges": True})
    check(notes() == after and clip_rows() == clips, "Exact notes/links after actual reopen")
    wav_path = output / "glue-edge-song.wav"
    job = client.data("vibera_project", {"action": "export", "path": str(wav_path), "overwrite": True})
    check(job["completionState"] == "pending", "Export acceptance is not false completion")
    client.wait_job(job)
    with wave.open(str(wav_path), "rb") as wav:
        check(wav.getframerate() == 48000 and wav.getsampwidth() == 3 and wav.getnchannels() == 2 and
              wav.getnframes() == 192000, "Actual linked eight-beat WAV timing and format")
        data = wav.readframes(wav.getnframes())
        peak = max(abs(int.from_bytes(data[i:i+3], "little", signed=True)) for i in range(0, len(data), 3)) / 8388608
        check(peak > .01, "Actual Glue export contains musical PCM above silence/dither")
    receipt.update(before=before, after=after, linkedClips=clips, project=str(project), wav=str(wav_path), peak=peak)
    client.close()
    receipt.update(passed=True, exitCode=client.process.returncode)
except Exception as error:
    receipt["error"] = str(error)
    raise
finally:
    try:
        if client and client.process.poll() is None:
            client.close()
    finally:
        receipt["elapsedSeconds"] = time.monotonic() - began
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps({k: receipt[k] for k in ["passed", "checks", "elapsedSeconds", "applicationSha256"]}))
