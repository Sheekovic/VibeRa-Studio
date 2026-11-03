"""Real Normalize/history/source/stereo-WAV/project/error workflow; no mouse simulation."""
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


def check(value, message):
    if not value:
        raise AssertionError(message)
    receipt["checks"] += 1


try:
    source = output / "original-stereo.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((2, 2, 48000, 48000, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<hh", 4096 if i % 100 < 50 else -4096,
                                            -1024 if i % 100 < 50 else 1024) for i in range(48000)))
    original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    client = module.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))

    def ready():
        end = time.monotonic() + 10
        current = state()
        while current["pendingAudioUpdate"] and time.monotonic() < end:
            time.sleep(.02)
            current = state()
        check(not current["pendingAudioUpdate"], "Prepared projection actually completed")
        return current

    current = ready()
    channel = current["channels"][0]["id"]
    other = current["channels"][1]["id"]
    pattern = current["selectedPattern"]
    settings = lambda: next(c["sampler"] for c in state()["channels"] if c["id"] == channel)
    contract = data("vibera_commands", dict(command="channel.sampler.normalize", includeSchemas=True))["operations"][0]
    check(contract["supported"] and set(contract["arguments"]["properties"]) == {"channel", "enabled"}
          and contract["arguments"]["additionalProperties"] is False,
          "Normalize discovers only its meaningful typed parameters")
    check(settings()["normalize"] is False and current["projectVersion"] == project_schema(root), "Default off/current schema")
    command("channel.source", channel=channel, text=str(source))
    command("notes.add", pattern=pattern, channel=channel, beat=0, length=2, pitch=60, value=1)
    command("clip.add", pattern=pattern, beat=0, length=4)
    ready()
    check(settings()["normalizationAvailable"] and settings()["sourcePeak"] == .125
          and settings()["normalizationGain"] == 8, "Actual prepared stereo source peak/gain readback")

    def render(name):
        path = output / name
        job = data("vibera_project", dict(action="export", path=str(path), overwrite=True))
        check(job["completionState"] == "pending", "Export acceptance is not completion")
        terminal = client.wait_job(job)
        with wave.open(str(path), "rb") as wav:
            check((wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()) ==
                  (48000, 2, 3, 96000), "Real finite one-pass stereo24-bit Song WAV")
            raw = wav.readframes(wav.getnframes())
        pcm = [int.from_bytes(raw[i:i+3], "little", signed=True) / 8388608
               for i in range(0, len(raw), 3)]
        return raw, pcm

    raw_off, off = render("normalize-off.wav")
    baseline = state()["revision"]
    command("channel.sampler.normalize", channel=channel, enabled=True)
    ready()
    check(settings()["normalize"] and state()["revision"] == baseline+1, "One canonical document edit")
    revision = state()["revision"]
    command("channel.sampler.normalize", channel=channel, enabled=True)
    check(state()["revision"] == revision, "Idempotent setter does not add history")
    check(not next(c["sampler"] for c in state()["channels"] if c["id"] == other)["normalize"],
          "Another Channel remains untouched")
    raw_on, on = render("normalize-on.wav")
    peak = lambda xs: max(abs(x) for x in xs[2000:40000])
    check(abs(peak(on)/peak(off)-8) < .0001, "Real normalized PCM gains8x")
    check(abs(peak(on[1::2])/peak(on[0::2])-.25) < .0001, "Stereo balance preserved in exported audio")
    command("edit.undo")
    ready()
    check(not settings()["normalize"], "Normalize Undo")
    raw_undo, _ = render("normalize-undo.wav")
    check(raw_undo == raw_off, "Undo restores exact exported PCM")
    command("edit.redo")
    ready()
    check(settings()["normalize"], "Normalize Redo")
    for args in [dict(channel=channel, enabled=1), dict(channel=channel, enabled="true"),
                 dict(channel=channel, enabled=True, value=1), dict(channel=0, enabled=True),
                 dict(channel=9999999, enabled=True)]:
        before = state()["revision"]
        response = client.call("vibera_command", dict(command="channel.sampler.normalize", args=args), allow_failure=True)
        check(response.get("isError") and response["structuredContent"]["completionState"] == "failed"
              and response["structuredContent"]["error"].get("code") and state()["revision"] == before,
              "Malformed/unknown-target Normalize fails atomically with a structured error")
    profile = state()["controllerProfile"]
    custom = dict(profile)
    custom["bindings"] = [dict(action="channel.sampler.normalize", label="Normalize", port=1, channel=16,
        kind=1, number=70, layer=0, mode=0, mixer=-1, minimum=0, maximum=1, pickup=False,
        banked=False, enabled=True, nativeArea=0)]
    data("vibera_select", dict(channel=channel))
    data("vibera_controller", dict(action="configure", profile=custom))
    data("vibera_controller", dict(action="inject", port=1, kind=1, channel=16, number=70, value=127))
    check(not settings()["normalize"], "Virtual MIDI button shares canonical Normalize toggle")
    data("vibera_controller", dict(action="inject", port=1, kind=1, channel=16, number=70, value=0))
    data("vibera_controller", dict(action="configure", profile=profile))
    command("edit.undo")
    check(settings()["normalize"], "Controller change has the same Undo history")
    project = output / "normalize-song.vibra"
    client.wait_job(data("vibera_project", dict(action="save", path=str(project), overwrite=True)))
    client.wait_job(data("vibera_project", dict(action="open", path=str(project), discardChanges=True)))
    ready()
    check(settings()["normalize"], "Actual project save/reopen retains Normalize")
    raw_reopen, _ = render("normalize-reopened.wav")
    check(raw_reopen == raw_on, "Project round trip restores exact rendered audio")
    check(hashlib.sha256(source.read_bytes()).hexdigest() == original_hash, "All operations preserve original source bytes")
    blocker = output / "blocked-parent"
    blocker.write_text("retained input", encoding="utf-8")
    saved = hashlib.sha256(project.read_bytes()).hexdigest()
    job = data("vibera_project", dict(action="save", path=str(blocker / "failed.vibra")))
    check(job["completionState"] == "pending", "Failed file job is initially only accepted")
    deadline = time.monotonic()+10
    while True:
        terminal = client.call("vibera_project", dict(action="job", operation=job["operation"]), allow_failure=True)["structuredContent"]
        if terminal["completionState"] != "pending":
            break
        check(time.monotonic() < deadline, "File failure becomes terminal")
        time.sleep(.02)
    check(terminal["completionState"] == "failed" and terminal["error"]["code"] == "operation_failed",
          "Actual asynchronous file failure is explicit")
    check(hashlib.sha256(project.read_bytes()).hexdigest() == saved and blocker.read_text() == "retained input",
          "Failed save preserves working project and unrelated file")
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
