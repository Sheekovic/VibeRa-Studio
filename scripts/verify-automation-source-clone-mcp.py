"""Actual independent Automation source clone/history/persistence stdio workflow.

Usage: verify-automation-source-clone-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
Uses a disposable --mcp-test session and the existing executable-derived output
admission. Exports actual PCM, but makes no live audio, listening or hardware claim.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import traceback
import wave

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
CLONE = "automation.source.clone"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Existing helper unavailable: " + str(path))
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
    guards = load_module("clone_output_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
    output = guards.owned_output_path(sys.argv[2], app)
    guards.ordinary(app, False)
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    worker = app.parent / "VibeRa Plugin Worker.exe"
    client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
    if not client_path.is_file():
        client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
    receipt = {"passed": False, "finished": False, "attemptDirectory": str(attempt),
               "checks": [], "operations": [], "responses": 0,
               "application": str(app), "applicationSha256": digest(app),
               "scriptSha256": digest(Path(__file__)),
               "outputAdmissionSha256": digest(ROOT / "scripts/verify-plugin-io-mcp.py"),
               "clientSha256": digest(client_path),
               "scope": "Canonical independent source clones, actual history/macros/project jobs and unchanged exported Song PCM",
               "nativeInput": False, "hardware": False, "listening": False, "liveAudio": False,
               "pcmExported": False, "macroReplay": False}
    started, client = time.monotonic(), None

    def retain():
        text = json.dumps(receipt, indent=2, ensure_ascii=False) + "\n"
        (attempt / "verification.json").write_text(text, encoding="utf-8")
        (output / "verification.json").write_text(text, encoding="utf-8")

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        receipt["checks"].append(message)

    retain()
    try:
        guards.ordinary(worker, False)
        receipt["workerSha256"] = digest(worker)
        client = load_module("clone_neutral_client", client_path).VibeRaClient(app)
        receipt.update(pid=client.process.pid, version=client.server.get("version"))

        def call(tool, args=None, allow_failure=False):
            response = client.call(tool, args or {}, allow_failure=True)
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "args": args or {}, "response": response}, ensure_ascii=False) + "\n")
            receipt["responses"] += 1
            if response.get("isError") and not allow_failure:
                raise AssertionError("Unexpected MCP refusal: " + str(response))
            return response

        def data(tool, args=None):
            return call(tool, args)["structuredContent"]

        def command(name, **args):
            result = data("vibera_command", {"command": name, "args": args})
            check(result.get("completionState") != "pending", "Canonical source/history command is not accepted-only: " + name)
            return result

        state = lambda: data("vibera_state")

        def items(entity, **args):
            rows, offset = [], 0
            while True:
                page = data("vibera_inspect", {"entity": entity, **args, "offset": offset, "limit": 1})
                check(len(page["items"]) <= 1 and page["total"] >= len(rows) + len(page["items"]),
                      "Actual bounded page retains complete row count: " + entity)
                rows.extend(page["items"])
                next_offset = page["nextOffset"]
                if next_offset is None:
                    check(len(rows) == page["total"], "Actual pagination covers every row: " + entity)
                    return rows
                check(type(next_offset) is int and next_offset == offset + len(page["items"]) and next_offset > offset,
                      "Actual pagination advances by exactly the returned rows: " + entity)
                offset = next_offset

        def snapshot():
            sources = items("automationSources")
            return {"sources": sources,
                    "points": {str(row["id"]): items("automationPoints", source=row["id"]) for row in sources},
                    "links": {str(row["id"]): items("automationLinks", source=row["id"]) for row in sources},
                    "initial": items("automationInitialValues"), "placements": items("clips")}

        def protected():
            current = state()
            return {"automation": snapshot(), **{key: current[key] for key in
                    ("projectIdentity", "revision", "dirty", "undoAvailable", "redoAvailable")}}

        def wait_job(initial, expected="completed", seconds=40):
            current, began = initial, time.monotonic()
            try:
                check(initial.get("completionState") in ("pending", expected), "Actual file job has explicit admission/terminal state")
                while current.get("completionState") == "pending" and time.monotonic() - began < seconds:
                    time.sleep(.03)
                    current = call("vibera_project", {"action": "job", "operation": initial["operation"]}, True)["structuredContent"]
                    check(current.get("operation") == initial["operation"], "Actual file job owner token remains exact")
                check(current.get("completionState") == expected, "Actual file job reaches " + expected)
                if expected == "failed":
                    check(bool(current.get("error", {}).get("code")), "Failed job retains structured error")
                return current
            finally:
                receipt["operations"].append({"initial": initial, "terminal": current, "seconds": time.monotonic() - began})
                retain()

        def export(path):
            wait_job(data("vibera_project", {"action": "export", "path": str(path), "overwrite": False}))
            with wave.open(str(path), "rb") as wav:
                fmt = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes())
                pcm = wav.readframes(wav.getnframes())
            check(fmt[:3] == (2, 3, 48000) and fmt[3] > 0 and len(pcm) == fmt[3] * 6,
                  "Actual export contains complete stereo 48kHz/24bit Song PCM")
            receipt.setdefault("exports", []).append({"path": str(path), "sha256": digest(path),
                                                       "format": fmt, "pcmSha256": hashlib.sha256(pcm).hexdigest()})
            return fmt, pcm

        tools = {tool["name"]: tool for tool in client.request("tools/list", {})["tools"]}
        check({"vibera_command", "vibera_commands", "vibera_inspect", "vibera_project", "vibera_batch"} <= tools.keys(),
              "Actual discovery exposes canonical command/readback/history/file services")
        contract = data("vibera_commands", {"command": CLONE, "includeSchemas": True})["operations"][0]
        schema, result_schema = contract["arguments"], contract["resultSchema"]
        check(contract["supported"] and contract["asynchronous"] is False and contract["macroAvailable"] and
              schema["additionalProperties"] is False and schema["required"] == ["target"] and
              schema["properties"]["target"]["minimum"] == 1 and
              schema["properties"]["text"]["enum"] == ["clear", "preserve"] and
              schema["properties"]["text"]["default"] == "clear" and
              result_schema["properties"]["completionScope"]["const"] == "canonicalAutomationSource" and
              result_schema["properties"]["dspSettlingAcknowledged"]["const"] is False,
              "Actual source-clone discovery has strict stable ID/policy and truthful synchronous result schema")
        (attempt / "clone-contract.json").write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")
        wait_job(data("vibera_project", {"action": "new", "template": "empty", "discardChanges": True}))
        initial_state = state()
        pattern, channel = initial_state["selectedPattern"], initial_state["channels"][0]["id"]
        for beat, pitch in ((0, 60), (1, 64), (2, 67)):
            command("notes.add", pattern=pattern, channel=channel, beat=beat, length=.25, pitch=pitch, value=.5)
        command("clip.add", pattern=pattern, beat=0, length=4)
        command("automation.create", mixer=1, automationControl="pan", beat=0, length=4, lane=2)
        source = items("automationSources")[0]["id"]
        command("automation.source.rename", target=source, text="Independent shape")
        command("automation.link", target=source, mixer=1, automationControl="stereoSeparation")
        command("automation.range", target=source, mixer=1, automationControl="stereoSeparation", beat=1, length=-1)
        command("automation.link", target=source, mixer=2, automationControl="pan")
        command("automation.range", target=source, mixer=2, automationControl="pan", beat=.75, length=-.75)
        points = items("automationPoints", source=source)
        command("automation.point.move", target=points[0]["id"], beat=0, value=.125)
        command("automation.point.move", target=points[-1]["id"], beat=4, value=.875)
        command("automation.point.add", target=source, beat=2, value=.625)
        middle = items("automationPoints", source=source)[1]
        command("automation.point.shape", target=middle["id"], text="smooth", value=.375)
        command("automation.init", mixer=2, automationControl="pan", value=-.25)
        original = snapshot()
        original_points = original["points"][str(source)]
        original_links = original["links"][str(source)]
        check(original_links == [dict(kind=1, owner=1, parameter=0, minimum=-1, maximum=1),
                                 dict(kind=5, owner=1, parameter=0, minimum=1, maximum=-1),
                                 dict(kind=1, owner=2, parameter=0, minimum=.75, maximum=-.75)],
              "Original source has exact multi-insert typed targets and independently inverted ranges")
        notes = items("notes", pattern=pattern)
        reference_pcm = export(attempt / "before-unused-clones.wav")

        def clone(policy=None):
            before, revision = snapshot(), state()["revision"]
            result = command(CLONE, target=source, **({} if policy is None else {"text": policy}))
            created = result.get("createdSource")
            check(result.get("completionState") == "completed" and
                  result.get("completionScope") == "canonicalAutomationSource" and
                  result.get("linkPolicy") == (policy or "clear") and result.get("changed") is True and
                  result.get("dspSettlingAcknowledged") is False and type(created) is int and created > 0 and
                  state()["revision"] == revision + 1,
                  "Actual clone response identifies one committed source without DSP acknowledgement")
            after = snapshot()
            new = next(row for row in after["sources"] if row["id"] == created)
            old = next(row for row in before["sources"] if row["id"] == source)
            check(new == {**old, "id": created, "name": old["name"] + " - Clone",
                          "linkCount": old["linkCount"] if policy == "preserve" else 0},
                  "Clone copies source settings/name suffix with the exact selected link policy")
            new_points = after["points"][str(created)]
            old_ids = ({row["id"] for rows in before["points"].values() for row in rows} |
                       {row["id"] for row in before["sources"]})
            check(len(new_points) == len(original_points) and
                  created not in old_ids and created not in {row["id"] for row in new_points} and
                  len({row["id"] for row in new_points}) == len(new_points) and
                  not old_ids.intersection(row["id"] for row in new_points) and
                  all({key: value for key, value in p.items() if key != "id"} ==
                      {key: value for key, value in q.items() if key != "id"}
                      for p, q in zip(new_points, original_points)),
                  "Every cloned point has a fresh identity and exact beat/value/shape/tension")
            check(after["links"][str(created)] == (original_links if policy == "preserve" else []) and
                  after["initial"] == before["initial"] and after["placements"] == before["placements"] and
                  all(after["points"][str(row["id"])] == before["points"][str(row["id"])] and
                      after["links"][str(row["id"])] == before["links"][str(row["id"])] for row in before["sources"]) and
                  after["sources"][:-1] == before["sources"],
                  "Clone retains all originals, placements and initialization; preserved links target exact owners")
            command("edit.undo")
            check(snapshot() == before, "Source clone has one exact shared Undo")
            command("edit.redo")
            check(snapshot() == after, "Source clone has exact shared Redo including allocated IDs")
            return created

        cleared = clone()
        preserved = clone("preserve")
        check(cleared != preserved and preserved != source and cleared != source,
              "Repeated clear/preserve clones own independent stable source IDs")
        command("automation.point.move", target=items("automationPoints", source=preserved)[1]["id"], beat=2, value=.25)
        check(items("automationPoints", source=source) == original_points and
              items("automationPoints", source=cleared)[1]["value"] == .625,
              "Editing the preserved clone's curve cannot alter original or clear clone")
        command("edit.undo")
        cloned = snapshot()

        for args in ({}, {"target": 0}, {"target": -1}, {"target": True}, {"target": 1000000001},
                     {"target": 999999999}, {"target": original_points[0]["id"]},
                     {"target": source, "text": "linked"}, {"target": source, "text": ""},
                     {"target": source, "text": True}, {"target": source, "beat": 0},
                     {"target": source, "unexpected": 1}):
            before = protected()
            response = call("vibera_command", {"command": CLONE, "args": args}, True)
            error = response.get("structuredContent", {}).get("error", {})
            check(response.get("isError") is True and bool(error.get("code")) and bool(error.get("message")) and
                  protected() == before, "Typed invalid source/policy/foreign fields refuse atomically: " + str(args))
        command("edit.undo")  # Remove preserve clone; retain its genuine Redo edge.
        before_batch = protected()
        failed = call("vibera_batch", {"commands": [
            {"command": CLONE, "args": {"target": source, "text": "preserve"}},
            {"command": CLONE, "args": {"target": 999999999}}]}, True)
        after_batch = protected()
        # The existing gesture contract publishes preview + rollback revisions to invalidate stale
        # clients. Project/history/dirty must restore exactly; a failed batch is not revision-neutral.
        check(failed.get("isError") is True and bool(failed["structuredContent"].get("error", {}).get("code")) and
              after_batch["revision"] > before_batch["revision"] and
              {key: value for key, value in after_batch.items() if key != "revision"} ==
              {key: value for key, value in before_batch.items() if key != "revision"},
              "Later invalid clone restores exact document/history/dirty while preview rollback advances revision")
        command("edit.redo")
        check(snapshot() == cloned, "Failed batch retains exact pre-existing source-clone Redo")

        config = data("vibera_macros", {"action": "state", "includeSchemas": True})
        check(CLONE in {row["id"] for row in config["supportedCommands"]}, "Actual saved recipes support canonical source cloning")
        recipe = {"id": "macro.source-clone-proof", "name": "Independent source clone", "steps": [
            {"action": CLONE, "parameters": {"target": source, "text": "preserve"}, "currentContext": False}]}
        configured = data("vibera_macros", {"action": "create", "generation": config["generation"], "macro": recipe})
        check(configured["configurationCommitted"] and configured["profileMatchesMemory"] and snapshot() == cloned,
              "Owned clone recipe persists without changing the musical document")
        run = data("vibera_macros", {"action": "run", "id": recipe["id"], "generation": configured["generation"]})
        macro_state = snapshot()
        macro_source = macro_state["sources"][-1]["id"]
        check(run["completionState"] == "completed" and len(macro_state["sources"]) == len(cloned["sources"]) + 1 and
              macro_state["links"][str(macro_source)] == original_links and
              macro_state["placements"] == cloned["placements"] and macro_state["initial"] == cloned["initial"],
              "Actual saved recipe creates one independent source retaining exact links and no placement")
        command("edit.undo")
        check(snapshot() == cloned, "Actual clone recipe has one exact Undo")
        command("edit.redo")
        check(snapshot() == macro_state, "Actual clone recipe has exact Redo")
        config = data("vibera_macros", {"action": "state"})
        data("vibera_macros", {"action": "remove", "id": recipe["id"], "generation": config["generation"]})
        receipt["macroReplay"] = True

        project = attempt / "independent-automation-sources.vibra"
        wait_job(data("vibera_project", {"action": "save", "path": str(project), "overwrite": False}))
        check(project.is_file() and not state()["dirty"], "Actual save completes and marks the exact document clean")
        wait_job(data("vibera_project", {"action": "new", "discardChanges": True}))
        wait_job(data("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
        check(snapshot() == macro_state and items("notes", pattern=pattern) == notes and not state()["dirty"],
              "Actual new/reopen restores exact independent curves, identities, links, placements, init and notes")
        check(export(attempt / "after-unused-clones.wav") == reference_pcm,
              "Unused clear/preserved/macro clones leave actual exported Song format and PCM byte-identical")
        receipt["pcmExported"] = True
        blocked = attempt / "blocked-parent"
        blocked.write_text("Owned asynchronous export failure fixture\n", encoding="utf-8")
        before_failure = protected()
        wait_job(call("vibera_project", {"action": "export", "path": str(blocked / "fail.wav"), "overwrite": False}, True)["structuredContent"], "failed")
        check(protected() == before_failure, "Actual failed export leaves cloned document/history/clean state intact")
        receipt.update(project=str(project), projectSha256=digest(project), originalSource=source,
                       clearSource=cleared, preservedSource=preserved, macroSource=macro_source)
        client.close()
        check(client.process.returncode == 0, "Owned disposable MCP process closes gracefully")
        check(digest(app) == receipt["applicationSha256"] and digest(worker) == receipt["workerSha256"] and
              digest(client_path) == receipt["clientSha256"] and digest(Path(__file__)) == receipt["scriptSha256"] and
              digest(ROOT / "scripts/verify-plugin-io-mcp.py") == receipt["outputAdmissionSha256"],
              "Exact application/worker/client/verifier/output-admission inputs remain unchanged")
        receipt["passed"] = True
    except Exception as failure:
        receipt.update(error=str(failure), traceback=traceback.format_exc())
    finally:
        try:
            if client and client.process.poll() is None:
                client.close()
        except Exception as failure:
            receipt.update(passed=False, closeError=str(failure))
        finally:
            if client:
                receipt["exitCode"] = client.process.returncode
                (attempt / "stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
            receipt.update(finished=True, elapsedSeconds=time.monotonic() - started)
            retain()
    print(json.dumps({key: receipt.get(key) for key in ("passed", "finished", "elapsedSeconds", "error", "attemptDirectory")}))
    return 0 if receipt["passed"] and receipt.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
