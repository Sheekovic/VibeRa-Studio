"""Audio-input recording through MCP on the real application without an audio device.

Covers discovery (schema, agent visibility), validation, preference readback, the
truthful no-device readiness, and refusals of takes in Pattern mode and without a
device through the ordinary transport path (transport.record + play). Capture,
latency compensation and placement on a running device are covered by
audio_take_gui. No microphone, hardware or listening is involved.
"""
import importlib.util
import json
import wave
from pathlib import Path
import sys
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stdio_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)
app = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve() / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


client = None
passed = False
try:
    client = client_module.VibeRaClient(app)
    client.process.stdin.reconfigure(newline="\n")
    data = client.data
    rec = lambda **args: data("vibera_audioRecording", args)
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))

    tools = {tool["name"]: tool for tool in client.request("tools/list", {})["tools"]}
    tool = tools.get("vibera_audioRecording")
    check(tool is not None and tool["inputSchema"]["properties"]["action"]["enum"] ==
          ["state", "configure", "cancel", "placeRecovered", "forgetRecovered"],
          "vibera_audioRecording is discoverable with its five actions")
    record = json.dumps(data("vibera_commands", dict(command="transport.record")))
    check("audio input when enabled" in record, "transport.record describes MIDI and audio-input recording")

    state = rec(action="state")
    check(state["enabled"] is False and state["readiness"] == "disabled" and state["capturing"] is False,
          "Audio input starts disabled and idle")
    check(state["take"]["phase"] == "idle" and state["take"]["completionState"] == "none",
          "No take before recording")
    check(state["device"]["running"] is False and state["device"]["activeInputs"] == 0,
          "Device readback is truthful without an audio device")

    for bad, label in ((dict(channels=3), "channels"), (dict(input=40), "input range"),
                       (dict(monitorGain=2.5), "monitor gain range")):
        rejected = client.call("vibera_audioRecording", dict(action="configure", **bad), allow_failure=True)
        check(bool(rejected.get("isError")), "Invalid " + label + " rejected")
    check(rec(action="state")["channels"] == 2, "Rejected configuration changes nothing")

    configured = rec(action="configure", enabled=True, input=1, channels=1, monitor=True, monitorGain=.5)
    check(configured["enabled"] and configured["input"] == 1 and configured["channels"] == 1 and
          configured["monitor"] and abs(configured["monitorGain"] - .5) < 1e-6,
          "Configuration reads back exactly")
    check(configured["readiness"] == "noDevice", "Enabled input without a device reports noDevice, not ready")

    cancel = client.call("vibera_audioRecording", dict(action="cancel"), allow_failure=True)
    check(bool(cancel.get("isError")), "Cancel without a capturing take is refused")

    # Pattern mode refuses audio takes through the ordinary Record + Play path.
    data("vibera_transport", dict(action="pattern"))
    data("vibera_command", dict(command="transport.record"))
    data("vibera_transport", dict(action="play"))
    take = rec(action="state")["take"]
    data("vibera_transport", dict(action="stop"))
    check(take["code"] == "audioTake.patternMode" and take["completionState"] == "failed",
          "Pattern mode refuses the audio take with a structured code")

    data("vibera_transport", dict(action="song"))
    data("vibera_command", dict(command="transport.record"))
    data("vibera_transport", dict(action="play"))
    take = rec(action="state")["take"]
    data("vibera_transport", dict(action="stop"))
    check(take["code"] == "audioTake.noDevice" and not rec(action="state")["capturing"],
          "Song mode without a device refuses truthfully and captures nothing")
    check(take["clip"] == 0 and take["file"] == "", "Refused takes place no Audio Clip and keep no file")

    check(not rec(action="configure", enabled=False)["enabled"], "Audio input disables")
    check(rec(action="state")["recovered"] == [], "No interrupted takes in a test session")
    missing = client.call("vibera_audioRecording", dict(action="placeRecovered", file=str(output / "none.wav")),
                          allow_failure=True)
    check(bool(missing.get("isError")), "Placing an unknown recovered take is rejected")
    check(rec(action="forgetRecovered")["recovered"] == [], "forgetRecovered clears the list")
    check(rec(action="configure", punchRange=[2, 6])["punchRange"] == [2, 6], "Punch range reads back")
    bad = client.call("vibera_audioRecording", dict(action="configure", punchRange=[4, 1]), allow_failure=True)
    check(bool(bad.get("isError")), "An inverted punch range is rejected")
    check(rec(action="configure", punchRange=None)["punchRange"] is None, "A null punch range clears it")

    # Loop takes and comping through the canonical commands (what a loop recording places).
    source = output / "takes.wav"
    with wave.open(str(source), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(bytes(2 * 48000 * 6))
    data("vibera_command", dict(command="project.tempo", args=dict(value=120)))
    takes = [dict(beat=0, lane=1 + i, offset=2 * i, duration=2, muted=i < 2) for i in range(3)]
    rejected = client.call("vibera_command", dict(command="audio.takes", args=dict(
        text=str(source), length=6, takes=[dict(beat=0, lane=1, offset=5, duration=2)])), allow_failure=True)
    check(bool(rejected.get("isError")), "A take window outside the recorded source is rejected")
    data("vibera_command", dict(command="audio.takes", args=dict(text=str(source), length=6, takes=takes)))

    def clips():
        page = data("vibera_inspect", dict(entity="clips"))
        rows = next(v for v in page.values() if isinstance(v, list))
        return sorted((r for r in rows if r["kind"] == "audio"), key=lambda r: (r["lane"], r["beat"]))

    placed = clips()
    check(len(placed) == 3 and len({r["groupId"] for r in placed}) == 1 and placed[0]["groupId"] != 0,
          "audio.takes places three grouped takes")
    check([r["muted"] for r in placed] == [True, True, False] and [r["offset"] for r in placed] == [0, 2, 4],
          "Newest take plays; each take reads its own source window")
    data("vibera_command", dict(command="clip.slice", args=dict(ids=[placed[0]["id"]], beat=2)))
    first = next(r for r in clips() if r["lane"] == 1 and abs(r["beat"] - 2) < 1e-9)
    data("vibera_command", dict(command="audio.comp", args=dict(target=first["id"])))
    after = clips()
    playing = [(r["lane"], round(r["beat"], 1)) for r in after if not r["muted"]]
    check(sorted(playing) == [(1, 2), (3, 0)],
          "Comp: take 1 plays beats 2..4, take 3 keeps beats 0..2, everything else muted")
    data("vibera_command", dict(command="edit.undo"))
    check([(r["lane"], r["muted"]) for r in clips() if r["lane"] in (1, 3)] ==
          [(1, True), (1, True), (3, False), (3, False)], "Undo restores the takes before comping")
    data("vibera_command", dict(command="edit.redo"))
    comped = next(r for r in clips() if r["lane"] == 1 and not r["muted"])
    check(abs(comped["beat"] - 1.99) < 1e-9 and abs(comped["offset"] - .995) < 1e-9 and
          abs(comped["fadeIn"] - .01) < 1e-9, "Comped take starts half a 10 ms crossfade early, faded in")
    data("vibera_command", dict(command="audio.fade", args=dict(ids=[comped["id"]], fadeIn=.05, fadeOut=.05)))
    refaded = next(r for r in clips() if r["id"] == comped["id"])
    check(abs(refaded["fadeIn"] - .05) < 1e-9 and abs(refaded["fadeOut"] - .05) < 1e-9, "audio.fade reads back")
    rejected = client.call("vibera_command", dict(command="audio.fade", args=dict(ids=[comped["id"]], fadeIn=30)),
                           allow_failure=True)
    check(bool(rejected.get("isError")), "Fades longer than the clip are rejected")
    target = clips()[0]
    job = data("vibera_project", dict(action="stretchClip", clip=target["id"], ratio=1.5))
    client.wait_job(job, seconds=60)
    stretched = next(r for r in clips() if r["id"] == target["id"])
    check(abs(stretched["length"] - target["length"] * 1.5) < 1e-6 and stretched["offset"] == 0,
          "stretchClip job stretches the clip by 1.5 (pitch kept) and plays the new file")
    take2 = next(r for r in clips() if r["lane"] == 2 and r["beat"] < 1e-9)
    data("vibera_command", dict(command="audio.comp", args=dict(target=take2["id"], range=[.5, 1])))
    swiped = [r for r in clips() if r["lane"] == 2 and not r["muted"]]
    check(len(swiped) == 1 and abs(swiped[0]["beat"] - .49) < 1e-6 and abs(swiped[0]["offset"] - 2.245) < 1e-6,
          "Swipe comp: only beats 0.5..1 of take 2 play (with the comp crossfade)")
    data("vibera_command", dict(command="edit.undo"))
    check(not [r for r in clips() if r["lane"] == 2 and not r["muted"]], "Swipe comp undoes in one step")
    for bad in (dict(command="audio.comp", args=dict(target=take2["id"], range=[1, .5])),
                dict(command="clip.slice", args=dict(ids=[take2["id"]], beat=1, range=[0, 1]))):
        rejected = client.call("vibera_command", bad, allow_failure=True)
        check(bool(rejected.get("isError")), "Bad swipe range rejected: " + bad["command"])
    before = len(clips())
    second = source.with_name(source.stem + "-in2.wav")
    second.write_bytes(source.read_bytes())
    two = [dict(beat=8, lane=20 + t, offset=0, duration=2, source=t) for t in range(2)]
    rejected = client.call("vibera_command", dict(command="audio.takes", args=dict(
        text=str(source), length=6, files=[str(source), str(second)],
        takes=[dict(beat=8, lane=20, offset=0, duration=2, source=2)])), allow_failure=True)
    check(bool(rejected.get("isError")), "A take source outside files is rejected")
    data("vibera_command", dict(command="audio.takes", args=dict(
        text=str(source), length=6, files=[str(source), str(second)], takes=two)))
    tracks = [r for r in clips() if r["lane"] in (20, 21)]
    check(len(clips()) == before + 2 and len({r["source"] for r in tracks}) == 2,
          "Several input tracks: one clip and one Audio Clip Channel per file")
    data("vibera_command", dict(command="edit.undo"))
    check(len(clips()) == before, "Several input tracks undo in one step")
    rejected = client.call("vibera_command", dict(command="audio.takes", args=dict(
        text=str(source), length=6, mixer=17, files=[str(source), str(second)], takes=two)), allow_failure=True)
    check(bool(rejected.get("isError")), "Several tracks into inserts past the last one are rejected")
    data("vibera_command", dict(command="audio.takes", args=dict(
        text=str(source), length=6, mixer=5, files=[str(source), str(second)], takes=two)))
    fed = sorted(c["mixer"] for c in data("vibera_inspect", dict(entity="channels", limit=512))["items"]
                 if c["source"] in (str(source), str(second)))[-2:]
    check(fed == [5, 6], "Several tracks feed consecutive Mixer inserts from the chosen one")
    data("vibera_command", dict(command="edit.undo"))
    rejected = client.call("vibera_audioRecording", dict(action="configure", tracks=9), allow_failure=True)
    check(bool(rejected.get("isError")), "More than 8 input tracks are rejected")
    lone = client.call("vibera_command", dict(command="audio.comp", args=dict(target=999999)), allow_failure=True)
    check(bool(lone.get("isError")), "Comp of a missing clip is rejected")
    passed = True
finally:
    receipt = dict(passed=passed, checks=checks, output=str(output), hardware=False, listening=False)
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
    if client:
        client.process.terminate()
sys.exit(0 if passed else 1)
