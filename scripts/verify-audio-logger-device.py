"""Real-device check of the audio logger on an owned --mcp instance (own profile, Master at -80 dB).

Enables a 1-minute logger, confirms it records continuously (also while stopped, so live playing
without the transport is kept, as in FL), plays the demo for ~3 s, saves the last 3 s and checks the
WAV: 3 s long, stereo 24-bit, with non-silent (inaudibly quiet) content. Needs an audio device. Not a listening test.
"""
import json, os, queue, subprocess, threading, time, uuid, wave
from pathlib import Path

ROOT = Path("E:/Projects/VibeRa Studio")
exe = ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
out = ROOT / "build/module-qualification/audio-logger" / ("real-" + uuid.uuid4().hex)
(out / "profile").mkdir(parents=True)
env = dict(os.environ, VIBERA_PROFILE_DIRECTORY=str(out / "profile"), VIBERA_NO_UPDATE_CHECK="1")
p = subprocess.Popen([str(exe), "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                     text=True, encoding="utf-8", env=env)
q = queue.Queue()
threading.Thread(target=lambda: [q.put(json.loads(l)) for l in p.stdout], daemon=True).start()
serial = 0
checks, results = [], {}


def req(method, params):
    global serial
    serial += 1
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": serial, "method": method, "params": params}) + "\n")
    p.stdin.flush()
    while True:
        r = q.get(timeout=60)
        if r.get("id") == serial:
            return r


def call(tool, args):
    r = req("tools/call", {"name": tool, "arguments": args}).get("result", {})
    return r.get("structuredContent", {}), r.get("isError", False)


def check(ok, label):
    checks.append((label, bool(ok)))
    if not ok:
        raise AssertionError(label)


passed = False
try:
    req("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "audio logger check", "version": "1"}})
    p.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n'); p.stdin.flush()
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    call("vibera_command", {"command": "mixer.gain", "args": {"mixer": 0, "value": 0.0001}})  # ~-80 dB: inaudible.
    state, _ = call("vibera_state", {})
    results["device"] = str(state.get("audio", {}).get("device"))[:80]
    enabled, error = call("vibera_project", {"action": "audioLogger", "enabled": True, "minutes": 1})
    check(not error and enabled.get("enabled"), "Logger enables on the real device: " + json.dumps(enabled)[:160])
    time.sleep(1)
    idle = call("vibera_state", {})[0]["audioLogger"]["recordedSeconds"]
    check(.5 < idle < 2, f"The logger records continuously, also while stopped ({idle:.2f} s after 1 s)")
    call("vibera_transport", {"action": "song"})
    call("vibera_transport", {"action": "seek", "beat": 0})
    call("vibera_transport", {"action": "play"})
    time.sleep(3)
    call("vibera_transport", {"action": "stop"})
    logged = call("vibera_state", {})[0]["audioLogger"]["recordedSeconds"]
    results["recordedSeconds"] = logged
    check(logged > idle + 2.5, f"Playback adds about 3 s ({idle:.2f} -> {logged:.2f})")
    path = out / "log.wav"
    saved, error = call("vibera_project", {"action": "audioLoggerSave", "path": str(path), "seconds": 3})
    check(not error and path.is_file(), "The log saves: " + json.dumps(saved)[:160])
    with wave.open(str(path)) as w:
        frames, rate, channels, width = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(frames)
    peak = max(abs(int.from_bytes(raw[i:i + 3], "little", signed=True)) for i in range(0, len(raw), 3))
    results.update(seconds=frames / rate, channels=channels, bits=width * 8, peak24=peak)
    check(channels == 2 and width == 3 and abs(frames / rate - 3) < .01, "Stereo 24-bit WAV of the requested 3 s")
    check(peak > 0, f"The saved audio carries the (inaudibly quiet) master signal (peak {peak} LSB)")
    call("vibera_project", {"action": "audioLogger", "enabled": False})
    passed = True
finally:
    try:
        req("tools/call", {"name": "vibera_command", "arguments": {"command": "transport.stop"}})
    except Exception:
        pass
    p.stdin.close()
    try:
        p.wait(timeout=20)
    except subprocess.TimeoutExpired:
        p.kill()  # Owned instance started above.
    (out / "verification.json").write_text(json.dumps(dict(passed=passed, results=results, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, results=results)))
