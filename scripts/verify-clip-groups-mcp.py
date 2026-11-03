"""Actual semantic Playlist grouping, history, leased gesture, project and WAV checks.

No pointer simulation, native-input, listening or hardware claim.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from dev_support import project_schema
import time
import wave

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
app, output = [Path(x).resolve() for x in sys.argv[1:3]]
output.mkdir(parents=True, exist_ok=True)
client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
if not client_path.exists():
    client_path = root / "skills/vibera-studio/scripts/stdio_client.py"
spec = importlib.util.spec_from_file_location("neutral", client_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=0,
               applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               nativeInput=False, listening=False, hardware=False)
began = time.monotonic()
client = None


def check(condition, message):
    if not condition:
        raise AssertionError(message)
    receipt["checks"] += 1


try:
    source = output / "original-audio.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((2, 2, 48000, 48000, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<hh", 3000 if i % 100 < 50 else -3000, 1000)
                                 for i in range(48000)))
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    client = module.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    # Compare musical placement state separately from membership readback.
    clips = lambda: [{k:v for k,v in c.items() if k != "groupId"}
                     for c in data("vibera_inspect", dict(entity="clips"))["items"]]
    groups = lambda: data("vibera_inspect", dict(entity="clipGroups"))["items"]
    current = state()
    check(current["projectVersion"] == project_schema(root) and current["clipGroupingEnabled"], "Current schema/default enabled")
    pattern, channel = current["selectedPattern"], current["channels"][0]["id"]
    command("notes.add", pattern=pattern, channel=channel, beat=0, length=1, pitch=60, value=.7)
    command("clip.add", pattern=pattern, beat=1, lane=1, length=2)
    command("audio.add", channel=0, text=str(source), length=1, beat=2, lane=2)
    command("automation.create", mixer=0, beat=3, lane=3, length=2)
    before = clips()
    members = [c["id"] for c in before]
    check(len(members) == 3 and {c["kind"] for c in before} == {"pattern", "audio", "automation"},
          "Actual mixed-source arrangement created through semantic commands")
    contract = data("vibera_commands", dict(command="clip.group", includeSchemas=True))["operations"][0]
    check(contract["supported"] and set(contract["arguments"]["properties"]) == {"ids", "grouping"},
          "Typed operation exposes only meaningful grouping parameters")
    command("clip.group", ids=members)
    grouped = groups()
    check(len(grouped) == 1 and grouped[0]["memberCount"] == 3, "One heterogeneous group")
    group = grouped[0]["id"]
    check(all(c["groupId"] == group for c in data("vibera_inspect",dict(entity="clips"))["items"]),
          "Every paginated Clip identifies its stable group")
    page = data("vibera_inspect", dict(entity="clipGroupMembers", group=group, limit=2))
    tail = data("vibera_inspect", dict(entity="clipGroupMembers", group=group, offset=page["nextOffset"]))
    check([v["id"] for v in page["items"] + tail["items"]] == sorted(members), "Paginated stable membership")
    revision = state()["revision"]
    command("clip.group", ids=members)
    check(state()["revision"] == revision, "Same group is idempotent")
    data("vibera_selection", dict(action="set", target="clips", ids=[members[1]]))
    check(set(state()["effectiveSelectedClips"]) == set(members) and state()["selectedClips"] == [members[1]],
          "Explicit anchor and effective whole-group targets separately readable")
    command("clip.move", ids=[members[1]], beat=1, lane=2)
    moved = clips()
    check(all(c["beat"] == old["beat"]+1 and c["lane"] == old["lane"]+2 and c["length"] == old["length"]
              for c, old in zip(moved, before)), "Member move preserves relative spacing and durations; default resize zero")
    command("edit.undo")
    check(clips() == before, "Move one exact Undo")
    command("edit.redo")
    check(clips() == moved, "Move exact Redo")
    command("edit.undo")
    command("clip.move", ids=[members[0]], beat=-100, lane=-127)
    check(all(c["beat"] == old["beat"]-1 and c["lane"] == old["lane"]-1 for c, old in zip(clips(), before)),
          "Common start/lane clamp preserves geometry")
    command("edit.undo")
    data("vibera_editor", dict(editor=2, action="grouping", enabled=False))
    check(state()["effectiveSelectedClips"] == [members[1]] and groups() == grouped, "Mode off preserves membership and individual anchor")
    command("clip.move", ids=[members[1]], beat=.5)
    changed = clips()
    check(changed[0] == before[0] and changed[2] == before[2] and changed[1]["beat"] == 2.5, "Bypass edits only target member")
    command("edit.undo")
    data("vibera_editor", dict(editor=2, action="grouping", enabled=True))
    command("clip.duplicate", ids=[members[0]], beat=8)
    copies = [c for c in clips() if c["id"] not in members]
    check(len(copies) == 3 and len(groups()) == 2 and groups()[1]["id"] != group, "Duplicate creates an independent group")
    check(all(c["source"] == old["source"] for c, old in zip(copies, before)), "Duplicate retains all source links")
    command("clip.delete", ids=[copies[0]["id"]])
    check(clips() == before and groups() == grouped, "Deleting copied member removes only copied group")
    command("edit.undo")
    check(len(clips()) == 6 and len(groups()) == 2, "Delete exact restore")
    command("edit.undo")
    command("clip.ungroup", ids=[members[0]])
    check(groups() == [] and clips() == before, "Ungroup from one member leaves musical data intact")
    command("edit.undo")
    gesture = data("vibera_gesture", dict(action="begin", label="Move group"))
    token = gesture["operation"]
    data("vibera_gesture", dict(action="update", operation=token, command="clip.move", args=dict(ids=[members[0]], beat=.5)))
    check(clips() != before, "Real continuous semantic gesture applies live")
    data("vibera_gesture", dict(action="cancel", operation=token))
    check(clips() == before and groups() == grouped, "Cancel restores exact original group/placements")
    for name, args in [("clip.group", dict(ids=[members[0]])), ("clip.group", dict(ids=[members[0], members[0]])),
                       ("clip.group", dict(ids=members+[9999999])), ("clip.move", dict(ids=[9999999], beat=1)),
                       ("clip.move", dict(ids=[members[0]], grouping="yes"))]:
        revision = state()["revision"]
        failed = client.call("vibera_command", dict(command=name, args=args), allow_failure=True)
        check(failed.get("isError") and failed["structuredContent"]["completionState"] == "failed"
              and failed["structuredContent"]["error"]["code"] and state()["revision"] == revision and clips() == before,
              "Invalid targets/parameters fail atomically with structured errors")
    def export(name):
        path = output/name
        job = data("vibera_project", dict(action="export", path=str(path), overwrite=True))
        check(job["completionState"] == "pending", "Export accepted separately from completion")
        client.wait_job(job)
        with wave.open(str(path), "rb") as wav:
            check(wav.getnframes() > 0 and wav.getnchannels() == 2, "Actual stereo WAV")
            return wav.readframes(wav.getnframes())
    grouped_pcm = export("grouped.wav")
    command("clip.ungroup", ids=[members[0]])
    check(export("ungrouped.wav") == grouped_pcm, "Group membership does not alter rendered PCM")
    command("edit.undo")
    project = output/"grouped-song.vibra"
    client.wait_job(data("vibera_project", dict(action="save", path=str(project), overwrite=True)))
    client.wait_job(data("vibera_project", dict(action="open", path=str(project), discardChanges=True)))
    check(groups() == grouped and clips() == before, "Real project save/reopen retains stable group/source identities")
    check(export("reopened.wav") == grouped_pcm, "Reopened project renders exact audio")
    check(hashlib.sha256(source.read_bytes()).hexdigest() == source_hash, "Source file remains unchanged")
    blocker = output/"blocked-parent"
    blocker.write_text("retained fixture", encoding="utf-8")
    job = data("vibera_project", dict(action="save", path=str(blocker/"fail.vibra")))
    check(job["completionState"] == "pending", "Actual failed save initially only accepted")
    deadline = time.monotonic()+10
    while job["completionState"] == "pending":
        check(time.monotonic() < deadline, "Failure reaches terminal state")
        time.sleep(.03)
        job = client.call("vibera_project", dict(action="job", operation=job["operation"]), allow_failure=True)["structuredContent"]
    check(job["completionState"] == "failed" and job["error"]["code"] == "operation_failed"
          and groups() == grouped, "Real async failure preserves the working group/project")
    receipt["passed"] = True
except Exception as error:
    receipt["error"] = str(error)
finally:
    if client:
        try:
            client.close()
            receipt["exitCode"] = client.process.returncode
        except Exception as error:
            receipt["passed"] = False
            receipt["closeError"] = str(error)
    receipt["finished"] = True
    receipt["seconds"] = time.monotonic()-began
    (output/"verification.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(receipt))
sys.exit(0 if receipt["passed"] else 1)
