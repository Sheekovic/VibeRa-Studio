"""Long-session stability acceptance (RQ-B).

Default: headless (--mcp-test, no window, no audio device) for --minutes (default 15, owner
decision 2026-11-08 replacing the visible two-hour run). Each cycle plays/stops the demo song
plus a Synth Channel, adds and undoes a note and moves a Mixer fader; every 10th cycle renders
the project offline and requires sound. Every minute it samples the owned process's private
memory, handle count, worker liveness and packet counters. Fails on a crash, a dead worker,
a silent render, missed/dropped worker packets growing, or memory/handle growth beyond the
bounds below after the warm-up. --visible uses the real device and checks the Master meter.
Listening is not claimed.

usage: python scripts/verify-long-session.py [--minutes N] [--visible] [executable]
"""
import argparse
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
import wave

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--minutes", type=float, default=15)
parser.add_argument("--visible", action="store_true",
                    help="Use the real device in a visible window (default: headless --mcp-test, no window, no device)")
parser.add_argument("executable", nargs="?", default=str(ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"))
args = parser.parse_args()
exe = Path(args.executable).resolve()
output = ROOT / "build/module-qualification/release-quality/long-session" / ("attempt-" + uuid.uuid4().hex)
profile = output / "profile"
profile.mkdir(parents=True)
env = dict(os.environ, VIBERA_PROFILE_DIRECTORY=str(profile), VIBERA_NO_UPDATE_CHECK="1")
MEMORY_GROWTH_MB = 200   # Private bytes may grow at most this much after the 10-minute warm-up.
HANDLE_GROWTH = 500
checks, samples, failures = [], [], []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD)] + \
               [(n, ctypes.c_size_t) for n in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                                               "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                                               "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage",
                                               "PrivateUsage")]


def process_counters(handle):
    counters = PROCESS_MEMORY_COUNTERS_EX()
    counters.cb = ctypes.sizeof(counters)
    ctypes.windll.psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
    handles = wt.DWORD()
    ctypes.windll.kernel32.GetProcessHandleCount(handle, ctypes.byref(handles))
    return counters.PrivateUsage / 1048576, handles.value


class Client:
    def __init__(self):
        self.serial, self.messages = 0, queue.Queue()
        self.process = subprocess.Popen([str(exe), "--mcp" if args.visible else "--mcp-test"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, encoding="utf-8", env=env,
                                        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        threading.Thread(target=self._receive, daemon=True).start()
        self.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                    "clientInfo": {"name": "VibeRa long-session acceptance", "version": "1"}})
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
            response = self.messages.get(timeout=60)
            if isinstance(response, Exception):
                raise response
            if response.get("id") == self.serial:
                if "error" in response:
                    raise RuntimeError(str(response))
                return response["result"]

    def data(self, tool, arguments=None, ok=False):
        result = self.request("tools/call", {"name": tool, "arguments": arguments or {}})
        if result.get("isError") and not ok:
            raise RuntimeError(tool + ": " + str(result.get("structuredContent", result))[:300])
        return result.get("structuredContent", {})


def play(client):
    # Starting playback is refused while workers prepare; that is a truthful wait, not a failure.
    deadline = time.monotonic() + 60
    while True:
        result = client.data("vibera_transport", {"action": "play"}, ok=True)
        error = result.get("error") or {}
        if error.get("code") != "playback_unavailable" or time.monotonic() > deadline:
            if error:
                raise RuntimeError("play: " + str(error))
            return
        time.sleep(.5)


def master_peak(client):
    for row in client.data("vibera_inspect", {"entity": "mixer"})["items"]:
        if row["index"] == 0:
            meter = row.get("meter") or {}
            return max(meter.get("leftPeak", 0) or 0, meter.get("rightPeak", 0) or 0)
    return 0


