"""Actual Pattern Auto/Manual extent, meter, history and offline PCM acceptance.

Usage: verify-pattern-extent-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
Disposable provider-neutral --mcp-test session. No OS input, real plugin,
listening or hardware claim. No-device playback/taking admission is not simulated.
"""
from __future__ import annotations

import copy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import traceback
import wave
import xml.etree.ElementTree as ET
from dev_support import project_schema

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
EXTENT, METER = "pattern.extent", "pattern.meter"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Required existing helper unavailable: " + str(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    app = Path(sys.argv[1])
    guards_path = ROOT / "scripts/verify-plugin-io-mcp.py"
    guards = load_module("extent_output_admission", guards_path)
    output = guards.owned_output_path(sys.argv[2], app)
    guards.ordinary(app, False)
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
    if not client_path.is_file():
        client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
    worker = app.parent / "VibeRa Plugin Worker.exe"
    report = {"passed": False, "finished": False, "checks": [], "operations": [], "responses": 0,
              "attemptDirectory": str(attempt), "application": str(app), "applicationSha256": digest(app),
              "scriptSha256": digest(Path(__file__)), "clientSha256": digest(client_path),
              "outputAdmissionSha256": digest(guards_path), "nativeInput": False, "hardware": False,
              "listening": False, "realPlugin": False, "liveAudio": False, "pcmExported": False,
              "scope": "Actual canonical Pattern extent/meter/recipes/history, current schema plus explicit schema30/29 migration/project I/O and offline PCM",
              "limits": ["No physical or virtual live callbacks are manufactured by this no-device stdio session",
                         "No live playback, native plugin or recording qualification",
                         "Project migration/refusal uses owned XML copies; JSON duplicate keys are not representable by the shared client"]}
    started, client = time.monotonic(), None

    def retain():
        text = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
        (attempt / "verification.json").write_text(text, encoding="utf-8")
        (output / "verification.json").write_text(text, encoding="utf-8")

    def check(value, message):
        if not value:
            raise AssertionError(message)
        report["checks"].append(message)

    retain()
    try:
        guards.ordinary(worker, False)
        report["workerSha256"] = digest(worker)
        client = load_module("extent_neutral_client", client_path).VibeRaClient(app)
        report.update(pid=client.process.pid, version=client.server.get("version"))

        def call(tool, args=None, allow_failure=False):
            response = client.call(tool, args or {}, allow_failure=True)
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "args": args or {}, "response": response}, ensure_ascii=False) + "\n")
            report["responses"] += 1
            if response.get("isError") and not allow_failure:
                raise AssertionError("Unexpected MCP refusal: " + str(response))
            return response

        data = lambda tool, args=None: call(tool, args)["structuredContent"]
        state = lambda: data("vibera_state")

        def command(name, **args):
            result = data("vibera_command", {"command": name, "args": args})
            check(result.get("completionState") != "pending", "Canonical score operation is not accepted-only: " + name)
            return result

        def items(entity, page_size=16, **args):
            rows, offset = [], 0
            while True:
                page = data("vibera_inspect", {"entity": entity, **args, "offset": offset, "limit": page_size})
                rows.extend(page["items"])
                following = page["nextOffset"]
                if following is None:
                    check(len(rows) == page["total"], "Complete actual paginated readback: " + entity)
                    return rows
                check(type(following) is int and following == offset + len(page["items"]) and following > offset,
                      "Pagination advances by the exact returned rows")
                offset = following

        def document():
            current = state()
            return {"patterns": current["patterns"], "notes": items("notes", pattern=pattern),
                    "placements": items("clips"), "numerator": current["numerator"],
                    "denominator": current["denominator"]}

        def protected():
            current = state()
            return {"document": document(), **{key: current[key] for key in
                    ("projectIdentity", "revision", "dirty", "undoAvailable", "redoAvailable")}}

        def refuse(name, args):
            before = protected()
            response = call("vibera_command", {"command": name, "args": args}, True)
            error = response.get("structuredContent", {}).get("error", {})
            check(response.get("isError") is True and bool(error.get("code")) and bool(error.get("message")) and
                  protected() == before, "Structured refusal retains exact score/extent/history: " + str(args))

        def wait_job(initial, expected="completed", seconds=40):
            current, began = initial, time.monotonic()
            try:
                while current.get("completionState") == "pending" and time.monotonic() - began < seconds:
                    time.sleep(.03)
                    current = call("vibera_project", {"action": "job", "operation": initial["operation"]}, True)["structuredContent"]
                    check(current.get("operation") == initial["operation"], "Actual file job retains exact operation ownership")
                check(current.get("completionState") == expected, "Actual file job reaches terminal " + expected)
                if expected == "failed":
                    check(bool(current.get("error", {}).get("code")), "Failed file job retains structured reason")
                return current
            finally:
                report["operations"].append({"initial": initial, "terminal": current, "seconds": time.monotonic() - began})
                retain()

        def project_job(action, **args):
            return wait_job(data("vibera_project", {"action": action, **args}))

        def export(name, frames):
            path = attempt / name
            project_job("export", path=str(path), overwrite=False)
            with wave.open(str(path), "rb") as wav:
                fmt = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes())
                pcm = wav.readframes(wav.getnframes())
            check(fmt == (2, 3, 48000, frames) and len(pcm) == fmt[3] * 6,
                  "Actual literal-duration Song export has exact stereo 48kHz/24bit duration")
            peak = max(abs(int.from_bytes(pcm[i:i + 3], "little", signed=True)) for i in range(0, len(pcm), 3))
            check(peak > 83886, "Actual built-in sample score exports nontrivial PCM")
            report.setdefault("exports", []).append({"path": str(path), "sha256": digest(path),
                "pcmSha256": hashlib.sha256(pcm).hexdigest(), "format": fmt, "linearPeak": peak / 8388608})
            return fmt, pcm

        fields = ("autoLength", "numerator", "denominator", "effectiveNumerator", "effectiveDenominator", "barBeats", "length")
        contracts = {}
        for name in (EXTENT, METER):
            contract = data("vibera_commands", {"command": name, "includeSchemas": True})["operations"][0]
            schema = contract["arguments"]
            check(contract["supported"] and contract["documentEdit"] and schema["additionalProperties"] is False,
                  "Actual typed canonical discovery: " + name)
            check(set(schema["required"]) == ({"pattern", "text"} if name == EXTENT else {"pattern", "enabled"}),
                  "Extent/meter requires an explicit stable Pattern and mode")
            contracts[name] = contract
        extent_schema = contracts[EXTENT]["arguments"]["properties"]
        manual_range = contracts[EXTENT]["arguments"]["allOf"][0]["then"]["properties"]["length"]
        check(manual_range["minimum"] == .25 and manual_range["maximum"] == 1000000 and
              manual_range["multipleOf"] == .25,
              "Manual-only musical length range is machine-readable without restricting unused Auto length")
        check(extent_schema["text"]["enum"] == ["auto", "manual"] and
              extent_schema["length"]["type"] == "number" and
              contracts[METER]["arguments"]["properties"]["enabled"]["type"] == "boolean",
              "Extent mode/length and explicit inherit/custom meter flag have typed discovery")
        (attempt / "contracts.json").write_text(json.dumps(contracts, indent=2) + "\n", encoding="utf-8")
        project_job("new", template="empty", discardChanges=True)
        initial = state()
        pattern, channel = initial["selectedPattern"], initial["channels"][0]["id"]

        def part():
            current = next(row for row in state()["patterns"] if row["id"] == pattern)
            inspected = next(row for row in items("patterns", page_size=1) if row["id"] == pattern)
            check(all(current[key] == inspected[key] for key in fields), "Paginated Pattern extent/meter readback equals actual state")
            return current

        def assert_extent(auto, length, numerator, denominator, effective_numerator, effective_denominator):
            current = part()
            check(all(current[key] == value for key, value in dict(autoLength=auto, length=length,
                      numerator=numerator, denominator=denominator, effectiveNumerator=effective_numerator,
                      effectiveDenominator=effective_denominator,
                      barBeats=effective_numerator * 4 / effective_denominator).items()),
                  "Literal actual extent/meter: " + str((auto, length, numerator, denominator, effective_numerator, effective_denominator)))

        def extent(mode, length=None):
            return command(EXTENT, pattern=pattern, text=mode, **({"length": length} if length is not None else {}))

        def meter(enabled, **args):
            return command(METER, pattern=pattern, enabled=enabled, **args)

        def history(name, args):
            before = document()
            command(name, **args)
            after = document()
            check(before != after, "Canonical extent/meter action changes the intended document")
            command("edit.undo")
            check(document() == before, "One exact extent/meter Undo")
            command("edit.redo")
            check(document() == after, "Exact extent/meter Redo")

        assert_extent(True, 4, 0, 0, 4, 4)
        command("notes.add", pattern=pattern, channel=channel, beat=0, length=.25, pitch=60, value=.5)
        command("notes.add", pattern=pattern, channel=channel, beat=6.25, length=.5, pitch=67, value=.5)
        raw = items("notes", pattern=pattern)
        far = next(row["id"] for row in raw if row["beat"] == 6.25)
        assert_extent(True, 8, 0, 0, 4, 4)
        command("notes.mute", pattern=pattern, ids=[far])
        command("notes.velocity", pattern=pattern, ids=[far], value=0)
        assert_extent(True, 8, 0, 0, 4, 4)
        check(next(row for row in items("notes", pattern=pattern) if row["id"] == far)["muted"],
              "Muted zero-velocity stored note still participates in Auto extent")
        before_delete = document()
        command("notes.delete", pattern=pattern, ids=[far])
        assert_extent(True, 4, 0, 0, 4, 4)
        command("edit.undo")
        check(document() == before_delete, "Auto shrink Undo restores exact raw note identity and extent")
        command("edit.redo")
        assert_extent(True, 4, 0, 0, 4, 4)
        command("edit.undo")
        command("notes.unmute", pattern=pattern, ids=[far])
        command("notes.velocity", pattern=pattern, ids=[far], value=.5)
        command("clip.add", pattern=pattern, beat=0, length=4)
        placements = items("clips")
        history(EXTENT, {"pattern": pattern, "text": "manual", "length": 3.25})
        assert_extent(False, 3.25, 0, 0, 4, 4)
        old_ids = {row["id"] for row in items("notes", pattern=pattern)}
        command("notes.add", pattern=pattern, channel=channel, beat=9, length=.25, pitch=72, value=.5)
        added = next(row["id"] for row in items("notes", pattern=pattern) if row["id"] not in old_ids)
        command("notes.move", pattern=pattern, ids=[added], beat=2, length=.5)
        assert_extent(False, 3.25, 0, 0, 4, 4)
        check(next(row for row in items("notes", pattern=pattern) if row["id"] == added)["beat"] == 11 and
              items("clips") == placements, "Manual extent retains moved/resized dormant notes and exact placement window")
        extent("auto")
        assert_extent(True, 12, 0, 0, 4, 4)
        history(METER, {"pattern": pattern, "enabled": True, "numerator": 7, "denominator": 8})
        assert_extent(True, 14, 7, 8, 7, 8)
        command("project.meter", numerator=3, denominator=4)
        assert_extent(True, 14, 7, 8, 7, 8)
        meter(False)
        assert_extent(True, 12, 0, 0, 3, 4)
        command("project.meter", numerator=7, denominator=8)
        assert_extent(True, 14, 0, 0, 7, 8)
        history(EXTENT, {"pattern": pattern, "text": "manual", "length": 5.25})
        assert_extent(False, 5.25, 0, 0, 7, 8)
        command("pattern.length", pattern=pattern, length=.125)
        assert_extent(False, .125, 0, 0, 7, 8)
        check(items("clips") == placements and {row["id"] for row in items("notes", pattern=pattern)} == old_ids | {added},
              "Legacy fractional positive pattern.length sets Manual without deleting raw identities or resizing placements")
        command("notes.delete", pattern=pattern, ids=[row["id"] for row in items("notes", pattern=pattern)])
        extent("auto")
        assert_extent(True, 3.5, 0, 0, 7, 8)
        command("pattern.channelLoop", pattern=pattern, channel=channel, text="fixed", length=5)
        assert_extent(True, 7, 0, 0, 7, 8)
        command("notes.add", pattern=pattern, channel=channel, beat=0, length=.25, pitch=60, value=.5)
        assert_extent(True, 7, 0, 0, 7, 8)
        command("pattern.channelLoop", pattern=pattern, channel=channel, text="fixed", length=.25)
        assert_extent(True, 3.5, 0, 0, 7, 8)
        command("pattern.channelLoop", pattern=pattern, channel=channel, text="off")
        assert_extent(True, 3.5, 0, 0, 7, 8)
        unchanged = protected()
        extent("auto", .3)  # Auto deliberately ignores unused length, including off-grid values.
        meter(False)
        check(protected() == unchanged, "Idempotent Auto/inherit commands retain revision, dirty state and both history directions")

        for args in ({}, {"pattern": pattern}, {"pattern": True, "text": "auto"},
                     {"pattern": 999999999, "text": "auto"}, {"pattern": pattern, "text": "fixed"},
                     {"pattern": pattern, "text": "manual", "length": .3},
                     {"pattern": pattern, "text": "manual", "length": 0},
                     {"pattern": pattern, "text": "manual", "length": 1000000.25},
                     {"pattern": pattern, "text": "manual", "length": True},
                     {"pattern": pattern, "text": "auto", "unexpected": 1}):
            refuse(EXTENT, args)
        for args in ({"pattern": pattern}, {"pattern": pattern, "enabled": 1},
                     {"pattern": pattern, "enabled": True, "numerator": 0, "denominator": 8},
                     {"pattern": pattern, "enabled": True, "numerator": 7, "denominator": 3},
                     {"pattern": pattern, "enabled": True, "numerator": 33, "denominator": 8},
                     {"pattern": pattern, "enabled": True, "numerator": True, "denominator": 8},
                     {"pattern": pattern, "enabled": False, "foreign": 1}):
            refuse(METER, args)
        history(EXTENT, {"pattern": pattern, "text": "manual", "length": 5.25})
        command("edit.undo")
        before_batch = protected()
        denied = call("vibera_batch", {"commands": [
            {"command": EXTENT, "args": {"pattern": pattern, "text": "manual", "length": 8}},
            {"command": METER, "args": {"pattern": 999999999, "enabled": True}}]}, True)
        after_batch = protected()
        check(denied.get("isError") is True and after_batch["revision"] > before_batch["revision"] and
              {key: value for key, value in after_batch.items() if key != "revision"} ==
              {key: value for key, value in before_batch.items() if key != "revision"},
              "Failed batch preview/rollback advances revision only, preserving exact document/dirty/history")
        command("edit.redo")
        assert_extent(False, 5.25, 0, 0, 7, 8)
        config = data("vibera_macros", {"action": "state", "includeSchemas": True})
        recipe = {"id": "macro.pattern-extent-proof", "name": "Pattern extent proof", "steps": [
            {"action": METER, "parameters": {"pattern": pattern, "enabled": True, "numerator": 7, "denominator": 8}, "currentContext": False},
            {"action": EXTENT, "parameters": {"pattern": pattern, "text": "auto"}, "currentContext": False}]}
        configured = data("vibera_macros", {"action": "create", "generation": config["generation"], "macro": recipe})
        check(configured["configurationCommitted"] and configured["profileMatchesMemory"], "Actual extent/meter recipe persists typed arguments")
        before_recipe = document()
        run = data("vibera_macros", {"action": "run", "id": recipe["id"], "generation": configured["generation"]})
        check(run["completionState"] == "completed", "Actual extent/meter recipe reaches canonical completion")
        assert_extent(True, 3.5, 7, 8, 7, 8)
        recipe_result = document()
        command("edit.undo")
        check(document() == before_recipe, "Combined meter/extent recipe has one exact Undo")
        command("edit.redo")
        check(document() == recipe_result, "Combined meter/extent recipe has exact Redo")
        config = data("vibera_macros", {"action": "state"})
        data("vibera_macros", {"action": "remove", "id": recipe["id"], "generation": config["generation"]})

        # Literal offline oracle: two raw onsets at0 and3.5 beats. Auto custom7/8
        # produces7 beats; explicit Manual7 must produce the exact same PCM.
        project_job("new", template="empty", discardChanges=True)
        current = state()
        pattern, channel = current["selectedPattern"], current["channels"][0]["id"]
        command("project.tempo", value=120)
        meter(True, numerator=7, denominator=8)
        for beat in (0, 3.5):
            command("notes.add", pattern=pattern, channel=channel, beat=beat, length=.125, pitch=60, value=.5)
        assert_extent(True, 7, 7, 8, 7, 8)
        command("clip.add", pattern=pattern, beat=0, length=7)
        auto_doc = document()
        automatic = export("auto-custom-seven-eight.wav", 168000)
        extent("manual", 7)
        check(document()["notes"] == auto_doc["notes"] and items("clips") == auto_doc["placements"],
              "Manual7 comparison retains exact source IDs and explicit placement windows")
        check(export("explicit-manual-seven-beats.wav", 168000) == automatic,
              "Actual Auto7/8 and independently explicit Manual7 export identical literal-duration Song PCM")
        command("edit.undo")
        check(document() == auto_doc, "Manual comparison Undo restores exact Auto/custom-meter document")
        project = attempt / "pattern-extent-current-schema.vibra"
        project_job("save", path=str(project), overwrite=False)
        saved = document()
        tree = ET.parse(project)
        check(int(tree.getroot().attrib["version"]) == project_schema(ROOT),
              "Actual saved extent/meter project matches authoritative current schema")
        pattern_xml = next(row for row in tree.getroot().iter("PATTERN") if int(row.attrib["id"]) == pattern)
        check(pattern_xml.attrib["autoLength"] in ("1", "true") and pattern_xml.attrib["numerator"] == "7" and
              pattern_xml.attrib["denominator"] == "8", "Actual XML retains Auto and custom meter independently")
        project_job("new", discardChanges=True)
        project_job("open", path=str(project), discardChanges=True)
        check(document() == saved and not state()["dirty"], "Actual save/new/reopen restores exact Auto/meter, source IDs and placements")
        legacy30 = copy.deepcopy(tree)
        legacy30.getroot().set("version", "30")
        for sampler in legacy30.getroot().iter("SAMPLER"):
            for key in ("filterType", "filterCutoff", "filterResonance"):
                sampler.attrib.pop(key, None)
            modulation = sampler.find("MODULATION")
            if modulation is not None:
                sampler.remove(modulation)
        check(all("pan" not in n.attrib and "fineCents" not in n.attrib for n in legacy30.getroot().iter("NOTE")),
              "Genuine schema30 fixture has no schema31 expression attributes")
        legacy30_path = attempt / "legacy-schema30-auto-meter.vibra"
        legacy30.write(legacy30_path, encoding="utf-8", xml_declaration=True)
        project_job("open", path=str(legacy30_path), discardChanges=True)
        check(document() == saved, "Schema30 retains exact Auto/meter/source IDs and neutral expression on migration")
        stale = copy.deepcopy(tree)
        target = next(row for row in stale.getroot().iter("PATTERN") if int(row.attrib["id"]) == pattern)
        target.set("length", "0.25")
        stale_path = attempt / "auto-stale-derived-window.vibra"
        stale.write(stale_path, encoding="utf-8", xml_declaration=True)
        project_job("open", path=str(stale_path), discardChanges=True)
        check(document() == saved, "Actual Auto reopen derives extent from unchanged score instead of trusting stale cached length")

        for index, attributes in enumerate(({"autoLength": "invalid"}, {"numerator": "7", "denominator": "0"},
                                            {"numerator": "0", "denominator": "8"}, {"denominator": "3"})):
            malformed = copy.deepcopy(tree)
            target = next(row for row in malformed.getroot().iter("PATTERN") if int(row.attrib["id"]) == pattern)
            target.attrib.update(attributes)
            path = attempt / ("malformed-extent-" + str(index) + ".vibra")
            malformed.write(path, encoding="utf-8", xml_declaration=True)
            before = protected()
            wait_job(call("vibera_project", {"action": "open", "path": str(path), "discardChanges": True}, True)["structuredContent"], "failed")
            check(protected() == before, "Malformed persisted extent/meter refuses without publication/history mutation")
        legacy = copy.deepcopy(tree)
        legacy.getroot().set("version", "29")
        for sampler in legacy.getroot().iter("SAMPLER"):
            for key in ("filterType", "filterCutoff", "filterResonance"):
                sampler.attrib.pop(key, None)
            modulation = sampler.find("MODULATION")
            if modulation is not None:
                sampler.remove(modulation)
        for row in legacy.getroot().iter("PATTERN"):
            for key in ("autoLength", "numerator", "denominator"):
                row.attrib.pop(key, None)
        old_file = attempt / "legacy-schema29-manual-inherit.vibra"
        legacy.write(old_file, encoding="utf-8", xml_declaration=True)
        project_job("open", path=str(old_file), discardChanges=True)
        assert_extent(False, 7, 0, 0, 4, 4)
        check(items("notes", pattern=pattern) == saved["notes"] and items("clips") == saved["placements"],
              "Schema29 migrates Manual/inherit while retaining exact legacy length, source IDs and placement windows")
        command("notes.add", pattern=pattern, channel=channel, beat=9, length=.25, pitch=67, value=.5)
        assert_extent(False, 7, 0, 0, 4, 4)
        report.update(project=str(project), projectSha256=digest(project), legacyProject=str(old_file),
                      legacyProjectSha256=digest(old_file), pcmExported=True)
        client.close()
        check(client.process.returncode == 0, "Owned disposable MCP process closes gracefully")
        check(digest(app) == report["applicationSha256"] and digest(worker) == report["workerSha256"] and
              digest(client_path) == report["clientSha256"] and digest(Path(__file__)) == report["scriptSha256"] and
              digest(guards_path) == report["outputAdmissionSha256"], "Exact application/worker/client/verifier/output-admission inputs remain unchanged")
        report["passed"] = True
    except Exception as failure:
        report.update(error=str(failure), traceback=traceback.format_exc())
    finally:
        try:
            if client and client.process.poll() is None:
                client.close()
        except Exception as failure:
            report.update(passed=False, closeError=str(failure))
        finally:
            if client:
                report["exitCode"] = client.process.returncode
                (attempt / "stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
            report.update(finished=True, elapsedSeconds=time.monotonic() - started)
            retain()
    print(json.dumps({key: report.get(key) for key in ("passed", "finished", "elapsedSeconds", "error", "attemptDirectory")}))
    return 0 if report["passed"] and report.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
