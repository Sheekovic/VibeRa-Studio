"""Real semantic MCP selection/gesture/persistence qualification; no OS input or audio claim."""
import array
import hashlib
import json
import pathlib
import sys
import wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
app, output = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
client_directory = app.parent / "skills/vibera-studio/scripts"
if not client_directory.is_dir():
    client_directory = ROOT / "skills/vibera-studio/scripts"
sys.path.insert(0, str(client_directory))
from stdio_client import VibeRaClient

output.mkdir(parents=True, exist_ok=True)
report = {"passed": False, "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
          "client": str(client_directory / "stdio_client.py"), "checks": [],
          "evidence": "Actual owned stdio application; semantic operations/persistence. No callbacks, OS input, hardware or listening qualified."}
client = None


def check(condition, case):
    if not condition:
        raise AssertionError(case)
    report["checks"].append(case)


try:
    client = VibeRaClient(app)
    report["version"] = client.server["version"]
    client.data("vibera_project", {"action": "new", "discardChanges": True})
    state = client.data("vibera_state")
    pattern, channel = state["selectedPattern"], state["selectedChannel"]

    def command(name, **args):
        return client.data("vibera_command", {"command": name, "args": args})

    command("project.tempo", value=120)
    command("notes.add", pattern=pattern, channel=channel, beat=0, length=2, pitch=60, value=.8)
    command("notes.add", pattern=pattern, channel=channel, beat=2, length=.25, pitch=64, value=.6)
    notes = client.data("vibera_inspect", {"entity": "notes"})["items"]
    sustain = notes[0]["id"]
    starts = client.data("vibera_selection", {"action": "range", "target": "notes", "start": 1, "end": 1.5})
    overlap = client.data("vibera_selection", {"action": "range", "target": "notes", "start": 1, "end": 1.5, "criterion": "overlaps"})
    check(starts["ids"] == [] and overlap["ids"] == [sustain], "Sustained notes: starts versus overlap")
    tone = output / "original-selection-fixture.wav"
    with wave.open(str(tone), "wb") as writer:
        writer.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        writer.writeframes(array.array("h", [1200 if i % 200 < 100 else -1200 for i in range(96000)]).tobytes())
    command("clip.add", pattern=pattern, beat=0, length=4, lane=1)
    command("audio.add", channel=0, text=str(tone), length=2, lane=2, mixer=1)
    command("automation.create", mixer=0, beat=0, length=4, lane=3)
    clips = client.data("vibera_inspect", {"entity": "clips"})["items"]
    ids = [clip["id"] for clip in clips]
    check({clip["kind"] for clip in clips} == {"pattern", "audio", "automation"}, "Three actual Playlist source kinds")
    revision = client.data("vibera_state")["revision"]
    ranged = client.data("vibera_selection", {"action": "range", "target": "clips", "start": 2.5,
                                                "end": 3.5, "low": 1, "high": 3, "criterion": "overlaps"})
    check(ranged["ids"] == ids and ranged["revision"] == revision, "Mixed clip overlap converts audio seconds to beats without document edits")
    audio = next(clip for clip in clips if clip["kind"] == "audio")
    audio_only = client.data("vibera_selection", {"action": "range", "target": "clips", "start": 3,
                                                    "end": 3.5, "low": 2, "high": 2, "criterion": "overlaps"})
    check(audio_only["ids"] == [audio["id"]], "Lane filtering retains the two-second Audio Clip at beat three")
    invalid = client.call("vibera_selection", {"action": "set", "target": "clips", "ids": [ids[0], 999999]}, allow_failure=True)
    check(invalid.get("isError") and client.data("vibera_selection", {"action": "state", "target": "clips"})["ids"] == [audio["id"]], "Invalid mixed selection is atomic")
    mixer_before = client.data("vibera_inspect", {"entity": "mixer"})["items"]
    gesture = client.data("vibera_gesture", {"action": "begin", "label": "Continuous Master gain"})
    for value in [.25, .55, .7]:
        preview = client.data("vibera_gesture", {"action": "update", "operation": gesture["operation"],
                                                   "command": "mixer.gain", "args": {"mixer": 0, "value": value}})
        check(preview["completionState"] == "pending", "Master gain preview remains pending: " + str(value))
    committed = client.data("vibera_gesture", {"action": "commit", "operation": gesture["operation"]})
    mixer_after = client.data("vibera_inspect", {"entity": "mixer"})["items"]
    check(committed["completionState"] == "completed" and abs(mixer_after[0]["gain"]-.7) < 1e-6, "Master gain commits actual model value")
    command("edit.undo")
    check(client.data("vibera_inspect", {"entity": "mixer"})["items"] == mixer_before, "Master gain sweep is one exact Undo")
    command("edit.redo")
    check(client.data("vibera_inspect", {"entity": "mixer"})["items"] == mixer_after, "Master gain exact Redo")
    project = output / "selection-gesture.vibra"
    client.wait_job(client.data("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
    check(not client.data("vibera_state")["dirty"], "Actual save establishes a clean baseline")
    cancelled = client.data("vibera_gesture", {"action": "begin"})
    client.data("vibera_gesture", {"action": "update", "operation": cancelled["operation"], "command": "notes.move",
                                 "args": {"pattern": pattern, "ids": [sustain], "beat": .5, "pitch": 2}})
    terminal = client.data("vibera_gesture", {"action": "cancel", "operation": cancelled["operation"]})
    check(terminal["completionState"] == "cancelled" and not client.data("vibera_state")["dirty"] and
          client.data("vibera_inspect", {"entity": "notes"})["items"] == notes, "Cancel restores a saved clean project exactly")
    client.data("vibera_project", {"action": "new"})
    client.data("vibera_project", {"action": "open", "path": str(project)})
    check(client.data("vibera_inspect", {"entity": "notes"})["items"] == notes and
          client.data("vibera_inspect", {"entity": "clips"})["items"] == clips and
          client.data("vibera_inspect", {"entity": "mixer"})["items"] == mixer_after, "Real project round trip preserves selection targets and committed mix")
    report["passed"] = True
except Exception as error:
    report["error"] = str(error)
finally:
    if client:
        try:
            client.close()
            report["exitCode"] = client.process.returncode
        except Exception as error:
            report["passed"] = False
            report["closeError"] = str(error)
    (output / "verification.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
print(json.dumps({"passed": report["passed"], "checks": len(report["checks"]), "error": report.get("error")}))
raise SystemExit(0 if report["passed"] else 1)
