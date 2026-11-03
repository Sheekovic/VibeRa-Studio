"""Actual ordered Mixer effects/navigation/history/project MCP regression.

Usage: verify-mixer-effects-mcp.py ABS_EXECUTABLE ABS_OWNED_OUTPUT

Uses the existing provider-neutral stdio client and disposable --mcp-test mode.
Creates a valid16-slot deliberately MISSING-plugin project from the application's
own saved current-schema project. No plugin binaries, scans, pointer input,
native plugin compatibility, playback, listening or hardware qualification.

Output policy: new/empty ordinary directory, or this script's marked output
directory. Every run gets an exclusive attempt; no failed attempt is overwritten.
Only its marked root verification.json index may be replaced. Every project save
uses overwrite=false and an unused path inside the new owned attempt.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import time
import traceback
import uuid
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OWNER = {"schemaVersion": 1, "owner": "VibeRa verify-mixer-effects-mcp"}
OWNER_FILE = ".vibera-mixer-effects-mcp-owner.json"
EFFECT_COUNT = 16  # Deliberate complete current-slot fixture, not a new model limit.
PLUGIN_FIELDS = ("id", "name", "instrument", "bypassed", "channel", "mixer", "slot", "wet", "chainBypassed", "effectiveBypass")


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def ordinary_ancestors(path: Path) -> None:
    for ancestor in (path, *path.parents):
        try:
            status = ancestor.lstat()
        except FileNotFoundError:
            continue
        if ancestor.is_symlink() or getattr(status, "st_file_attributes", 0) & 0x400:
            raise ValueError("Owned output refuses symbolic links/reparse ancestors: " + str(ancestor))
        if not ancestor.is_dir():
            raise ValueError("Owned output requires ordinary directory ancestors: " + str(ancestor))


def prepare_output(raw: str) -> tuple[Path, Path]:
    path = Path(raw)
    if not path.is_absolute():
        raise ValueError("Owned output must be an explicit absolute directory")
    if any(part in (".", "..") for part in raw.replace("\\", "/").split("/")):
        raise ValueError("Owned output refuses traversal components")
    if os.name == "nt" and (not re.match(r"^[A-Za-z]:[/\\]", raw) or ":" in raw[2:]):
        raise ValueError("Owned output must be an ordinary absolute local-drive directory")
    ordinary_ancestors(path)
    path.mkdir(parents=True, exist_ok=True)
    ordinary_ancestors(path)
    marker = path / OWNER_FILE
    if marker.exists():
        if marker.is_symlink() or getattr(marker.lstat(), "st_file_attributes", 0) & 0x400:
            raise ValueError("Owned output marker cannot be a reparse file")
        if json.loads(marker.read_text(encoding="utf-8")) != OWNER:
            raise ValueError("Output ownership marker does not match this verifier")
    else:
        if any(path.iterdir()):
            raise ValueError("Refusing to overwrite an unmarked nonempty output directory")
        with marker.open("x", encoding="utf-8") as stream:
            json.dump(OWNER, stream)
    # The only replaceable index is a regular file beneath the verified marker.
    index = path / "verification.json"
    try:
        index_status = index.lstat()
    except FileNotFoundError:
        index_status = None
    if index_status is not None and (not index.is_file() or index.is_symlink() or
                                     getattr(index_status, "st_file_attributes", 0) & 0x400):
        raise ValueError("Owned result index must be an ordinary file")
    attempt = path / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ-") + uuid.uuid4().hex)
    attempt.mkdir(exist_ok=False)
    ordinary_ancestors(attempt)
    return path, attempt


def missing_fixture(base: Path, missing: Path, target: int, version: int) -> tuple[ET.ElementTree, list[int]]:
    """Modify only a parsed copy of the actual server's saved disposable base.

    No native-plugin behavior is represented by the original descriptor/payload.
    '1...' and '1.A.' encode unequal original bytes0/1 as placeholder data.
    """
    if missing.exists():
        raise ValueError("Missing-plugin fixture path unexpectedly exists; do not load an actual provider")
    tree = ET.parse(base)
    document = tree.getroot()
    if document.tag != "VIBERA" or int(document.attrib["version"]) != version:
        raise ValueError("Actual saved base does not match the authoritative current project schema")
    candidates = [entry for entry in document.findall("./MIXER/INSERT") if int(entry.attrib["index"]) == target]
    if len(candidates) != 1 or document.findall(".//PLUGIN"):
        raise ValueError("Fixture requires one actual empty target insert and no pre-existing plugins")
    # Only canonical numeric allocator entities reserve IDs. Sampler modulation
    # TARGET.id is a destination name; Mixer GROUP.id is an independent UUID.
    numeric_id_paths = ("./CHANNELS/CHANNEL", "./CHANNELS/CHANNEL/PLUGIN",
                        "./PATTERNS/PATTERN", "./PATTERNS/PATTERN/NOTE",
                        "./PLACEMENTS/PLACEMENT", "./AUDIO_PLACEMENTS/AUDIO_PLACEMENT",
                        "./MIXER/INSERT/PLUGIN", "./CLIP_GROUPS/GROUP", "./CHANNEL_GROUPS/GROUP",
                        "./AUTOMATION/SOURCES/SOURCE", "./AUTOMATION/SOURCES/SOURCE/POINTS/POINT",
                        "./AUTOMATION/PLACEMENTS/PLACEMENT")
    used = {int(node.attrib["id"]) for path in numeric_id_paths for node in document.findall(path)}
    first = int(document.attrib["nextId"])
    if first <= max(used, default=0):
        raise ValueError("Actual project's nextId does not exceed its existing stable IDs")
    ids = list(range(first, first + EFFECT_COUNT))
    if set(ids) & used:
        raise ValueError("Original fixture IDs must be fresh within this project")
    document.set("nextId", str(first + EFFECT_COUNT))
    document.set("name", "Original16 missing effects - semantic navigation fixture")
    for slot, identity in enumerate(ids):
        name = "Original missing effect " + str(slot + 1)
        descriptor = ET.Element("PLUGIN", {"name": name, "format": "VST3", "category": "Fx",
            "manufacturer": "Original VibeRa missing fixture", "version": "1", "file": str(missing),
            "uniqueId": format(identity, "x"), "isInstrument": "0", "fileTime": "0",
            "infoUpdateTime": "0", "numInputs": "2", "numOutputs": "2", "isShell": "0",
            "hasARAExtension": "0", "uid": "0"})
        plugin = ET.SubElement(candidates[0], "PLUGIN", {"id": str(identity), "name": name,
            "descriptor": ET.tostring(descriptor, encoding="unicode"), "state": "1.A." if slot % 2 else "1...",
            "instrument": "0", "bypass": "0"})
        ET.SubElement(plugin, "AUTOMATION_BINDINGS")
    return tree, ids


def xml_value(node: ET.Element):
    """Order-independent attributes, order-sensitive musical children; no value normalization."""
    return node.tag, sorted(node.attrib.items()), (node.text or "").strip(), [xml_value(child) for child in node]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        if not Path(sys.argv[1]).is_absolute():
            raise ValueError("Application must be an explicit absolute file")
        app = Path(sys.argv[1]).resolve(strict=True)
        if not app.is_file():
            raise ValueError("Application is not a file")
        output, attempt = prepare_output(sys.argv[2])
    except Exception as failure:
        print(json.dumps({"passed": False, "finished": True, "preflightError": str(failure)}), file=sys.stderr)
        return 2
    began, client = time.monotonic(), None
    report = {"passed": False, "finished": False, "attemptDirectory": str(attempt),
        "checks": [], "operations": [], "invalidInputs": [], "timings": {}, "projects": [],
        "application": str(app), "applicationSha256": digest(app),
        "verificationScriptSha256": digest(Path(__file__)), "nativeInput": False,
        "physicalInput": False, "liveAudio": False, "listening": False, "hardware": False,
        "pluginCompatibilityQualified": False, "fixtureKind": "Original deliberately missing VST3 descriptors; no plugin binaries",
        "limits": ["No arbitrary native plugin UI, provider editor success, PCM or desktop input qualification",
                   "Logical component navigation visibility is not OS foreground/monitor visibility",
                   "Undo availability and exact canonical Undo/Redo are tested; internal history storage is not exposed"]}

    def retain(name, value):
        (attempt / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def check(value, message):
        if not value:
            raise AssertionError(message)
        report["checks"].append(message)

    retain("verification.json", report)
    try:
        from dev_support import project_schema
        version = project_schema(ROOT)
        report.update(authoritativeProjectVersion=version, projectHeaderSha256=digest(ROOT / "src/Project.h"))
        worker = app.parent / "VibeRa Plugin Worker.exe"
        check(worker.is_file(), "Application-relative worker input exists; protocol/provider qualification is separate")
        report.update(worker=str(worker), workerSha256=digest(worker))
        client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
        check(client_path.is_file(), "Existing provider-neutral stdio client is available")
        report.update(skillClient=str(client_path), skillClientSha256=digest(client_path))
        loader = importlib.util.spec_from_file_location("vibera_effects_neutral_client", client_path)
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
        client = module.VibeRaClient(app)
        report.update(pid=client.process.pid, version=client.server.get("version"))

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

        def rows(entity, **arguments):
            result = data("vibera_inspect", {"entity": entity, "limit": 512, **arguments})
            check(result["nextOffset"] is None, "Complete actual canonical readback: " + entity)
            return result["items"]

        def plugins():
            result = data("vibera_plugin", {"action": "list", "limit": 512})
            check(result["nextOffset"] is None, "Complete actual stable plugin list")
            return result["items"]

        def command(name, **arguments):
            if name in {"edit.undo", "edit.redo", "plugin.effect.move"}:
                # The preceding canonical move does not promise that its
                # asynchronous native projection is already settled. Observe
                # that existing owner before submitting history once; do not
                # retry a refused mutation until it happens to pass.
                started = time.monotonic()
                current = state()
                while current["pendingAudioUpdate"] and time.monotonic() - started < 40:
                    time.sleep(.03)
                    current = state()
                check(not current["pendingAudioUpdate"],
                      "Previous canonical projection settled before effect-order/history admission")
            admission = data("vibera_command", {"command": name, "args": arguments})
            if admission.get("completionState") == "pending":
                check(name in {"edit.undo", "edit.redo"} and admission.get("action") == "history" and
                      admission.get("operation"), "Order history returns its exact pending chain operation")
                started = time.monotonic()
                current = admission
                while current["completionState"] == "pending" and time.monotonic() - started < 40:
                    time.sleep(.03)
                    current = call("vibera_mixerchain", {"action": "job", "operation": admission["operation"]},
                                   True)["structuredContent"]
                report["operations"].append({"label": "effectOrderHistory", "admission": admission,
                                             "terminal": current, "seconds": time.monotonic() - started})
                retain("operations.json", report["operations"])
                check(current["completionState"] == "completed" and current["documentChanged"] and
                      current["undoable"] and not current["nativeRecoveryRequired"],
                      "Order history completed canonical publication and actual/deferred audio recovery")
                return current
            return admission

        def model():
            # Canonical fields only. Exclude meter ages, worker readiness/status,
            # geometry and pending graph publication; none are project history.
            current = state()
            view = {key: current[key] for key in ("projectIdentity", "revision", "dirty", "undoAvailable",
                "redoAvailable", "tempo", "swing", "numerator", "denominator", "playing", "patternMode",
                "recordArmed", "recordingTake", "selectedPattern", "selectedChannel")}
            view["channels"] = rows("channels")
            view["patterns"] = rows("patterns")
            view["notes"] = {str(pattern["id"]): rows("notes", pattern=pattern["id"]) for pattern in view["patterns"]}
            view["clips"] = rows("clips")
            view["mixer"] = [{key: value for key, value in row.items() if key != "meter"} for row in rows("mixer")]
            view["plugins"] = [{key: row[key] for key in PLUGIN_FIELDS} for row in plugins()]
            return view

        def content(snapshot):
            return {key: value for key, value in snapshot.items() if key not in
                    ("projectIdentity", "revision", "dirty", "undoAvailable", "redoAvailable", "selectedPattern", "selectedChannel")}

        def navigation():
            current = state()
            return {"pane": current["mixerEffectsPane"], "browser": current["mixerEffectsBrowser"]}

        def fail(tool, arguments, code, description):
            response = call(tool, arguments, True)
            result = response.get("structuredContent", {})
            report["invalidInputs"].append({"tool": tool, "arguments": arguments, "response": response})
            check(response.get("isError") is True and result.get("completionState") == "failed" and
                  result.get("error", {}).get("code") == code and bool(result["error"].get("message")), description)
            return result

        def wait_job(admission, label):
            check(admission.get("completionState") in ("pending", "completed", "failed", "cancelled"),
                  label + " has explicit actual admission state")
            started = time.monotonic()
            current = admission
            deadline = started + 40  # Existing neutral-client project-job budget; unchanged.
            while current["completionState"] == "pending" and time.monotonic() < deadline:
                time.sleep(.03)
                current = call("vibera_project", {"action": "job", "operation": admission["operation"]}, True)["structuredContent"]
            report["operations"].append({"label": label, "admission": admission, "terminal": current,
                                         "seconds": time.monotonic() - started})
            retain("operations.json", report["operations"])
            check(current["completionState"] == "completed", label + " reached actual completed state: " + str(current.get("error", "")))
            return current

        def save(path, label):
            check(path.parent == attempt and not path.exists(), label + " has an unused owned destination")
            wait_job(data("vibera_project", {"action": "save", "path": str(path), "overwrite": False}), label)
            check(path.is_file() and path.stat().st_size > 0, label + " produced a real project file")
            report["projects"].append({"label": label, "path": str(path), "sha256": digest(path)})

        tools = client.request("tools/list", {})["tools"]
        retain("tools.json", tools)
        report.update(toolCount=len(tools), toolDiscoverySha256=hashlib.sha256(json.dumps(tools, sort_keys=True).encode()).hexdigest())
        mixer_tool = next(tool for tool in tools if tool["name"] == "vibera_mixer")
        fields = mixer_tool["inputSchema"]["properties"]
        check({"state", "select", "resetOverload", "browseEffects", "scrollEffects"} <= set(fields["action"]["enum"]),
              "Existing Mixer actions preserved and new navigation discoverable")
        check(fields["insert"]["type"] == "integer" and fields["insert"]["minimum"] == 0 and
              fields["offset"]["type"] == "integer" and fields["offset"]["minimum"] == 0 and
              fields["offset"]["maximum"] == 2147483647, "Typed existing-index/slot-offset input ranges")
        outputs = mixer_tool["outputSchema"]["properties"]
        browser_fields = outputs["effectsBrowser"]["properties"]
        pane_fields = outputs["effectsPane"]["properties"]
        check(browser_fields["insertsPlugin"]["const"] is False and browser_fields["open"]["type"] == "boolean" and
              set(outputs["effectsBrowser"]["required"]) == {"open", "targetCurrent", "insert", "projectIdentity", "insertsPlugin"},
              "Typed browser readiness/target and navigation-only result")
        check(set(outputs["effectsPane"]["required"]) == {"visible", "insert", "offset", "maximumOffset", "visibleRows", "effectCount", "rowUnits"}
              and pane_fields["offset"]["type"] == "integer" and pane_fields["maximumOffset"]["minimum"] == 0,
              "Typed actual pane geometry/cardinality readback")
        for identity, action, field in (("mixer.effects.browse", "browseEffects", "insert"),
                                        ("mixer.effects.scroll", "scrollEffects", "offset")):
            entry = data("vibera_commands", {"command": identity, "includeSchemas": True})["operations"]
            check(len(entry) == 1 and entry[0]["supported"] and not entry[0]["documentEdit"] and
                  entry[0]["invocation"] == {"tool": "vibera_mixer", "action": action} and
                  entry[0]["arguments"]["required"] == [field] and
                  set(entry[0]["arguments"]["properties"]) == {field} and not entry[0]["macroAvailable"] and
                  bool(entry[0]["stateReadback"]) and bool(entry[0]["prerequisitesDetail"]),
                  "Stable canonical discovery, typed prerequisites/readback: " + identity)
        move_contract = data("vibera_commands", {"command": "plugin.effect.move", "includeSchemas": True})["operations"][0]
        move_args = move_contract["arguments"]
        check(move_contract["supported"] and move_contract["documentEdit"] and
              move_args["required"] == ["target"] and set(move_args["properties"]) == {"target", "lane"} and
              move_args["properties"]["lane"]["default"] == 0 and move_args["properties"]["lane"]["maximum"] == 15,
              "Exact effect-only final-slot schema preserves omitted destination0")
        check(outputs["effectsPane"]["properties"]["reorder"]["type"] == ["object", "null"],
              "Pending UI reorder intent is explicitly nullable and never a false completed move")
        initial = state()
        check(initial["projectVersion"] == version and not initial["playing"], "Actual current-schema disposable stopped app")
        check(initial["audio"]["device"].startswith("No audio device"), "Intentional no-device/no-playback environment")
        check(data("vibera_catalogue", {"action": "state"})["disposableSettings"], "Catalogue/settings are disposable")
        base = attempt / "actual-empty-base.vibra"
        save(base, "initial real base save")
        inserts = rows("mixer")
        nonmaster = [row["index"] for row in inserts if row["index"] != 0]
        check(len(nonmaster) >= 2 and not plugins(), "Actual empty distinct Mixer targets")
        source, alternate = nonmaster[:2]
        fixture = attempt / "original-missing16.vibra"
        missing = attempt / "Deliberately absent original fixture.vst3"
        tree, ids = missing_fixture(base, missing, source, version)
        with fixture.open("xb") as stream:
            tree.write(stream, encoding="utf-8", xml_declaration=True)
        report["fixture"] = {"project": str(fixture), "sha256": digest(fixture), "pluginIds": ids,
            "missingBinaryPath": str(missing), "nativeBinaryExists": False, "stateEncodings": ["1...", "1.A."],
            "stateMeaning": "Original unequal one-byte payloads (values0/1); not real provider state/compatibility claims"}
        wait_job(data("vibera_project", {"action": "open", "path": str(fixture), "discardChanges": True}), "load original missing fixture")
        data("vibera_mixer", {"action": "select", "insert": source})
        ordered = plugins()
        check([row["id"] for row in ordered] == ids and [row["slot"] for row in ordered] == list(range(EFFECT_COUNT)) and
              all(row["mixer"] == source and not row["instrument"] and not row["available"] for row in ordered),
              "Actual ordered16 missing placeholders expose stable loaded project IDs, never fake available providers")
        unbypassed = content(model())
        target = ids[3]
        command("plugin.bypass", target=target, enabled=True)
        check([row["id"] for row in plugins() if row["bypassed"]] == [target], "Canonical bypass targets discovered fourth ID only")
        baseline = model()
        nav = data("vibera_mixer", {"action": "state"})
        maximum = nav["effectsPane"]["maximumOffset"]
        check(nav["effectsPane"]["effectCount"] == EFFECT_COUNT and maximum > 0 and
              nav["effectsPane"]["insert"] == source, "Actual default compact pane admits a nonzero scroll range")
        first = min(3, maximum)
        for offset in (maximum, 0, first):
            reply = data("vibera_mixer", {"action": "scrollEffects", "offset": offset})
            check(reply["completionState"] == "completed" and reply["effectsPane"]["offset"] == offset and
                  state()["mixerEffectsPane"] == reply["effectsPane"], "Shared actual semantic scroll result/readback")
        for insert in (source, alternate, source):
            reply = data("vibera_mixer", {"action": "browseEffects", "insert": insert})
            browser = reply["effectsBrowser"]
            check(reply["completionState"] == "completed" and reply["undoable"] is False and
                  browser["open"] and browser["targetCurrent"] and browser["insert"] == insert and
                  browser["projectIdentity"] == baseline["projectIdentity"] and browser["insertsPlugin"] is False and
                  state()["mixerEffectsBrowser"] == browser, "Actual reused Settings browser targets exact current insert")
        check(model() == baseline, "Navigation preserves full canonical model, revision, dirty/history flags and transport")
        invalid = [
            ({"action": "browseEffects"}, "invalid_arguments"),
            ({"action": "browseEffects", "insert": -1}, "invalid_arguments"),
            ({"action": "browseEffects", "insert": True}, "invalid_arguments"),
            ({"action": "browseEffects", "insert": 0.5}, "invalid_arguments"),
            ({"action": "browseEffects", "insert": max(row["index"] for row in inserts) + 1}, "unavailable_target"),
            ({"action": "browseEffects", "insert": source, "offset": 0}, "invalid_arguments"),
            ({"action": "scrollEffects"}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": -1}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": True}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": 0.5}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": "0"}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": maximum + 1}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": 2147483648}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": 0, "insert": source}, "invalid_arguments"),
            ({"action": "scrollEffects", "offset": 0, "extra": 1}, "invalid_arguments"),
        ]
        stable_navigation = navigation()
        for arguments, code in invalid:
            fail("vibera_mixer", arguments, code, "Structured invalid navigation refusal: " + json.dumps(arguments))
            check(navigation() == stable_navigation and model() == baseline, "Invalid navigation has no partial view/model/history mutation")
        lease = data("vibera_gesture", {"action": "begin", "domain": "mixerControl", "sourceInsert": source})
        check(lease["completionState"] == "pending" and bool(lease["operation"]), "Actual canonical Mixer gesture owns admission")
        during_lease = model()
        for arguments in ({"action": "browseEffects", "insert": alternate}, {"action": "scrollEffects", "offset": 0}):
            fail("vibera_mixer", arguments, "gesture_owned", "Owned gesture blocks unrelated navigation explicitly")
            check(model() == during_lease and navigation() == stable_navigation, "Gesture refusal retains model/history and view")
        cancelled = data("vibera_gesture", {"action": "cancel", "operation": lease["operation"]})
        after_cancel = model()
        check(cancelled["completionState"] == "cancelled" and content(after_cancel) == content(baseline) and
              all(after_cancel[key] == baseline[key] for key in ("projectIdentity", "dirty", "undoAvailable", "redoAvailable")) and
              after_cancel["revision"] == baseline["revision"] + 1,
              "Real cancellation retains canonical baseline/history; existing cancellation epoch increments once")
        command("edit.undo")
        check(content(model()) == unbypassed, "One Undo after all navigation removes only the original bypass")
        command("edit.redo")
        check(content(model()) == content(baseline), "Exact bypass Redo after navigation")
        # A complete existing PluginSpec moves; payload preservation is verified
        # by actual XML below, not inferred from names or placeholder availability.
        command("transport.record")
        armed = model()
        check(armed["recordArmed"] and not armed["recordingTake"], "Canonical no-device record arming, not capture/hardware qualification")
        fail("vibera_command", {"command": "plugin.effect.move", "args": {"target": ids[0], "lane": 1}},
             "busy", "Shared final reorder admission refuses armed recording without finalizing or disarming it")
        check(model() == armed, "Refused order leaves musical/history/armed state exact")
        gesture_schema = next(tool for tool in tools if tool["name"] == "vibera_gesture")["inputSchema"]
        check("mixerEffects" in gesture_schema["properties"]["domain"]["enum"] and
              outputs["effectsPane"]["properties"]["wetGesture"]["type"] == ["object", "null"],
              "Typed scoped continuous chain domain and pending local wet readback")
        effects_lease = data("vibera_gesture", {"action": "begin", "domain": "mixerEffects", "sourceInsert": source})
        gesture_output = next(tool for tool in tools if tool["name"] == "vibera_gesture")["outputSchema"]
        check(all(gesture_output["properties"][key] == gesture_schema["properties"][key]
                  for key in ("domain", "sourceInsert", "targetInsert", "leaseMs")),
              "Gesture readback derives target/domain/lease contracts from authoritative inputs")
        check(effects_lease["sourceInsert"] == source and effects_lease["domain"] == "mixerEffects" and
              effects_lease["completionState"] == "pending" and
              set(gesture_output["required"]) <= effects_lease.keys() and
              effects_lease.keys() <= gesture_output["properties"].keys(),
              "Actual admitted chain lease returns its exact captured target and typed state")
        data("vibera_gesture", {"action": "update", "operation": effects_lease["operation"],
             "command": "plugin.effect.mix", "args": {"target": ids[0], "value": .5}})
        leased_mix = model()
        fail("vibera_gesture", {"action": "update", "operation": effects_lease["operation"],
             "command": "mixer.effects.bypass", "args": {"mixer": alternate, "enabled": True}},
             "gesture_domain", "Recording-safe lease cannot edit another chain")
        check(model() == leased_mix and state()["recordArmed"], "Refused scope preserves preview and armed capture")
        cancelled_effects = data("vibera_gesture", {"action": "cancel", "operation": effects_lease["operation"]})
        check(cancelled_effects["sourceInsert"] == source,
              "Cancelled chain readback retains captured target for verification")
        check(cancelled_effects["completionState"] == "cancelled" and content(model()) == content(armed) and state()["recordArmed"],
              "Actual recording-safe wet cancellation restores complete musical model without disarming")
        command("transport.record")
        check(not state()["recordArmed"], "Canonical recording disarm")
        before_owned_preview = model()
        document_lease = data("vibera_gesture", {"action": "begin", "domain": "document"})
        data("vibera_gesture", {"action": "update", "operation": document_lease["operation"],
             "command": "plugin.effect.move", "args": {"target": ids[0], "lane": 1}})
        check([row["id"] for row in plugins()] == [ids[1], ids[0]] + ids[2:],
              "Shared admission preserves legitimate owned document-gesture reorder updates")
        preview_cancel = data("vibera_gesture", {"action": "cancel", "operation": document_lease["operation"]})
        after_owned_preview = model()
        check(preview_cancel["completionState"] == "cancelled" and content(after_owned_preview) == content(before_owned_preview) and
              all(after_owned_preview[key] == before_owned_preview[key] for key in ("dirty", "undoAvailable", "redoAvailable")),
              "Actual semantic preview cancellation restores exact source/order/history")
        move_before = model()
        stale_revision = move_before["revision"]
        command("plugin.effect.move", target=ids[0], lane=1)
        expected_ids = [ids[1], ids[0]] + ids[2:]
        check([row["id"] for row in plugins()] == expected_ids and
              [row["slot"] for row in plugins()] == list(range(EFFECT_COUNT)),
              "Canonical move changes exact final order without replacing/duplicating plugin IDs")
        moved = model()
        command("edit.undo")
        check(content(model()) == content(move_before), "One exact effect order Undo")
        redo_tail = model()
        command("plugin.effect.move", target=ids[0], lane=0)
        check(model() == redo_tail and redo_tail["redoAvailable"], "Same-slot move preserves the actual redo tail")
        command("edit.redo")
        check(content(model()) == content(moved), "Exact effect order Redo")
        same_slot = model()
        command("plugin.effect.move", target=ids[0], lane=1)
        command("plugin.effect.move", target=ids[1])  # Existing omitted lane defaults0.
        check(model() == same_slot, "Same-slot/omitted-default no-ops preserve revision/dirty/history")
        for arguments, code in [
            ({"command": "plugin.effect.move", "args": {"target": ids[0], "lane": -1}}, "invalid_arguments"),
            ({"command": "plugin.effect.move", "args": {"target": ids[0], "lane": EFFECT_COUNT}}, "invalid_arguments"),
            ({"command": "plugin.effect.move", "args": {"target": ids[0], "lane": 0.5}}, "invalid_arguments"),
            ({"command": "plugin.effect.move", "args": {"target": ids[0], "lane": True}}, "invalid_arguments"),
            ({"command": "plugin.effect.move", "args": {"lane": 0}}, "invalid_arguments"),
            ({"command": "plugin.effect.move", "args": {"target": ids[0], "lane": 0}, "revision": stale_revision}, "stale_revision"),
            ({"command": "plugin.effect.move", "args": {"target": same_slot["channels"][0]["id"], "lane": 0}}, "validation_failed"),
        ]:
            fail("vibera_command", arguments, code, "Invalid/stale order refuses without clamping: " + json.dumps(arguments))
            check(model() == same_slot, "Failed move preserves exact model/history/revision")
        # Exercise a nonadjacent destination too; preserve exact positive order.
        command("plugin.effect.move", target=ids[1], lane=EFFECT_COUNT - 1)
        far_ids = [ids[0]] + ids[2:] + [ids[1]]
        check([row["id"] for row in plugins()] == far_ids, "Explicit final last-slot destination has no off-by-one")
        command("edit.undo")
        check(content(model()) == content(moved), "Far move Undo restores the complete previous chain")
        command("edit.redo")
        check([row["id"] for row in plugins()] == far_ids, "Far move Redo restores exact stable order")
        command("edit.undo")
        for command_id, required in (("plugin.effect.mix", ["target", "value"]), ("mixer.effects.bypass", ["mixer", "enabled"])):
            contract = data("vibera_commands", {"command": command_id, "includeSchemas": True})["operations"][0]
            check(contract["supported"] and contract["documentEdit"] and contract["macroAvailable"] and
                  contract["arguments"]["required"] == required and bool(contract["stateReadback"]) and bool(contract["prerequisitesDetail"]),
                  "Authoritative chain-control schema, prerequisites and readback: " + command_id)
        command("plugin.effect.mix", target=ids[0], value=.25)
        command("plugin.effect.mix", target=ids[1], value=.75)
        flags = {row["id"]: row["bypassed"] for row in plugins()}
        command("mixer.effects.bypass", mixer=source, enabled=True)
        mixed = model()
        check(all(row["chainBypassed"] and row["effectiveBypass"] for row in plugins()) and
              {row["id"]: row["bypassed"] for row in plugins()} == flags and
              {row["id"]: row["wet"] for row in plugins()} == {identity: (.25 if identity == ids[0] else .75 if identity == ids[1] else 1) for identity in ids},
              "Chain override retains distinct per-slot wet, bypass and IDs")
        command("edit.undo")
        check(all(not row["chainBypassed"] and row["effectiveBypass"] == row["bypassed"] for row in plugins()),
              "One chain Undo restores individual bypass without losing mix")
        command("edit.redo")
        check(content(model()) == content(mixed), "Chain Redo restores complete model")
        stable_mix = model()
        command("mixer.effects.bypass", mixer=source, enabled=True)
        command("plugin.effect.mix", target=ids[0], value=.25)
        check(model() == stable_mix, "Redundant wet and chain targets retain exact history/revision/dirty")
        for arguments, code in [
            ({"command": "plugin.effect.mix", "args": {"target": ids[0], "value": -.1}}, "invalid_arguments"),
            ({"command": "plugin.effect.mix", "args": {"target": ids[0], "value": 1.1}}, "invalid_arguments"),
            ({"command": "plugin.effect.mix", "args": {"target": ids[0], "value": True}}, "invalid_arguments"),
            ({"command": "plugin.effect.mix", "args": {"target": ids[0], "value": "0.5"}}, "invalid_arguments"),
            ({"command": "plugin.effect.mix", "args": {"value": .5}}, "invalid_arguments"),
            ({"command": "plugin.effect.mix", "args": {"target": stable_mix["channels"][0]["id"], "value": .5}}, "validation_failed"),
            ({"command": "mixer.effects.bypass", "args": {"mixer": source, "enabled": 1}}, "invalid_arguments"),
            ({"command": "mixer.effects.bypass", "args": {"mixer": source}}, "invalid_arguments"),
            ({"command": "mixer.effects.bypass", "args": {"mixer": max(row["index"] for row in inserts) + 1, "enabled": True}}, "validation_failed"),
        ]:
            fail("vibera_command", arguments, code, "Strict chain-control refusal: " + json.dumps(arguments))
            check(model() == stable_mix, "Invalid chain control leaves all model/history unchanged")
        preview = data("vibera_gesture", {"action": "begin", "domain": "document"})
        data("vibera_gesture", {"action": "update", "operation": preview["operation"], "command": "plugin.effect.mix", "args": {"target": ids[0], "value": .5}})
        data("vibera_gesture", {"action": "cancel", "operation": preview["operation"]})
        check(content(model()) == content(stable_mix), "Owned wet preview cancellation restores exact model")
        baseline = model()  # New asserted positive BA order, retained for real file checks.
        check([row["id"] for row in plugins()] == expected_ids, "Final musical save order independently fixed")
        saved = attempt / "saved-missing16.vibra"
        save(saved, "real missing-plugin state save")
        saved_tree = ET.parse(saved)
        expected_tree = ET.parse(fixture)
        expected = expected_tree.getroot().findall("./MIXER/INSERT/PLUGIN")
        next(node for node in expected if int(node.attrib["id"]) == target).set("bypass", "1")
        expected_chain = next(node for node in expected_tree.getroot().findall("./MIXER/INSERT") if int(node.attrib["index"]) == source)
        by_identity = {int(node.attrib["id"]): node for node in expected}
        for node in expected: expected_chain.remove(node)
        for identity in expected_ids: expected_chain.append(by_identity[identity])
        by_identity[ids[0]].set("wet", "0.25")
        by_identity[ids[1]].set("wet", "0.75")
        expected_chain.set("effectsBypassed", "1")
        check(by_identity[ids[0]].attrib["state"] != by_identity[ids[1]].attrib["state"],
              "Distinct original placeholder payloads make ordering/state preservation observable")
        check(xml_value(saved_tree.getroot()) == xml_value(expected_tree.getroot()),
              "Exact actual project round trip retains ordered IDs/descriptors/original state and only intended bypass/order/wet/chain override; navigation is not persisted")
        saved_sha = digest(saved)
        before_overwrite = model()
        fail("vibera_project", {"action": "save", "path": str(saved), "overwrite": False}, "output_exists",
             "Existing disposable output requires explicit overwrite; no silent replacement")
        check(digest(saved) == saved_sha and model() == before_overwrite, "Rejected overwrite preserves exact bytes/model/history")
        old_identity = state()["projectIdentity"]
        data("vibera_mixer", {"action": "browseEffects", "insert": alternate})
        data("vibera_mixer", {"action": "scrollEffects", "offset": first})
        wait_job(data("vibera_project", {"action": "open", "path": str(saved), "discardChanges": True}), "actual saved-project reopen")
        reopened = state()
        check(reopened["projectIdentity"] != old_identity and reopened["mixerEffectsPane"]["offset"] == 0 and
              not reopened["undoAvailable"] and not reopened["redoAvailable"], "Project replacement resets runtime pane navigation/history")
        check(content(model()) == content(baseline), "Actual reopen restores exact musical/model order and bypass state")
        check(not reopened["mixerEffectsBrowser"]["targetCurrent"] or
              reopened["mixerEffectsBrowser"]["projectIdentity"] == reopened["projectIdentity"], "Old Settings target is never reported as current in the new project")
        data("vibera_mixer", {"action": "browseEffects", "insert": source})
        rebound_model, rebound_navigation = model(), navigation()
        fail("vibera_mixer", {"action": "browseEffects", "insert": alternate, "projectIdentity": old_identity},
             "stale_project", "Stale Settings Add intent refuses against the actual new project")
        check(model() == rebound_model and navigation() == rebound_navigation, "Stale request cannot replace the newer-project browser dropdown")
        second = attempt / "reopened-missing16.vibra"
        save(second, "reopened actual state save")
        check(xml_value(ET.parse(second).getroot()) == xml_value(saved_tree.getroot()), "Second actual project save retains every recoverable placeholder byte")
        wait_job(data("vibera_project", {"action": "new", "discardChanges": True}), "actual new disposable project")
        fresh = state()
        check(not plugins() and fresh["mixerEffectsPane"]["offset"] == 0 and
              fresh["mixerEffectsPane"]["effectCount"] == 0, "New project clears effect viewport without retaining foreign plugin targets")
        reply = data("vibera_mixer", {"action": "browseEffects", "insert": alternate})
        check(reply["effectsBrowser"]["targetCurrent"] and reply["effectsBrowser"]["insert"] == alternate and
              reply["effectsBrowser"]["projectIdentity"] == fresh["projectIdentity"] and not plugins(),
              "Explicit Add retargets an empty current project and inserts no plugin")
        check(not missing.exists(), "No native plugin binary was created or loaded by this fixture")
        report["passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        report["traceback"] = traceback.format_exc()
    finally:
        if client is not None:
            closing = time.monotonic()
            try:
                client.close()
                report.update(exitCode=client.process.returncode, gracefulClose=client.process.returncode == 0)
            except Exception as failure:
                report.update(passed=False, closeError=str(failure), exitCode=client.process.poll(), gracefulClose=False)
            report["timings"]["closeSeconds"] = time.monotonic() - closing
            (attempt / "application-stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
        report.update(finished=True, seconds=time.monotonic() - began)
        retain("verification.json", report)
        (output / "verification.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({key: report.get(key) for key in ("passed", "finished", "seconds", "attemptDirectory", "error")}))
    return 0 if report["passed"] and report.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
