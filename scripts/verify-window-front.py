"""New windows open in front (RQ-F), on an owned real-mode instance with its own profile.

Launches the app (--mcp, profile redirected), reads the desktop z-order and requires the main
window above every ordinary window of other processes (always-on-top system overlays are
excluded), then opens the Synthesizer editor (a separate worker process) and requires it on
top too. Keyboard activation itself is not claimed: Windows refuses foreground to processes
started from the background, which is exactly the case the raise must still handle.

usage: python scripts/verify-window-front.py [executable]
"""
import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
output = ROOT / "build/module-qualification/release-quality/window-front" / ("attempt-" + uuid.uuid4().hex)
(output / "profile").mkdir(parents=True)
env = dict(os.environ, VIBERA_PROFILE_DIRECTORY=str(output / "profile"), VIBERA_NO_UPDATE_CHECK="1")
user32 = ctypes.windll.user32
checks, observations = [], {}


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def windows():
    """Visible titled top-level windows in z-order (top first), without always-on-top overlays."""
    out = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def visit(hwnd, _):
        if user32.IsWindowVisible(hwnd) and not (user32.GetWindowLongW(hwnd, -20) & 0x8):  # WS_EX_TOPMOST
            buffer = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, buffer, 256)
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if buffer.value:
                out.append((buffer.value, pid.value))
        return True

    user32.EnumWindows(visit, 0)
    return out


class Client:
    def __init__(self):
        self.serial, self.messages = 0, queue.Queue()
        self.process = subprocess.Popen([str(exe), "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, encoding="utf-8", env=env)
        threading.Thread(target=self._receive, daemon=True).start()
        self.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                    "clientInfo": {"name": "VibeRa window acceptance", "version": "1"}})
        self.process.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
        self.process.stdin.flush()

    def _receive(self):
        for line in self.process.stdout:
            self.messages.put(json.loads(line))
        self.messages.put(RuntimeError("server stdout closed"))

    def request(self, method, params):
        self.serial += 1
        self.process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.serial, "method": method, "params": params}) + "\n")
        self.process.stdin.flush()
        while True:
            response = self.messages.get(timeout=60)
            if isinstance(response, Exception):
                raise response
            if response.get("id") == self.serial:
                return response.get("result", {})

    def data(self, tool, arguments=None):
        return self.request("tools/call", {"name": tool, "arguments": arguments or {}}).get("structuredContent", {})


passed = False
client = None
try:
    client = Client()
    time.sleep(8)
    order = windows()
    observations["afterLaunch"] = order[:5]
    check(order and order[0][1] == client.process.pid, "At launch the VibeRa window is above every other ordinary window")
    client.data("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    client.data("vibera_synth", {"action": "create"})
    deadline = time.monotonic() + 30
    workers = []
    while time.monotonic() < deadline and not workers:
        time.sleep(.5)
        workers = client.data("vibera_state").get("audio", {}).get("pluginWorkers", [])
    check(workers, "The Synthesizer worker started")
    plugin = workers[-1]["plugin"]
    rows = next(v for v in client.data("vibera_plugin", {"action": "list"}).values() if isinstance(v, list))
    instance = next(r["instance"] for r in rows if r.get("id") == plugin)
    client.data("vibera_synth", {"action": "openEditor", "plugin": plugin, "instance": instance})
    time.sleep(4)
    order = windows()
    observations["afterEditor"] = order[:5]
    check(order and "Synthesizer" in order[0][0] and order[0][1] != client.process.pid,
          "The worker's Synthesizer window opens above every other ordinary window")
    passed = True
finally:
    if client and client.process.poll() is None:
        client.process.stdin.close()
        try:
            client.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            client.process.kill()  # The exact owned handle after graceful EOF failed.
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    deps = ["scripts/verify-window-front.py", "src/WindowForeground.cpp", "src/DetachedWindow.h"]
    receipt = dict(passed=passed, checks=checks,
                   observations={k: [t for t, _ in v] for k, v in observations.items()},
                   dependencyHashes=[dict(path=d, mode="content", sha256=content_identity(ROOT / d)["contentSha256"]) for d in deps],
                   candidateHashes=[dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                    for p in (exe, exe.parent / "VibeRa Plugin Worker.exe") if p.is_file() and ROOT in p.parents],
                   scope="Desktop z-order on this machine; activation, mixed-monitor DPI and monitor unplugging by a person are separate")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
