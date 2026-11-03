"""Actual original VST3 native-editor lifecycle through semantic MCP.

Usage: verify-plugin-editors-mcp.py ABS_EXE ABS_OWNED_OUTPUT ABS_ParameterFixture.vst3
       [ABS_MixerLatencyFixture.vst3]

Uses the provider-neutral stdio client and disposable --mcp-test settings.
Scans only byte-identical copies of the explicit original SDK fixtures. No
installed-provider scan, simulated input, playback, listening or hardware claim.
Each attempt preserves its own projects, RPC evidence and failures. Root may
update verification.json as a current-result index; attempts are never replaced.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import time
import traceback
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
    if len(sys.argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 2
    app, output, original = [Path(value).resolve() for value in sys.argv[1:4]]
    unsupported = Path(sys.argv[4]).resolve() if len(sys.argv) == 5 else None
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    receipt = {
        "passed": False, "finished": False, "checks": [], "operations": [],
        "invalidInputs": [], "timings": {}, "attemptDirectory": str(attempt),
        "nativeInput": False, "physicalInput": False, "listening": False,
        "hardware": False, "liveAudio": False, "installedProviderQualified": False,
        "scope": "Actual original SDK native-editor worker lifecycle, semantic jobs, history and project state",
        "verificationScriptSha256": digest(Path(__file__)), "fixtures": [],
        "limits": ["Native component visibility is not OS foreground/monitor proof",
                   "No arbitrary native plugin widget control", "No deterministic dispatch-cancel winner asserted"],
    }
    began = time.monotonic()
    client = None
    staged = []

    def retain(name, value):
        (attempt / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def check(value, message):
        if not value:
            raise AssertionError(message)
        receipt["checks"].append(message)

    try:
        for index in range(1, len(sys.argv)):
            check(Path(sys.argv[index]).is_absolute(), "CLI input is explicit absolute path: " + str(index))
        check(app.is_file(), "Application input exists")
        worker = app.parent / "VibeRa Plugin Worker.exe"
        check(worker.is_file(), "Application-relative plugin worker exists")
        receipt.update(application=str(app), applicationSha256=digest(app), worker=str(worker), workerSha256=digest(worker))
        receipt["fixtureConfiguration"] = {key: os.environ.get(key) for key in
                                           ("VIBERA_FIXTURE_INIT_FAIL", "VIBERA_FIXTURE_INIT_DELAY_MS")}
        check(not receipt["fixtureConfiguration"]["VIBERA_FIXTURE_INIT_FAIL"],
              "Default-success native fixture has no initialization-failure injection")
        client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
        check(client_path.is_file(), "Reusable provider-neutral stdio client exists")
        receipt.update(skillClient=str(client_path), skillClientSha256=digest(client_path))
        loader = importlib.util.spec_from_file_location("vibera_editor_neutral_client", client_path)
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
        scan_folder = attempt / "owned-original-scan"
        scan_folder.mkdir()
        inputs = [(original, "VibeRa Parameter Fixture")]
        if unsupported is not None:
            inputs.append((unsupported, "VibeRa Mixer Latency Fixture"))
        for fixture, name in inputs:
            check(fixture.is_file() and fixture.suffix.lower() == ".vst3", "Explicit standalone SDK fixture exists: " + name)
            copied = scan_folder / fixture.name
            check(not copied.exists(), "Owned fixture copy has unique destination")
            fingerprint = digest(fixture)
            shutil.copyfile(fixture, copied)
            staged.append((copied, fingerprint))
            check(digest(copied) == fingerprint, "Owned scan copy is byte-identical: " + name)
            receipt["fixtures"].append({"name": name, "original": str(fixture), "ownedCopy": str(copied), "sha256": fingerprint})
        client = module.VibeRaClient(app)
        receipt.update(pid=client.process.pid, version=client.server.get("version"))

        def call(tool, arguments=None, allow_failure=False):
            started = time.monotonic()
            response = client.call(tool, arguments, allow_failure=True)
            with (attempt / "rpc.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "arguments": arguments or {}, "response": response,
                                         "seconds": time.monotonic() - started}, ensure_ascii=False) + "\n")
            if response.get("isError") and not allow_failure:
                raise AssertionError("Actual MCP refusal: " + json.dumps(response, ensure_ascii=False))
            return response

        def data(tool, arguments=None):
            return call(tool, arguments)["structuredContent"]

        def state():
            return data("vibera_state")

        def plugins():
            result = data("vibera_plugin", {"action": "list", "limit": 512})
            check(result["nextOffset"] is None, "Loaded plugin list is complete")
            return result["items"]

        def command(name, **arguments):
            admission = data("vibera_command", {"command": name, "args": arguments})
            def record(observation):
                receipt["operations"].append({"tool": "vibera_command", "command": name, **observation})
                retain("operations.json", receipt["operations"])
            return await_command_result(
                admission,
                lambda tool, args: call(tool, args, True)["structuredContent"],
                record)

        def model():
            current = state()
            keys = ("revision", "dirty", "undoAvailable", "redoAvailable", "projectIdentity",
                    "tempo", "swing", "numerator", "denominator", "channels", "patterns", "notes", "clips")
            result = {key: current[key] for key in keys if key in current}
            result["plugins"] = [{key: item[key] for key in ("id", "name", "instrument", "bypassed", "channel", "mixer", "slot")}
                                 for item in plugins()]
            return result

        def wait_job(tool, admission, expected="completed", seconds=40):
            started = time.monotonic()
            current = admission
            check(current.get("completionState") in ("pending", "completed", "failed", "cancelled"),
                  "Actual job admission has explicit state: " + tool)
            deadline = started + seconds
            while current["completionState"] == "pending" and time.monotonic() < deadline:
                time.sleep(.02)
                current = call(tool, {"action": "job", "operation": admission["operation"]}, True)["structuredContent"]
            receipt["operations"].append({"tool": tool, "initial": admission, "terminal": current,
                                           "seconds": time.monotonic() - started})
            retain("operations.json", receipt["operations"])
            check(current.get("completionState") == expected,
                  tool + " actual terminal " + expected + ": " + str(current.get("error", "")))
            return current

        def runtime(plugin, label):
            started = time.monotonic()
            while True:
                current = state()
                rows = [item for item in current["audio"]["pluginWorkers"] if item["plugin"] == plugin]
                if not current["pendingAudioUpdate"] and len(rows) == 1 and rows[0]["alive"]:
                    break
                if time.monotonic() - started > 25:
                    retain(label + "-readiness-failure.json", current)
                    raise AssertionError("Actual original runtime readiness deadline: " + label)
                time.sleep(.03)
            check(not current["playing"] and current["audio"]["device"].startswith("No audio device"),
                  label + " intentionally no-device/no-playback environment")
            retain(label + "-runtime.json", current["audio"])
            receipt["timings"][label + "ReadinessSeconds"] = time.monotonic() - started
            return rows[0]

        def editor_state(plugin, instance):
            current = data("vibera_plugin", {"action": "editorState", "plugin": plugin, "instance": instance})["nativeEditor"]
            check(current["instance"] == instance and current["nativeWidgetControl"] is False and
                  type(current["revision"]) is int and type(current["busy"]) is bool,
                  "Actual editor state is instance-scoped and excludes arbitrary widget control")
            return current

        def unchanged(baseline, label):
            actual = model()
            if actual != baseline:
                retain(label + "-model-change.json", {"before": baseline, "after": actual})
            check(actual == baseline, label + " preserves revision/dirty/history and musical model")

        def refusal(tool, arguments, codes, baseline):
            response = call(tool, arguments, True)
            receipt["invalidInputs"].append({"tool": tool, "arguments": arguments, "response": response})
            retain("invalid-input-responses.json", receipt["invalidInputs"])
            detail = response.get("structuredContent", {}).get("error", {})
            check(response.get("isError") is True and detail.get("code") in codes and
                  isinstance(detail.get("message"), str) and bool(detail["message"]),
                  "Structured refusal: " + tool + "/" + str(arguments.get("action")))
            unchanged(baseline, "refused-operation")

        def request_editor(plugin, instance, opening, baseline, via_select=False, expected=None):
            if via_select:
                admission = data("vibera_select", {"plugin": plugin, "instance": instance,
                                                   "pluginView": "native" if opening else "closeNative"})
            else:
                admission = data("vibera_plugin", {"action": "openEditor" if opening else "closeEditor",
                                                   "plugin": plugin, "instance": instance})
            check(admission.get("completionState") == "pending" and bool(admission.get("operation")),
                  "Native request acceptance is pending with an owned job token")
            unchanged(baseline, "editor-admission")
            result = wait_job("vibera_plugin", admission, "failed" if expected == "unsupported" else "completed", 12)
            check(result.get("requestedInstance") == instance and result.get("plugin") == plugin and
                  result.get("documentChanged") is False and result.get("undoable") is False,
                  "Native result retains exact owner and declares no document transaction")
            outcome = result.get("outcome")
            check(outcome == (expected or ("opened" if opening else "closed")), "Actual native outcome: " + str(outcome))
            observed = result.get("windowAtCompletion", {})
            check(result.get("dispatched") is True and result.get("nativeCompletionKnown") is True and
                  result.get("nativeOutcome") == outcome and type(result.get("elapsedMs")) is int and
                  0 <= result["elapsedMs"] <= 5000 and observed.get("known") is True and
                  type(observed.get("revision")) is int,
                  "Actual stamped native completion differs from acceptance/poll timing")
            if expected == "unsupported":
                check(result["error"]["code"] == "unsupported" and observed["exists"] is False and
                      observed["visible"] is False, "Unsupported native editor is an actual asynchronous failure")
            else:
                check(observed["exists"] is opening and observed["visible"] is opening and
                      observed["minimised"] is False, "Actual native window state matches completed action")
            current = editor_state(plugin, instance)
            check(not current["busy"] and current["known"] and current["exists"] is observed["exists"] and
                  current["visible"] is observed["visible"] and current["revision"] >= observed["revision"] and
                  current["availability"] == ("unsupported" if expected == "unsupported" else "available"),
                  "Current worker window observation verifies actual completed result")
            unchanged(baseline, "editor-completion")
            return result

        tools = {tool["name"]: tool for tool in client.request("tools/list", {})["tools"]}
        retain("tools.json", tools)
        check(all(name in tools for name in ("vibera_plugin", "vibera_select", "vibera_catalogue", "vibera_project", "vibera_command")),
              "Required real semantic tools discovered")
        plugin_schema = tools["vibera_plugin"]
        check({"openEditor", "closeEditor", "editorState", "job", "cancel"} <=
              set(plugin_schema["inputSchema"]["properties"]["action"]["enum"]), "Native lifecycle actions discovered")
        properties = plugin_schema["outputSchema"]["properties"]
        native_schema = properties["nativeEditor"]
        required = {"instance", "known", "exists", "visible", "minimised", "revision", "busy", "availability", "nativeWidgetControl", "nativeInterfaceAvailable"}
        check(native_schema["type"] == "object" and native_schema["additionalProperties"] is False and
              set(native_schema["required"]) == required and properties["editorState"] == native_schema and
              properties["items"]["items"]["properties"]["nativeEditor"] == native_schema and
              native_schema["properties"]["nativeWidgetControl"].get("const") is False and
              native_schema["properties"]["revision"]["type"] == "integer" and
              native_schema["properties"]["busy"]["type"] == "boolean" and
              properties["nativeCompletionKnown"]["type"] == "boolean" and
              set(properties["elapsedMs"]["type"]) == {"integer", "null"}, "Typed native-state/job discovery agrees across readbacks")
        catalogue = data("vibera_catalogue", {"action": "state"})
        check(catalogue["disposableSettings"] and catalogue["pluginCount"] == 0,
              "Isolated test catalogue does not modify user plugin settings")
        scan = data("vibera_catalogue", {"action": "scan", "customFolders": [str(scan_folder)], "standardLocations": False})
        check(scan["completionState"] == "pending", "Actual isolated scanner starts pending")
        scanned = wait_job("vibera_catalogue", scan)
        check(scanned["persisted"] and scanned["failureCount"] == 0 and scanned["blacklistCount"] == 0 and
              scanned["requestedStandardLocations"] is False, "Original-only scanner completes without broader locations")
        listing = data("vibera_catalogue", {"action": "list", "kind": "effects", "limit": 128})
        check(listing["itemCount"] == len(inputs) and listing["nextOffset"] is None, "Only explicit original fixture classes discovered")
        descriptors = {}
        for fixture in receipt["fixtures"]:
            matches = [item for item in listing["items"] if item["name"] == fixture["name"] and
                       item["format"] == "VST3" and not item["instrument"] and
                       Path(item["path"]).resolve() == Path(fixture["ownedCopy"])]
            check(len(matches) == 1, "Exact native class/path uniquely identified: " + fixture["name"])
            descriptor = data("vibera_catalogue", {"action": "descriptor", "id": matches[0]["id"]})
            xml = ET.fromstring(descriptor["descriptor"])
            check(xml.get("name") == fixture["name"] and Path(xml.get("file")).resolve() == Path(fixture["ownedCopy"]),
                  "Descriptor belongs to exact original class and owned binary")
            descriptors[fixture["name"]] = descriptor["descriptor"]
        before_ids = {item["id"] for item in plugins()}
        command("plugin.effect.insert", mixer=3, text=descriptors["VibeRa Parameter Fixture"])
        created = [item for item in plugins() if item["id"] not in before_ids]
        check(len(created) == 1 and created[0]["mixer"] == 3 and created[0]["slot"] == 0,
              "Canonical insertion creates exactly one ordered original Mixer effect")
        plugin = created[0]["id"]
        first_runtime = runtime(plugin, "inserted")
        instance = first_runtime["instance"]
        command("plugin.bypass", target=plugin, enabled=True)
        command("edit.undo")  # A known existing Redo entry must survive all window actions.
        initial_project = attempt / "before-native.vibra"
        saved = data("vibera_project", {"action": "save", "path": str(initial_project), "overwrite": False})
        check(saved["completionState"] == "pending", "Actual plugin-state save starts pending")
        wait_job("vibera_project", saved)
        check(initial_project.is_file(), "Original opaque state actually saved")
        baseline = model()
        check(baseline["redoAvailable"] and not baseline["dirty"], "Known Redo marker and saved dirty baseline established")
        tweak_schema = tools["vibera_state"]["outputSchema"]["properties"]["audio"]["properties"]["lastTweakedPlugin"]
        check({"projectIdentity", "plugin", "instance", "parameter", "generation", "serial"} <=
              set(tweak_schema["required"]) and state()["audio"]["lastTweakedPlugin"] is None,
              "Last tweaked has typed owner/parameter discovery and no invented native gesture")
        for suffix in ("controls", "create", "init", "removeInit"):
            refusal("vibera_command", {"command": "tools.lastTweaked." + suffix, "args": {}},
                    {"unavailable_target"}, baseline)
        refusal("vibera_command", {"command": "channel.pluginControls", "args": {}},
                {"unavailable_target"}, baseline)
        request_editor(plugin, instance, True, baseline)
        request_editor(plugin, instance, True, baseline, expected="brought_to_front")
        # Exercise genuine fast no-op replies across many host/worker ticks.
        # The retained timeGetTime failure was a valid completion timestamp4ms
        # before admission; no retry, settling exclusion or deadline tolerance.
        for _ in range(16):
            request_editor(plugin, instance, True, baseline, expected="brought_to_front")
        request_editor(plugin, instance, False, baseline)
        request_editor(plugin, instance, False, baseline)
        for _ in range(16):
            request_editor(plugin, instance, False, baseline)
        request_editor(plugin, instance, True, baseline, via_select=True)
        request_editor(plugin, instance, False, baseline, via_select=True)
        for arguments, codes in (
            ({"action": "openEditor", "plugin": plugin}, {"invalid_arguments"}),
            ({"action": "openEditor", "plugin": plugin, "instance": "foreign-instance"}, {"stale_instance"}),
            ({"action": "closeEditor", "plugin": 999999999, "instance": instance}, {"unavailable_target"}),
            ({"action": "editorState", "plugin": plugin, "instance": "foreign-instance"}, {"stale_instance"}),
            ({"action": "openEditor", "plugin": True, "instance": instance}, {"invalid_arguments"}),
            ({"action": "openEditor", "plugin": plugin, "instance": instance, "unsupportedField": True}, {"invalid_arguments"}),
            ({"action": "job", "operation": "not-an-owned-operation"}, {"unknown_operation"}),
            ({"action": "cancel", "operation": "not-an-owned-operation"}, {"unknown_operation"}),
        ):
            refusal("vibera_plugin", arguments, codes, baseline)
        cancellation = data("vibera_plugin", {"action": "openEditor", "plugin": plugin, "instance": instance})
        check(cancellation["completionState"] == "pending", "Cancellable request first reports pending")
        cancelled = call("vibera_plugin", {"action": "cancel", "operation": cancellation["operation"]}, True)
        retain("cancel-attempt.json", cancelled)
        if cancelled.get("isError"):
            check(cancelled["structuredContent"]["error"]["code"] == "too_late",
                  "Dispatch-winning cancellation explicitly requires continued outcome polling")
            terminal = wait_job("vibera_plugin", cancellation, "completed", 12)
        else:
            current = cancelled["structuredContent"]
            expected = "cancelled" if current["completionState"] == "cancelled" else "completed"
            terminal = wait_job("vibera_plugin", current, expected, 12)
        if terminal["completionState"] == "cancelled":
            check(terminal["outcome"] == "cancelled" and terminal["dispatched"] is False and
                  terminal["nativeCompletionKnown"] is False and terminal["elapsedMs"] is None,
                  "Cancellation winner reports undispatched/unknown native completion truthfully")
        else:
            check(terminal["outcome"] == "opened" and terminal["dispatched"] is True and
                  terminal["nativeCompletionKnown"] is True, "Dispatch winner retains actual native open result")
        unchanged(baseline, "cancel-or-dispatch")
        request_editor(plugin, instance, False, baseline)
        current_runtime = runtime(plugin, "after-lifecycle")
        check(current_runtime["pid"] == first_runtime["pid"] and current_runtime["instance"] == instance,
              "Window lifecycle retains original worker process/instance")
        command("edit.redo")
        check(next(item for item in plugins() if item["id"] == plugin)["bypassed"] is True,
              "Pre-existing Redo survived every editor operation")
        command("edit.undo")
        check(next(item for item in plugins() if item["id"] == plugin)["bypassed"] is False,
              "Exactly the bypass document edit is undone after window operations")

        def opaque_plugin(path):
            matches = [row for row in ET.parse(path).getroot().findall(".//PLUGIN") if row.get("name") == "VibeRa Parameter Fixture"]
            check(len(matches) == 1 and bool(matches[0].get("state")), "Project has genuine recoverable opaque original plugin state")
            return dict(matches[0].attrib)

        original_opaque = opaque_plugin(initial_project)
        after_project = attempt / "after-native.vibra"
        wait_job("vibera_project", data("vibera_project", {"action": "save", "path": str(after_project), "overwrite": False}))
        check(opaque_plugin(after_project) == original_opaque, "Native window actions preserve actual persisted original plugin state")
        wait_job("vibera_project", data("vibera_project", {"action": "open", "path": str(initial_project), "discardChanges": True}))
        restored_runtime = runtime(plugin, "reopened")
        restored_instance = restored_runtime["instance"]
        check(restored_instance != instance, "Actual project reopen has a new worker-scoped identity")
        restored_baseline = model()
        refusal("vibera_plugin", {"action": "openEditor", "plugin": plugin, "instance": instance}, {"stale_instance"}, restored_baseline)
        request_editor(plugin, restored_instance, True, restored_baseline)
        request_editor(plugin, restored_instance, False, restored_baseline)
        roundtrip = attempt / "reopened-native.vibra"
        wait_job("vibera_project", data("vibera_project", {"action": "save", "path": str(roundtrip), "overwrite": False}))
        check(opaque_plugin(roundtrip) == original_opaque, "Reopened worker retains matching recoverable opaque state")
        receipt["projects"] = [{"path": str(path), "sha256": digest(path)} for path in (initial_project, after_project, roundtrip)]
        if unsupported is not None:
            before_ids = {item["id"] for item in plugins()}
            command("plugin.effect.insert", mixer=4, text=descriptors["VibeRa Mixer Latency Fixture"])
            created = [item for item in plugins() if item["id"] not in before_ids]
            check(len(created) == 1, "Canonical insertion adds exact no-editor original fixture")
            missing = created[0]["id"]
            missing_runtime = runtime(missing, "unsupported-editor")
            missing_baseline = model()
            request_editor(missing, missing_runtime["instance"], True, missing_baseline, expected="unsupported")
            receipt["asyncUnsupportedQualified"] = True
        else:
            receipt["asyncUnsupportedQualified"] = False
            receipt["limits"].append("No-editor asynchronous failure not run: optional original MixerLatencyFixture input absent")
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
        # Remove only exact owned binary copies, after their owned worker closed.
        # Preserve an identity mismatch or live process rather than deleting it.
        for copied, fingerprint in staged:
            try:
                if client is not None and client.process.poll() is None:
                    raise RuntimeError("Owned application still live; fixture retained")
                if copied.exists():
                    if digest(copied) != fingerprint:
                        raise RuntimeError("Owned scan copy identity changed; retained")
                    copied.unlink()
            except Exception as failure:
                receipt["passed"] = False
                receipt.setdefault("cleanupErrors", []).append(str(failure))
        receipt["finished"] = True
        receipt["seconds"] = time.monotonic() - began
        retain("verification.json", receipt)
        (output / "verification.json").write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(receipt, ensure_ascii=False))
    return 0 if receipt["passed"] and receipt.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
