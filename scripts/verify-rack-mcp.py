"""Exercise Rack groups through a real owned stdio application, including file jobs.

No simulated pointer input, physical MIDI, hardware or listening qualification.
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
    rack = lambda **args: client.data("vibera_rack", args or {"action": "state"})
    command = lambda name, **args: client.data("vibera_command", {"command": name, "args": args})
    initial = client.data("vibera_state")
    pattern = initial["selectedPattern"]
    channels = [c["id"] for c in initial["channels"]]
    check(initial["projectVersion"] == project_schema(root) and len(channels) >= 4, "Current-schema disposable project")
    check(rack()["visibleChannelIds"] == channels and not rack()["groups"], "All initially includes every Channel")
    active = rack()["activeChannel"]
    selected = channels[1:3]
    state = rack(action="select", ids=selected)
    check(state["selectedChannels"] == selected and state["activeChannel"] == active,
          "Bulk selection leaves live MIDI target unchanged")
    state = rack(action="group", name="Drums")
    group = state["groups"][0]["id"]
    check(state["visibleChannelIds"] == selected and state["filter"]["group"] == group,
          "Group Selected atomically creates membership/filter")
    state = rack(action="select", ids=[channels[0]], mode="add")
    check(state["selectedChannels"] == selected + channels[:1], "Hidden Channels remain addressable by stable ID")
    rack(action="all")
    check(rack()["selectedChannels"] == selected, "Select all defaults to visible Channels")
    command("rack.group.rename", target=group, text="Percussion")
    check(rack()["groups"][0]["id"] == group and rack()["groups"][0]["name"] == "Percussion",
          "Rename retains membership identity")
    before = rack()
    for tool, args in [
        ("vibera_rack", {"action": "select", "ids": [999999]}),
        ("vibera_rack", {"action": "filter", "kind": "group", "group": 999999}),
        ("vibera_rack", {"action": "select", "ids": [channels[0], channels[0]]}),
        ("vibera_rack", {"action": "group", "name": "All"}),
        ("vibera_rack", {"action": "filter", "kind": "all", "revision": before["revision"] - 1}),
        ("vibera_command", {"command": "rack.group.assign", "args": {"ids": selected + [999999], "target": group}}),
    ]:
        failed = client.call(tool, args, allow_failure=True)
        check(failed.get("isError") and failed["structuredContent"]["error"]["code"] and rack() == before,
              "Structured atomic rejection: " + json.dumps(args))
    command("channel.add", text="builtin:hat")
    new_channel = client.data("vibera_state")["channels"][-1]["id"]
    check(new_channel in rack()["visibleChannelIds"] and rack()["groups"][0]["channelCount"] == 3,
          "New Channel inherits displayed named group")
    command("edit.undo")
    check(rack()["visibleChannelIds"] == selected, "One Undo removes only new grouped Channel")
    command("edit.redo")
    command("rack.group.create", text="Empty")
    empty = rack()["groups"][-1]["id"]
    check(rack()["visibleChannelIds"] == [] and rack()["filter"]["group"] == empty,
          "Empty named group is usable")
    rack(action="filter", kind="unsorted")
    check(rack()["visibleChannelIds"] == [c for c in channels if c not in selected], "Unsorted excludes assigned Channels")
    rack(action="filter", kind="group", group=group)
    command("steps.set", pattern=pattern, channel=selected[0], beat=0, enabled=True)
    command("steps.set", pattern=pattern, channel=selected[1], beat=1, enabled=True)
    notes = client.data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"]
    check({n["channel"] for n in notes} == set(selected), "Stable filtered Channel targets produce intended beat")
    command("clip.add", pattern=pattern, beat=0, length=4)
    project = output / "grouped-beat.vibra"
    job = client.data("vibera_project", {"action": "save", "path": str(project), "overwrite": True})
    check(job["completionState"] == "pending", "Save acceptance is not completion")
    client.wait_job(job)
    command("rack.group.delete", target=group)
    check(len(client.data("vibera_state")["channels"]) == len(channels) + 1 and
          all(c["group"] == 0 for c in client.data("vibera_state")["channels"]),
          "Delete group unassigns but never deletes Channels")
    command("edit.undo")
    check(rack()["groups"][0]["id"] == group and rack()["groups"][0]["channelCount"] == 3,
          "Group deletion Undo restores exact identity/membership")
    command("edit.redo")
    client.data("vibera_project", {"action": "open", "path": str(project), "discardChanges": True})
    restored = rack()
    check(restored["filter"]["kind"] == "all" and [(g["name"], g["channelCount"]) for g in restored["groups"]] ==
          [("Percussion", 3), ("Empty", 0)], "Real reopen restores empty/named groups; view filter resets to All")
    check(client.data("vibera_inspect", {"entity": "notes", "pattern": pattern})["items"] == notes,
          "Grouped beat survives project persistence")
    wav_path = output / "grouped-beat.wav"
    job = client.data("vibera_project", {"action": "export", "path": str(wav_path), "overwrite": True})
    check(job["completionState"] == "pending", "Export acceptance is not completion")
    client.wait_job(job)
    with wave.open(str(wav_path), "rb") as wav:
        check(wav.getframerate() == 48000 and wav.getnframes() == 96000 and wav.getsampwidth() == 3,
              "Actual four-beat WAV has expected timing/format")
        data = wav.readframes(wav.getnframes())
        peak = max(abs(int.from_bytes(data[i:i+3], "little", signed=True)) for i in range(0, len(data), 3)) / 8388608
        check(peak > .01, "Actual grouped beat export contains musical PCM")
    (output / "blocked-parent").write_text("Owned failure fixture", encoding="utf-8")
    failed_job = client.data("vibera_project", {"action": "export", "path": str(output / "blocked-parent" / "fail.wav"), "overwrite": True})
    # A regular file cannot be used as the export's parent directory.
    deadline = time.monotonic() + 40
    while failed_job["completionState"] == "pending" and time.monotonic() < deadline:
        time.sleep(.03)
        failed_reply = client.call("vibera_project", {"action": "job", "operation": failed_job["operation"]}, allow_failure=True)
        failed_job = failed_reply["structuredContent"]
    check(failed_job["completionState"] == "failed" and failed_job["error"]["code"] == "operation_failed",
          "Export failure is terminal and explicit")
    for _ in range(20):
        command("channel.add", text="builtin:hat")
    scrolled = rack(action="scroll", firstRow=128, firstStep=64)
    viewport = scrolled["viewport"]
    check(viewport["firstRow"] == viewport["maximumFirstRow"] > 0 and
          viewport["firstStep"] == viewport["maximumFirstStep"], "Semantic Rack scrolling uses shared clamped viewport")
    filtered = rack(action="filter", kind="group", group=group)
    check(filtered["viewport"]["firstRow"] == 0 and filtered["visibleChannelIds"] == selected + [new_channel],
          "Filtering resets scroll and retains exact stable targets")
    bad_scroll = client.call("vibera_rack", {"action":"scroll","firstRow":-1}, allow_failure=True)
    check(bad_scroll.get("isError") and rack() == filtered, "Invalid scroll input fails without changing viewport")
    receipt.update(groups=restored["groups"], notes=notes, peak=peak, project=str(project), wav=str(wav_path))
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
