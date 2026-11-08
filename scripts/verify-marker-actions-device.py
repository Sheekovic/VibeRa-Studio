"""Real-device check of marker playback actions on an owned --mcp instance (Master muted, own profile).

Start marker at 4, Loop 8..16, Skip at 20 (to 28), Pause at 32. Needs an audio device; plays muted.
"""
import json, os, queue, subprocess, threading, time, uuid
from pathlib import Path

ROOT = Path("E:/Projects/VibeRa Studio")
exe = ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
out = ROOT / "build/module-qualification/marker-actions" / ("real-" + uuid.uuid4().hex)
(out / "profile").mkdir(parents=True)
env = dict(os.environ, VIBERA_PROFILE_DIRECTORY=str(out / "profile"), VIBERA_NO_UPDATE_CHECK="1")
p = subprocess.Popen([str(exe), "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                     text=True, encoding="utf-8", env=env)
q = queue.Queue()
threading.Thread(target=lambda: [q.put(json.loads(l)) for l in p.stdout], daemon=True).start()
serial = 0


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
    return req("tools/call", {"name": tool, "arguments": args}).get("result", {}).get("structuredContent", {})


results = {}
try:
    req("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "marker check", "version": "1"}})
    p.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n'); p.stdin.flush()
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    cmd = lambda name, **a: call("vibera_command", {"command": name, "args": a})
    cmd("mixer.mute", mixer=0, enabled=True)
    for name, beat in (("Intro", 4), ("Loop", 8), ("Out", 16), ("Skip", 20), ("Land", 28), ("Stop", 32), ("End", 40)):
        cmd("marker.add", beat=beat, text=name)
    m = {x["name"]: x["id"] for x in call("vibera_inspect", {"entity": "markers"})["items"]}
    cmd("marker.action", target=m["Intro"], text="start")
    cmd("marker.action", target=m["Loop"], text="loop")
    cmd("marker.action", target=m["Skip"], text="skip")
    cmd("marker.action", target=m["Stop"], text="pause")
    print("device", str(call("vibera_state", {}).get("audio", {}).get("device"))[:80])
    call("vibera_transport", {"action": "song"})
    call("vibera_transport", {"action": "seek", "beat": 0})
    call("vibera_transport", {"action": "play"})
    time.sleep(.6)
    results["afterStart"] = call("vibera_state", {}).get("positionBeat")
    trace = []
    for _ in range(80):  # 8 s at 120 BPM = 16 beats: loops 8..16 twice.
        time.sleep(.1)
        trace.append(round(call("vibera_state", {}).get("positionBeat", 0), 2))
    results["loopTrace"] = [min(trace), max(trace)]
    call("vibera_transport", {"action": "seek", "beat": 18})
    time.sleep(2.2)  # 4 beats: crosses Skip at 20 -> lands at 28.
    results["afterSkip"] = call("vibera_state", {}).get("positionBeat")
    time.sleep(2.8)  # 28 -> 32 pause.
    st = call("vibera_state", {})
    results["afterPause"] = [st.get("positionBeat"), st.get("playing")]
    call("vibera_transport", {"action": "stop"})
finally:
    try:
        p.stdin.close(); p.wait(timeout=20)
    except Exception:
        p.terminate()
checks = {
    "Start marker: Play from the Song start begins at 4": 4 <= results.get("afterStart", 0) < 6,
    "Loop marker: the playhead stays in 8..16": results.get("loopTrace", [0, 99])[1] < 16,
    "Skip marker at 20 jumps to the next marker (28)": 28 <= results.get("afterSkip", 0) < 32,
    "Pause marker stops playback at 32": results.get("afterPause", [0, True]) == [32.0, False],
}
results["checks"] = checks
results["passed"] = all(checks.values())
print(json.dumps(results))
(out / "real-playback.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
raise SystemExit(0 if results["passed"] else 1)
