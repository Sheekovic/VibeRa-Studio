"""Actual per-Pattern Channel loops, Burn, history and offline PCM acceptance.

Usage: verify-channel-loops-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
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

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
LOOP, BURN = "pattern.channelLoop", "pattern.burnChannelLoop"


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
    guards = load_module("channel_loop_output_admission", guards_path)
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
              "scope": "Actual canonical Channel loop/Burn/recipes/history, current-schema project I/O and independent offline PCM",
              "limits": ["No physical or virtual live callbacks are manufactured by this no-device stdio session",
                         "Armed Burn refusal tested; playback/taking refusal requires the separate active-device fixture",
                         "Duplicate persisted loop entries tested; JSON duplicate keys are not representable by the shared client"]}
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
        client = load_module("channel_loop_neutral_client", client_path).VibeRaClient(app)
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
                  protected() == before, "Structured refusal retains exact score/loops/history: " + str(args))
            return response

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

        def export(name):
            path = attempt / name
            project_job("export", path=str(path), overwrite=False)
            with wave.open(str(path), "rb") as wav:
                fmt = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes())
                pcm = wav.readframes(wav.getnframes())
            check(fmt == (2, 3, 48000, 192000) and len(pcm) == fmt[3] * 6,
                  "Actual eight-beat Song export has exact stereo 48kHz/24bit duration")
            peak = max(abs(int.from_bytes(pcm[i:i + 3], "little", signed=True)) for i in range(0, len(pcm), 3))
            check(peak > 83886, "Actual built-in sample score exports nontrivial PCM")
            report.setdefault("exports", []).append({"path": str(path), "sha256": digest(path),
                "pcmSha256": hashlib.sha256(pcm).hexdigest(), "format": fmt, "linearPeak": peak / 8388608})
            return fmt, pcm

        contracts = {}
        for name in (LOOP, BURN):
            contract = data("vibera_commands", {"command": name, "includeSchemas": True})["operations"][0]
            schema = contract["arguments"]
            check(contract["supported"] and contract["documentEdit"] and schema["additionalProperties"] is False and
                  schema["required"] == (["pattern", "channel", "text"] if name == LOOP else ["pattern", "channel"]),
                  "Actual strict stable Pattern/Channel discovery: " + name)
            contracts[name] = contract
        loop_schema = contracts[LOOP]["arguments"]["properties"]
        check(loop_schema["text"]["enum"] == ["off", "fixed", "step", "beat", "bar"] and
              loop_schema["length"]["multipleOf"] == .25 and loop_schema["length"]["default"] == .25,
              "Actual fixed sixteenth-grid/default and automatic modes are discoverable")
        (attempt / "contracts.json").write_text(json.dumps(contracts, indent=2) + "\n", encoding="utf-8")
        project_job("new", template="empty", discardChanges=True)
        initial = state()
        pattern, channel, other = initial["selectedPattern"], initial["channels"][0]["id"], initial["channels"][1]["id"]
        command("project.tempo", value=120)
        command("pattern.length", pattern=pattern, length=8)
        for owner, beat, length, pitch in ((channel, 0, .125, 60), (channel, .5, .25, 67),
                                           (channel, 9, .25, 73), (other, 1.25, .25, 48)):
            command("notes.add", pattern=pattern, channel=owner, beat=beat, length=length, pitch=pitch, value=.5)
        # notes.add legitimately grows the source Pattern. Explicitly shorten
        # afterward so beat9 is the intended preserved out-of-window raw note.
        command("pattern.length", pattern=pattern, length=8)
        command("clip.add", pattern=pattern, beat=0, length=8)

        def loop_state():
            visible = state()["patterns"]
            inspected = items("patterns", page_size=1)
            check({row["id"]: row["channelLoops"] for row in inspected} ==
                  {row["id"]: row["channelLoops"] for row in visible},
                  "Actual paginated Pattern loop metadata equals canonical state for every Pattern")
            return next(row for row in visible if row["id"] == pattern)["channelLoops"]

        def set_loop(mode, length=None):
            return command(LOOP, pattern=pattern, channel=channel, text=mode,
                           **({"length": length} if length is not None else {}))

        raw = items("notes", pattern=pattern)
        for meter, expected in (((4, 4), {"step": .75, "beat": 1, "bar": 4}),
                                ((7, 8), {"step": .75, "beat": 1, "bar": 3.5})):
            command("project.meter", numerator=meter[0], denominator=meter[1])
            for mode, resolved in expected.items():
                set_loop(mode)
                check(loop_state() == [{"channel": channel, "mode": mode, "length": 1, "resolvedLength": resolved}] and
                      items("notes", pattern=pattern) == raw,
                      "Automatic mode rounds actual active end in exact meter without materializing notes: " + str((meter, mode)))
        set_loop("off")
        command("project.meter", numerator=4, denominator=4)
        set_loop("fixed")
        check(loop_state()[0]["resolvedLength"] == .25, "Omitted fixed length uses exact quarter-beat default")
        unchanged = protected()
        set_loop("fixed")
        check(protected() == unchanged, "Repeated setter never duplicates Channel loop or creates history")
        set_loop("off")
        command("pattern.length", pattern=pattern, length=1000000)
        refuse(LOOP, {"pattern": pattern, "channel": channel, "text": "fixed", "length": .25})
        command("edit.undo")
        check(next(row for row in state()["patterns"] if row["id"] == pattern)["length"] == 8,
              "Capacity refusal retains prior exact Pattern-length Undo")
        for args in ({}, {"pattern": pattern}, {"pattern": pattern, "channel": channel},
                     {"pattern": 999999999, "channel": channel, "text": "beat"},
                     {"pattern": pattern, "channel": True, "text": "beat"},
                     {"pattern": pattern, "channel": 999999999, "text": "beat"},
                     {"pattern": pattern, "channel": channel, "text": "cycle"},
                     {"pattern": pattern, "channel": channel, "text": "fixed", "length": .3},
                     {"pattern": pattern, "channel": channel, "text": "fixed", "length": 0},
                     {"pattern": pattern, "channel": channel, "text": "fixed", "length": True},
                     {"pattern": pattern, "channel": channel, "text": "step", "ids": [channel, channel]},
                     {"pattern": pattern, "channel": channel, "text": "beat", "unexpected": 1}):
            refuse(LOOP, args)
        refuse(BURN, {"pattern": pattern, "channel": channel})
        for args in ({}, {"pattern": pattern}, {"pattern": pattern, "channel": channel, "text": "fixed"}):
            refuse(BURN, args)
        set_loop("fixed", .75)
        command("edit.undo")
        before_batch = protected()
        failed = call("vibera_batch", {"commands": [
            {"command": LOOP, "args": {"pattern": pattern, "channel": channel, "text": "fixed", "length": .5}},
            {"command": LOOP, "args": {"pattern": pattern, "channel": 999999999, "text": "step"}}]}, True)
        after_batch = protected()
        check(failed.get("isError") is True and after_batch["revision"] > before_batch["revision"] and
              {key: value for key, value in after_batch.items() if key != "revision"} ==
              {key: value for key, value in before_batch.items() if key != "revision"},
              "Failed batch preview/rollback advances revision but preserves exact document, dirty state and history")
        command("edit.redo")
        check(loop_state()[0]["resolvedLength"] == .75, "Failed batch preserves genuine loop Redo")
        set_loop("off")

        config = data("vibera_macros", {"action": "state", "includeSchemas": True})
        recipe = {"id": "macro.channel-loop-proof", "name": "Channel loop proof", "steps": [
            {"action": LOOP, "parameters": {"pattern": pattern, "channel": channel, "text": "fixed", "length": .75},
             "currentContext": False}]}
        saved_recipe = data("vibera_macros", {"action": "create", "generation": config["generation"], "macro": recipe})
        check(saved_recipe["configurationCommitted"] and saved_recipe["profileMatchesMemory"], "Actual typed loop recipe persists")
        before_recipe = document()
        run = data("vibera_macros", {"action": "run", "id": recipe["id"], "generation": saved_recipe["generation"]})
        check(run["completionState"] == "completed" and loop_state()[0]["resolvedLength"] == .75,
              "Saved recipe runs the same stable-target loop command")
        command("edit.undo")
        check(document() == before_recipe, "Loop recipe has one exact canonical Undo")
        config = data("vibera_macros", {"action": "state"})
        data("vibera_macros", {"action": "remove", "id": recipe["id"], "generation": config["generation"]})

        # A dormant raw note inside the Pattern and another outside it are distinct:
        # looping hides both; Burn removes only the dormant in-window one.
        command("notes.add", pattern=pattern, channel=channel, beat=1.5, length=.25, pitch=72, value=.5)
        # Auto-growth scans all retained notes, including beat9, even when the
        # newly inserted note is inside the intended eight-beat window.
        command("pattern.length", pattern=pattern, length=8)
        baseline = document()
        base_notes = baseline["notes"]
        base_file = attempt / "raw-eight-beat-score.vibra"
        project_job("save", path=str(base_file), overwrite=False)
        seeds = [note for note in base_notes if note["channel"] == channel and note["beat"] < .75]
        expected = [dict(note) for note in base_notes if note["channel"] != channel or note["beat"] >= 8]
        occurrences = []
        for note in seeds:
            for repetition in range(11):
                beat = note["beat"] + repetition * .75
                if beat < 8:
                    occurrences.append({**note, "beat": beat, "playbackBeat": beat})
        expected += occurrences
        check(len(occurrences) == 21, "Independent literal .75-period reference has exactly21 in-window notes")
        command("notes.delete", pattern=pattern, ids=[note["id"] for note in base_notes
                                                     if note["channel"] == channel and note["beat"] < 8])
        for note in occurrences:
            command("notes.add", pattern=pattern, channel=channel, beat=note["beat"], length=note["length"],
                    pitch=note["pitch"], value=note["velocity"])
        command("pattern.length", pattern=pattern, length=8)
        reference = export("independent-explicit-notes.wav")
        project_job("open", path=str(base_file), discardChanges=True)
        check(document() == baseline, "Fresh open restores original raw IDs/notes/placements before enabling loop")
        before_loop = document()
        set_loop("fixed", .75)
        looped = document()
        check(looped["notes"] == base_notes and looped["placements"] == baseline["placements"] and
              loop_state() == [{"channel": channel, "mode": "fixed", "length": .75, "resolvedLength": .75}],
              "Loop publishes metadata only; source note IDs and placements remain exact")
        # Piano preview is a read-only projection, never a materialized score.
        preview = items("noteOccurrences", page_size=5, pattern=pattern)
        source = next(note for note in base_notes if note["channel"] == channel and note["beat"] == .5)
        source_id = source["id"]
        raw_ids = {note["id"] for note in base_notes}
        check(len(preview) == 24 and all(row["id"] == row["sourceId"] and row["id"] in raw_ids for row in preview),
              "Paginated Piano preview has literal24 rows and only genuine source IDs")
        check([row["beat"] for row in preview if row["sourceId"] == source_id and row["repeated"]] ==
              [1.25, 2, 2.75, 3.5, 4.25, 5, 5.75, 6.5, 7.25] and
              all(row["sourceBeat"] == .5 and row["sourceLength"] == .25
                  for row in preview if row["sourceId"] == source_id) and
              sorted(row["beat"] for row in preview if row["dormant"]) == [1.5, 9],
              "Piano repetition geometry and dormant originals agree with the independent literal oracle")
        preview_guard = protected()
        data("vibera_select", {"pattern": pattern, "channel": channel, "scope": 1})
        range_args = dict(action="range", target="notes", start=1.25, end=3, low=67, high=67, loopPreview=True)
        selected = data("vibera_selection", range_args)
        check(selected["ids"] == [source_id] and selected["documentChanged"] is False and protected() == preview_guard,
              "Default starts selection deduplicates appearances without changing document or history")
        overlap_args = {**range_args, "start": 1.3, "end": 1.4}
        check(data("vibera_selection", overlap_args)["ids"] == [] and
              data("vibera_selection", {**overlap_args, "criterion": "overlaps"})["ids"] == [source_id],
              "Occurrence starts and body overlap retain distinct half-open semantics")
        for invalid in ({**range_args, "loopPreview": 1}, {**range_args, "target": "clips"},
                        {**range_args, "revision": max(0, state()["revision"] - 1)},
                        dict(action="all", target="notes", loopPreview=True)):
            before = protected()
            failed = call("vibera_selection", invalid, True)
            check(failed.get("isError") is True and bool(failed["structuredContent"]["error"]["code"]) and
                  protected() == before, "Invalid/stale occurrence selection refuses atomically")
        command("notes.move", pattern=pattern, channel=channel, ids=[source_id], beat=-.25, length=0, pitch=0)
        moved_preview = items("noteOccurrences", pattern=pattern)
        check(next(note for note in items("notes", pattern=pattern) if note["id"] == source_id)["beat"] == .25 and
              [row["beat"] for row in moved_preview if row["sourceId"] == source_id and row["iteration"] == 1] == [1],
              "Canonical source delta updates all linked appearances without allocating copies")
        moved = document()
        command("edit.undo")
        check(document() == looped, "Occurrence-source move has one exact Undo")
        command("edit.redo")
        check(document() == moved, "Occurrence-source move has exact Redo")
        command("edit.undo")
        line = dict(startBeat=1.375, endBeat=1.375, startPitch=66.5, endPitch=67.5,
                    grid=0, loopPreview=True)
        token = data("vibera_slice", {"action": "begin"})["operation"]
        planned = data("vibera_slice", {"action": "update", "operation": token, "slice": line, "ids": [source_id]})
        check(planned["completionState"] == "pending" and len(planned["items"]) == 1 and
              planned["items"][0]["left"] == source_id and planned["items"][0]["right"] == 0 and
              planned["items"][0]["beat"] == .625 and document() == looped,
              "Actual display-space Slice preview maps1.375 to source.625 without mutation")
        cancelled = data("vibera_slice", {"action": "cancel", "operation": token})
        check(cancelled["completionState"] == "cancelled" and document() == looped,
              "Actual display-space Slice cancellation retains exact raw score")
        command("notes.slice", pattern=pattern, channel=channel, ids=[source_id], slice=line)
        sliced_notes = items("notes", pattern=pattern)
        added = [note for note in sliced_notes if note["id"] not in raw_ids]
        check(len(added) == 1 and added[0]["beat"] == .625 and added[0]["length"] == .125 and
              next(note for note in sliced_notes if note["id"] == source_id)["length"] == .125,
              "Canonical repeated Slice creates exactly one real source segment at the mapped cut")
        command("edit.undo")
        check(document() == looped, "Repeated Slice one Undo restores source IDs, loops and placements")
        command("notes.move", pattern=pattern, channel=channel, ids=[source_id], beat=0, length=1, pitch=0)
        ambiguous = refuse("notes.slice", dict(pattern=pattern, channel=channel, ids=[source_id], slice=line))
        check("loopPreview.ambiguousSlice" in ambiguous["structuredContent"]["error"]["message"],
              "Overlapping appearances report the specific ambiguous-source reason")
        command("edit.undo")
        check(document() == looped, "Ambiguous overlapping Slice refusal preserves the actual resize Undo")
        refuse("notes.slice", dict(pattern=pattern, channel=channel, ids=[source_id], slice={**line, "loopPreview": 1}))
        check(protected()["document"] == preview_guard["document"], "Preview workflows leave the original export fixture intact")
        rack = data("vibera_rack", {"action": "state"})
        row = next(row for row in rack["rows"] if row["id"] == channel)
        check(row["loopMode"] == "fixed" and row["loopLength"] == .75, "Actual Rack readback agrees with per-Pattern loop")
        command("edit.undo")
        check(document() == before_loop, "Loop enable has one exact Undo")
        command("edit.redo")
        check(document() == looped, "Loop enable exact Redo")
        # Source editing uses the absolute displayed column. Repeated playback
        # does not redirect a click to an earlier source cell. The installed
        # dispatch/writer study corrects the earlier unqualified modulo oracle.
        command("steps.set", pattern=pattern, channel=channel, beat=1.75, enabled=True)
        step_added = document()
        extra = [note for note in step_added["notes"] if note["id"] not in {row["id"] for row in base_notes}]
        check(len(extra) == 1 and extra[0]["channel"] == channel and extra[0]["step"] is True and
              extra[0]["beat"] == 1.75 and extra[0]["length"] == .25 and
              all(note in step_added["notes"] for note in base_notes) and
              step_added["patterns"][0]["channelLoops"] == looped["patterns"][0]["channelLoops"] and
              step_added["placements"] == looped["placements"],
              "Later displayed column creates its own source Step and retains every prior ID/loop/placement")
        before_same_step = protected()
        command("steps.set", pattern=pattern, channel=channel, beat=1.75, enabled=True)
        check(protected() == before_same_step, "Repeated activation of the same source cell is an exact no-op")
        command("steps.set", pattern=pattern, channel=channel, beat=4.75, enabled=True)
        separate_step = document()
        later = [note for note in separate_step["notes"] if note["id"] not in {row["id"] for row in step_added["notes"]}]
        check(len(later) == 1 and later[0]["beat"] == 4.75 and
              all(note in separate_step["notes"] for note in step_added["notes"]),
              "A different repeated position gets its own source identity without changing prior notes")
        command("edit.undo")
        check(document() == step_added, "Separate later source insertion has exact independent Undo")
        command("edit.undo")
        check(document() == looped, "One repetition Step Undo restores exact original score IDs and loop")
        command("edit.redo")
        check(document() == step_added, "Repetition Step Redo restores the same allocated raw Step ID")
        command("edit.undo")
        check(document() == looped, "Original independent PCM fixture restored after Step history proof")
        check(export("channel-loop.wav") == reference, "Actual looped Song WAV equals independently materialized note reference byte-for-byte")
        command("transport.record")
        check(state()["recordArmed"] is True, "Actual shared transport arm admitted without fabricating a take")
        refuse(BURN, {"pattern": pattern, "channel": channel})
        check(state()["recordArmed"] is True and not state()["recordingTake"], "Burn refusal preserves armed/no-take state")
        command("transport.record")
        check(state()["recordArmed"] is False, "Owned recording arm restored before Burn")
        command(BURN, pattern=pattern, channel=channel)
        burned = document()
        canonical = lambda rows: sorted([{key: value for key, value in row.items() if key != "id"} for row in rows],
                                        key=lambda row: (row["channel"], row["beat"], row["pitch"]))
        check(loop_state() == [] and canonical(burned["notes"]) == canonical(expected) and
              burned["placements"] == baseline["placements"], "Burn materializes exact raw repetitions, retaining other part/out-of-window notes")
        by_id = {note["id"]: note for note in burned["notes"]}
        retained = [note for note in base_notes if note["beat"] < .75 or note["channel"] != channel or note["beat"] >= 8]
        check(all(by_id.get(note["id"]) == note for note in retained) and
              len(by_id) == len(burned["notes"]) and
              not {note["id"] for note in base_notes if note["channel"] == channel and .75 <= note["beat"] < 8}.intersection(by_id),
              "Burn preserves genuine first-cycle/unaffected IDs, removes dormant note, and allocates distinct repetitions")
        check(export("burned-channel-loop.wav") == reference, "Actual Burn WAV equals both loop and independent reference exactly")
        command("edit.undo")
        check(document() == looped, "One Burn Undo restores exact dormant notes, source IDs and loop")
        command("edit.redo")
        check(document() == burned, "Burn Redo restores exact materialized repetition IDs")
        command("edit.undo")
        project = attempt / "channel-loops.vibra"
        project_job("save", path=str(project), overwrite=False)
        xml = ET.parse(project)
        source_contract = load_module("channel_loop_schema_contract", ROOT / "scripts/dev_support.py")
        check(int(xml.getroot().attrib["version"]) == source_contract.project_schema(ROOT),
              "Actual project persists the authoritative current schema with Channel-loop data")
        project_job("new", discardChanges=True)
        project_job("open", path=str(project), discardChanges=True)
        check(document() == looped and not state()["dirty"], "Save/new/reopen preserves complete raw score, loop, placement and source identities")
        # Reject duplicate persisted ownership rather than silently taking first/last.
        element = next(row for row in xml.getroot().iter("PATTERN") if int(row.attrib["id"]) == pattern)
        element.append(copy.deepcopy(element.find("CHANNEL_LOOP")))
        malformed = attempt / "duplicate-channel-loop.vibra"
        xml.write(malformed, encoding="utf-8", xml_declaration=True)
        before_bad_open = protected()
        initial_error = call("vibera_project", {"action": "open", "path": str(malformed), "discardChanges": True}, True)["structuredContent"]
        wait_job(initial_error, "failed")
        check(protected() == before_bad_open, "Duplicate loop owner fails project open without replacing canonical document/history")
        blocked = attempt / "blocked-parent"
        blocked.write_text("Owned async export failure fixture\n", encoding="utf-8")
        before_failure = protected()
        wait_job(call("vibera_project", {"action": "export", "path": str(blocked / "failure.wav"), "overwrite": False}, True)["structuredContent"], "failed")
        check(protected() == before_failure, "Actual asynchronous export failure retains exact looped project/history")
        report.update(project=str(project), projectSha256=digest(project), pattern=pattern, channel=channel,
                      originalNoteIds=[note["id"] for note in base_notes], pcmExported=True)
        client.close()
        check(client.process.returncode == 0, "Owned disposable MCP process closes gracefully")
        check(digest(app) == report["applicationSha256"] and digest(worker) == report["workerSha256"] and
              digest(client_path) == report["clientSha256"] and digest(Path(__file__)) == report["scriptSha256"] and
              digest(guards_path) == report["outputAdmissionSha256"], "Exact verifier/application/worker/client/guard inputs remain unchanged")
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
