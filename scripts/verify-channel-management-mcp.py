"""Real Channel settings clone, score transfer, reorder/delete and async acceptance."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import wave

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
app, output = [Path(value).resolve() for value in sys.argv[1:3]]
output.mkdir(parents=True, exist_ok=True)
path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
if not path.exists():
    path = root / "skills/vibera-studio/scripts/stdio_client.py"
spec = importlib.util.spec_from_file_location("neutral_client", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=0, applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               nativeInput=False, physicalMidi=False, listening=False)
receipt_path = output / "verification.json"
receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
client = None
began = time.monotonic()


def check(condition, text):
    if not condition:
        raise AssertionError(text)
    receipt["checks"] += 1


try:
    client = module.VibeRaClient(app)
    receipt["version"] = client.server["version"]
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    rack = lambda **args: data("vibera_rack", {"action":"state", **args})
    ids = [c["id"] for c in state()["channels"]]
    kick, snare, hat, bass = ids
    pattern = state()["selectedPattern"]
    for name in ["channel.clone", "channel.delete"]:
        entry = data("vibera_commands", dict(command=name, includeSchemas=True))["operations"][0]
        check(entry["mapping"] == "vibera_rack" and entry["invocation"]["action"] in ["clone", "delete"] and
              "pending" not in entry["invocation"], "Stable typed service discovery")
    for channel in [kick, hat, bass]:
        command("notes.add", pattern=pattern, channel=channel, beat=0, length=1, pitch=60, value=.7)
    command("clip.add", pattern=pattern, beat=0, length=4)
    command("clip.add", pattern=pattern, beat=4, length=4)
    command("channel.sampler.cents", channel=kick, value=17)
    command("rack.group.assign", ids=[kick, hat], text="Drums")
    group = next(g["id"] for g in rack()["groups"] if g["name"] == "Drums")
    rack(action="filter", kind="group", group=group)
    rack(action="select", ids=[kick, hat], activeChannel=kick)
    command("clipboard.copy")
    baseline = output / "baseline-beat.vibra"
    client.wait_job(data("vibera_project", dict(action="save", path=str(baseline), overwrite=True)))
    before = state()
    read_notes = lambda: data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"]
    before_notes = read_notes()

    def wait(job, expected="completed"):
        deadline = time.monotonic() + 20
        while job["completionState"] == "pending" and time.monotonic() < deadline:
            time.sleep(.01)
            job = client.call("vibera_rack", dict(action="job", operation=job["operation"]), allow_failure=True)["structuredContent"]
        check(job["completionState"] == expected, "Channel job has actual terminal " + expected)
        return job

    job = rack(action="clone")
    check(job["completionState"] == "pending" and not job["modelCommitted"], "Clone acceptance is not completion")
    job = wait(job)
    check(any(entry["operation"] == job["operation"] for entry in rack()["channelOperations"]), "State discovery recovers existing jobs after an interrupted client")
    check(data("vibera_rack", dict(action="job", operation=job["operation"], projectIdentity="older-project"))["modelCommitted"], "Owned terminal job remains readable across project-session guards")
    created = job["createdChannels"]
    check(len(created) == 2 and job["modelCommitted"] and not job["cloneIncludesScore"], "Committed independent settings-only clones")
    order = [c["id"] for c in state()["channels"]]
    check(order == [kick, created[0], snare, hat, created[1], bass], "Bulk clone inserts each adjacent in global order")
    check(read_notes() == before_notes and len(data("vibera_inspect", dict(entity="clips"))["items"]) == 2, "Cloning preserves score and linked arrangement")
    check(rack()["selectedChannels"] == created and rack()["activeChannel"] == created[0], "Clones become shared selection/active paste recipient")
    original = next(c for c in state()["channels"] if c["id"] == kick)
    clone = next(c for c in state()["channels"] if c["id"] == created[0])
    check(clone["sampler"] == original["sampler"] and clone["mixer"] == original["mixer"] and clone["gain"] == original["gain"], "Real clone retains sampler controls and Mixer/gain settings")
    check(all(c["group"] == group for c in rack()["rows"] if c["id"] in created), "Clone stays in its display group")
    command("clipboard.paste")
    notes = data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"]
    check(any(n["channel"] == created[0] for n in notes), "Familiar Copy, Clone, Paste transfers the score explicitly")
    command("edit.undo")
    command("edit.undo")
    (output / "undo-diagnostic.json").write_text(json.dumps(dict(before=before, after=state()), indent=2), encoding="utf-8")
    check([c["id"] for c in state()["channels"]] == ids and read_notes() == before_notes, "Clone and paste Undo independently")
    command("edit.redo"); command("edit.redo")
    original_notes = data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"]
    command("channel.moveDown", ids=[kick, created[0]])
    check([c["id"] for c in state()["channels"]] == [snare, kick, created[0], hat, created[1], bass], "Selected contiguous block moves without reversing")
    check(data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"] == original_notes, "Reorder preserves actual musical identities/data")
    command("edit.undo")
    stable = state()
    for bad_ids in [[kick, kick], [kick, 999999], []]:
        for action in ["clone", "delete"]:
            result = client.call("vibera_rack", dict(action=action, ids=bad_ids), allow_failure=True)
            check(result.get("isError") and state()["revision"] == stable["revision"], "Invalid Channel transaction refuses atomically")
    # Cancellation and stale-revision failure exercise real asynchronous requests.
    # If a transaction already won the race, undo it before a bounded retry.
    for expected in ["cancelled", "failed"]:
        observed = False
        for attempt in range(20):
            job = rack(action="clone", ids=[kick])
            if expected == "cancelled":
                token = job["operation"]
                response = client.call("vibera_rack", dict(action="cancel", operation=job["operation"]), allow_failure=True)
                job = response.get("structuredContent", {})
                if job.get("completionState") != "cancelled":
                    job = wait(rack(action="job", operation=token))
            else:
                command("notes.add", pattern=pattern, channel=kick, beat=2, length=.5, pitch=62, value=.5)
                job = client.call("vibera_rack", dict(action="job", operation=job["operation"]), allow_failure=True)["structuredContent"]
                deadline = time.monotonic()+20
                while job["completionState"] == "pending" and time.monotonic() < deadline:
                    time.sleep(.01)
                    job = client.call("vibera_rack", dict(action="job", operation=job["operation"]), allow_failure=True)["structuredContent"]
                command("edit.undo")
            if job and job.get("completionState") == expected:
                check(not job["modelCommitted"] and (expected == "cancelled" or job["error"]["code"] == "stale_revision"), "Real async cancellation/stale failure retains project")
                observed = True
                break
            command("edit.undo")
        check(observed, "Actual asynchronous " + expected + " observed, never inferred from schema")
        # A cancelled capture must drain before another writer can start.
        time.sleep(.1)
    job = wait(rack(action="delete", ids=[created[0]]))
    check(not any(c["id"] == created[0] for c in state()["channels"]), "Delete removes Channel")
    check(not any(n["channel"] == created[0] for n in data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"]), "Delete removes only owned score")
    command("edit.undo")
    target = output / "managed-beat.vibra"
    client.wait_job(data("vibera_project", dict(action="save", path=str(target), overwrite=True)))
    client.wait_job(data("vibera_project", dict(action="open", path=str(target), discardChanges=True)))
    check([c["id"] for c in state()["channels"]] == order, "Cloned/reordered/restored project round trip")
    wav = output / "managed-beat.wav"
    client.wait_job(data("vibera_project", dict(action="export", path=str(wav), overwrite=True)))
    with wave.open(str(wav), "rb") as audio:
        check(audio.getframerate() == 48000 and audio.getnchannels() == 2 and audio.getnframes() == 192000 and audio.getsampwidth() == 3, "Actual exact-length 24-bit WAV export after Channel transactions")
        pcm = audio.readframes(audio.getnframes())
    peak = max(abs(int.from_bytes(pcm[i:i+3], "little", signed=True)) for i in range(0,len(pcm),3))/(1<<23)
    check(peak > .01, "Rendered clone/score/arrangement has non-silent PCM; not a listening claim")
    receipt.update(project=str(target), wav=str(wav), cloneIds=created, peak=peak)
    client.close()
    receipt.update(passed=True, exitCode=client.process.returncode)
except Exception as failure:
    receipt["error"] = str(failure)
    raise
finally:
    if client and client.process.poll() is None:
        client.close()
    receipt.update(finished=True, elapsedSeconds=time.monotonic()-began)
    receipt_path.write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
print(json.dumps({k: receipt[k] for k in ["passed", "checks", "elapsedSeconds", "applicationSha256"]}))
