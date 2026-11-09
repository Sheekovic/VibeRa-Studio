"""Real-device check of Quantize song jumps on an owned --mcp instance (Master muted, own profile).

With jumps quantized to the next bar, a jump requested at beat ~1 waits for beat 4 and then lands at 16;
with quantize off the same jump is immediate. Needs an audio device; plays muted.
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
    req("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "jump check", "version": "1"}})
    p.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}' + chr(10)); p.stdin.flush()
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    call("vibera_command", {"command": "mixer.mute", "args": {"mixer": 0, "enabled": True}})
    call("vibera_transport", {"action": "song"})
    call("vibera_transport", {"action": "jumpQuantize", "quantize": 1})
    call("vibera_transport", {"action": "seek", "beat": 0})
    call("vibera_transport", {"action": "play"})
    time.sleep(.5)
    call("vibera_transport", {"action": "jump", "beat": 16})
    st = call("vibera_state", {})
    results["requestedAt"] = st.get("positionBeat")
    results["pending"] = st.get("pendingSongJump")
    time.sleep(.3)
    results["beforeBoundary"] = call("vibera_state", {}).get("positionBeat")
    time.sleep(2.2)
    results["afterBoundary"] = call("vibera_state", {}).get("positionBeat")
    call("vibera_transport", {"action": "jumpQuantize", "quantize": 0})
    call("vibera_transport", {"action": "jump", "beat": 40})
    time.sleep(.15)
    results["unquantized"] = call("vibera_state", {}).get("positionBeat")
    call("vibera_transport", {"action": "stop"})
finally:
    try:
        p.stdin.close(); p.wait(timeout=20)
    except Exception:
        p.terminate()
checks = {
    "The jump waits for the next bar line (pending at beat 4)": (results.get("pending") or {}).get("at") == 4,
    "Before the bar line the playhead keeps playing below 4": results.get("beforeBoundary", 99) < 4,
    "After the bar line the playhead continues from 16": 16 <= results.get("afterBoundary", 0) < 20,
    "With quantize off the jump is immediate": 40 <= results.get("unquantized", 0) < 41,
}
results["checks"] = checks
results["passed"] = all(checks.values())
print(json.dumps(results))
(out / "real-playback.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
raise SystemExit(0 if results["passed"] else 1)
