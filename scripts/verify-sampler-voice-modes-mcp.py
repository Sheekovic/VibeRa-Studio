"""Original local-sampler voice-mode workflow through the production MCP server.

Actual canonical commands/history, musical WAV, MIDI mappings and file outcomes;
not native OS input, physical MIDI, listening or exact FL behavioral qualification.
"""
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


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


app = Path(sys.argv[1]).resolve()
admission = load("voice_mode_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
admission.ordinary(app, False)
output_root = admission.owned_output_path(sys.argv[2], app)
output = output_root / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
neutral = load("voice_mode_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
support = load("voice_mode_support", ROOT / "scripts/dev_support.py")
receipt = dict(passed=False, finished=False, checks=[], output=str(output),
               applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               verifierSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               nativeInput=False, listening=False, hardware=False, callbackTiming=False)
began = time.monotonic()
client = None
NAMES = ("mono", "portamento", "slideTime", "gateBeats", "fullPortamento")
DEFAULTS = dict(mono=False, portamento=False, slideTime=.06, gateBeats=0, fullPortamento=False)


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    receipt["checks"].append(label)


try:
    source = output / "original-voice-tone.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((1, 2, 48000, 192000, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<h", round(6000 * math.sin(2 * math.pi * 440 * i / 48000)))
                                 for i in range(192000)))
    client = neutral.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    initial = state()
    channel, pattern = initial["selectedChannel"], initial["selectedPattern"]
    settings = lambda: next(c["sampler"] for c in state()["channels"] if c["id"] == channel)
    fields = lambda: {name: settings()[name] for name in NAMES}
    command("channel.source", channel=channel, text=str(source))
    command("channel.sampler.root", channel=channel, value=60)
    command("channel.pan", channel=channel, value=-1)
    command("notes.add", channel=channel, pattern=pattern, beat=0, length=3, pitch=60, value=.7)
    command("clip.add", pattern=pattern, beat=0, length=4)
    command("channel.sampler.envelope", channel=channel, enabled=True)
    for name, value in (("attack", 0), ("decay", 0), ("sustain", 1), ("release", .02)):
        command("channel.sampler." + name, channel=channel, value=value)
    check(fields() == DEFAULTS and settings()["voiceModeAcknowledged"] is None,
          "Neutral five-control state is explicit and not a false DSP acknowledgement")
    for name in NAMES:
        op = data("vibera_commands", dict(command="channel.sampler." + name,
                                          includeSchemas=True))["operations"][0]
        schema = op["arguments"]
        flag = isinstance(DEFAULTS[name], bool)
        field = "enabled" if flag else "value"
        check(op["supported"] and op["mapping"] == "vibera_command" and not schema["additionalProperties"] and
              set(schema["required"]) == {"channel", field} and
              schema["properties"][field]["type"] == ("boolean" if flag else "number") and
              schema["properties"][field]["default"] == DEFAULTS[name],
              "Discoverable typed canonical voice contract: " + name)
        if not flag:
            check(schema["properties"][field]["minimum"] == 0 and
                  schema["properties"][field]["maximum"] == (10 if name == "slideTime" else 64),
                  "Physical seconds/beats range: " + name)

    def change(name, value):
        result = command("channel.sampler." + name, channel=channel,
                         **{"enabled" if isinstance(value, bool) else "value": value})
        check(result["completionState"] == "completed" and
              result["completionScope"] == "canonicalSamplerVoice" and
              not result["dspSettlingAcknowledged"] and settings()[name] == value,
              "Actual model completion with separate DSP outcome: " + name)
        return result

    def render(name):
        path = output / name
        accepted = data("vibera_project", dict(action="export", path=str(path), overwrite=False))
        check(accepted["completionState"] == "pending", "WAV request acceptance is pending")
        check(client.wait_job(accepted)["completionState"] == "completed" and path.is_file(),
              "WAV job reports its actual completed file")
        with wave.open(str(path), "rb") as wav:
            check((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000),
                  "Real stereo24/48k musical render")
            return wav.readframes(wav.getnframes())

    def peak(pcm, start, end):
        values = [abs(int.from_bytes(pcm[i:i + 3], "little", signed=True)) / 8388608
                  for i in range(round(start * 48000) * 6, round(end * 48000) * 6, 6)]
        return max(values, default=0)

    baseline = render("neutral.wav")
    check(peak(baseline, .1, .2) > .01 and peak(baseline, .8, 1) > .01 and
          peak(baseline, 1.7, 1.9) < 1.e-6, "Independent literal note/release oracle")
    change("gateBeats", .5)
    gated = render("gate-half-beat.wav")
    check(peak(gated, .1, .2) > .01 and peak(gated, .35, 1.4) < 1.e-6,
          "Half quarter-note Gate releases at250ms at120BPM plus declared20ms release")
    command("edit.undo")
    check(fields() == DEFAULTS and render("gate-undo.wav") == baseline,
          "Gate Undo restores exact neutral model and actual PCM")
    command("edit.redo")
    check(settings()["gateBeats"] == .5 and render("gate-redo.wav") == gated,
          "Gate Redo restores exact actual gated PCM")
    command("project.tempo", value=90)
    slower = render("gate90bpm.wav")
    check(peak(slower, .28, .30) > .01 and peak(slower, .38, .6) < 1.e-6,
          "Gate is musical beats rather than a cached fixed wall-clock duration")
    command("project.tempo", value=120)
    change("portamento", True)
    change("fullPortamento", True)
    check(render("full-portamento.wav") == baseline,
          "Declared FullPorta policy exempts the incoming Porta note from Gate")
    change("fullPortamento", False)
    check(render("porta-gated.wav") == gated,
          "Porta alone does not silently disable Gate")
    change("portamento", False)
    change("gateBeats", 0)
    command("notes.add", channel=channel, pattern=pattern, beat=1, length=2, pitch=60, value=.7)
    poly = render("overlap-poly.wav")
    change("mono", True)
    change("slideTime", 0)
    mono = render("overlap-mono.wav")
    check(abs(peak(poly, .8, 1) / peak(mono, .8, 1) - 2) < .001,
          "Mono releases the logically held predecessor while ordinary polyphony retains both voices")
    note_rows = data("vibera_inspect", dict(entity="notes", pattern=pattern))["items"]
    second_note = next(n["id"] for n in note_rows if n["channel"] == channel and n["beat"] == 1)
    command("notes.delete", pattern=pattern, ids=[second_note])
    command("notes.add", channel=channel, pattern=pattern, beat=1, length=2, pitch=72, value=.7)
    change("slideTime", .125)
    glide = render("overlap-slide125ms.wav")

    def frequency(pcm, start, end):
        values = [int.from_bytes(pcm[i:i + 3], "little", signed=True)
                  for i in range(round(start * 48000) * 6, round(end * 48000) * 6, 6)]
        return sum(a <= 0 < b for a, b in zip(values, values[1:])) / (end - start)

    initial_hz, settled_hz = frequency(glide, .535, .575), frequency(glide, .65, .70)
    receipt["musicalMeasurements"] = dict(initialGlideHz=initial_hz, settledGlideHz=settled_hz)
    check(480 < initial_hz < 800 and 840 < settled_hz < 920,
          "Actual125ms Mono pitch transition approaches and reaches the literal octave target")
    change("mono", True)
    change("slideTime", .125)
    snapshot = fields()
    revision = state()["revision"]
    change("slideTime", .125)
    check(state()["revision"] == revision, "Repeated identical voice edit is an exact history no-op")
    data("vibera_rack", dict(action="select", ids=[channel], activeChannel=channel))
    command("transport.record")
    lease = data("vibera_gesture", dict(action="begin", domain="samplerVoices"))["operation"]
    for name, value in (("mono", False), ("portamento", True), ("fullPortamento", True),
                        ("slideTime", .3), ("gateBeats", 2)):
        args = dict(channel=channel, **{"enabled" if isinstance(value, bool) else "value": value})
        updated = data("vibera_gesture", dict(action="update", operation=lease,
                       command="channel.sampler." + name, args=args))
        check(updated["completionState"] == "pending" and state()["recordArmed"],
              "Owned continuous voice update preserves armed MIDI: " + name)
    configured = fields()
    check(data("vibera_gesture", dict(action="commit", operation=lease))["completionState"] == "completed",
          "Five-control gesture has actual terminal commit")
    command("transport.record")
    command("edit.undo")
    check(fields() == snapshot, "Five changes form one exact canonical Undo")
    command("edit.redo")
    check(fields() == configured, "Five-control gesture exact Redo")
    for expire in (False, True):
        lease = data("vibera_gesture", dict(action="begin", domain="samplerVoices", leaseMs=250))["operation"]
        data("vibera_gesture", dict(action="update", operation=lease,
             command="channel.sampler.slideTime", args=dict(channel=channel, value=.9)))
        if expire:
            time.sleep(.45)
        result = data("vibera_gesture", dict(action="state" if expire else "cancel", operation=lease))
        check(result["completionState"] == "cancelled" and fields() == configured,
              "Explicit cancel/real lease expiry restores exact voice settings")
    for name, bad in (("mono", 1), ("portamento", "true"), ("fullPortamento", None),
                      ("slideTime", -1), ("slideTime", 10.1), ("slideTime", True),
                      ("gateBeats", -1), ("gateBeats", 64.1), ("gateBeats", "2")):
        before = state()["revision"]
        field = "enabled" if name in ("mono", "portamento", "fullPortamento") else "value"
        result = client.call("vibera_command", dict(command="channel.sampler." + name,
                             args=dict(channel=channel, **{field: bad})), allow_failure=True)
        check(result.get("isError") and result["structuredContent"].get("error") and
              state()["revision"] == before and fields() == configured,
              "Invalid typed/ranged value refuses atomically: " + name + "=" + repr(bad))

    original_profile = state()["controllerProfile"]
    profile = dict(original_profile, bindings=[])
    for index, name in enumerate(NAMES):
        flag = isinstance(DEFAULTS[name], bool)
        profile["bindings"].append(dict(action="channel.sampler." + name, label=name, port=1,
            channel=16, kind=0, number=80 + index, layer=0, mode=1, mixer=-1,
            minimum=0, maximum=1 if flag else 10 if name == "slideTime" else 64,
            pickup=False, banked=False, enabled=True, nativeArea=0))
    data("vibera_controller", dict(action="configure", profile=profile))
    for index, name in enumerate(NAMES):
        old = fields()
        data("vibera_controller", dict(action="inject", port=1, kind=0, channel=16,
                                       number=80 + index, value=127))
        expected = True if isinstance(DEFAULTS[name], bool) else 10 if name == "slideTime" else 64
        check(settings()[name] == expected, "Global virtual CC uses canonical physical range: " + name)
        if old[name] != expected:
            command("edit.undo")
            check(fields() == old, "Mapped voice control has exact shared history: " + name)
    data("vibera_controller", dict(action="configure", profile=original_profile))
    saved = output / "voice-modes.vibra"
    check(client.wait_job(data("vibera_project", dict(action="save", path=str(saved), overwrite=False)))["completionState"] == "completed",
          "Project save reports actual terminal file completion")
    xml = ET.parse(saved)
    voice = xml.find("./CHANNELS/CHANNEL[@id='" + str(channel) + "']/SAMPLER/VOICE")
    check(int(xml.getroot().get("version")) == support.project_schema(ROOT) and voice is not None and
          set(voice.attrib) == set(NAMES) and len(voice) == 0,
          "Actual current-schema project has the exact sparse five-field VOICE contract")
    before_reopen = render("before-reopen.wav")
    check(client.wait_job(data("vibera_project", dict(action="open", path=str(saved), discardChanges=True)))["completionState"] == "completed" and
          fields() == configured and render("after-reopen.wav") == before_reopen,
          "Actual reopen retains every voice setting and identical musical WAV")
    malformed = output / "invalid-voice.vibra"
    voice.set("slideTime", "11")
    xml.write(malformed, encoding="utf-8", xml_declaration=True)
    revision = state()["revision"]
    result = client.call("vibera_project", dict(action="open", path=str(malformed), discardChanges=True),
                         allow_failure=True)["structuredContent"]
    deadline = time.monotonic() + 10
    while result["completionState"] == "pending" and time.monotonic() < deadline:
        time.sleep(.03)
        result = client.call("vibera_project", dict(action="job", operation=result["operation"]),
                             allow_failure=True)["structuredContent"]
    check(result["completionState"] == "failed" and fields() == configured and
          state()["revision"] == revision, "Malformed saved voice state fails terminally without live data loss")
    receipt["passed"] = True
except Exception as error:
    receipt["failure"] = str(error)
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
