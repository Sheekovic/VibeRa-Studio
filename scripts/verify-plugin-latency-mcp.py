"""Actual original VST3 declaration/nullable-consumption MCP qualification.

Usage: verify-plugin-latency-mcp.py EXECUTABLE OWNED_OUTPUT ORIGINAL_FIXTURE.vst3
Uses the provider-neutral client and disposable --mcp-test catalogue. There is
no audio driver, playback, synthetic pointer input, installed-provider scan or
listening claim. Core live/packet acceptance qualifies consumed wet PCM apart.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import time
import traceback
from datetime import datetime, timezone
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def await_command_result(admission, call, record, clock=time.monotonic, pause=time.sleep):
    """Observe this admitted owner once; never retry the canonical mutation."""
    started = clock()
    deadline = started + 40  # Existing job-observation budget; no audio deadline change.
    current = admission
    token = admission.get("operation")
    asynchronous = admission.get("completionState") == "pending"
    try:
        while current.get("completionState") == "pending":
            if clock() >= deadline:
                raise AssertionError("Canonical command operation timed out: " + str(current))
            if isinstance(token, str) and token:
                tool, request = "vibera_mixerchain", {"action": "job", "operation": token}
            elif type(token) is int and token > 0:
                tool, request = "vibera_mixer", {"action": "routingJob", "operation": token}
            else:
                raise AssertionError("Unknown asynchronous command operation: " + str(current))
            pause(.02)
            response = call(tool, request)
            current = response if tool == "vibera_mixerchain" else response["routing"]
            if type(current.get("operation")) is not type(token) or current.get("operation") != token:
                raise AssertionError("Canonical command observation changed operation: " + str(current))
        # Older ordinary commands return direct state without a completionState.
        # An admitted asynchronous job must instead reach its own explicit terminal.
        completion = current.get("completionState")
        if completion not in (("completed",) if asynchronous else (None, "completed")):
            raise AssertionError("Canonical command did not complete: " + str(current))
        if current.get("nativeRecoveryRequired"):
            raise AssertionError("Canonical command still requires native recovery: " + str(current))
        if not asynchronous and current.get("pendingAudioUpdate") is True:
            project = current.get("projectIdentity")
            while current["pendingAudioUpdate"]:
                if clock() >= deadline:
                    raise AssertionError("Canonical projection did not settle: " + str(current))
                pause(.02)
                current = call("vibera_state", {})
                if type(current.get("pendingAudioUpdate")) is not bool:
                    raise AssertionError("Projection observation lacks explicit pending state: " + str(current))
                if current.get("projectIdentity") != project:
                    raise AssertionError("Projection observation changed project: " + str(current))
        return current
    finally:
        record({"initial": admission, "terminal": current, "seconds": clock() - started})


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    app, output, original = [Path(value).resolve() for value in sys.argv[1:]]
    output.mkdir(parents=True, exist_ok=True)
    # Each attempt retains its own failures/projects/receipts. verification.json
    #at the output root is only the current-result index, never the sole history.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    attempt = output / ("attempt-" + stamp)
    attempt.mkdir(exist_ok=False)
    receipt = {
        "passed": False, "finished": False, "checks": [], "timings": {},
        "attemptDirectory": str(attempt), "nativeInput": False,
        "listening": False, "hardware": False, "liveAudio": False,
        "consumedWetPacketQualified": False,
        "scope": "Actual original VST3 scanner/runtime, canonical MCP history and project I/O; no device or wet-packet consumption",
        "observations": [], "operations": [], "invalidInputs": [],
    }
    began = time.monotonic()
    client = None
    staged = None

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        receipt["checks"].append(message)

    def retain(name, value):
        (attempt / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    try:
        preflight = time.monotonic()
        check(Path(sys.argv[1]).is_absolute() and app.is_file(), "Explicit absolute application exists")
        check(Path(sys.argv[3]).is_absolute() and original.is_file() and original.suffix.lower() == ".vst3",
              "Explicit absolute original standalone SDK VST3 fixture exists")
        worker = app.parent / "VibeRa Plugin Worker.exe"
        check(worker.is_file(), "Application-relative matching worker input exists")
        receipt.update(application=str(app), applicationSha256=digest(app), worker=str(worker),
                       workerSha256=digest(worker), originalFixture=str(original),
                       fixtureSha256=digest(original), verificationScriptSha256=digest(Path(__file__)))
        client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
        check(client_path.is_file(), "Reusable provider-neutral stdio client exists")
        receipt.update(skillClient=str(client_path), skillClientSha256=digest(client_path))
        loader = importlib.util.spec_from_file_location("vibera_latency_neutral_client", client_path)
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
        # Scan exactly one owned original fixture. Never scan its possibly broad
        #source folder or standard/installed-plugin directories.
        scan_folder = attempt / "owned-original-scan"
        scan_folder.mkdir()
        staged = scan_folder / original.name
        shutil.copyfile(original, staged)
        check(digest(staged) == receipt["fixtureSha256"], "Owned isolated scan input is byte-identical to original fixture")
        receipt["timings"]["preflightSeconds"] = time.monotonic() - preflight
        client = module.VibeRaClient(app)
        receipt.update(pid=client.process.pid, version=client.server.get("version"))
        data = client.data

        def state():
            return data("vibera_state")

        def command(name, **arguments):
            admission = data("vibera_command", {"command": name, "args": arguments})
            def record(observation):
                receipt["operations"].append({"tool": "vibera_command", "command": name, **observation})
                retain("operations.json", receipt["operations"])
            return await_command_result(
                admission,
                lambda tool, args: client.call(tool, args, allow_failure=True)["structuredContent"],
                record)

        def plugins():
            return data("vibera_plugin", {"action": "list"})["items"]

        def model():
            current = state()
            return {"revision": current["revision"], "projectIdentity": current["projectIdentity"],
                    "plugins": [{key: item[key] for key in ("id", "name", "instrument", "bypassed", "channel", "mixer", "slot")}
                                for item in plugins()]}

        def wait_job(tool, request, expected="completed", seconds=40):
            started = time.monotonic()
            operation = dict(request)
            check(operation.get("completionState") in ("pending", "completed"),
                  tool + " distinguishes accepted pending from actual completion")
            deadline = started + seconds
            while operation["completionState"] == "pending" and time.monotonic() < deadline:
                time.sleep(.02)
                response = client.call(tool, {"action": "job", "operation": operation["operation"]}, allow_failure=True)
                operation = response["structuredContent"]
            receipt["operations"].append({"tool": tool, "initial": request, "terminal": operation,
                                           "seconds": time.monotonic() - started})
            retain("operations.json", receipt["operations"])
            check(operation.get("completionState") == expected,
                  tool + " actual terminal state " + expected + ": " + str(operation.get("error", "")))
            return operation

        def runtime(plugin, label):
            started = time.monotonic()
            deadline = started + 25
            while True:
                current = state()
                workers = current["audio"]["pluginWorkers"]
                matched = [item for item in workers if item["plugin"] == plugin]
                if not current["pendingAudioUpdate"] and len(matched) == 1 and matched[0]["alive"]:
                    worker_state = matched[0]
                    break
                if time.monotonic() >= deadline:
                    retain(label + "-readiness-failure.json", current)
                    raise AssertionError("Actual original effect worker readiness deadline: " + str(current.get("status")))
                time.sleep(.03)
            receipt["timings"][label + "ReadinessSeconds"] = time.monotonic() - started
            receipt["observations"].append({"label": label, "audio": current["audio"], "playing": current["playing"]})
            retain("runtime-observations.json", receipt["observations"])
            declared = worker_state["latencyDeclared"]
            check(type(declared.get("nativeSamples")) is int and declared["nativeSamples"] == 31 and
                  type(declared.get("generation")) is int and declared["generation"] == 1,
                  label + " actual original native31 generation1 declaration")
            check(worker_state.get("latencyConsumed", "absent") is None and worker_state["submitted"] == 0 and
                  worker_state["processed"] == 0 and not current["playing"] and
                  current["audio"]["device"].startswith("No audio device"),
                  label + " no-device consumption is explicitly null with no DSP packets")
            check(type(worker_state["latencySamples"]) is int and worker_state["latencySamples"] >= 31 + 16,
                  label + " total declaration includes a supported positive worker quantum")
            check(worker_state["auxiliaryInputBus"] == -1 and worker_state["auxiliaryInputChannels"] == 0,
                  label + " no auxiliary routing is silently enabled")
            return worker_state

        discovery = client.request("tools/list", {})["tools"]
        tools = {tool["name"]: tool for tool in discovery}
        check(all(name in tools for name in ("vibera_state", "vibera_catalogue", "vibera_command", "vibera_commands", "vibera_plugin", "vibera_project")),
              "Required semantic services are actually discovered")
        worker_schema = tools["vibera_state"]["outputSchema"]["properties"]["audio"]["properties"]["pluginWorkers"]["items"]["properties"]
        declared_schema, consumed_schema = worker_schema["latencyDeclared"], worker_schema["latencyConsumed"]
        check(declared_schema["type"] == "object" and declared_schema["additionalProperties"] is False and
              set(declared_schema["required"]) == {"nativeSamples", "generation"} and
              declared_schema["properties"]["nativeSamples"]["type"] == "integer" and
              declared_schema["properties"]["nativeSamples"]["minimum"] == 0 and
              declared_schema["properties"]["generation"]["type"] == "integer" and
              declared_schema["properties"]["generation"]["minimum"] == 1,
              "Declared latency discovery has typed native-sample/generation fields and valid lower bounds")
        check(set(consumed_schema["type"]) == {"object", "null"} and
              consumed_schema["additionalProperties"] is False and
              set(consumed_schema["required"]) == {"nativeSamples", "generation", "sequence"} and
              consumed_schema["properties"]["sequence"]["type"] == "integer" and
              consumed_schema["properties"]["sequence"]["minimum"] == 0 and
              consumed_schema["properties"]["nativeSamples"] == declared_schema["properties"]["nativeSamples"] and
              consumed_schema["properties"]["generation"] == declared_schema["properties"]["generation"],
              "Consumed wet packet discovery is nullable, typed and sequence-scoped")
        retain("latency-output-schema.json", {"declared": declared_schema, "consumed": consumed_schema})
        initial_catalogue = data("vibera_catalogue", {"action": "state"})
        check(initial_catalogue["disposableSettings"] and initial_catalogue["pluginCount"] == 0,
              "Actual test catalogue starts isolated from user plugin settings")
        check(state()["audio"]["pluginWorkers"] == [], "No runtime workers before canonical creation")
        scan_started = time.monotonic()
        admission = data("vibera_catalogue", {"action": "scan", "customFolders": [str(scan_folder)], "standardLocations": False})
        check(admission["completionState"] == "pending", "Real isolated scanner admission is pending")
        terminal = wait_job("vibera_catalogue", admission)
        check(terminal["persisted"] and terminal["failureCount"] == 0 and terminal["blacklistCount"] == 0 and
              terminal["requestedStandardLocations"] is False,
              "Actual one-fixture discovery completes/persists without installed scanning")
        receipt["timings"]["scannerSeconds"] = time.monotonic() - scan_started
        listed = data("vibera_catalogue", {"action": "list", "kind": "effects", "limit": 128})
        matches = [item for item in listed["items"] if item["name"] == "VibeRa Mixer Latency Fixture" and
                   item["format"] == "VST3" and not item["instrument"] and Path(item["path"]).resolve() == staged]
        check(listed["itemCount"] == 1 and len(matches) == 1 and listed["nextOffset"] is None,
              "Exact original class and staged fixture path uniquely identified")
        descriptor = data("vibera_catalogue", {"action": "descriptor", "id": matches[0]["id"]})
        xml = ET.fromstring(descriptor["descriptor"])
        check(descriptor["instrument"] is False and xml.get("name") == "VibeRa Mixer Latency Fixture" and
              Path(xml.get("file")).resolve() == staged, "Actual class ID resolves matching native effect descriptor")
        receipt["catalogueClass"] = matches[0]
        before_ids = {item["id"] for item in plugins()}
        command("plugin.effect.insert", mixer=3, text=descriptor["descriptor"])
        created = [item for item in plugins() if item["id"] not in before_ids]
        check(len(created) == 1 and created[0]["mixer"] == 3 and not created[0]["instrument"],
              "Canonical descriptor inserts exactly one ordered Mixer effect")
        plugin = created[0]["id"]
        first_runtime = runtime(plugin, "inserted")
        before = model()
        for tool, arguments in (
            ("vibera_command", {"command": "plugin.bypass", "args": {"target": 999999999, "enabled": True}}),
            ("vibera_command", {"command": "plugin.bypass", "args": {"target": plugin, "enabled": "yes"}}),
            ("vibera_command", {"command": "plugin.bypass", "args": {"target": -1, "enabled": True}}),
            ("vibera_state", {"latencyConsumed": {"sequence": 0}}),
        ):
            refused = client.call(tool, arguments, allow_failure=True)
            receipt["invalidInputs"].append({"tool": tool, "arguments": arguments, "response": refused})
            retain("invalid-input-responses.json", receipt["invalidInputs"])
            detail = refused.get("structuredContent", {}).get("error", {})
            check(refused.get("isError") is True and isinstance(detail.get("code"), str) and
                  bool(detail["code"]) and isinstance(detail.get("message"), str),
                  "Invalid semantic input returns a structured refusal: " + tool)
            check(model() == before, "Refused target/input leaves canonical plugin model/history unchanged")
        command("plugin.bypass", target=plugin, enabled=True)
        check(next(item for item in plugins() if item["id"] == plugin)["bypassed"] is True, "Canonical bypass model becomes true")
        command("edit.undo")
        check(next(item for item in plugins() if item["id"] == plugin)["bypassed"] is False, "Canonical bypass exact Undo")
        command("edit.redo")
        check(next(item for item in plugins() if item["id"] == plugin)["bypassed"] is True, "Canonical bypass exact Redo")
        bypass_runtime = runtime(plugin, "bypassed")
        check(bypass_runtime["pid"] == first_runtime["pid"] and bypass_runtime["instance"] == first_runtime["instance"],
              "Stopped bypass/Undo/Redo retains actual prepared worker")
        project = attempt / "owned-latency-project.vibra"
        saving = data("vibera_project", {"action": "save", "path": str(project), "overwrite": True})
        check(saving["completionState"] == "pending", "Actual plugin state/project save admission is pending")
        wait_job("vibera_project", saving)
        check(project.is_file(), "Actual saved project exists")
        saved_hash = digest(project)
        saved_xml = ET.parse(project).getroot()
        saved_plugins = saved_xml.findall(".//PLUGIN")
        check(len(saved_plugins) == 1 and saved_plugins[0].get("name") == "VibeRa Mixer Latency Fixture" and
              bool(saved_plugins[0].get("state")) and saved_plugins[0].get("bypass") == "1",
              "Real saved project contains opaque native state and bypass target")
        command("plugin.bypass", target=plugin, enabled=False)
        wait_job("vibera_project", data("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
        check(next(item for item in plugins() if item["id"] == plugin)["bypassed"] is True,
              "Actual project reopen restores canonical bypass target")
        runtime(plugin, "reopened")
        # A genuinely asynchronous atomic-save failure, scoped to our disposable
        #directory. The original saved project and its bytes must survive.
        blocked = attempt / "owned-blocked-parent"
        blocked.write_text("Owned non-directory save-failure fixture; not a project or audio file.\n", encoding="utf-8")
        failed_save = data("vibera_project", {"action": "save", "path": str(blocked / "blocked.vibra"), "overwrite": True})
        check(failed_save["completionState"] == "pending", "Real writer failure is not preemptively reported completed")
        failed = wait_job("vibera_project", failed_save, "failed")
        check(bool(failed.get("error", {}).get("code")) and digest(project) == saved_hash,
              "Actual async save failure remains explicit and preserves prior project bytes")
        runtime(plugin, "after-failed-save")
        receipt["passed"] = True
    except Exception as failure:
        receipt["error"] = str(failure)
        receipt["traceback"] = traceback.format_exc()
    finally:
        if client is not None:
            closing = time.monotonic()
            try:
                client.close()
                receipt["exitCode"] = client.process.returncode
                receipt["gracefulClose"] = client.process.returncode == 0
            except Exception as failure:
                receipt["passed"] = False
                receipt["closeError"] = str(failure)
                receipt["exitCode"] = client.process.poll()
                receipt["gracefulClose"] = False
            receipt["timings"]["closeSeconds"] = time.monotonic() - closing
            (attempt / "application-stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
        if staged is not None and staged.is_file():
            try:
                #Only remove the exact fixture copy created in this attempt;
                #never the input/plugin, an unknown replacement or any directory.
                if digest(staged) != receipt.get("fixtureSha256"):
                    raise RuntimeError("Owned staged fixture identity changed; preserving it")
                staged.unlink()
                receipt["temporaryFixtureRemoved"] = True
            except Exception as failure:
                receipt["passed"] = False
                receipt["cleanupError"] = str(failure)
        receipt["finished"] = True
        receipt["seconds"] = time.monotonic() - began
        retain("verification.json", receipt)
        (output / "verification.json").write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(receipt, ensure_ascii=False))
    return 0 if receipt["passed"] and receipt.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
