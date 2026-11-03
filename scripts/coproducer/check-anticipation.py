# SPDX-License-Identifier: AGPL-3.0-or-later
"""Boundary/failure tests plus real offline worker qualification on an original score."""
import copy
import json
import math
import pathlib
import subprocess
import sys
import time
from anticipation_context import prepare, decode, DURATION, NOTE

root = pathlib.Path(__file__).resolve().parents[2]
installation = root / "build/coproducer-research/anticipation"
output = installation / ("worker-check-" + time.strftime("%Y%m%d-%H%M%S"))
output.mkdir(exist_ok=False)
profile = lambda program: {"program": program, "source": "user-specified", "pitches": list(range(128))}
request = {"schema": 1, "model": "anticipation-small", "requestId": "original-worker-check", "fingerprint": "a" * 64,
           "destination": "1", "seed": 17, "count": 1, "tempo": 120, "length": 4, "numerator": 4, "denominator": 4,
           "style": "", "role": "native part", "variation": "new idea", "chords": [],
           "profiles": {"1": profile(32), "2": profile(0), "3": profile(32), "4": profile(25)}, "events": []}
def event(channel, start, length, pitch):
    request["events"].append({"channel": channel, "note": str(len(request["events"]) + 1), "start": start,
                              "length": length, "pitch": pitch, "velocity": .8, "audible": True, "locked": False})
for start, pitches in [(0, (60, 64, 67)), (2, (65, 69, 72))]:
    for pitch in pitches:
        event("2", start, 2, pitch)
event("3", 1, .6, 43)
event("1", -3, 1, 43)
event("1", -1.5, 1, 48)
event("4", 6, 1, 67)
checks = []
def check(name, condition):
    if not condition:
        raise AssertionError(name)
    checks.append(name)
def rejects(name, mutate):
    r = copy.deepcopy(request)
    mutate(r)
    try:
        prepare(r)
    except (ValueError, TypeError):
        checks.append(name)
        return
    raise AssertionError("Unexpectedly accepted " + name)

prepared = prepare(request)
check("Quarter notes mapped to seconds", prepared["startTick"] == 1000 and prepared["endTick"] == 1200)
check("Harmony/future controls and destination history consumed", len(prepared["controls"]) == 24 and len(prepared["inputs"]) == 6)
check("All source identities retained", len(prepared["ledger"]) == 10)
check("Round-trip target quarter-note timing", decode([1000, DURATION + 100, NOTE + 32 * 128 + 36], prepared)[0]["length"] == 2)
for name, mutation in [
    ("Unknown destination profile", lambda r: r["profiles"].pop("1")),
    ("Unknown included context", lambda r: r["profiles"].pop("2")),
    ("No plugin-name guessing", lambda r: r["profiles"]["1"].update(source="plugin-name")),
    ("Text style rejected", lambda r: r.update(style="funk")),
    ("Unsupported role rejected", lambda r: r.update(role="accompaniment")),
    ("Performance variation rejected", lambda r: r.update(variation="performance")),
    ("Chord override rejected", lambda r: r.update(chords=[{"pitches": [0, 4, 7]}])),
    ("Duration truncation rejected", lambda r: r["events"][0].update(length=24)),
    ("Explicit range enforced", lambda r: r["profiles"]["2"].update(pitches=[64])),
    ("Seed type enforced", lambda r: r.update(seed=True)),
    ("Schema boolean rejected", lambda r: r.update(schema=True)),
    ("Meter boolean rejected", lambda r: r.update(denominator=True)),
    ("Tempo map rejected", lambda r: r.update(tempoMap=[{"start": 0, "bpm": 100}])),
    ("Tuning rejected", lambda r: r.update(tuning="maqam")),
    ("Per-note tuning rejected", lambda r: r["events"][0].update(cents=50)),
    ("Nonfinite time rejected", lambda r: r["events"][0].update(start=float("nan"))),
    ("Subtick target endpoint rejected", lambda r: r.update(length=4.001)),
    ("Context budget enforced", lambda r: r.update(events=r["events"] * 25)),
    ("Exact destination lock rejected", lambda r: r["events"][0].update(channel="1", locked=True)),
]:
    rejects(name, mutation)
r = copy.deepcopy(request)
r["profiles"].pop("2")
r["allowUnmappedContext"] = True
check("Explicit unknown omissions reported", sum("omitted" in row for row in prepare(r)["ledger"]) == 6)
r = copy.deepcopy(request)
r.update(tempo=90, length=3, numerator=6, denominator=8)
check("Compound meter does not alter quarter-note time", prepare(r)["endTick"] == 1200)
r = copy.deepcopy(request)
r["events"][0]["start"] = .003
check("Timing quantization exposed", abs(prepare(r)["ledger"][0]["onsetRoundingSeconds"] + .0015) < 1e-9)
for name, tokens in [("Bad generated program rejected", [1000, 10001, NOTE + 60]),
                     ("Output region overrun rejected", [1199, 10002, NOTE + 32 * 128 + 60]),
                     ("Silence rejected", [])]:
    try:
        decode(tokens, prepared)
    except ValueError:
        checks.append(name)
    else:
        raise AssertionError(name)

