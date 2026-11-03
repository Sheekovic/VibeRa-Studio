"""Real sampler cut/polyphony workflow, WAV evidence, shared history and persistence."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import time
import wave

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
app, output = [Path(x).resolve() for x in sys.argv[1:3]]
output.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("neutral", root/"skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=0, applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(), nativeInput=False, listening=False, hardware=False)
began = time.monotonic()
client = None

def check(ok, message):
    if not ok:
        raise AssertionError(message)
    receipt["checks"] += 1

try:
    tone, pulse = output/"original-open.wav", output/"original-closed.wav"
    for path, count in [(tone, 48000), (pulse, 100)]:
        with wave.open(str(path), "wb") as wav:
            wav.setparams((1, 2, 48000, count, "NONE", "not compressed"))
            wav.writeframes(b"".join(struct.pack("<h", int(10000*min(i/8, (count-1-i)/8, 1))) for i in range(count)))
    client = module.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    pattern = state()["selectedPattern"]
    open_id, closed_id = [c["id"] for c in state()["channels"][:2]]
    settings = lambda channel: next(c["sampler"] for c in state()["channels"] if c["id"] == channel)
    notes = lambda: data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"]
    for channel, path, pan in [(open_id, tone, -1), (closed_id, pulse, 1)]:
        command("channel.source", channel=channel, text=str(path))
        command("channel.pan", channel=channel, value=pan)
    command("steps.set", pattern=pattern, channel=open_id, beat=0, enabled=True)
    command("steps.set", pattern=pattern, channel=closed_id, beat=1, enabled=True)
    command("clip.add", pattern=pattern, beat=0, length=4)

    def render(name):
        job = data("vibera_project", dict(action="export", path=str(output/name), overwrite=True))
        check(job["completionState"] == "pending", "Export acceptance is pending, not completion")
        client.wait_job(job)
        with wave.open(str(output/name), "rb") as wav:
            check((wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()) == (48000, 2, 3, 96000), "Actual two-second Song WAV")
            pcm = wav.readframes(wav.getnframes())
        left = [int.from_bytes(pcm[i:i+3], "little", signed=True)/8388608 for i in range(0, len(pcm), 6)]
        right = b"".join(pcm[i:i+3] for i in range(3, len(pcm), 6))
        return left, right

    peak = lambda pcm, start, end: max(abs(x) for x in pcm[int(start*48000):int(end*48000)])
    mean = lambda pcm, start, end: sum(pcm[int(start*48000):int(end*48000)])/(int(end*48000)-int(start*48000))
    baseline, right = render("no-cuts.wav")
    check(peak(baseline, .75, .9) > .01, "Original open one-shot rings beyond the closed hit")
    command("channel.sampler.cut", channel=closed_id, value=1)
    command("channel.sampler.cutBy", channel=open_id, value=1)
    cut, cut_right = render("cross-cut.wav")
    check(peak(cut, .45, .49) > .01 and peak(cut, .52, .9) == 0, "Closed hat releases open one-shot at the matching group cue")
    check(right == cut_right, "Cut sender actual PCM is unchanged")
    command("edit.undo")
    command("edit.undo")
    check(settings(open_id)["cutBy"] == settings(closed_id)["cut"] == 0, "Two group commands undo exactly")
    command("edit.redo")
    command("edit.redo")
    check(settings(open_id)["cutBy"] == settings(closed_id)["cut"] == 1, "Group Redo restores actual state")
    command("notes.delete", pattern=pattern, ids=[n["id"] for n in notes() if n["channel"] == open_id])
    command("notes.add", pattern=pattern, channel=open_id, beat=0, length=3, pitch=60, value=.8)
    for name, value in [("attack", 0), ("decay", 0), ("sustain", 1), ("release", .2)]:
        command("channel.sampler."+name, channel=open_id, value=value)
    command("channel.sampler.envelope", channel=open_id, enabled=True)
    released, _ = render("configured-release.wav")
    check(peak(released, .51, .54) > .01 and peak(released, .65, .67) > .001 and peak(released, .71, .9) == 0, "Cut preserves configured200ms release, not a hard mute")
    command("channel.sampler.envelope", channel=open_id, enabled=False)
    command("notes.delete", pattern=pattern, ids=[n["id"] for n in notes() if n["channel"] == open_id])
    command("channel.sampler.cutBy", channel=open_id, value=0)
    for beat in [0, 1]:
        command("steps.set", pattern=pattern, channel=open_id, beat=beat, enabled=True)
    overlap, _ = render("step-overlap.wav")
    command("channel.sampler.cutItself", channel=open_id, enabled=True)
    groups = settings(open_id)
    check(groups["cut"] == groups["cutBy"] and groups["cut"] not in [0, 1], "Cut itself assigns an independent matching pair")
    revision = state()["revision"]
    command("channel.sampler.cutItself", channel=open_id, enabled=True)
    check(state()["revision"] == revision, "Already unique Cut itself is idempotent")
    self_cut, _ = render("step-self-cut.wav")
    check(abs(mean(overlap, .55, .65)-2*mean(self_cut, .55, .65)) < .0001, "Step self-cut releases the previous hit without replacing the source")
    command("notes.delete", pattern=pattern, ids=[n["id"] for n in notes() if n["channel"] == open_id])
    for beat in [0, 1]:
        command("notes.add", pattern=pattern, channel=open_id, beat=beat, length=.25, pitch=60, value=.8, step=False)
    piano_self, _ = render("piano-self-group.wav")
    command("channel.sampler.cutItself", channel=open_id, enabled=False)
    piano_plain, _ = render("piano-no-group.wav")
    check(piano_self == piano_plain, "Piano notes do not self-cut merely because Cut/Cut-by match")
    command("channel.sampler.maxPoly", channel=open_id, value=1)
    one_voice, _ = render("max-poly-one.wav")
    check(abs(mean(piano_plain, .55, .65)-2*mean(one_voice, .55, .65)) < .0001, "Max poly1 limits overlapping Piano notes, unlike step-only self-cut")
    data("vibera_rack", dict(action="select", ids=[open_id], activeChannel=open_id))
    before_preview = {k:settings(open_id)[k] for k in ["cut", "cutBy", "maxPoly"]}
    command("transport.record")
    lease = data("vibera_gesture", dict(action="begin", domain="samplerVoices"))["operation"]
    for name, value in [("cut", 8), ("cutBy", 8), ("maxPoly", 2)]:
        result = data("vibera_gesture", dict(action="update", operation=lease, command="channel.sampler."+name, args=dict(value=value)))
        check(result["completionState"] == "pending" and state()["recordArmed"], "Recording-safe voice preview stays pending and armed")
    refused = client.call("vibera_gesture", dict(action="update", operation=lease, command="notes.delete", args={}), allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain", "Voice lease refuses unrelated score edits")
    check(data("vibera_gesture", dict(action="commit", operation=lease))["completionState"] == "completed", "Voice preview explicit commit completion")
    command("transport.record")
    command("edit.undo")
    check(all(settings(open_id)[k] == v for k, v in before_preview.items()), "Three control changes form one exact Undo")
    command("edit.redo")
    check([settings(open_id)[k] for k in ["cut", "cutBy", "maxPoly"]] == [8, 8, 2], "Continuous voice gesture exact Redo")
    command("edit.undo")
    for expire in [False, True]:
        lease = data("vibera_gesture", dict(action="begin", domain="samplerVoices", leaseMs=250))["operation"]
        data("vibera_gesture", dict(action="update", operation=lease, command="channel.sampler.maxPoly", args=dict(value=3)))
        if expire:
            time.sleep(.45)
            result = data("vibera_gesture", dict(action="state", operation=lease))
        else:
            result = data("vibera_gesture", dict(action="cancel", operation=lease))
        check(result["completionState"] == "cancelled" and all(settings(open_id)[k] == v for k,v in before_preview.items()), "Voice cancel/real expiry restores exact settings")
    for name in ["cut", "cutBy", "maxPoly"]:
        contract = data("vibera_commands", dict(command="channel.sampler."+name, includeSchemas=True))["operations"][0]
        value = contract["arguments"]["properties"]["value"]
        check(contract["supported"] and (value["type"], value["minimum"], value["maximum"]) == ("integer", 0, 99), "Discoverable typed voice-control range")
        for invalid in [-1, 1.5, 100, True]:
            before = state()["revision"]
            refused = client.call("vibera_command", dict(command="channel.sampler."+name, args=dict(channel=open_id, value=invalid)), allow_failure=True)
            check(refused.get("isError") and refused["structuredContent"].get("error") and state()["revision"] == before, "Invalid voice value fails atomically with a structured error")
    project = output/"sampler-voices.vibra"
    client.wait_job(data("vibera_project", dict(action="save", path=str(project), overwrite=True)))
    client.wait_job(data("vibera_project", dict(action="open", path=str(project), discardChanges=True)))
    check(settings(open_id)["maxPoly"] == 1, "Actual schema14 save/reopen retains audible voice controls")
    reopened, _ = render("reopened.wav")
    check(one_voice == reopened, "Saved/reopened project reproduces actual limited-voice PCM")
    data("vibera_rack", dict(action="select", ids=[open_id], activeChannel=open_id))
    original_profile = state()["controllerProfile"]
    profile = dict(original_profile)
    profile["bindings"] = []
    for index, name in enumerate(["cut", "cutBy", "maxPoly", "cutItself"]):
        profile["bindings"].append(dict(action="channel.sampler."+name, label=name, port=1, channel=16,
            kind=0 if index<3 else 1, number=70+index, layer=0, mode=1 if index<3 else 0,
            mixer=-1, minimum=0, maximum=99, pickup=False, banked=False, enabled=True, nativeArea=0))
    data("vibera_controller", dict(action="configure", profile=profile))
    for index, name in enumerate(["cut", "cutBy", "maxPoly"]):
        previous=settings(open_id)[name]
        data("vibera_controller", dict(action="inject", port=1,kind=0,channel=16,number=70+index,value=64))
        check(settings(open_id)[name] == 50, "Virtual CC uses canonical integer voice-control units: "+json.dumps({"target":open_id,"selected":state()["selectedChannel"],"actual":settings(open_id)[name],"status":state().get("status")}))
        command("edit.undo")
        check(settings(open_id)[name] == previous, "Virtual CC voice edit has exact canonical Undo")
    data("vibera_controller", dict(action="inject",port=1,kind=1,channel=16,number=73,value=127))
    check(settings(open_id)["cut"] == settings(open_id)["cutBy"] > 0, "Virtual mapped button executes Cut itself")
    command("edit.undo")
    data("vibera_controller", dict(action="configure", profile=original_profile))
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
