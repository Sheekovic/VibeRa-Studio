"""Forced-termination recovery acceptance (RQ-C), on an owned real-mode instance.

Runs the app in normal (--mcp) mode with its shortcut profile, autosaves and session markers
redirected to an owned folder (VIBERA_PROFILE_DIRECTORY) and a 5 s autosave interval. It edits
a project, waits for the autosave, kills the exact owned process (no graceful exit), then
checks the crash marker and autosave survived, starts a second owned instance on the same
profile, opens the autosave and checks the edit is there. The user's profile is never used.
The recovery dialog itself is covered in-process by the smoke test (verifyAutosave).

usage: python scripts/verify-crash-recovery.py [executable] [output folder]
"""
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
base = Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / "build/module-qualification/release-quality/recovery").resolve()
output = base / ("attempt-" + uuid.uuid4().hex)
profile = output / "profile"
profile.mkdir(parents=True)
env = dict(os.environ, VIBERA_PROFILE_DIRECTORY=str(profile), VIBERA_AUTOSAVE_SECONDS="5", VIBERA_NO_UPDATE_CHECK="1")
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


class Client:
    def __init__(self):
        self.serial = 0
        self.messages = queue.Queue()
        self.process = subprocess.Popen([str(exe), "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, encoding="utf-8", env=env,
                                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        threading.Thread(target=self._receive, daemon=True).start()
        self.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                    "clientInfo": {"name": "VibeRa recovery acceptance", "version": "1"}})
        self.process.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
        self.process.stdin.flush()

    def _receive(self):
        try:
            for line in self.process.stdout:
                self.messages.put(json.loads(line))
        except Exception as failure:
            self.messages.put(failure)
        self.messages.put(RuntimeError("server stdout closed"))

    def request(self, method, params):
        self.serial += 1
        self.process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.serial, "method": method, "params": params}) + "\n")
        self.process.stdin.flush()
        while True:
            response = self.messages.get(timeout=30)
            if isinstance(response, Exception):
                raise response
            if response.get("id") == self.serial:
                if "error" in response:
                    raise RuntimeError(str(response))
                return response["result"]

    def data(self, tool, arguments=None):
        result = self.request("tools/call", {"name": tool, "arguments": arguments or {}})
        if result.get("isError"):
            raise RuntimeError(str(result.get("structuredContent", result)))
        return result["structuredContent"]

    def job(self, started):
        deadline = time.monotonic() + 60
        while started.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            started = self.data("vibera_project", {"action": "job", "operation": started["operation"]})
        return started

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.process.kill()  # The exact owned handle, after graceful EOF failed.
                self.process.wait(timeout=10)


def marked_notes(client):
    items = client.data("vibera_inspect", {"entity": "notes", "limit": 500}).get("items", [])
    return sorted((n["channel"], n["pitch"], round(n["beat"], 4)) for n in items if n["pitch"] == 97)


passed = False
receipt = {}
first = second = None
try:
    first = Client()
    first.data("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    kick = first.data("vibera_state")["rack"]["rows"][0]["id"]
    for beat in (0.5, 1.5, 2.75):  # Pitch 97 marks the edits made after the last save.
        first.data("vibera_command", {"command": "notes.add",
                                      "args": {"channel": kick, "pitch": 97, "beat": beat, "length": 0.25, "value": 0.8}})
    edited = marked_notes(first)
    check(len(edited) == 3, "Three marked notes were added before the crash")
    autosave_dir = profile / "autosave"
    deadline = time.monotonic() + 60
    autosaves = []
    while time.monotonic() < deadline and not autosaves:
        time.sleep(.5)
        autosaves = list(autosave_dir.glob("*.autosave.vibra")) if autosave_dir.is_dir() else []
    check(len(autosaves) == 1, "The dirty project was autosaved within the shortened interval")
    time.sleep(1)  # Let the marker rewrite that follows the autosave land.
    marker = autosave_dir / f"session-{first.process.pid}.json"
    check(marker.is_file() and json.loads(marker.read_text(encoding="utf-8"))["autosave"] == str(autosaves[0]),
          "The session marker points at the autosave")

    first.process.kill()  # Forced termination of the exact owned process: no clean exit runs.
    first.process.wait(timeout=20)
    check(first.process.returncode != 0, "The first instance was terminated, not closed cleanly")
    check(marker.is_file() and autosaves[0].is_file(), "The crash marker and the autosave survive the forced termination")
    receipt["autosaveBytes"] = autosaves[0].stat().st_size

    second = Client()
    check(second.process.pid != first.process.pid, "A new instance starts on the same profile")
    check(marker.is_file(), "The crashed session's marker is still there for the recovery offer")
    opened = second.job(second.data("vibera_project", {"action": "open", "path": str(autosaves[0]), "discardChanges": True}))
    check(opened.get("completionState") == "completed", "The autosave opens: " + str(opened)[:200])
    check(marked_notes(second) == edited, "Every edit made after the last save is recovered")
    second.close()
    check(second.process.returncode == 0, "The recovering instance exits cleanly")
    check(not (autosave_dir / f"session-{second.process.pid}.json").exists(), "A clean exit leaves no crash marker of its own")
    passed = True
finally:
    for client in (first, second):
        if client and client.process.poll() is None:
            client.process.kill()
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    sources = ["scripts/verify-crash-recovery.py", "src/StudioAutosave.cpp", "src/Studio.cpp", "src/Project.h"]
    receipt["dependencyHashes"] = [dict(path=s, mode="content", sha256=content_identity(ROOT / s)["contentSha256"]) for s in sources]
    worker = exe.parent / "VibeRa Plugin Worker.exe"
    receipt["candidateHashes"] = [dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                  for p in (exe, worker) if p.is_file() and ROOT in p.parents]
    receipt.update(passed=passed, checks=checks,
                   scope="Owned real-mode instance with a redirected profile; recovery dialog covered in-process")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
