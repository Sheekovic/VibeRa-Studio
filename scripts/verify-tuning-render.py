"""Rendered tuning acceptance (M5 / TM-F): the audio a tuned Channel renders has the right pitch.

Loads a generated sine sample on a sampler Channel of an isolated --mcp-test instance, renders
one held note in equal temperament and again with Channel tuning Rast on C (E and B a quarter
tone flat), and measures the rendered frequency by interpolated zero crossings. E must drop by
50 cents (+/-1 cent) and A must not move. Also checks per-note fine cents (+25) and Undo of the
tuning. Measurement, not listening.

usage: python scripts/verify-tuning-render.py [executable] [output folder]
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

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/tuning/render"
output = base / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks, measured = [], {}
RATE = 48000


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def write_sine(path, hz=261.6256, seconds=3.0):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = bytearray()
        for i in range(int(RATE * seconds)):
            frames += struct.pack("<h", int(20000 * math.sin(2 * math.pi * hz * i / RATE)))
        w.writeframes(bytes(frames))


def frequency(path):
    with wave.open(str(path), "rb") as w:
        channels, width, rate, count = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(count)
    step = channels * width
    samples = []
    for i in range(0, len(raw) - step + 1, step):
        chunk = raw[i:i + width]
        if width == 2:
            samples.append(struct.unpack("<h", chunk)[0])
        elif width == 3:
            samples.append(int.from_bytes(chunk, "little", signed=True))
        else:
            samples.append(struct.unpack("<f", chunk)[0] if width == 4 else 0)
    # Steady middle of the held note: skip attack and release.
    start, end = int(rate * .3), int(rate * 1.3)
    window = samples[start:end]
    crossings = []
    for i in range(1, len(window)):
        a, b = window[i - 1], window[i]
        if a < 0 <= b and b != a:
            crossings.append(i - 1 + (-a) / (b - a))
    if len(crossings) < 10:
        return 0.0
    return (len(crossings) - 1) / ((crossings[-1] - crossings[0]) / rate)


def cents(a, b):
    return 1200 * math.log2(a / b)


passed = False
client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
try:
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def command(name, **args):
        result = call("vibera_command", {"command": name, "args": args})
        check(result.get("completionState") != "failed", f"{name} succeeds: {str(result.get('error'))[:160]}")
        return result

    def render(name, pitch):
        notes = call("vibera_inspect", {"entity": "notes", "limit": 500}).get("items", [])
        if notes:
            command("notes.delete", ids=[n["id"] for n in notes])
        command("notes.add", channel=channel, pitch=pitch, beat=0, length=3, value=.8)
        wav = output / (name + ".wav")
        job = call("vibera_project", {"action": "export", "path": str(wav), "overwrite": True, "pattern": pattern})
        deadline = time.monotonic() + 60
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        check(job.get("completionState") == "completed", f"render {name}: {str(job)[:160]}")
        measured[name] = round(frequency(wav), 3)
        return measured[name]

    sine = output / "sine-c5.wav"
    write_sine(sine)
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    state = call("vibera_state")
    channel, pattern = state["rack"]["rows"][0]["id"], state["selectedPattern"]
    command("channel.source", channel=channel, text=str(sine))
    e_equal = render("E-equal", 64)
    a_equal = render("A-equal", 69)
    check(e_equal > 0 and a_equal > 0, f"Rendered notes have a measurable pitch (E {e_equal} Hz, A {a_equal} Hz)")
    check(abs(cents(a_equal, e_equal) - 500) < 1, f"Equal temperament: A is 500 cents above E ({cents(a_equal, e_equal):.2f})")
    rast = [0] * 12
    rast[4] = rast[11] = -50
    command("channel.tuning", channel=channel, tuning=rast)
    command("edit.undo")  # The tuning is the last edit, so Undo removes exactly it.
    e_undo = render("E-undo", 64)
    check(abs(cents(e_undo, e_equal)) < 1, f"Undo of the tuning renders equal temperament again ({cents(e_undo, e_equal):.2f} cents)")
    command("channel.tuning", channel=channel, tuning=rast)
    e_rast = render("E-rast", 64)
    a_rast = render("A-rast", 69)
    check(abs(cents(e_rast, e_equal) + 50) < 1, f"Rast on C renders E a quarter tone flat ({cents(e_rast, e_equal):.2f} cents)")
    check(abs(cents(a_rast, a_equal)) < 1, f"Rast on C leaves A unchanged ({cents(a_rast, a_equal):.2f} cents)")
    passed = True
finally:
    client.close()
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    deps = ["scripts/verify-tuning-render.py", "src/StudioChannels.cpp", "src/Commands.cpp"]
    receipt = dict(passed=passed, checks=checks, measuredHz=measured,
                   dependencyHashes=[dict(path=d, mode="content", sha256=content_identity(ROOT / d)["contentSha256"]) for d in deps],
                   candidateHashes=[dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                    for p in (exe, exe.parent / "VibeRa Plugin Worker.exe") if p.is_file() and ROOT in p.parents],
                   scope="Offline render of the local sampler; frequency measured by zero crossings; not listening")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), measuredHz=measured, receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
