"""Project migration acceptance (RQ-D).

Opens real older-schema projects (the bundled examples are schema 1; optional extra paths,
for example an owner project, are copied first and never modified), saves each at the current
schema, reopens the saved file and compares the full semantic readback. For the examples it
also renders the Song before and after the round trip and requires identical PCM, so the
migration keeps every audible setting. A future version must be refused without changing the
file.

usage: python scripts/verify-project-migration.py [executable] [extra project ...]
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys
import time
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
extra = [Path(p).resolve() for p in sys.argv[2:]]
output = ROOT / "build/module-qualification/release-quality/migration" / ("attempt-" + uuid.uuid4().hex)
work = output / "work"
work.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks, projects = [], []
ENTITIES = ["patterns", "notes", "channels", "clips", "mixer", "markers", "tracks", "tempo", "arrangements",
            "automationSources", "automationPoints"]


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def schema(path):
    match = re.search(rb'<VIBERA\s+version="(\d+)"', path.read_bytes()[:400])
    return int(match.group(1)) if match else None


def wait(client, started):
    deadline = time.monotonic() + 120
    while started.get("completionState") == "pending" and time.monotonic() < deadline:
        time.sleep(.1)
        started = client.call("vibera_project", {"action": "job", "operation": started["operation"]}, allow_failure=True)["structuredContent"]
    return started


def readback(client):
    state = {}
    for entity in ENTITIES:
        items, offset = [], 0
        while True:
            page = client.call("vibera_inspect", {"entity": entity, "limit": 500, "offset": offset}, allow_failure=True)["structuredContent"]
            batch = page.get("items")
            if batch is None:
                state[entity] = page.get("error")
                break
            items += batch
            offset += len(batch)
            if not batch or page.get("nextOffset") is None and len(batch) < 500:
                break
            offset = page.get("nextOffset", offset)
        else:
            pass
        if isinstance(items, list):
            state[entity] = items
    return state


def pcm(path):
    with wave.open(str(path), "rb") as w:
        params = w.getparams()
        frames = w.readframes(w.getnframes())
    check(frames.strip(b"\x00") != b"", "The render is not silent: " + path.name)
    return params[:3], hashlib.sha256(frames).hexdigest()


passed = False
client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
try:
    sources = [(p, True) for p in sorted((ROOT / "examples").glob("*.vibra"))] + [(p, False) for p in extra]
    for source, render in sources:
        copy = work / ("source-" + source.name)
        shutil.copy2(source, copy)  # Never open or save the original file.
        before_bytes = copy.read_bytes()
        version = schema(copy)
        opened = wait(client, client.call("vibera_project", {"action": "open", "path": str(copy), "discardChanges": True}, allow_failure=True)["structuredContent"])
        check(opened.get("completionState") == "completed", f"{source.name} (schema {version}) opens: " + str(opened)[:200])
        first = readback(client)
        if render:
            wav_a = work / (source.stem + "-before.wav")
            done = wait(client, client.call("vibera_project", {"action": "export", "path": str(wav_a), "overwrite": True}, allow_failure=True)["structuredContent"])
            check(done.get("completionState") == "completed", f"{source.name} renders before the round trip: " + str(done)[:200])
        saved = work / ("migrated-" + source.name)
        done = wait(client, client.call("vibera_project", {"action": "save", "path": str(saved), "overwrite": True}, allow_failure=True)["structuredContent"])
        check(done.get("completionState") == "completed", f"{source.name} saves at the current schema")
        check(copy.read_bytes() == before_bytes, f"{source.name}: opening and saving elsewhere leaves the source unchanged")
        reopened = wait(client, client.call("vibera_project", {"action": "open", "path": str(saved), "discardChanges": True}, allow_failure=True)["structuredContent"])
        check(reopened.get("completionState") == "completed", f"{source.name}: the migrated file reopens")
        second = readback(client)
        differing = [e for e in ENTITIES if first.get(e) != second.get(e)]
        check(not differing, f"{source.name}: semantic readback is identical after migration; differs: {differing}")
        row = dict(name=source.name, fromSchema=version, toSchema=schema(saved),
                   counts={e: len(first[e]) if isinstance(first[e], list) else first[e] for e in ENTITIES})
        if render:
            wav_b = work / (source.stem + "-after.wav")
            done = wait(client, client.call("vibera_project", {"action": "export", "path": str(wav_b), "overwrite": True}, allow_failure=True)["structuredContent"])
            check(done.get("completionState") == "completed", f"{source.name} renders after the round trip")
            a, b = pcm(wav_a), pcm(wav_b)
            check(a == b, f"{source.name}: the migrated project renders identical PCM")
            row["pcmSha256"] = a[1]
        projects.append(row)
    future = work / "future.vibra"
    data = (work / ("migrated-" + sources[0][0].name)).read_bytes()
    future.write_bytes(re.sub(rb'(<VIBERA\s+version=")\d+', rb"\g<1>999", data, count=1))
    future_bytes = future.read_bytes()
    refused = wait(client, client.call("vibera_project", {"action": "open", "path": str(future), "discardChanges": True}, allow_failure=True)["structuredContent"])
    check(refused.get("completionState") == "failed" and future.read_bytes() == future_bytes,
          "A newer-schema project is refused and left unchanged")
    passed = True
finally:
    client.close()
    if passed:
        shutil.rmtree(work, ignore_errors=True)  # Copies of user material stay only on failure.
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    deps = ["scripts/verify-project-migration.py", "src/Project.cpp", "src/Project.h"] + \
           [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "examples").glob("*.vibra"))]
    receipt = dict(passed=passed, checks=checks, projects=projects,
                   dependencyHashes=[dict(path=d, mode="content" if d.endswith((".py", ".cpp", ".h")) else "exact",
                                          sha256=content_identity(ROOT / d)["contentSha256" if d.endswith((".py", ".cpp", ".h")) else "sha256"]) for d in deps],
                   candidateHashes=[dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                    for p in (exe, exe.parent / "VibeRa Plugin Worker.exe") if p.is_file() and ROOT in p.parents],
                   scope="Isolated --mcp-test instance; owner projects only as copies; listening not claimed")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), projects=projects, receipt=str(output / "verification.json")))[:1500])
sys.exit(0 if passed else 1)