def run(name, r, cancel=False):
    directory = output / name
    directory.mkdir()
    (directory / "request.json").write_text(json.dumps(r, allow_nan=False), encoding="utf-8")
    if cancel:
        (directory / "cancel").write_text("owned test cancellation", encoding="utf-8")
    started = time.perf_counter()
    with (directory / "worker.log").open("w", encoding="utf-8") as log:
        result = subprocess.run([sys.executable, str(root / "scripts/coproducer/anticipation-worker.py"), str(installation),
                                 str(directory / "request.json"), str(directory / "response.json")],
                                 stdout=log, stderr=subprocess.STDOUT, timeout=150,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    check(name + " owned child terminal", result.returncode == 0)
    response = json.loads((directory / "response.json").read_text(encoding="utf-8"))
    check(name + " response identity", response["requestId"] == r["requestId"] and response["fingerprint"] == r["fingerprint"])
    return response, time.perf_counter() - started

response, seconds = run("real-context", request)
check("Real local model success", response["status"] == "ok")
check("No fixture substitute", response["candidates"][0]["testFixture"] is False)
notes = response["candidates"][0]["events"]
check("Same conditioned notes as independent seconds probe", [(n["start"], n["length"], n["pitch"]) for n in notes] == [(1.5, .6, 42), (2, 2, 41)])
check("Destination source/history not returned as generated notes", len(notes) == 2)
check("All consumed/omitted context reported", len(response["context"]["ledger"]) == len(request["events"]))
check("Offline inference", response["measurements"]["networkInference"] is False)
compound = copy.deepcopy(request)
compound.update(tempo=90, length=3, numerator=6, denominator=8)
for e in compound["events"]:
    e["start"] *= .75
    e["length"] *= .75
compound_result, _ = run("real-compound-meter", compound)
check("Compound meter real inference", compound_result["status"] == "ok")
compound_notes = compound_result["candidates"][0]["events"]
check("Same seconds phrase at different tempo/meter", len(compound_notes) == 2 and all(
    n["pitch"] == pitch and math.isclose(n["start"], start, abs_tol=1e-12) and math.isclose(n["length"], length, abs_tol=1e-12)
    for n, (start, length, pitch) in zip(compound_notes, [(1.125, .45, 42), (1.5, 1.5, 41)])))
check("Identical model tokens across quarter-note tempos", compound_result["trials"][0]["generatedTokens"] == response["trials"][0]["generatedTokens"])
drums = copy.deepcopy(request)
drums.update(seed=20, count=2)
for channel in ("1", "3"):
    drums["profiles"][channel] = {"program": 128, "source": "user-specified", "pitches": [36, 38, 42, 46]}
for e in drums["events"]:
    if e["channel"] in ("1", "3"):
        e["pitch"] = 42 if e["start"] == -1.5 else 36
drum_result, _ = run("real-explicit-drum-map", drums)
check("Explicit drum map real inference", drum_result["status"] == "ok")
check("Two real candidates retained", len(drum_result["candidates"]) == 2)
check("No drum pitches outside user mapping", all(e["pitch"] in (36, 38, 42, 46) for c in drum_result["candidates"] for e in c["events"]))
cancelled, _ = run("cancelled-before-load", request, True)
check("Cancellation terminates without results", cancelled["status"] == "error" and cancelled["error"] == "Generation cancelled")
bad = copy.deepcopy(request)
bad["style"] = "funk"
unsupported, _ = run("unsupported-style", bad)
check("Unsupported request fails without model output", unsupported["status"] == "error" and "not qualified" in unsupported["error"])
directory = output / "cancel-during-inference"
directory.mkdir()
long_request = copy.deepcopy(request)
long_request.update(length=30, count=3)
(directory / "request.json").write_text(json.dumps(long_request), encoding="utf-8")
with (directory / "worker.log").open("w", encoding="utf-8") as log:
    child = subprocess.Popen([sys.executable, str(root / "scripts/coproducer/anticipation-worker.py"), str(installation),
                              str(directory / "request.json"), str(directory / "response.json")],
                              stdout=log, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        deadline = time.monotonic() + 60
        observed = None
        while child.poll() is None and time.monotonic() < deadline:
            try:
                observed = json.loads((directory / "progress.json").read_text(encoding="utf-8"))
                if observed.get("stage") == "generating" and observed.get("calls", 0) >= 3:
                    (directory / "cancel").write_text("owned running-inference cancellation", encoding="utf-8")
                    break
            except (FileNotFoundError, PermissionError, json.JSONDecodeError):
                pass
            time.sleep(.005)
        check("At least three real forward calls before cancellation", observed is not None and observed.get("stage") == "generating" and observed.get("calls", 0) >= 3)
        check("Running cancelled child terminal", child.wait(timeout=10) == 0)
    finally:
        if child.poll() is None:
            child.kill()  # Exact retained owned child handle; no executable-name cleanup.
            child.wait(timeout=10)
running_cancel = json.loads((directory / "response.json").read_text(encoding="utf-8"))
check("Running inference cancellation has no accepted candidates", running_cancel["status"] == "error" and running_cancel["error"] == "Generation cancelled" and "candidates" not in running_cancel)
summary = {"passed": True, "checks": checks, "count": len(checks), "realWorkerSeconds": seconds,
           "scope": "Original boundary fixtures and real offline structured inference; no native containment/playback/listening/style/lock qualification"}
(output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps({"passed": True, "checks": len(checks), "realWorkerSeconds": seconds, "output": str(output)}))
