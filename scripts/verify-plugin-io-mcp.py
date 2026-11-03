"""Actual original SDK native main-bus Studio stdio acceptance.

Usage: verify-plugin-io-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT ABSOLUTE_BUS_CHOICE_FIXTURE.vst3
Uses the existing provider-neutral client/disposable --mcp-test catalogue. No
audio device, playback PCM, native OS input, installed-provider or listening claim.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import stat
import struct
import sys
import time
import traceback
from datetime import datetime, timezone
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
DEFAULT = {"inputBus": 0, "outputBus": 0, "inputChannels": 2, "monoSource": 0}
CHOSEN = {"inputBus": 2, "outputBus": 1, "inputChannels": 1, "monoSource": 3}


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def ordinary(path: Path, directory: bool) -> None:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise ValueError("Refuse reparse acceptance input/output: " + str(path))
    if not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)):
        raise ValueError("Unexpected acceptance input/output type: " + str(path))


def verification_output_root(app: Path) -> Path:
    """Exact packaged executable selects ONE verified workspace output root.

    Other executables retain the existing source build/windows restriction.
    This never authorizes arbitrary build directories or cross-package outputs.
    """
    packages = {
        ROOT / "build/package-stage/VibeRa Studio.exe": ROOT / "build/distribution-verification/staging",
        ROOT / "dist/VibeRa Studio/VibeRa Studio.exe": ROOT / "build/distribution-verification/current",
    }
    allowed = packages.get(app)
    if allowed is None:
        return ROOT / "build/windows"
    for ancestor in app.parents:
        ordinary(ancestor, True)
    required = ("VibeRa Studio.exe", "VibeRaTests.exe", "VibeRa Plugin Worker.exe")
    for name in (*required, "provenance.json"):
        ordinary(app.parent / name, False)
    provenance = app.parent / "provenance.json"
    if not 2 <= provenance.stat().st_size <= 4 * 1024 * 1024:
        raise ValueError("Package acceptance requires bounded existing provenance")
    manifest = json.loads(provenance.read_text(encoding="utf-8-sig"))
    if (not isinstance(manifest, dict) or type(manifest.get("format")) is not int or
            manifest["format"] != 1 or not isinstance(manifest.get("sourceCommit"), str) or
            not re.fullmatch(r"[0-9a-fA-F]{40}", manifest["sourceCommit"]) or
            not isinstance(manifest.get("sourceHashes"), list) or not manifest["sourceHashes"] or
            not isinstance(manifest.get("files"), list)):
        raise ValueError("Package acceptance requires existing versioned source provenance")
    for name in required:
        rows = [row for row in manifest["files"] if isinstance(row, dict) and row.get("path") == name]
        if (len(rows) != 1 or not isinstance(rows[0].get("sha256"), str) or
                not re.fullmatch(r"[0-9a-fA-F]{64}", rows[0]["sha256"])):
            raise ValueError("Package manifest omits or ambiguously identifies a required binary")
    # Reuse actual full cryptographic package verification, including packaged
    # skill references. Shape/input checks above close empty/omitted-binary cases.
    module = importlib.util.spec_from_file_location("vibera_acceptance_distribution", ROOT / "scripts/distribution.py")
    if module is None or module.loader is None:
        raise ValueError("Existing distribution verifier unavailable")
    distribution = importlib.util.module_from_spec(module)
    module.loader.exec_module(distribution)
    distribution.verify(app.parent)
    return allowed


def owned_output_path(raw: str, app: Path) -> Path:
    output = Path(raw)
    if not output.is_absolute():
        raise ValueError("Explicit absolute owned output required")
    if not app.is_absolute():
        raise ValueError("Explicit absolute application required")
    if any(p in (".", "..") or p.endswith((".", " ")) for p in raw.replace("\\", "/").split("/")):
        raise ValueError("Output refuses traversal and Win32 alias components")
    if os.name == "nt" and (not re.match(r"^[A-Za-z]:[/\\]", raw) or ":" in raw[2:]):
        raise ValueError("Output requires an ordinary local-drive path")
    owned = verification_output_root(app)
    relative = output.relative_to(owned)
    if not relative.parts:
        raise ValueError("Choose a dedicated child below the executable-derived verification root")
    for ancestor in [Path(raw), *Path(raw).parents]:
        if ancestor.exists():
            info = ancestor.lstat()
            if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("Reparse output ancestry is not an owned ordinary directory")
    return output


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    app, output, original = [Path(value) for value in sys.argv[1:]]
    if not Path(sys.argv[2]).is_absolute():
        print("Explicit absolute owned output required", file=sys.stderr)
        return 2
    try:
        output = owned_output_path(sys.argv[2], app)
    except (ValueError, RuntimeError, OSError, KeyError, TypeError) as failure:
        print("Owned ordinary executable-derived verification child required: " + str(failure), file=sys.stderr)
        return 2
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    receipt = {"passed": False, "finished": False, "attemptDirectory": str(attempt),
               "checks": [], "operations": [], "responseCount": 0, "invalidInputs": [], "timings": {},
               "nativeInput": False, "hardware": False, "listening": False, "liveAudio": False,
               "pcmQualified": False, "scope": "Original SDK native bus startup, canonical stdio history and project I/O; no device"}
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
        check(Path(sys.argv[1]).is_absolute() and app.is_file(), "Explicit absolute application exists")
        check(Path(sys.argv[3]).is_absolute() and original.is_file() and original.suffix.lower() == ".vst3",
              "Explicit original standalone SDK bus-choice binary exists")
        worker = app.parent / "VibeRa Plugin Worker.exe"
        check(worker.is_file(), "Application-relative matching worker exists")
        client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
        check(client_path.is_file(), "Existing provider-neutral stdio client exists")
        receipt.update(application=str(app), applicationSha256=digest(app), worker=str(worker),
                       workerSha256=digest(worker), originalFixture=str(original), fixtureSha256=digest(original),
                       skillClient=str(client_path), skillClientSha256=digest(client_path),
                       verificationScriptSha256=digest(Path(__file__)))
        loader = importlib.util.spec_from_file_location("vibera_io_neutral_client", client_path)
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
        scan_folder = attempt / "owned-original-scan"
        scan_folder.mkdir()
        staged = scan_folder / original.name
        shutil.copyfile(original, staged)
        check(digest(staged) == receipt["fixtureSha256"], "Isolated scanner copy exactly matches original fixture bytes")
        client = module.VibeRaClient(app)
        receipt.update(pid=client.process.pid, version=client.server.get("version"))

        def call(tool, arguments=None, allow_failure=False):
            response = client.call(tool, arguments or {}, allow_failure=True)
            entry = {"tool": tool, "args": arguments or {}, "response": response}
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(entry, ensure_ascii=False) + "\n")
            receipt["responseCount"] += 1
            if response.get("isError") and not allow_failure:
                raise AssertionError("Unexpected canonical refusal: " + str(response))
            return response["structuredContent"]

        def state():
            return call("vibera_state")

        def plugins():
            page = call("vibera_plugin", {"action": "list", "limit": 512})
            check(page["nextOffset"] is None, "Complete bounded original plugin readback")
            return page["items"]

        def terminal(tool, admission, expected="completed"):
            started = time.monotonic()
            current = admission
            token = admission.get("operation")
            check(isinstance(token, str) and bool(token) and
                  current.get("completionState") in ("pending", "completed", "cancelled", "failed"),
                  "Owned asynchronous operation identity and explicit phase: " + tool)
            try:
                while current["completionState"] == "pending" and time.monotonic() - started < 40:
                    time.sleep(.02)
                    current = call(tool, {"action": "job", "operation": token}, allow_failure=True)
                    check(current.get("operation") == token, "Observation retains the exact accepted owner")
                check(current.get("completionState") == expected,
                      "Actual operation terminal " + expected + ": " + str(current))
                if expected == "completed":
                    check(current.get("nativeRecoveryRequired", False) is False,
                          "Completed operation needs no native recovery")
                return current
            finally:
                receipt["operations"].append({"tool": tool, "initial": admission, "terminal": current,
                                               "seconds": time.monotonic() - started})
                retain("operations.json", receipt["operations"])

        def command(name, arguments=None):
            admission = call("vibera_command", {"command": name, "args": arguments or {},
                                                "revision": state()["revision"]})
            if admission.get("completionState") == "pending":
                return terminal("vibera_mixerchain", admission)
            check(admission.get("completionState") in (None, "completed"), "Ordinary canonical command completion")
            return admission

        def runtime(plugin, label):
            started = time.monotonic()
            current = None
            while time.monotonic() - started < 40:
                current = state()
                rows = [row for row in current["audio"]["pluginWorkers"] if row["plugin"] == plugin]
                if not current["pendingAudioUpdate"] and len(rows) == 1 and rows[0]["alive"]:
                    native = rows[0]
                    check(not current["playing"] and current["audio"]["device"].startswith("No audio device") and
                          native["submitted"] == 0 and native["processed"] == 0 and native["latencyConsumed"] is None,
                          label + " actual worker without device, packets or false consumed-PCM evidence")
                    receipt["timings"][label + "Seconds"] = time.monotonic() - started
                    retain(label + "-runtime.json", current)
                    return native
                time.sleep(.03)
            retain(label + "-runtime-failure.json", current)
            raise AssertionError("Actual worker readiness timed out: " + label)

        def buses(plugin, selection):
            row = next(row for row in plugins() if row["id"] == plugin)
            result = call("vibera_plugin", {"action": "buses", "plugin": plugin, "instance": row["instance"]})
            info = result["audioIo"]
            check(result["plugin"] == plugin and result["instance"] == row["instance"] and
                  info["available"] is True and info["catalogueKnown"] is True and
                  info["mixerChannels"] == 2 and info["simultaneousMultiOutput"] is False,
                  "Actual native bus catalogue belongs to exact current runtime and stereo graph")
            check(info["requested"] == selection and info["negotiated"] == selection,
                  "Canonical requested and actual negotiated bus selections exactly match")
            for direction, names in (("inputs", ["Input zero", "Input one", "Input two"]),
                                     ("outputs", ["Output zero", "Output one", "Unsupported surround output"])):
                rows = info[direction]
                check(len(rows) == 3 and [entry["index"] for entry in rows] == [0, 1, 2] and
                      [entry["name"] for entry in rows] == names,
                      "Original SDK exact native bus names and order: " + direction)
                selected = selection["inputBus" if direction == "inputs" else "outputBus"]
                channels = selection["inputChannels"] if direction == "inputs" else 2
                check(all(type(entry["actualChannels"]) is int and
                          entry["actualChannels"] == (channels if entry["index"] == selected else 0)
                          for entry in rows), "Only the selected native buses carry negotiated channels")
            return result

        def model(plugin):
            current = state()
            result = {key: current[key] for key in ("projectIdentity", "revision", "dirty", "undoAvailable",
                      "redoAvailable", "playing", "patternMode", "recordArmed", "selectedPattern", "selectedChannel", "tempo")}
            for entity in ("channels", "patterns", "clips", "mixer"):
                page = call("vibera_inspect", {"entity": entity, "limit": 512})
                check(page["nextOffset"] is None, "Complete canonical snapshot " + entity)
                result[entity] = [{key: value for key, value in row.items() if key not in {"meter", "routingTargets"}}
                                  for row in page["items"]]
            result["plugins"] = [{key: row[key] for key in ("id", "name", "instrument", "bypassed", "channel", "mixer", "slot")}
                                 for row in plugins()]
            result["requested"] = buses(plugin, CHOSEN)["audioIo"]["requested"]
            return result

        def io_request(plugin, selection):
            return {"command": "plugin.io", "revision": state()["revision"],
                    "args": {"target": plugin, "pluginIo": dict(selection)}}

        def pending_io(plugin, selection):
            before = state()["revision"]
            admission = call("vibera_command", io_request(plugin, selection))
            check(admission["completionState"] == "pending" and isinstance(admission["operation"], str) and
                  admission["documentChanged"] is False and admission["revision"] == before,
                  "Actual I/O admission is pending before native negotiation and canonical commit")
            return admission

        tools = {tool["name"]: tool for tool in client.request("tools/list", {})["tools"]}
        retain("tools.json", tools)
        check({"vibera_catalogue", "vibera_command", "vibera_commands", "vibera_plugin", "vibera_project",
               "vibera_mixerchain", "vibera_batch", "vibera_state", "vibera_inspect"} <= set(tools),
              "All actual shared workflow services discovered")
        check("buses" in tools["vibera_plugin"]["inputSchema"]["properties"]["action"]["enum"] and
              "audioIo" in tools["vibera_plugin"]["outputSchema"]["properties"],
              "Typed buses action/output is discoverable")
        contract = call("vibera_commands", {"command": "plugin.io", "includeSchemas": True})["operations"][0]
        fields = contract["arguments"]["properties"]["pluginIo"]
        check(contract["supported"] and contract["documentEdit"] and contract["asynchronous"] and
              contract["macroAvailable"] is False and set(contract["arguments"]["required"]) == {"target", "pluginIo"} and
              fields["additionalProperties"] is False and set(fields["required"]) == set(DEFAULT) and
              fields["properties"]["inputChannels"]["enum"] == [1, 2],
              "Canonical typed main I/O is async, one document history action and unavailable to synchronous macros")
        native_schema = tools["vibera_plugin"]["inputSchema"]["properties"]
        check("audioIo" in native_schema["action"]["enum"] and native_schema["pluginIo"] == fields and
              {"requestedIo", "canonicalOperation", "structuralOperation", "historyCommitted"} <=
              set(tools["vibera_plugin"]["outputSchema"]["properties"]),
              "Semantic native audioIo shares the canonical complete typed selection and durable parent readback")
        catalogue = call("vibera_catalogue", {"action": "state"})
        check(catalogue["disposableSettings"] and catalogue["pluginCount"] == 0,
              "Disposable test catalogue never scans user or installed providers")
        scan = call("vibera_catalogue", {"action": "scan", "customFolders": [str(scan_folder)], "standardLocations": False})
        check(scan["completionState"] == "pending", "Actual one-fixture scanner is asynchronously admitted")
        scanned = terminal("vibera_catalogue", scan)
        check(scanned["persisted"] and scanned["failureCount"] == 0 and scanned["blacklistCount"] == 0 and
              scanned["requestedStandardLocations"] is False, "Exact original scanner finishes with no broadened search")
        page = call("vibera_catalogue", {"action": "list", "kind": "effects", "limit": 128})
        matched = [row for row in page["items"] if row["name"] == "VibeRa Native Bus Choice Fixture" and
                   row["format"] == "VST3" and not row["instrument"] and Path(row["path"]).resolve() == staged]
        check(page["itemCount"] == 1 and page["nextOffset"] is None and len(matched) == 1,
              "Only actual original native bus-choice class scanned")
        descriptor = call("vibera_catalogue", {"action": "descriptor", "id": matched[0]["id"]})
        xml = ET.fromstring(descriptor["descriptor"])
        check(xml.get("name") == "VibeRa Native Bus Choice Fixture" and Path(xml.get("file")).resolve() == staged,
              "Real catalogue descriptor targets the exact copied binary")
        check(plugins() == [] and state()["audio"]["pluginWorkers"] == [],
              "Disposable initial Project has no pre-existing native slots or workers")
        before_ids = {row["id"] for row in plugins()}
        command("plugin.effect.insert", {"mixer": 3, "text": descriptor["descriptor"]})
        created = [row for row in plugins() if row["id"] not in before_ids]
        check(len(created) == 1 and created[0]["mixer"] == 3 and not created[0]["instrument"],
              "Canonical effect creation inserts one actual SDK slot in existing stable insert3")
        plugin = created[0]["id"]
        initial = runtime(plugin, "default")
        buses(plugin, DEFAULT)
        baseline_file = attempt / "default-native-io.vibra"
        save = call("vibera_project", {"action": "save", "path": str(baseline_file), "overwrite": False})
        check(save["completionState"] == "pending", "Fresh actual state capture/save is pending")
        terminal("vibera_project", save)
        default_xml = ET.parse(baseline_file).getroot()
        saved_plugin = default_xml.find(".//PLUGIN")
        check(saved_plugin is not None and bool(saved_plugin.get("state")), "Actual opaque native state saved")
        opaque = saved_plugin.get("state")

        revision = state()["revision"]
        applied = terminal("vibera_mixerchain", pending_io(plugin, CHOSEN))
        check(applied["documentChanged"] is True and applied["undoable"] is True and
              applied["audioState"] == "deferredNoDevice" and state()["revision"] == revision + 1,
              "One actual native-ready I/O change commits one canonical history edge; no-device status stays honest")
        changed = runtime(plugin, "chosen")
        check(changed["instance"] != initial["instance"], "Actual negotiated selection owns a replacement worker")
        buses(plugin, CHOSEN)
        for action, expected in (("edit.undo", DEFAULT), ("edit.redo", CHOSEN)):
            previous = runtime(plugin, action + "-before")
            admission = call("vibera_command", {"command": action})
            check(admission.get("action") == "ioHistory" and admission["completionState"] == "pending",
                  "Actual history returns its exact asynchronous I/O owner")
            terminal("vibera_mixerchain", admission)
            current = runtime(plugin, action + "-after")
            check(current["instance"] != previous["instance"] and {row["id"] for row in plugins()} == {plugin},
                  "Undo/Redo reprepare real workers while retaining historical slot identity")
            buses(plugin, expected)

        before = model(plugin)
        current_runtime = runtime(plugin, "before-refusals")
        invalid = [
            {"command": "plugin.io", "args": {"target": plugin}},
            {"command": "plugin.io", "args": {"target": True, "pluginIo": DEFAULT}},
            {"command": "plugin.io", "args": {"target": plugin, "pluginIo": {**DEFAULT, "inputBus": -1}}},
            {"command": "plugin.io", "args": {"target": plugin, "pluginIo": {**DEFAULT, "unexpected": 1}}},
            {"command": "plugin.io", "revision": before["revision"] - 1, "args": {"target": plugin, "pluginIo": DEFAULT}},
        ]
        for args in invalid:
            response = client.call("vibera_command", args, allow_failure=True)
            receipt["invalidInputs"].append({"args": args, "response": response})
            retain("invalid-inputs.json", receipt["invalidInputs"])
            expected = "stale_revision" if "revision" in args else "invalid_arguments"
            check(response.get("isError") is True and
                  response.get("structuredContent", {}).get("error", {}).get("code") == expected,
                  "Invalid wire payload returns exact structured refusal " + expected)
            check(model(plugin) == before, "Invalid payload preserves exact model, selection and real history")
        batch = {"revision": before["revision"], "commands": [
            {"command": "project.tempo", "args": {"value": 137}},
            {"command": "plugin.io", "args": {"target": plugin, "pluginIo": DEFAULT}}]}
        refused = client.call("vibera_batch", batch, allow_failure=True)
        receipt["invalidInputs"].append({"batch": batch, "response": refused})
        retain("invalid-inputs.json", receipt["invalidInputs"])
        check(refused.get("isError") is True and refused["structuredContent"]["error"]["code"] == "pluginIo.unsupportedBatch" and
              model(plugin) == before, "Batch rejects I/O before preceding ordinary tempo edit; whole model/history unchanged")

        cancellation = pending_io(plugin, DEFAULT)
        cancelled = call("vibera_mixerchain", {"action": "cancel", "operation": cancellation["operation"]}, allow_failure=True)
        if cancelled.get("code") in {"effectChain.tooLate", "effectChain.terminal"}:
            # Distinct stdio requests may straddle the actual commit. A refused
            # cancellation must observe the original result, never fabricate a
            # cancellation or retry the mutation until a desirable outcome.
            completed = terminal("vibera_mixerchain", cancellation)
            check(completed["documentChanged"] is True, "Too-late cancellation retains actual committed outcome")
            runtime(plugin, "cancel-too-late")
            buses(plugin, DEFAULT)
            admission = call("vibera_command", {"command": "edit.undo"})
            terminal("vibera_mixerchain", admission)
            runtime(plugin, "cancel-too-late-undo")
            buses(plugin, CHOSEN)
            before = model(plugin)
            current_runtime = runtime(plugin, "after-cancel-restoration")
        else:
            terminal("vibera_mixerchain", cancelled, "cancelled")
            check(model(plugin) == before and runtime(plugin, "after-cancel")["instance"] == current_runtime["instance"],
                  "Accepted cancellation drains actual owned native work without changing model/history/incumbent")
        unsupported = pending_io(plugin, {**CHOSEN, "outputBus": 2})
        failed = terminal("vibera_mixerchain", unsupported, "failed")
        check("pluginIo." in (failed.get("code", "") + " " + failed.get("message", "")) and
              failed["documentChanged"] is False and failed["nativeRecoveryRequired"] is False and
              model(plugin) == before and runtime(plugin, "after-unsupported")["instance"] == current_runtime["instance"],
              "Actual surround-only output2 refuses during worker negotiation and retains exact incumbent/model/history")

        # A no-op history action can leave undoAvailable unchanged. Exercise
        # the real cursor after all refusals rather than accepting that flag as
        # proof that rejected work did not insert a hidden history edge.
        for action, expected in (("edit.undo", DEFAULT), ("edit.redo", CHOSEN)):
            admission = call("vibera_command", {"command": action})
            check(admission.get("action") == "ioHistory", "Refusals retain exact next native I/O history edge")
            terminal("vibera_mixerchain", admission)
            runtime(plugin, "post-refusal-" + action)
            buses(plugin, expected)

        saved = attempt / "selected-native-io.vibra"
        terminal("vibera_project", call("vibera_project", {"action": "save", "path": str(saved), "overwrite": False}))
        saved_xml = ET.parse(saved).getroot()
        saved_slots = saved_xml.findall(".//PLUGIN")
        check(len(saved_slots) == 1 and saved_slots[0].get("state") == opaque and
              {key: int(saved_slots[0].get(key, "-1")) for key in
               ("mainInputBus", "mainOutputBus", "mainInputChannels", "mainInputMonoSource")} ==
              {"mainInputBus": 2, "mainOutputBus": 1, "mainInputChannels": 1, "mainInputMonoSource": 3},
              "Actual project bytes persist all four chosen I/O fields and exact original opaque native state")
        saved_hash = digest(saved)
        old_identity = state()["projectIdentity"]
        terminal("vibera_project", call("vibera_project", {"action": "open", "path": str(saved), "discardChanges": True}))
        reopened = runtime(plugin, "reopened")
        buses(plugin, CHOSEN)
        readback = state()
        check(readback["projectIdentity"] != old_identity and not readback["undoAvailable"] and not readback["redoAvailable"] and
              {row["id"] for row in plugins()} == {plugin} and reopened["instance"] != current_runtime["instance"] and
              digest(saved) == saved_hash,
              "Actual Open restores stable slot/native selection in a new session with empty history and unchanged saved bytes")

        # Exercise the wrapper's exact-runtime service separately from the
        # canonical command above. The fixture genuinely has no native view;
        # Settings must be an acknowledged controls-only peer, not a fake editor.
        wrapper = terminal("vibera_plugin", call("vibera_plugin", {
            "action": "wrapper", "plugin": plugin, "instance": reopened["instance"], "view": "settings"}))
        window = wrapper["editorState"]
        check(wrapper["kind"] == "editor" and wrapper["nativeCompletionKnown"] is True and
              window["known"] is True and window["exists"] is True and window["visible"] is True and
              window["availability"] == "controls_only" and window["nativeInterfaceAvailable"] is False and
              window["wrapperView"] == "settings" and window["wrapperReady"] is True,
              "Editorless original fixture opens an actual acknowledged Settings wrapper without native-widget claims")

        def audio_io(selection, instance=None):
            current = state()
            return call("vibera_plugin", {"action": "audioIo", "plugin": plugin,
                "instance": instance or next(row["instance"] for row in plugins() if row["id"] == plugin),
                "pluginIo": dict(selection), "projectIdentity": current["projectIdentity"],
                "revision": current["revision"]})

        parent_source = state()
        parent = audio_io(DEFAULT, reopened["instance"])
        check(parent["completionState"] == "pending" and parent["kind"] == "audioIo" and
              isinstance(parent["operation"], str) and bool(parent["operation"]),
              "Semantic audioIo returns its exact parent token as pending, never admission-as-completion")
        joined = terminal("vibera_plugin", parent)
        child = joined["structuralOperation"]
        check(joined["kind"] == "audioIo" and joined["plugin"] == plugin and
              joined["requestedInstance"] == reopened["instance"] and
              joined["sourceProjectIdentity"] == parent_source["projectIdentity"] and
              joined["requestedIo"] == DEFAULT and joined["historyCommitted"] is True and
              joined["documentChanged"] is True and joined["undoable"] is True and
              isinstance(joined["canonicalOperation"], str) and bool(joined["canonicalOperation"]) and
              child["operation"] == joined["canonicalOperation"] and child["completionState"] == "completed" and
              child["nativeRecoveryRequired"] is False and child["audioState"] == "deferredNoDevice" and
              state()["revision"] == parent_source["revision"] + 1,
              "Parent survives expected replacement and reports the actual one-edge canonical child completion")
        closures = [row for row in child.get("editorClosures", [])
                    if row["plugin"] == plugin and row["instance"] == reopened["instance"]]
        check(len(closures) == 1 and closures[0]["serial"] > 0 and
              closures[0]["outcome"] == "closed" and closures[0]["nativeCompletionKnown"] is True and
              closures[0]["windowExists"] is False,
              "Structural audioIo retains the exact old Settings wrapper's actual close acknowledgement")
        semantic_runtime = runtime(plugin, "semantic-default")
        check(semantic_runtime["instance"] != reopened["instance"] and
              {row["id"] for row in plugins()} == {plugin},
              "Semantic I/O replaces the original worker while preserving the exact stable effect slot")
        buses(plugin, DEFAULT)
        for action, expected in (("edit.undo", CHOSEN), ("edit.redo", DEFAULT), ("edit.undo", CHOSEN)):
            previous = runtime(plugin, "semantic-" + action + "-before")
            admission = call("vibera_command", {"command": action})
            check(admission.get("action") == "ioHistory" and admission["completionState"] == "pending",
                  "Semantic I/O Undo/Redo uses the exact canonical native history transaction")
            terminal("vibera_mixerchain", admission)
            current = runtime(plugin, "semantic-" + action + "-after")
            check(current["instance"] != previous["instance"] and {row["id"] for row in plugins()} == {plugin},
                  "Semantic history preserves slot identity and genuinely reprepares the selected native layout")
            buses(plugin, expected)
        durable = call("vibera_plugin", {"action": "job", "operation": parent["operation"]})
        check(all(durable[key] == joined[key] for key in
                  ("operation", "kind", "requestedInstance", "sourceProjectIdentity", "requestedIo",
                   "canonicalOperation", "completionState", "historyCommitted", "documentChanged", "undoable")),
              "Historical parent receipt remains truthful and readable after further real worker replacements")

        before = model(plugin)
        current_runtime = runtime(plugin, "semantic-before-refusals")
        noop = terminal("vibera_plugin", audio_io(CHOSEN, current_runtime["instance"]))
        check(noop["requestedInstance"] == current_runtime["instance"] and noop["requestedIo"] == CHOSEN and
              noop["canonicalOperation"] == "" and noop["historyCommitted"] is False and
              noop["documentChanged"] is False and noop["undoable"] is False and
              model(plugin) == before and runtime(plugin, "semantic-noop")["instance"] == current_runtime["instance"],
              "Current-runtime semantic no-op creates neither history nor a replacement worker")
        native_invalid = [
            ({"instance": reopened["instance"], "pluginIo": DEFAULT}, "stale_instance"),
            ({"instance": current_runtime["instance"]}, "invalid_arguments"),
            ({"instance": current_runtime["instance"], "pluginIo": {**DEFAULT, "inputBus": True}}, "invalid_arguments"),
            ({"instance": current_runtime["instance"], "pluginIo": {**DEFAULT, "outputBus": 32}}, "invalid_arguments"),
            ({"instance": current_runtime["instance"], "pluginIo": {**DEFAULT, "unexpected": 1}}, "invalid_arguments"),
            ({"instance": current_runtime["instance"], "pluginIo": {**DEFAULT, "inputBus": 31}}, "pluginIo.invalidArguments"),
            ({"instance": current_runtime["instance"], "pluginIo": DEFAULT,
              "projectIdentity": "another-project"}, "stale_project"),
            ({"instance": current_runtime["instance"], "pluginIo": DEFAULT,
              "revision": before["revision"] - 1}, "stale_revision"),
        ]
        for fields, expected in native_invalid:
            args = {"action": "audioIo", "plugin": plugin, **fields}
            response = client.call("vibera_plugin", args, allow_failure=True)
            receipt["invalidInputs"].append({"tool": "vibera_plugin", "args": args, "response": response})
            retain("invalid-inputs.json", receipt["invalidInputs"])
            check(response.get("isError") is True and
                  response.get("structuredContent", {}).get("error", {}).get("code") == expected,
                  "Semantic audioIo invalid/stale payload has exact structured refusal: " + expected)
            check(model(plugin) == before and
                  runtime(plugin, "semantic-invalid")["instance"] == current_runtime["instance"],
                  "Rejected semantic payload preserves the exact native owner, model and history")

        unsupported = terminal("vibera_plugin", audio_io({**CHOSEN, "outputBus": 2}), "failed")
        check(unsupported["kind"] == "audioIo" and bool(unsupported["canonicalOperation"]) and
              unsupported["structuralOperation"]["completionState"] == "failed" and
              unsupported["structuralOperation"]["nativeRecoveryRequired"] is False and
              "pluginIo." in (unsupported.get("error", {}).get("code", "") + " " + unsupported["message"]) and
              unsupported["historyCommitted"] is False and unsupported["documentChanged"] is False and
              unsupported["undoable"] is False and model(plugin) == before and
              runtime(plugin, "semantic-surround-refused")["instance"] == current_runtime["instance"],
              "Parent observes genuine unsupported-surround child failure without committing or replacing the incumbent")

        cancellation = audio_io(DEFAULT)
        cancel_result = call("vibera_plugin", {"action": "cancel", "operation": cancellation["operation"]},
                             allow_failure=True)
        cancel_code = cancel_result.get("error", {}).get("code", cancel_result.get("code"))
        if (cancel_code in {"effectChain.tooLate", "effectChain.terminal", "too_late"} or
                cancel_result.get("completionState") == "completed"):
            observed = terminal("vibera_plugin", cancellation)
            check(observed["historyCommitted"] is True and observed["documentChanged"] is True,
                  "Too-late semantic cancellation preserves the actual committed parent outcome")
            runtime(plugin, "semantic-cancel-too-late")
            buses(plugin, DEFAULT)
            terminal("vibera_mixerchain", call("vibera_command", {"command": "edit.undo"}))
            runtime(plugin, "semantic-cancel-undo")
            buses(plugin, CHOSEN)
            receipt["semanticCancellationObserved"] = False
        else:
            observed = terminal("vibera_plugin", cancel_result, "cancelled")
            check(observed["operation"] == cancellation["operation"] and
                  observed["structuralOperation"]["completionState"] == "cancelled" and
                  observed["historyCommitted"] is False and observed["documentChanged"] is False and
                  observed["undoable"] is False and model(plugin) == before and
                  runtime(plugin, "semantic-cancelled")["instance"] == current_runtime["instance"],
                  "Accepted parent cancellation drains its exact child before reporting unchanged incumbent/history")
            receipt["semanticCancellationObserved"] = True
        # Prove cursor integrity after semantic no-op/refusals/cancel, including
        # when cancellation lost the real publication race; never retry mutation.
        for action, expected in (("edit.redo", DEFAULT), ("edit.undo", CHOSEN)):
            admission = call("vibera_command", {"command": action})
            check(admission.get("action") == "ioHistory", "Semantic refusals preserve the exact next history edge")
            terminal("vibera_mixerchain", admission)
            runtime(plugin, "semantic-final-" + action)
            buses(plugin, expected)

        # Chain actions are explicitly for the whole containing insert. The
        # BUS1 fixture has immutable opaque state/no writable native parameters;
        # mutate real host wet/bypass targets, never invent a hidden-state edit.
        chain_schema = tools["vibera_plugin"]["inputSchema"]["properties"]
        check("chain" in chain_schema["action"]["enum"] and
              {"copy", "paste", "save", "loadApply", "configureDirectory", "sidechain"} <=
              set(chain_schema["chainAction"]["enum"]),
              "Exact-plugin semantic chain actions are discoverable independently of current Mixer selection")
        presets = attempt / "wrapper-presets"
        presets.mkdir()

        def chain_request(action, **fields):
            current = state()
            owner = next(row for row in plugins() if row["id"] == plugin)
            admission = call("vibera_plugin", {"action": "chain", "chainAction": action, "plugin": plugin,
                "instance": owner["instance"], "projectIdentity": current["projectIdentity"],
                "revision": current["revision"], **fields})
            check(admission["completionState"] == "pending" and admission["kind"] == "chain" and
                  isinstance(admission["operation"], str) and bool(admission["operation"]),
                  "Chain " + action + " admission owns a pending parent token, not completed work")
            return admission

        def chain_done(action, **fields):
            result = terminal("vibera_plugin", chain_request(action, **fields))
            check(result["kind"] == "chain" and result["chainAction"] == action and result["mixer"] == 3 and
                  result["slot"] == 0, "Chain parent retains exact original owning insert and slot: " + action)
            return result

        def read_portable(path):
            ordinary(path, False)
            payload = path.read_bytes()
            check(24 <= len(payload) <= 64 * 1024 * 1024 and payload[:8] == b"VibeRaFX",
                  "Actual portable preset has documented bounded magic and bytes")
            offset = 8

            def u32():
                nonlocal offset
                check(offset + 4 <= len(payload), "Independent portable integer is complete")
                value = struct.unpack_from("<I", payload, offset)[0]
                offset += 4
                return value

            def text(maximum):
                nonlocal offset
                length = u32()
                check(length <= maximum and offset + length <= len(payload), "Independent portable text is bounded")
                value = payload[offset:offset + length].decode("utf-8", errors="strict")
                offset += length
                check("\0" not in value, "Portable text contains no embedded NUL")
                return value

            version, flags = u32(), u32()
            check(version == 2 and flags in (0, 1), "Nondefault selected I/O genuinely requires portable Format2")
            name, count = text(1024), u32()
            check(bool(name) and count == 1, "Original one-slot containing chain saved without invented effects")
            slot_flags, wet_bits = u32(), u32()
            io = dict(zip(DEFAULT, (u32(), u32(), u32(), u32())))
            slot = {"bypass": bool(slot_flags), "wet": struct.unpack("<f", struct.pack("<I", wet_bits))[0],
                    "io": io, "name": text(1024), "descriptor": text(65536), "state": text(24 * 1024 * 1024)}
            check(slot_flags in (0, 1) and offset == len(payload), "Independent portable reader consumes exact flags and complete file")
            return {"effectsBypassed": bool(flags), "slot": slot}

        before = model(plugin)
        configured = chain_done("configureDirectory", directory=str(presets))
        check(configured["canonicalOperation"] == "" and configured["historyCommitted"] is False and
              configured["documentChanged"] is False and configured["undoable"] is False and model(plugin) == before and
              call("vibera_mixerchain", {"action": "state"})["state"]["directory"] == str(presets),
              "Exact owned preset directory is actually configured without child, model or history mutation")
        call("vibera_mixer", {"action": "select", "insert": 1})
        check(state()["selectedInsert"] == 1, "Deliberately select a different empty insert before wrapper chain actions")
        before = model(plugin)
        chain_owner = runtime(plugin, "chain-source")
        terminal("vibera_plugin", call("vibera_plugin", {"action": "wrapper", "plugin": plugin,
                 "instance": chain_owner["instance"], "view": "settings"}))
        copied = chain_done("copy")
        child = copied["structuralOperation"]
        closes = [row for row in copied["editorClosures"] if row["plugin"] == plugin and
                  row["instance"] == chain_owner["instance"]]
        check(len(closes) == 1 and closes[0]["serial"] > 0 and closes[0]["outcome"] == "closed" and
              closes[0]["nativeCompletionKnown"] is True and closes[0]["windowExists"] is False,
              "Copy retains actual exact Settings-close acknowledgement before native state capture")
        provenance = child["provenance"]
        check(len(provenance) == 1 and provenance[0]["plugin"] == plugin and
              provenance[0]["instance"] == chain_owner["instance"] and
              provenance[0]["stateSource"] == "nativeAcknowledgement" and
              child["state"]["clipboardAvailable"] is True and
              len(child["state"]["clipboard"]["slots"]) == 1 and
              copied["historyCommitted"] is False and copied["documentChanged"] is False and
              copied["undoable"] is False and model(plugin) == before and state()["selectedInsert"] == 1,
              "Copy captures real target-only native state on insert3 while preserving independent selection/model/history")
        preset = presets / "original-bus-choice.vibfx"
        saved_chain = chain_done("save", path=str(preset), overwrite=False)
        check(saved_chain["structuralOperation"]["fileCommitted"] is True and preset.is_file() and
              saved_chain["historyCommitted"] is False and saved_chain["documentChanged"] is False and
              saved_chain["undoable"] is False and model(plugin) == before,
              "Save parent reports real atomic file commitment without document/history change")
        portable = read_portable(preset)
        check(portable["effectsBypassed"] is False and portable["slot"]["bypass"] is False and
              portable["slot"]["wet"] == 1.0 and portable["slot"]["io"] == CHOSEN and
              portable["slot"]["state"] == opaque and portable["slot"]["descriptor"] == descriptor["descriptor"],
              "Independent Format2 decode preserves actual opaque bytes, descriptor, wet/bypass and all chosen bus fields")
        preset_hash = digest(preset)
        overwrite = terminal("vibera_plugin", chain_request("save", path=str(preset), overwrite=False), "failed")
        check(overwrite["structuralOperation"]["code"] == "chain.file.outputExists" and
              overwrite["structuralOperation"]["fileCommitted"] is False and digest(preset) == preset_hash and
              overwrite["documentChanged"] is False and overwrite["historyCommitted"] is False and model(plugin) == before,
              "Explicit no-overwrite failure preserves the original real preset and exact model/history")

        command("plugin.effect.mix", {"target": plugin, "value": .25})
        command("mixer.effects.bypass", {"mixer": 3, "enabled": True})
        modified = model(plugin)
        check(next(row for row in plugins() if row["id"] == plugin)["wet"] == .25 and
              next(row for row in plugins() if row["id"] == plugin)["chainBypassed"] is True,
              "Actual canonical wet and chain-bypass mutations differ from copied state")
        previous_id = plugin
        pasted = chain_done("paste")
        replacement = plugins()
        check(len(replacement) == 1 and replacement[0]["id"] != previous_id and replacement[0]["mixer"] == 3 and
              replacement[0]["slot"] == 0 and replacement[0]["wet"] == 1 and replacement[0]["chainBypassed"] is False and
              pasted["requestedInstance"] != replacement[0]["instance"] and pasted["historyCommitted"] is True and
              pasted["documentChanged"] is True and pasted["undoable"] is True and state()["selectedInsert"] == 1,
              "Paste replaces the actual containing chain with fresh IDs on insert3, independent of selected insert1")
        plugin = replacement[0]["id"]
        runtime(plugin, "chain-pasted")
        buses(plugin, CHOSEN)
        undo = call("vibera_command", {"command": "edit.undo"})
        check(undo.get("action") == "history", "Chain Paste Undo returns its actual owned chain-history job")
        terminal("vibera_mixerchain", undo)
        plugin = previous_id
        runtime(plugin, "chain-paste-undo")
        # Revisions/Redo availability legitimately change; compare actual source
        # slots and all authored musical entities, not a fitted new identity.
        restored = model(plugin)
        check(all(restored[key] == modified[key] for key in
                  ("channels", "patterns", "clips", "mixer", "plugins", "requested")) and
              next(row for row in plugins() if row["id"] == plugin)["wet"] == .25 and
              next(row for row in plugins() if row["id"] == plugin)["chainBypassed"] is True,
              "Paste Undo restores exact previous IDs, host controls, chosen I/O and all authored music")
        loaded = chain_done("loadApply", path=str(preset))
        replacement = plugins()
        check(len(replacement) == 1 and replacement[0]["id"] != previous_id and replacement[0]["mixer"] == 3 and
              replacement[0]["wet"] == 1 and replacement[0]["chainBypassed"] is False and
              loaded["historyCommitted"] is True and loaded["documentChanged"] is True and loaded["undoable"] is True and
              digest(preset) == preset_hash and state()["selectedInsert"] == 1,
              "LoadApply genuinely decodes the unchanged file and restores its chain on the captured owner")
        plugin = replacement[0]["id"]
        live_chain = runtime(plugin, "chain-loaded")
        buses(plugin, CHOSEN)
        loaded_project = attempt / "wrapper-loaded-native-state.vibra"
        terminal("vibera_project", call("vibera_project", {"action": "save", "path": str(loaded_project), "overwrite": False}))
        loaded_slots = ET.parse(loaded_project).getroot().findall(".//PLUGIN")
        check(len(loaded_slots) == 1 and loaded_slots[0].get("state") == portable["slot"]["state"] and
              int(loaded_slots[0].get("id", "0")) == plugin,
              "Fresh acknowledgement/save after LoadApply retains exact original native opaque state on its new slot")
        before = model(plugin)
        clipboard_before = call("vibera_mixerchain", {"action": "state"})["state"]["clipboard"]
        for fields, expected in [({"instance": chain_owner["instance"]}, "stale_instance"),
                                 ({"chainAction": "unknown"}, "invalid_arguments"),
                                 ({"revision": before["revision"] - 1}, "stale_revision")]:
            args = {"action": "chain", "chainAction": "copy", "plugin": plugin,
                    "instance": live_chain["instance"], **fields}
            response = client.call("vibera_plugin", args, allow_failure=True)
            receipt["invalidInputs"].append({"tool": "vibera_plugin", "args": args, "response": response})
            retain("invalid-inputs.json", receipt["invalidInputs"])
            check(response.get("isError") is True and response["structuredContent"]["error"]["code"] == expected and
                  model(plugin) == before and digest(preset) == preset_hash and
                  call("vibera_mixerchain", {"action": "state"})["state"]["clipboard"] == clipboard_before,
                  "Chain stale/invalid request refuses without model/history/clipboard-file mutation: " + expected)
        cancellation = chain_request("loadApply", path=str(preset))
        cancelled = call("vibera_plugin", {"action": "cancel", "operation": cancellation["operation"]}, allow_failure=True)
        code = cancelled.get("error", {}).get("code", cancelled.get("code"))
        if code in {"effectChain.tooLate", "effectChain.terminal", "too_late"} or cancelled.get("completionState") == "completed":
            observed = terminal("vibera_plugin", cancellation)
            check(observed["historyCommitted"] is True and observed["documentChanged"] is True,
                  "Too-late chain cancellation preserves actual committed replacement rather than retrying")
            receipt["chainCancellationObserved"] = False
        else:
            observed = terminal("vibera_plugin", cancelled, "cancelled")
            check((observed["structuralOperation"]["completionState"] == "cancelled"
                   if observed["canonicalOperation"] else observed["structuralOperation"] == {}) and
                  observed["historyCommitted"] is False and observed["documentChanged"] is False and
                  observed["undoable"] is False and model(plugin) == before and
                  runtime(plugin, "chain-cancelled")["instance"] == live_chain["instance"] and digest(preset) == preset_hash and
                  call("vibera_mixerchain", {"action": "state"})["state"]["clipboard"] == clipboard_before,
                  "Accepted chain cancellation drains exact child and preserves source owner/model/history/file")
            receipt["chainCancellationObserved"] = True
        # The no-device workspace must refuse real detector layout publication
        # truthfully; successful active-source coverage lives in the existing
        # original SDK studio_channel_routing_provider application scenario.
        detector_targets = plugins()
        check(len(detector_targets) == 1, "Rediscover exact single effect after the real cancellation/publication race")
        plugin = detector_targets[0]["id"]
        detector_before = model(plugin)
        detector_owner = runtime(plugin, "wrapper-detector-before")
        detector_failure = terminal("vibera_plugin", chain_request("sidechain", detector={
            "sourceMixer": 2, "inputBus": 1, "channels": 2}), "failed")
        check(detector_failure["error"]["code"] == "sidechain.deviceRequired" and
              detector_failure["historyCommitted"] is False and
              detector_failure["documentChanged"] is False and
              model(plugin) == detector_before and
              runtime(plugin, "wrapper-detector-refused")["instance"] == detector_owner["instance"],
              "Actual no-device detector child refuses its active-source prerequisite without model/history/runtime replacement")
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
                if digest(staged) != receipt.get("fixtureSha256"):
                    raise RuntimeError("Owned fixture bytes changed; preserving replacement")
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