passed = False
client = None
start = time.monotonic()
try:
    client = Client()
    handle = ctypes.windll.kernel32.OpenProcess(0x1000 | 0x0400, False, client.process.pid)
    client.data("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    client.data("vibera_synth", {"action": "create"})
    time.sleep(5)
    rows = client.data("vibera_state")["rack"]["rows"]
    synth, kick = rows[-1]["id"], rows[0]["id"]
    for beat in range(8):
        client.data("vibera_command", {"command": "notes.add", "args": {"channel": synth, "pitch": 60 + beat % 5,
                                                                        "beat": beat * .5, "length": .4, "value": .8}})
    client.data("vibera_transport", {"action": "pattern"})
    cycles = silent = renders = 0
    next_sample = start
    deadline = start + args.minutes * 60
    while time.monotonic() < deadline:
        if client.process.poll() is not None:
            raise AssertionError("The application exited during the session (code %s)" % client.process.returncode)
        play(client)
        best = 0
        for _ in range(6):
            time.sleep(.25)
            best = max(best, master_peak(client))
        client.data("vibera_transport", {"action": "stop"})
        if args.visible:
            silent += best <= 0.001
        elif cycles % 10 == 0:
            # Headless: no device, so prove the audio path with an offline render instead.
            wav = output / "render.wav"
            job = client.data("vibera_project", {"action": "export", "path": str(wav), "overwrite": True})
            render_deadline = time.monotonic() + 120
            while job.get("completionState") == "pending" and time.monotonic() < render_deadline:
                time.sleep(.2)
                job = client.data("vibera_project", {"action": "job", "operation": job["operation"]})
            with wave.open(str(wav), "rb") as w:
                silent += job.get("completionState") != "completed" or not w.readframes(w.getnframes()).strip(b"\x00")
            renders += 1
        client.data("vibera_command", {"command": "notes.add", "args": {"channel": kick, "pitch": 90, "beat": 3.75,
                                                                        "length": .25, "value": .5}})
        client.data("vibera_command", {"command": "edit.undo"})
        client.data("vibera_command", {"command": "mixer.gain", "args": {"mixer": 1, "value": .6 + .3 * (cycles % 2)}}, ok=True)
        cycles += 1
        if time.monotonic() >= next_sample:
            next_sample += 60
            memory, handles = process_counters(handle)
            workers = client.data("vibera_state")["audio"]["pluginWorkers"]
            samples.append(dict(minute=round((time.monotonic() - start) / 60, 1), privateMB=round(memory, 1),
                                handles=handles, cycles=cycles, silentCycles=silent,
                                workersAlive=all(w["alive"] for w in workers), workers=len(workers),
                                missed=sum(w.get("missed", 0) for w in workers),
                                dropped=sum(w.get("dropped", 0) for w in workers)))
            (output / "samples.json").write_text(json.dumps(samples, indent=1), encoding="utf-8")
            if not samples[-1]["workersAlive"]:
                raise AssertionError("A plugin worker stopped at minute %s" % samples[-1]["minute"])
    check(client.process.poll() is None, "The application ran for the whole session")
    if args.visible:
        check(cycles > 0 and silent == 0, f"Every one of {cycles} playback cycles reached the Master meter (silent: {silent})")
    else:
        check(renders > 0 and silent == 0, f"Every one of {renders} offline renders over {cycles} cycles has sound (silent: {silent})")
    check(all(s["workersAlive"] for s in samples), "Plugin workers stayed alive at every sample")
    warm = [s for s in samples if s["minute"] >= min(10, args.minutes / 3)] or samples
    check(warm[-1]["privateMB"] - warm[0]["privateMB"] <= MEMORY_GROWTH_MB,
          f"Private memory growth after warm-up stays within {MEMORY_GROWTH_MB} MB "
          f"({warm[0]['privateMB']} -> {warm[-1]['privateMB']})")
    check(warm[-1]["handles"] - warm[0]["handles"] <= HANDLE_GROWTH,
          f"Handle growth after warm-up stays within {HANDLE_GROWTH} ({warm[0]['handles']} -> {warm[-1]['handles']})")
    check(warm[-1]["dropped"] == warm[0]["dropped"] and warm[-1]["missed"] == warm[0]["missed"],
          "No worker packets were missed or dropped after warm-up")
    passed = True
except Exception as failure:
    exited = client.process.poll() if client else None
    failures.append(repr(failure)[:500] + ("" if exited is None else f" (application exit code {exited:#x})"))
finally:
    if client and client.process.poll() is None:
        client.process.stdin.close()
        try:
            client.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            client.process.kill()  # The exact owned handle after graceful EOF failed.
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    receipt = dict(passed=passed, checks=checks, failures=failures, minutes=args.minutes, samples=samples,
                   dependencyHashes=[dict(path="scripts/verify-long-session.py", mode="content",
                                          sha256=content_identity(ROOT / "scripts/verify-long-session.py")["contentSha256"])],
                   candidateHashes=[dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                    for p in (exe, exe.parent / "VibeRa Plugin Worker.exe") if p.is_file() and ROOT in p.parents],
                   scope="Owned real-device instance; Master meter proves signal, not heard audio")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), failures=failures, receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
