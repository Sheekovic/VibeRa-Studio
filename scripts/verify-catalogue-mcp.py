"""Actual VST3 discovery-to-export through MCP, with isolated filesystem faults.

No downloads, native pointer injection, user settings writes or listening claim.
"""
import hashlib
import importlib.util
import json
import pathlib
import sys
import tempfile
import time
import wave
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]
app, fixture, output = (pathlib.Path(x).resolve() for x in sys.argv[1:4])
client_directory = app.parent / "skills/vibera-studio/scripts"
if not client_directory.is_dir():
    client_directory = ROOT / "skills/vibera-studio/scripts"
sys.path.insert(0, str(client_directory))
from stdio_client import VibeRaClient

# Reuse the installed-provider observer: command acceptance is not completion.
observer_spec = importlib.util.spec_from_file_location(
    "catalogue_command_observer", ROOT / "scripts/verify-installed-mcp.py")
observer = importlib.util.module_from_spec(observer_spec)
observer_spec.loader.exec_module(observer)

output.mkdir(parents=True, exist_ok=True)
report = {"passed": False, "checks": [], "applicationSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
          "verificationScriptSha256": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
          "clientSha256": hashlib.sha256((client_directory / "stdio_client.py").read_bytes()).hexdigest(),
          "evidence": "Actual installed VST3 scanner and runtime through semantic MCP; disposable settings/filesystem fault, virtual/offline audio. No listening, hardware or native pointer input."}
client = None
settings = None
saved_xml = None
sentinel = None
began = time.monotonic()


def check(value, description):
    if not value:
        raise AssertionError(description)
    report["checks"].append(description)


def wait_scan(receipt, expected="completed"):
    check(receipt["completionState"] == "pending", "Scan acceptance is pending, not false completion")
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        response = client.call("vibera_catalogue", {"action": "job", "operation": receipt["operation"]}, allow_failure=True)
        result = response["structuredContent"]
        if result["completionState"] != "pending":
            check(result["completionState"] == expected, "Observed scan terminal state: " + expected + ": " + str(result.get("error", "")))
            while client.data("vibera_catalogue", {"action": "state"})["scanning"]:
                if time.monotonic() >= deadline:
                    raise AssertionError("Scan thread did not finish after terminal result")
                time.sleep(.01)
            return result
        time.sleep(.02)
    raise AssertionError("Scan observation deadline; no outcome invented")


def command(request):
    admission = client.data("vibera_command", request)
    def record(observation):
        report.setdefault("commandObservations", []).append(
            {"command": request["command"], **observation})
        (output / "command-observations.json").write_text(
            json.dumps(report["commandObservations"], indent=2) + "\n", encoding="utf-8")
    return observer.await_command_result(
        admission,
        lambda tool, args: client.call(tool, args, allow_failure=True)["structuredContent"],
        record)


