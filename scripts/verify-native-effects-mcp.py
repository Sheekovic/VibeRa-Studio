"""Actual compiled-in effects/control/history/project/WAV acceptance through VibeRa.

Usage: verify-native-effects-mcp.py ABS_APP ABS_FRESH_OUTPUT
Output is an owned directory below this repository's build/windows directory;
every invocation creates a fresh attempt child and preserves previous receipts.
The caller owns the engineering lease. This script never acquires another lease,
installs a plugin, touches user settings, or drives native desktop input.
"""
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import traceback
import wave
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    if not all(Path(value).is_absolute() for value in sys.argv[1:]):
        print("Both paths must be absolute", file=sys.stderr)
        return 2
    app, output = (Path(value).resolve() for value in sys.argv[1:])
    allowed = (ROOT / "build/windows").resolve()
    if output == allowed or not output.is_relative_to(allowed) or (output.exists() and not output.is_dir()):
        print("Refusing output outside build/windows or a non-directory: " + str(output), file=sys.stderr)
        return 2
    output = output / ("attempt-" + str(time.time_ns()))
    output.mkdir(parents=True, exist_ok=False)
    began = time.monotonic()
    report = {
        "passed": False, "finished": False, "checks": [], "stages": [],
        "commandObservations": [], "jobObservations": [], "refusals": [],
        "application": str(app), "output": str(output),
        "nativeInput": False, "listening": False, "hardware": False,
        "nativeWidgetControl": False, "releaseQualification": False,
        "scope": "Actual built-in native effect discovery, worker ACK, supported scalar history, current native save/reopen and offline PCM through application stdio MCP",
        "preservedFailures": [],
    }
    client = None
    phase = "preflight"

    def persist():
        (output / "verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def check(value, message):
        if not value:
            raise AssertionError(message)
        report["checks"].append(message)

    @contextmanager
    def stage(name):
        nonlocal phase
        phase = name
        row = {"name": name, "passed": False}
        report["stages"].append(row)
        started = time.monotonic()
        try:
            yield
            row["passed"] = True
        finally:
            row["seconds"] = time.monotonic() - started
            persist()

    def call(tool, arguments=None, allow_failure=False):
        response = client.call(tool, arguments or {}, allow_failure=True)
        with (output / "stdio-calls.jsonl").open("a", encoding="utf-8") as log:
            log.write(json.dumps({"seconds": time.monotonic() - began, "tool": tool,
                                  "arguments": arguments or {}, "response": response}) + "\n")
        if response.get("isError") and not allow_failure:
            raise RuntimeError(str(response.get("structuredContent", response)))
        return response

    def data(tool, arguments=None):
        return call(tool, arguments)["structuredContent"]

    def command(name, **arguments):
        admission = data("vibera_command", {"command": name, "args": arguments})

        def record(observation):
            report["commandObservations"].append({"command": name, **observation})
            persist()

        return observer.await_command_result(
            admission, lambda tool, args: call(tool, args, True)["structuredContent"], record)

    def wait_job(tool, admission, seconds=25):
        check(admission.get("completionState") == "pending", tool + " admission is genuinely pending")
        token = admission["operation"]
        current, polls = admission, 0
        started = time.monotonic()
        try:
            while current.get("completionState") == "pending":
                if time.monotonic() - started >= seconds:
                    raise AssertionError(tool + " exact operation timed out: " + str(current))
                time.sleep(.02)
                current = call(tool, {"action": "job", "operation": token}, True)["structuredContent"]
                polls += 1
                check(current.get("operation") == token, tool + " retains the admitted operation")
            check(current.get("completionState") == "completed", tool + " actual terminal completed: " + str(current))
            return current
        finally:
            report["jobObservations"].append({"tool": tool, "initial": admission, "terminal": current,
                                              "polls": polls, "seconds": time.monotonic() - started})
            persist()

    def projection(plugin_id=None):
        started = time.monotonic()
        identity = None
        while True:
            state = data("vibera_state")
            if identity is None:
                identity = state["projectIdentity"]
            check(state["projectIdentity"] == identity, "Projection retains the observed project")
            workers = state["audio"]["pluginWorkers"]
            matches = [w for w in workers if w["plugin"] == plugin_id] if plugin_id else workers
            ready = not state["pendingAudioUpdate"] and all(w["alive"] for w in workers)
            if ready and (plugin_id is None or len(matches) == 1):
                return state
            if time.monotonic() - started >= 25:
                (output / "projection-failure.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
                raise AssertionError("Actual worker projection failed to settle: " + str(state.get("status")))
            time.sleep(.03)

    def runtime(plugin_id):
        projection(plugin_id)
        matches = [p for p in data("vibera_plugin", {"action": "list"})["items"] if p["id"] == plugin_id]
        check(len(matches) == 1 and matches[0]["available"] and matches[0]["instance"],
              "Exact effect has one live native runtime")
        meter = matches[0].get("nativeOutputMeter", {})
        status = meter.get("status")
        check(status in ("notProcessed", "ready") and meter.get("available") == (status == "ready") and
              meter.get("units") == "linear sample peak",
              "Native output meter exposes actual processing availability and declared units")
        for key in ("leftPeak", "rightPeak"):
            value = meter.get(key)
            check((type(value) in (int, float) and math.isfinite(value) and value >= 0) if status == "ready"
                  else value is None,
                  "Native output meter retains finite measured peak or explicit unavailable null: " + key)
        return matches[0]

    def parameters(plugin_id, refresh=False):
        target = runtime(plugin_id)
        request = {"plugin": plugin_id, "instance": target["instance"]}
        refreshed = None
        if refresh:
            refreshed = wait_job("vibera_plugin", data("vibera_plugin", dict(request, action="refresh")))
        values, offset, generation = [], 0, None
        while True:
            page = data("vibera_plugin", dict(request, action="parameters", offset=offset, limit=128))
            if refreshed is not None:
                check(page["catalogueSerial"] == refreshed["catalogueSerial"] and
                      page["generation"] == refreshed["generation"],
                      "Readback uses the exact acknowledged fresh native catalogue")
            if generation is None:
                generation = page["generation"]
            check(page["generation"] == generation, "Parameter pagination retains exact generation")
            values.extend(page["items"])
            if page["nextOffset"] is None:
                break
            offset = page["nextOffset"]
        check(len(values) == page["total"] and len({p["id"] for p in values}) == len(values),
              "Complete native parameter catalogue has unique uint32 IDs")
        check(page["manualWriteSupported"] is True and "supported scalar" in page["manualHistoryScope"],
              "Native metadata explicitly supports bounded scalar writes/history")
        return target, generation, {p["name"]: p for p in values}

    def snapshot(plugin_id, names, refresh=True):
        # Parameters is a retained catalogue snapshot, not instantaneous native
        # state. Every current-value oracle and write baseline needs an explicit
        # completed refresh, even after a successful scalar ACK/state capture.
        target, generation, items = parameters(plugin_id, refresh)
        return target, generation, items, {key: items[name]["normalized"] for key, name in names.items()}

    def same(actual, expected, message):
        check(all(abs(actual[key] - value) <= 2e-6 for key, value in expected.items()), message)

    def write(plugin_id, names, key, value):
        target, generation, items, before = snapshot(plugin_id, names)
        parameter = items[names[key]]["id"]
        terminal = wait_job("vibera_plugin", data("vibera_plugin", {
            "action": "set", "plugin": plugin_id, "instance": target["instance"],
            "generation": generation, "parameter": parameter, "value": value}))
        check(terminal.get("nativeDispatched") is True and terminal.get("nativeApplied") is True and
              terminal.get("historyCommitted") is True and terminal.get("undoable") is True and
              terminal.get("completionScope") == "nativeScalarAckAndCanonicalHistory" and
              terminal.get("beforeKnown") is True,
              "Actual worker ACK and fresh captured scalar history completed: " + key)
        check(abs(terminal["before"] - before[key]) <= 2e-6 and abs(terminal["after"] - value) <= 2e-6,
              "Native ACK receipt retains exact scalar before/after: " + key)
        after = snapshot(plugin_id, names)[3]
        same(after, dict(before, **{key: value}), "Native parameter readback matches completed scalar edit: " + key)
        return before, after

    def confirmed_close(terminal, plugin_id, instance):
        rows = terminal.get("editorClosures", [])
        matches = [row for row in rows if row.get("plugin") == plugin_id and row.get("instance") == instance]
        check(len(matches) == 1 and type(matches[0].get("serial")) is int and matches[0]["serial"] > 0 and
              matches[0].get("outcome") == "closed" and matches[0].get("nativeCompletionKnown") is True and
              matches[0].get("dispatched") is True and matches[0].get("windowExists") is False,
              "Append owns exact original worker close ACK before fresh capture/publication")
        check(terminal.get("completionState") == "completed" and terminal.get("documentChanged") is True and
              terminal.get("undoable") is True and terminal.get("nativeRecoveryRequired") is False,
              "Append completion includes canonical history and actual native preparation")
        report.setdefault("appendEditorClosures", []).append({"plugin": plugin_id, "instance": instance,
                                                               "operation": terminal["operation"], "close": matches[0]})

    def export(name, expect_nonzero=True):
        path = output / name
        wait_job("vibera_project", data("vibera_project", {
            "action": "export", "path": str(path), "overwrite": False}), seconds=40)
        with wave.open(str(path), "rb") as wav:
            check(wav.getnchannels() == 2 and wav.getsampwidth() == 3 and
                  wav.getframerate() == 48000 and wav.getnframes() > 0, "Actual stereo 48kHz/24-bit WAV: " + name)
            pcm = wav.readframes(wav.getnframes())
            frames = wav.getnframes()
        if expect_nonzero:
            check(any(pcm), "Export contains actual nonzero PCM signal: " + name)
        report.setdefault("exports", []).append({"path": str(path), "sha256": sha256(path), "frames": frames})
        return pcm

    try:
        with stage("preflight and source identities"):
            check(app.is_file(), "Explicit application exists")
            report["applicationSha256"] = sha256(app)
            worker = app.with_name("VibeRa Plugin Worker.exe")
            check(worker.is_file(), "Exact application-relative native worker exists")
            report["worker"], report["workerSha256"] = str(worker), sha256(worker)
            report["verificationScriptSha256"] = sha256(Path(__file__))
            for filename in ("EffectsTypes.h", "EffectsCatalogue.h"):
                source = ROOT / "src/effects/dsp" / filename
                report.setdefault("sourceContracts", {})[str(source.relative_to(ROOT))] = sha256(source)
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
            observer_path = ROOT / "scripts/verify-installed-mcp.py"
            report["clientSha256"], report["commandObserverSha256"] = sha256(client_path), sha256(observer_path)
            client_module = load_module("effects_stdio_client", client_path)
            # Its only argv-dependent import reads argv[1], which is this exact app.
            # Its executable main is guarded, so importing does not launch a session.
            observer = load_module("effects_command_observer", observer_path)
            fixture = ROOT / "examples/First Light.vibra"
            report["originalExampleSha256"] = sha256(fixture)
            xml = ET.parse(fixture)
            channels = xml.getroot().findall("./CHANNELS/CHANNEL")
            kick = next(c for c in channels if c.get("source") == "builtin:kick")
            for channel in channels:
                channel.set("muted", "0" if channel is kick else "1")
            kick.set("mixer", "1")
            fixture_copy = output / "original-kick-only.vibra"
            xml.write(fixture_copy, encoding="utf-8", xml_declaration=True)
            check(sha256(fixture) == report["originalExampleSha256"], "Original builtin example remains unchanged")

        with stage("always available native discovery without scan or install"):
            client = client_module.VibeRaClient(app)
            report["pid"], report["server"] = client.process.pid, client.server
            initial = data("vibera_catalogue", {"action": "state"})
            check(initial["disposableSettings"] is True and initial["pluginCount"] == 0,
                  "External catalogue is disposable and empty; no plugin scan occurs")
            builtins = data("vibera_effects", {"action": "list"})["effects"]
            check(len(builtins) == 40 and len({p["type"] for p in builtins}) == 40,
                  "40 independently insertable built-in effects available immediately")
            descriptors = {}
            for row in builtins:
                descriptor = data("vibera_effects", {"action": "descriptor", "type": row["type"]})
                parsed = ET.fromstring(descriptor["descriptor"])
                check(parsed.get("format") == "VibeRa" and row["builtIn"] and not parsed.get("isInstrument") == "1",
                      "Compiled-in descriptor resolves without a plugin binary: " + row["type"])
                descriptors[row["type"]] = descriptor
            descriptor = descriptors["balance"]
            report["nativeDiscovery"] = builtins

        with stage("canonical insertion and native parameter discovery"):
            client.wait_job(data("vibera_project", {"action": "open", "path": str(fixture_copy), "discardChanges": True}))
            projection()
            dry = export("dry.wav")
            command("plugin.effect.insert", text=descriptor["descriptor"], mixer=1)
            mixer = data("vibera_inspect", {"entity": "mixer"})["items"]
            effects = next(m for m in mixer if m["index"] == 1)["effects"]
            check(len(effects) == 1, "Canonical insertion creates exactly one Mixer1 effect")
            plugin_id = effects[0]["id"]
            target, generation, items = parameters(plugin_id, refresh=True)
            names = {"gain": "Gain", "pan": "Pan"}
            check(set(items) == set(names.values()) and len(items) == 2,
                  "Balance exposes only its own two parameters and no mode selector")
            report["nativeMetadata"] = {"plugin": plugin_id, "parameters": items}

        with stage("actual native editor and wrapper view acknowledgement"):
            target = runtime(plugin_id)
            request = {"plugin": plugin_id, "instance": target["instance"]}
            opened = wait_job("vibera_plugin", data("vibera_plugin", dict(request, action="openEditor")))
            editor = data("vibera_plugin", dict(request, action="editorState"))["nativeEditor"]
            check(editor["known"] and editor["exists"] and editor["visible"],
                  "Built-in editor has actual owned visible window acknowledgement")
            for view in ("settings", "parameters", "plugin"):
                admission = data("vibera_plugin", dict(request, action="wrapper", view=view))
                wait_job("vibera_plugin", admission)
            wait_job("vibera_plugin", data("vibera_plugin", dict(request, action="closeEditor")))
            editor = data("vibera_plugin", dict(request, action="editorState"))["nativeEditor"]
            check(editor["known"] and not editor["visible"], "Native editor closes with truthful acknowledgement")

        with stage("real scalar ACK, refusal and exact supported Undo/Redo"):
            before, changed = write(plugin_id, names, "gain", .75)
            target, generation, items, _ = snapshot(plugin_id, names)
            revision = data("vibera_state")["revision"]
            for invalid, expected in [
                ({"instance": "owned-stale-instance"}, "stale_instance"),
                ({"value": 1.01}, "invalid_arguments")]:
                args = {"action": "set", "plugin": plugin_id, "instance": target["instance"],
                        "generation": generation, "parameter": items[names["gain"]]["id"], "value": .6}
                args.update(invalid)
                refused = call("vibera_plugin", args, True)
                report["refusals"].append({"arguments": args, "response": refused})
                check(refused.get("isError") is True and refused["structuredContent"]["error"]["code"] == expected,
                      "Truthful typed scalar rejection: " + expected)
                check(data("vibera_state")["revision"] == revision, "Rejected scalar leaves canonical revision unchanged")
                same(snapshot(plugin_id, names)[3], changed, "Rejected scalar preserves actual native values")
            command("edit.undo")
            same(snapshot(plugin_id, names, refresh=True)[3], before, "One canonical Undo restores exact native scalar values")
            command("edit.redo")
            same(snapshot(plugin_id, names, refresh=True)[3], changed, "Canonical Redo restores exact acknowledged native values")
            quieter = export("balance-gain.wav")
            check(len(dry) == len(quieter) and quieter != dry, "Balance scalar edit changes actual offline PCM at unchanged musical length")
            peak = lambda pcm: max(abs(int.from_bytes(pcm[i:i + 3], "little", signed=True)) for i in range(0, len(pcm), 3))
            dry_peak, gain_peak = peak(dry), peak(quieter)
            check(0 < gain_peak < dry_peak * .3, "Balance / Gain edit measurably attenuates the original builtin signal")
            report["balancePcm"] = {"dryPeak24": dry_peak, "gainPeak24": gain_peak}

        with stage("independent native types, scalar isolation, save and reopen"):
            current_target, current_generation, current_items, expected = snapshot(plugin_id, names)
            held = {"action": "set", "plugin": plugin_id, "instance": current_target["instance"],
                    "generation": current_generation, "parameter": current_items["Gain"]["id"],
                    "value": expected["gain"], "gestureOpen": True}
            held_admission = data("vibera_plugin", held)
            held_token = held_admission["operation"]
            began_ack = time.monotonic()
            while True:
                observed = data("vibera_plugin", {"action": "job", "operation": held_token})
                if observed.get("nativeApplied"):
                    break
                check(observed.get("completionState") == "pending", "Actual scalar owner remains pending before ACK")
                if time.monotonic() - began_ack >= 25:
                    raise AssertionError("Actual held scalar ACK exceeded existing25s plugin budget")
                time.sleep(.02)
            refused = call("vibera_command", {"command": "plugin.effect.insert", "args": {
                "text": descriptors["delay"]["descriptor"], "mixer": 1}}, True)
            report["refusals"].append({"scenario": "append while actual scalar gesture owns native ACK/history", "response": refused})
            check(refused.get("isError") is True and
                  refused["structuredContent"]["error"]["code"] == "parameterHistory.busy",
                  "Occupied append refuses an actual active scalar owner without stealing its token")
            retained_owner = data("vibera_plugin", {"action": "job", "operation": held_token})
            check(retained_owner.get("operation") == held_token and
                  retained_owner.get("completionState") == "pending" and
                  retained_owner.get("nativeApplied") is True and retained_owner.get("historyCommitted") is False,
                  "Canonical refusal retains the exact acknowledged open scalar operation")
            held.update(operation=held_token, gestureOpen=False)
            ended = wait_job("vibera_plugin", data("vibera_plugin", held))
            check(ended.get("nativeApplied") is True and ended.get("historyCommitted") is False,
                  "Unchanged-value scalar owner closes through actual ACK without fabricated history")
            retained = next(row for row in data("vibera_inspect", {"entity": "mixer"})["items"] if row["index"] == 1)["effects"]
            check([slot["id"] for slot in retained] == [plugin_id], "Busy refusal preserves the occupied chain exactly")
            first_runtime = runtime(plugin_id)
            first_request = {"plugin": plugin_id, "instance": first_runtime["instance"]}
            wait_job("vibera_plugin", data("vibera_plugin", dict(first_request, action="openEditor")))
            editor = data("vibera_plugin", dict(first_request, action="editorState"))["nativeEditor"]
            check(editor["known"] and editor["exists"] and editor["visible"],
                  "Occupied append begins with the actual first native editor still open")
            appended = command("plugin.effect.insert", text=descriptors["delay"]["descriptor"], mixer=1)
            confirmed_close(appended, plugin_id, first_runtime["instance"])
            effects = next(m for m in data("vibera_inspect", {"entity": "mixer"})["items"] if m["index"] == 1)["effects"]
            check(len(effects) == 2 and effects[0]["id"] == plugin_id, "Balance and Delay occupy separate ordered slots")
            delay_id = effects[1]["id"]
            _, _, delay_items = parameters(delay_id, True)
            delay_names = {"time": "Time"}
            check("Effect" not in delay_items and "Gain" not in delay_items and "Time" in delay_items,
                  "Delay has its own controls without Balance controls or a mode selector")
            write(delay_id, delay_names, "time", .35)
            expected_delay = snapshot(delay_id, delay_names)[3]
            same(snapshot(plugin_id, names)[3], expected, "Editing Delay preserves independent Balance values")
            saved = output / "native-effects-roundtrip.vibra"
            wait_job("vibera_project", data("vibera_project", {"action": "save", "path": str(saved), "overwrite": False}), seconds=40)
            previous_instance = runtime(plugin_id)["instance"]
            client.wait_job(data("vibera_project", {"action": "open", "path": str(saved), "discardChanges": True}))
            reopened = snapshot(plugin_id, names, True)
            check(reopened[0]["instance"] != previous_instance, "Reopen restores an independent worker")
            same(reopened[3], expected, "Balance native state survives save/reopen")
            same(snapshot(delay_id, delay_names, True)[3], expected_delay, "Independent Delay state survives save/reopen")
            delay_pcm = export("reopened-delay.wav")
            check(delay_pcm != quieter, "Independent restored Delay changes actual PCM")
            command("plugin.remove", target=delay_id)
            projection(plugin_id)

        with stage("canonical removal and restored effect history"):
            command("plugin.remove", target=plugin_id)
            effects = next(m for m in data("vibera_inspect", {"entity": "mixer"})["items"] if m["index"] == 1)["effects"]
            check(not effects, "Canonical removal leaves Mixer1 effect chain empty")
            command("edit.undo")
            restored = next(m for m in data("vibera_inspect", {"entity": "mixer"})["items"] if m["index"] == 1)["effects"]
            check(len(restored) == 1 and restored[0]["id"] == plugin_id, "Removal Undo restores the exact stable effect identity")
            same(snapshot(plugin_id, names, refresh=True)[3], expected, "Removal Undo restores independent scalar native state")
            check(sha256(fixture) == report["originalExampleSha256"], "Source example remains byte-identical after all application operations")
        with stage("each built-in type prepares independently with only relevant parameters"):
            command("plugin.remove", target=plugin_id)
            for row in builtins:
                command("plugin.effect.insert", text=descriptors[row["type"]]["descriptor"], mixer=1)
                effects = next(m for m in data("vibera_inspect", {"entity": "mixer"})["items"] if m["index"] == 1)["effects"]
                check(len(effects) == 1, "One independent slot for " + row["type"])
                current_id = effects[0]["id"]
                _, _, items = parameters(current_id, True)
                check(len(items) == len(row["parameters"]) and set(items) == {p["name"] for p in row["parameters"]},
                      "Actual fixed-type native controls: " + row["type"])
                writable = [items[p["name"]] for p in row["parameters"]
                            if items[p["name"]]["writable"] and items[p["name"]]["automatable"]]
                check(bool(writable), "Fixed native type has a relevant writable scalar: " + row["type"])
                first = writable[0]
                probe_names = {"probe": first["name"]}
                # Both endpoints are legal for every continuous/discrete native
                # range. An arbitrary .5 target can be off-grid for discrete controls.
                requested = 0.0 if first["normalized"] == 1.0 else 1.0
                before, after = write(current_id, probe_names, "probe", requested)
                check(after["probe"] != before["probe"],
                      "Actual native scalar branch changes its current value: " + row["type"])
                report.setdefault("nativeScalarBranches", []).append({
                    "type": row["type"], "plugin": current_id,
                    "parameter": first["id"], "name": first["name"], "steps": first["steps"],
                    "before": before["probe"], "requested": requested, "actualAfter": after["probe"],
                })
                command("edit.undo")
                same(snapshot(current_id, probe_names, refresh=True)[3], before,
                     "Exact scalar Undo on independent native type: " + row["type"])
                command("edit.redo")
                same(snapshot(current_id, probe_names, refresh=True)[3], after,
                     "Exact scalar Redo on independent native type: " + row["type"])
                command("plugin.remove", target=current_id)
            check(data("vibera_catalogue", {"action": "state"})["pluginCount"] == 0,
                  "Every native effect worked with zero external catalogue entries")

        with stage("Master native effects, occupied append close ACK and exact history/persistence"):
            mixer_before = data("vibera_inspect", {"entity": "mixer"})["items"]
            check(not next(row for row in mixer_before if row["index"] == 0)["effects"],
                  "Master starts with a genuine empty chain")
            command("plugin.effect.insert", text=descriptors["balance"]["descriptor"], mixer=0)
            master = next(row for row in data("vibera_inspect", {"entity": "mixer"})["items"] if row["index"] == 0)
            check(len(master["effects"]) == 1, "Canonical native insertion supports Master0")
            master_balance = master["effects"][0]["id"]
            write(master_balance, names, "gain", .83)
            master_before = snapshot(master_balance, names)[3]
            master_runtime = runtime(master_balance)
            master_request = {"plugin": master_balance, "instance": master_runtime["instance"]}
            wait_job("vibera_plugin", data("vibera_plugin", dict(master_request, action="openEditor")))
            editor = data("vibera_plugin", dict(master_request, action="editorState"))["nativeEditor"]
            check(editor["known"] and editor["exists"] and editor["visible"],
                  "Master first native editor is actually open before appending")
            appended = command("plugin.effect.insert", text=descriptors["delay"]["descriptor"], mixer=0)
            confirmed_close(appended, master_balance, master_runtime["instance"])
            master = next(row for row in data("vibera_inspect", {"entity": "mixer"})["items"] if row["index"] == 0)
            master_ids = [slot["id"] for slot in master["effects"]]
            check(len(master_ids) == 2 and master_ids[0] == master_balance and master_ids[1] != master_balance,
                  "Master appends a distinct second stable effect without replacing its first slot")
            same(snapshot(master_balance, names)[3], master_before,
                 "Master append preserves first effect's actual native scalar state")
            command("edit.undo")
            master = next(row for row in data("vibera_inspect", {"entity": "mixer"})["items"] if row["index"] == 0)
            check([slot["id"] for slot in master["effects"]] == [master_balance],
                  "One Master append Undo restores exactly the original occupied chain")
            same(snapshot(master_balance, names, True)[3], master_before,
                 "Master append Undo restores first native state")
            command("edit.redo")
            master = next(row for row in data("vibera_inspect", {"entity": "mixer"})["items"] if row["index"] == 0)
            check([slot["id"] for slot in master["effects"]] == master_ids,
                  "Master append Redo restores both exact effect IDs in order")
            master_delay = snapshot(master_ids[1], {"time": "Time"})[3]
            saved = output / "master-native-effects-roundtrip.vibra"
            wait_job("vibera_project", data("vibera_project", {"action": "save", "path": str(saved), "overwrite": False}), seconds=40)
            saved_slots = ET.parse(saved).getroot().findall("./MIXER/INSERT[@index='0']/PLUGIN")
            check([int(slot.get("id")) for slot in saved_slots] == master_ids and
                  all(slot.get("state") for slot in saved_slots),
                  "Real Master Save retains ordered IDs and nonempty acknowledged native opaque bytes")
            client.wait_job(data("vibera_project", {"action": "open", "path": str(saved), "discardChanges": True}))
            projection(master_balance)
            master = next(row for row in data("vibera_inspect", {"entity": "mixer"})["items"] if row["index"] == 0)
            check([slot["id"] for slot in master["effects"]] == master_ids,
                  "Master save/reopen preserves both exact ordered slots")
            same(snapshot(master_balance, names, True)[3], master_before,
                 "Master save/reopen restores original Balance native state")
            same(snapshot(master_ids[1], {"time": "Time"}, True)[3], master_delay,
                 "Master save/reopen restores independent Delay native state")
            master_pcm = export("master-native-chain.wav")
            write(master_balance, names, "gain", 0.0)  # Native minimum is -96 dB, not a fake mute.
            attenuated_pcm = export("master-native-attenuated.wav", expect_nonzero=False)
            def peak24(pcm):
                return max(abs(int.from_bytes(pcm[index:index + 3], "little", signed=True))
                           for index in range(0, len(pcm), 3))
            master_peak, quiet_peak = peak24(master_pcm), peak24(attenuated_pcm)
            check(master_peak > 1000 and quiet_peak <= max(4, master_peak * .00012),
                  "Actual Master native gain changes exported PCM by the declared deep attenuation")
            report["masterAudio"] = {"baselinePeak24":master_peak, "attenuatedPeak24":quiet_peak,
                                     "listening":False, "hardware":False}
            command("edit.undo")
            same(snapshot(master_balance, names, True)[3], master_before,
                 "Master attenuation Undo restores prior actual native gain after export")
            current_mixer = data("vibera_inspect", {"entity": "mixer"})["items"]
            check({row["index"]: row["effects"] for row in current_mixer if row["index"] != 0} ==
                  {row["index"]: row["effects"] for row in mixer_before if row["index"] != 0},
                  "Master operations preserve every other insert's effect IDs/order/state")
        report["passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        report["traceback"] = traceback.format_exc()
        report["preservedFailures"].append({"stage": phase, "error": str(failure)})
        if client:
            try:
                report["failureState"] = data("vibera_state")
            except Exception as state_failure:
                report["failureStateError"] = str(state_failure)
    finally:
        if client:
            try:
                client.close()
                report["exitCode"] = client.process.returncode
            except Exception as close_failure:
                report["passed"] = False
                report["closeError"] = str(close_failure)
                report["preservedFailures"].append({"stage": "owned process shutdown", "error": str(close_failure)})
            (output / "application-stderr.log").write_text("".join(client.stderr), encoding="utf-8")
        if "applicationSha256" in report and "workerSha256" in report:
            try:
                report["finalApplicationSha256"] = sha256(app)
                report["finalWorkerSha256"] = sha256(Path(report["worker"]))
                report["binaryIdentitiesStable"] = (
                    report["finalApplicationSha256"] == report["applicationSha256"] and
                    report["finalWorkerSha256"] == report["workerSha256"])
                check(report["binaryIdentitiesStable"],
                      "Exact application and worker inputs remain byte-identical through owned shutdown")
            except Exception as identity_failure:
                report["passed"] = False
                report["binaryIdentityError"] = str(identity_failure)
                report["preservedFailures"].append({
                    "stage": "final application and worker identities", "error": str(identity_failure)})
        report["finished"] = True
        report["seconds"] = time.monotonic() - began
        persist()
    print(json.dumps({"passed": report["passed"], "checks": len(report["checks"]),
                      "seconds": report["seconds"], "receipt": str(output / "verification.json"),
                      "error": report.get("error"), "closeError": report.get("closeError")}))
    return 0 if report["passed"] and report.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