try:
    original = next(p for p in ET.parse(fixture).getroot().findall(".//PLUGIN") if p.get("instrument") == "1")
    installed = pathlib.Path(ET.fromstring(original.get("descriptor")).get("file")).resolve(strict=True)
    check(installed.suffix.lower() == ".vst3", "Explicit actual installed VST3 provider")
    report["provider"] = original.get("name")
    client = VibeRaClient(app)
    report["pid"] = client.process.pid
    report["version"] = client.server["version"]
    initial = client.data("vibera_catalogue", {"action": "state"})
    check(initial["disposableSettings"] and initial["standardLocations"] and initial["pluginCount"] == 0,
          "Application test catalogue isolated from user settings, familiar standard locations default")
    settings = pathlib.Path(initial["settingsPath"]).resolve()
    check(settings.parent == pathlib.Path(tempfile.gettempdir()).resolve() and not settings.exists(),
          "Exact new owned temporary settings path verified before filesystem fault injection")
    before_revision = client.data("vibera_state")["revision"]
    invalid = client.call("vibera_catalogue", {"action": "scan", "customFolders": ["relative"]}, True)
    check(invalid.get("isError") and invalid["structuredContent"]["error"]["code"] == "scan_refused",
          "Canonical invalid-folder refusal leaves settings untouched")
    bad_folder = output / "invalid-bundle"
    bad_folder.mkdir(exist_ok=True)
    bad_bundle = bad_folder / "Owned invalid scanner input.vst3"
    bad_bundle.write_text("Deliberately invalid owned scanner fixture, not a vendor plugin.")
    folders = [str(installed.parent), str(bad_folder)]
    scan_args = {"action": "scan", "customFolders": folders, "standardLocations": False}
    complete = wait_scan(client.data("vibera_catalogue", scan_args))
    check(complete["persisted"] and complete["blacklistCount"] == 1 and complete["failureCount"] == 1,
          "Actual invalid bundle failure is persisted/blacklisted without discarding successful installed provider")
    items, offset = [], 0
    while True:
        page = client.data("vibera_catalogue", {"action": "list", "kind": "instruments", "offset": offset, "limit": 1})
        items.extend(page["items"])
        if page["nextOffset"] is None:
            break
        offset = page["nextOffset"]
    target = next(p for p in items if pathlib.Path(p["path"]).resolve() == installed)
    descriptor = client.data("vibera_catalogue", {"action": "descriptor", "id": target["id"]})
    check(descriptor["instrument"] and ET.fromstring(descriptor["descriptor"]).get("name") == target["name"],
          "Paged stable class ID resolves the actual instrument descriptor")
    blacklist = client.data("vibera_catalogue", {"action": "list", "section": "blacklist"})["items"]
    failures = client.data("vibera_catalogue", {"action": "list", "section": "failures"})["items"]
    check(str(bad_bundle) in blacklist and len(failures) == 1, "Blacklist/failure semantic readback identifies exact owned bad bundle")
    saved_xml = settings.read_text(encoding="utf-8-sig")
    xml = ET.fromstring(saved_xml)
    check(xml.get("standardLocations") == "0" and len(xml.findall("BLACKLIST")) == 1 and
          any(p.get("name") == target["name"] for p in xml.findall("PLUGIN")),
          "Actual settings file contains matching class/configuration/blacklist")
    replay = wait_scan(client.data("vibera_catalogue", scan_args))
    check(replay["failureCount"] == 0 and replay["blacklistCount"] == 1,
          "Rescan skips retained blacklist without rerunning the failed bundle")
    refreshed_descriptor = client.data("vibera_catalogue", {"action": "descriptor", "id": target["id"]})
    check(refreshed_descriptor["id"] == descriptor["id"] and
          ET.fromstring(refreshed_descriptor["descriptor"]).get("file") == ET.fromstring(descriptor["descriptor"]).get("file"),
          "Class identity remains stable through rescan; refreshed metadata timestamps need not be identical")
    descriptor = refreshed_descriptor
    retry = wait_scan(client.data("vibera_catalogue", dict(scan_args, retryBlacklisted=True)))
    check(retry["failureCount"] == 1 and retry["blacklistCount"] == 1,
          "Explicit retry actually rescans the rejected bundle and retains its failure")
    saved_xml = settings.read_text(encoding="utf-8-sig")
    check(client.data("vibera_state")["revision"] == before_revision, "Global scan/settings do not create project Undo entries")

    cancellation = client.data("vibera_catalogue", {"action": "scan", "customFolders": folders,
                               "standardLocations": False, "retryBlacklisted": True})
    cancel = client.data("vibera_catalogue", {"action": "cancel", "operation": cancellation["operation"]})
    check(cancel["completionState"] in ("pending", "cancelled"), "Cancellation observes request/terminal state without pretending immediate completion")
    cancelled = wait_scan(cancellation, "cancelled")
    check(not cancelled["persisted"] and cancelled["blacklistCount"] == 1 and
          settings.read_text(encoding="utf-8-sig") == saved_xml,
          "Actual cancelled scan preserves previous catalogue/configuration/blacklist file")
    report["cancellation"] = cancelled["completionState"]

    # A real asynchronous writer failure against this exact disposable settings
    # file. Never inject faults into the normal user catalogue or a supplied path.
    saved_xml = settings.read_text(encoding="utf-8-sig")
    settings.unlink()
    settings.mkdir()
    sentinel = settings / "owned-catalogue-fault.txt"
    sentinel.write_text(str(client.process.pid))
    empty = output / "empty-scan"
    empty.mkdir(exist_ok=True)
    failed = wait_scan(client.data("vibera_catalogue", {"action": "scan", "customFolders": [str(empty)], "standardLocations": False}), "failed")
    check(failed["error"]["code"] == "persistence_failed" and failed["error"]["effectMayHaveOccurred"] and
          not failed["persisted"] and sentinel.read_text() == str(client.process.pid),
          "Async settings failure is explicit, truthful about memory effects, and preserves the owned sentinel")
    sentinel.unlink()
    settings.rmdir()
    settings.write_text(saved_xml, encoding="utf-8")
    sentinel = None
    recovered = wait_scan(client.data("vibera_catalogue", scan_args))
    check(recovered["persisted"] and client.call("vibera_catalogue", {"action": "job", "operation": failed["operation"]}, allow_failure=True)["structuredContent"]["completionState"] == "failed",
          "Later success does not hide a retained failed operation")

    before = client.data("vibera_inspect", {"entity": "channels"})["items"]
    command({"command": "plugin.instrument.create", "args": {"text": descriptor["descriptor"]}})
    channels = client.data("vibera_inspect", {"entity": "channels"})["items"]
    channel = next(c for c in channels if c["id"] not in {c["id"] for c in before})
    command({"command": "edit.undo"})
    check(client.data("vibera_inspect", {"entity": "channels"})["items"] == before, "Scanned descriptor creation is canonical and undoable")
    command({"command": "edit.redo"})
    state = client.data("vibera_state")
    def ready_instrument():
        return next((p for p in client.data("vibera_plugin", {"action":"list"})["items"]
                     if p["instrument"] and p["channel"] == channel["id"] and p["available"] and p["instance"]), None)
    runtime = ready_instrument()
    deadline = time.monotonic() + 25
    while state["pendingAudioUpdate"] or runtime is None:
        if time.monotonic() >= deadline:
            raise AssertionError("Scanned instrument worker not ready within unchanged25s guard: " + str(state["status"]))
        time.sleep(.03)
        state = client.data("vibera_state")
        runtime = ready_instrument()
    check(runtime["available"] and runtime["instance"], "Exact newly created Channel has a live ready instrument runtime after Undo/Redo")
    report["runtimeInstance"] = runtime["instance"]
    report["runtimePlugin"] = runtime["id"]
    pattern = state["selectedPattern"]
    command({"command": "notes.add", "args": {"pattern": pattern, "channel": channel["id"], "pitch": 60, "beat": 0, "length": .5, "value": .8}})
    command({"command": "clip.add", "args": {"pattern": pattern, "beat": 0, "length": 4, "lane": 0}})
    project = output / "discovered-instrument.vibra"
    client.wait_job(client.data("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
    opened = client.data("vibera_project", {"action": "open", "path": str(project), "discardChanges": True})
    check(opened["completionState"] == "completed", "Saved discovered instrument actually reopened")
    wav_path = output / "discovered-instrument.wav"
    client.wait_job(client.data("vibera_project", {"action": "export", "path": str(wav_path), "overwrite": True}))
    with wave.open(str(wav_path), "rb") as wav:
        check(wav.getnframes() > 0 and any(wav.readframes(wav.getnframes())), "Discovered actual instrument survives save/reopen and produces nonzero hosted WAV")
    report["stableClassId"] = target["id"]
    report["settingsPath"] = str(settings)
    report["asyncFailure"] = failed["error"]["code"]
    report["passed"] = True
except Exception as failure:
    report["error"] = str(failure)
finally:
    if sentinel is not None and settings is not None and sentinel.is_file() and client is not None:
        # Exact nonrecursive cleanup of this test's own fault target only.
        if sentinel.read_text() == str(client.process.pid) and list(settings.iterdir()) == [sentinel]:
            sentinel.unlink()
            settings.rmdir()
            settings.write_text(saved_xml, encoding="utf-8")
    if client is not None:
        try:
            client.close()
            report["exitCode"] = client.process.returncode
            if settings is not None:
                report["temporarySettingsRemoved"] = not settings.exists()
                check(not settings.exists(), "Owned test catalogue removed after scanner/service destruction")
        except Exception as failure:
            report["passed"] = False
            report["shutdownError"] = str(failure)
    report["seconds"] = time.monotonic() - began
    (output / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
if not report["passed"]:
    raise SystemExit(1)
