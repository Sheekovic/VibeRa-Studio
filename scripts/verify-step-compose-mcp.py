"""Actual stdio Step Composer drafts, preview/cancel, atomic history and offline PCM.

Usage: verify-step-compose-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
Uses the existing disposable --mcp-test client. No native input, hardware,
installed plugin, listening or live-callback qualification.
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

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TOOL = "vibera_stepCompose"


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
    admission_path = ROOT / "scripts/verify-plugin-io-mcp.py"
    admission = load_module("step_compose_output_admission", admission_path)
    output = admission.owned_output_path(sys.argv[2], app)
    admission.ordinary(app, False)
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
    if not client_path.is_file():
        client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
    worker = app.parent / "VibeRa Plugin Worker.exe"
    report = {"passed": False, "finished": False, "checks": [], "responses": 0, "operations": [],
              "attemptDirectory": str(attempt), "application": str(app), "applicationSha256": digest(app),
              "scriptSha256": digest(Path(__file__)), "clientSha256": digest(client_path),
              "outputAdmissionSha256": digest(admission_path), "nativeInput": False, "hardware": False,
              "realPlugin": False, "liveAudio": False, "listening": False, "pcmExported": False,
              "scope": "Actual shared Step Composer/canonical commands, drafts, preview, history, files and built-in offline PCM",
              "limits": ["No playback or callback is fabricated in this no-device stdio session",
                         "Wrong operation ownership and lease expiry tested; unrelated project mutation while owned is refused rather than forced",
                         "Original integer mask oracles are independent of the production mask generator"]}
    client = None
    started = time.monotonic()

    def retain():
        text = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
        (attempt / "verification.json").write_text(text, encoding="utf-8")
        (output / "verification.json").write_text(text, encoding="utf-8")

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        report["checks"].append(message)

    retain()
    try:
        admission.ordinary(worker, False)
        report["workerSha256"] = digest(worker)
        client = load_module("step_compose_neutral_client", client_path).VibeRaClient(app)
        report.update(pid=client.process.pid, version=client.server.get("version"))

        def call(tool, args=None, allow_failure=False):
            response = client.call(tool, args or {}, allow_failure=True)
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "args": args or {}, "response": response}, ensure_ascii=False) + "\n")
            report["responses"] += 1
            if response.get("isError") and not allow_failure:
                raise AssertionError("Unexpected MCP refusal: " + str(response))
            return response

        def data(tool, args=None):
            return call(tool, args)["structuredContent"]

        def command(name, **args):
            result = data("vibera_command", {"command": name, "args": args})
            check(result.get("completionState") != "pending", "Synchronous canonical edit: " + name)
            return result

        def items(entity, **args):
            rows, offset = [], 0
            while True:
                page = data("vibera_inspect", {"entity": entity, **args, "offset": offset, "limit": 16})
                rows.extend(page["items"])
                if page["nextOffset"] is None:
                    check(len(rows) == page["total"], "Complete actual pagination: " + entity)
                    return rows
                check(type(page["nextOffset"]) is int and page["nextOffset"] == offset + len(page["items"]) and
                      page["nextOffset"] > offset, "Actual inspection cursor advances exactly")
                offset = page["nextOffset"]

        def document():
            current = data("vibera_state")
            return {"patterns": current["patterns"], "channels": items("channels"),
                    "notes": items("notes", pattern=pattern), "clips": items("clips"),
                    "tempo": current["tempo"], "numerator": current["numerator"], "denominator": current["denominator"]}

        def history():
            current = data("vibera_state")
            return {key: current[key] for key in ("dirty", "undoAvailable", "redoAvailable")}

        def protected():
            current = data("vibera_state")
            return {"document": document(), "history": history(), "revision": current["revision"],
                    "projectIdentity": current["projectIdentity"]}

        def refuse(tool, args):
            before = protected()
            response = call(tool, args, True)
            error = response.get("structuredContent", {}).get("error", {})
            check(response.get("isError") is True and bool(error.get("code")) and bool(error.get("message")) and
                  protected() == before, "Typed atomic refusal: " + json.dumps(args, sort_keys=True))

        def project_job(action, **args):
            initial = data("vibera_project", {"action": action, **args})
            current, began = initial, time.monotonic()
            try:
                while current.get("completionState") == "pending" and time.monotonic() - began < 40:
                    time.sleep(.03)
                    current = data("vibera_project", {"action": "job", "operation": initial["operation"]})
                    check(current.get("operation") == initial["operation"], "Actual async file operation retains token")
                check(current.get("completionState") == "completed", "Actual file job completed: " + action)
                return current
            finally:
                report["operations"].append({"initial": initial, "terminal": current, "seconds": time.monotonic() - began})
                retain()

        def session(action="state", operation=None, **args):
            request = {"action": action, **args}
            if operation is not None:
                request["operation"] = operation
            result = data(TOOL, request)
            if operation is not None:
                check(result.get("operation") == operation, "Shared Step Composer retains exact session ownership")
            return result

        def recipe(state, channel):
            found = [row for row in state["recipes"] if row["channel"] == channel]
            check(len(found) == 1, "Exactly one draft recipe per stable Channel")
            return found[0]

        def mask(state, channel, expected):
            row = recipe(state, channel)
            check(row["mask"] == expected and len(row["mask"]) == row["sequenceSteps"] and
                  all(type(bit) is int and bit in (0, 1) for bit in row["mask"]),
                  "Independent literal mask readback: " + str(channel))

        def export(name):
            path = attempt / name
            project_job("export", path=str(path), overwrite=False)
            with wave.open(str(path), "rb") as wav:
                fmt = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes())
                pcm = wav.readframes(wav.getnframes())
            check(fmt == (2, 3, 48000, 192000) and len(pcm) == 192000 * 6,
                  "Actual eight-beat stereo48k/24bit export preserves literal duration")
            peak = max(abs(int.from_bytes(pcm[i:i + 3], "little", signed=True)) for i in range(0, len(pcm), 3))
            check(peak > 83886, "Built-in composed score exports nontrivial PCM")
            report.setdefault("exports", []).append({"path": str(path), "sha256": digest(path),
                "pcmSha256": hashlib.sha256(pcm).hexdigest(), "format": fmt, "linearPeak": peak / 8388608})
            return pcm

        tools = client.request("tools/list", {})["tools"]
        tool = next(row for row in tools if row["name"] == TOOL)
        actions = tool["inputSchema"]["properties"]["action"]["enum"]
        check(set(actions) == {"state", "begin", "update", "select", "reset", "preview", "solo", "dice", "manual", "commit", "cancel", "play"},
              "Actual discovery lists every shared Step Composer operation")
        check(tool["inputSchema"]["additionalProperties"] is False,
              "Step Composer tool has closed typed arguments")
        output_schema = tool["outputSchema"]
        check(output_schema["additionalProperties"] is False and
              set(output_schema["required"]) == {"completionState", "playing", "dspSettlingAcknowledged", "pendingAudioUpdate"} and
              set(output_schema["properties"]["completionState"]["enum"]) == {"idle", "pending", "completed", "cancelled"} and
              output_schema["properties"]["recipes"]["items"]["properties"]["mask"]["readOnly"] is True,
              "Discovery types actual session outcomes, audio acknowledgement and read-only final masks")
        recipe_schema = tool["inputSchema"]["properties"]["recipes"]["items"]
        check(recipe_schema["additionalProperties"] is False and recipe_schema["required"] == ["channel"] and
              set(recipe_schema["properties"]) == {"channel", "sequenceSteps", "mode", "every", "pulses",
                  "rotation", "dice", "seed", "loop", "convertMelodic", "manual"} and
              recipe_schema["properties"]["seed"]["maximum"] == 4294967295 and
              recipe_schema["properties"]["sequenceSteps"]["maximum"] == 4096,
              "Recipe schema exposes the exact whitelist, units and full seed/count bounds")
        contract = data("vibera_commands", {"command": "steps.compose", "includeSchemas": True})["operations"][0]
        check(contract["supported"] and contract["documentEdit"], "Direct canonical composition is discoverable")
        (attempt / "contracts.json").write_text(json.dumps({"tool": tool, "command": contract}, indent=2) + "\n", encoding="utf-8")
        project_job("new", template="empty", discardChanges=True)
        initial = data("vibera_state")
        pattern = initial["selectedPattern"]
        first, second, untouched, excluded = [row["id"] for row in initial["channels"][:4]]
        command("project.tempo", value=120)
        command("pattern.length", pattern=pattern, length=8)
        command("notes.add", pattern=pattern, channel=first, beat=.125, length=.5, pitch=60, value=.8)
        command("notes.add", pattern=pattern, channel=first, beat=9, length=.25, pitch=73, value=.5)
        command("notes.add", pattern=pattern, channel=untouched, beat=7.5, length=.25, pitch=60, value=.3, step=True)
        command("clip.add", pattern=pattern, beat=0, length=8)
        baseline, baseline_history = document(), history()
        every = {"channel": first, "sequenceSteps": 16, "mode": "every", "every": 4,
                 "rotation": 1, "dice": False, "seed": 0, "loop": True, "convertMelodic": True}
        euclidean = {"channel": second, "sequenceSteps": 8, "mode": "euclidean", "pulses": 3,
                     "rotation": -1, "dice": False, "seed": 4294967295, "loop": False, "convertMelodic": False}
        every_mask = [int(i in (1, 5, 9, 13)) for i in range(16)]
        euclidean_mask = [int(i in (1, 4, 7)) for i in range(8)]

        # Direct canonical conversion must refuse without borrowing an interactive session.
        no_conversion = {**every, "convertMelodic": False}
        refuse("vibera_command", {"command": "steps.compose", "args": {"pattern": pattern, "recipes": [no_conversion]}})
        command("steps.compose", pattern=pattern, recipes=[every, euclidean])
        direct = document()
        command("edit.undo")
        check(document() == baseline, "Direct canonical composition is one exact Undo")
        command("edit.redo")
        check(document() == direct, "Direct canonical composition Redo retains generated identities")
        command("edit.undo")
        baseline_history = history()
        refuse(TOOL, {"action": "begin", "pattern": pattern, "channels": [first], "projectIdentity": "stale-project"})
        begun = session("begin", pattern=pattern, channels=[first, second, untouched], leaseMs=120000,
                        projectIdentity=data("vibera_state")["projectIdentity"])
        token = begun["operation"]
        check(type(token) is str and token and begun["completionState"] == "pending" and
              begun["preview"] is False and begun["pattern"] == pattern and document() == baseline,
              "Actual dialog service begins pending, preview disabled, without applying drafts")
        initial_recipes = copy.deepcopy(begun["recipes"])
        check(all(row["loop"] is True for row in initial_recipes) and begun["solo"] is False,
              "New shorter drafts default to automatic looping without mutating opening loop metadata")
        refuse(TOOL, {"action": "update", "operation": token, "recipes": [every], "projectIdentity": "stale-project"})
        refuse(TOOL, {"action": "update", "operation": token, "recipes": [no_conversion]})
        check(session("state", token)["recipes"] == initial_recipes,
              "Refused melodic conversion leaves actual opening drafts untouched")
        changed = session("update", token, recipes=[every, euclidean])
        mask(changed, first, every_mask)
        mask(changed, second, euclidean_mask)
        check(document() == baseline and history() == baseline_history, "Preview-disabled multi-Channel updates are drafts only")
        selected = session("select", token, channel=second)
        check(selected["activeChannel"] == second and recipe(selected, first) == recipe(changed, first),
              "Selecting another Channel retains the independent first draft")
        refuse(TOOL, {"action": "solo", "operation": token})
        audition = session("solo", token, enabled=True)
        audition_document = document()
        check(audition["solo"] is True and audition["preview"] is False and
              audition_document["notes"] == baseline["notes"] and
              all(row["muted"] == (row["id"] != second) for row in audition_document["channels"]),
              "Temporary Solo auditions the selected Channel without applying draft score")
        session("select", token, channel=first)
        check(all(row["muted"] == (row["id"] != first) for row in document()["channels"]),
              "Changing retained Channel transfers temporary Solo atomically")
        session("solo", token, enabled=False)
        check(document() == baseline and history() == baseline_history,
              "Solo off restores opening document and history while retaining drafts")
        session("select", token, channel=second)
        manual = {**euclidean, "mode": "manual", "rotation": 0, "manual": [1, 0, 1, 0, 0, 1, 0, 0]}
        mask(session("update", token, recipes=[manual]), second, manual["manual"])
        dice = {**every, "dice": True, "seed": 4294967295}
        diced = recipe(session("update", token, recipes=[dice]), first)
        check(diced["seed"] == 4294967295 and sum(diced["mask"]) == 4 and
              recipe(session("update", token, recipes=[dice]), first)["mask"] == diced["mask"],
              "Actual seeded Dice preserves active count and repeats deterministically at full uint32 seed")
        session("select", token, channel=first)
        rerolled = recipe(session("dice", token), first)
        check(rerolled["seed"] == 0 and rerolled["dice"] is True and sum(rerolled["mask"]) == 4,
              "Repeated Dice action advances seed with defined uint32 wrap and fixed hit count")
        frozen = recipe(session("manual", token), first)
        check(frozen["mode"] == "manual" and frozen["mask"] == rerolled["mask"] and
              frozen["manual"] == rerolled["mask"] and frozen["rotation"] == 0 and frozen["dice"] is False,
              "Manual action freezes the final generated mask without altering its rhythm")
        reset = session("reset", token, all=True)
        check(reset["recipes"] == initial_recipes and document() == baseline, "Reset all restores actual baseline drafts without model mutation")
        session("update", token, recipes=[every, euclidean])
        previewed = session("preview", token, enabled=True)
        preview_document = document()
        check(previewed["preview"] is True and preview_document != baseline and
              history()["undoAvailable"] == baseline_history["undoAvailable"], "Actual preview changes score without a committed Undo")
        session("preview", token, enabled=True)
        check(document() == preview_document, "Repeated preview is baseline-relative, never cumulative")
        session("preview", token, enabled=False)
        check(document() == baseline and history() == baseline_history, "Disabling preview restores exact original score, loops and history")
        refuse(TOOL, {"action": "select", "operation": token, "channel": excluded})
        refuse(TOOL, {"action": "update", "operation": token + "-foreign", "recipes": [every]})
        refuse(TOOL, {"action": "update", "recipes": [every]})
        refuse(TOOL, {"action": "preview", "operation": token, "enabled": True, "unexpected": 1})
        before_invalid_recipes = copy.deepcopy(session("state", token)["recipes"])
        for bad in ({**every, "sequenceSteps": 0}, {**every, "sequenceSteps": 4097},
                    {**every, "seed": -1}, {**every, "seed": 4294967296},
                    {**every, "mode": "invented"}, {**manual, "manual": [1, 2]},
                    {**every, "unexpected": 1}):
            refuse(TOOL, {"action": "update", "operation": token, "recipes": [bad]})
        refuse(TOOL, {"action": "update", "operation": token,
                      "recipes": [{**every, "every": 2}, {**euclidean, "pulses": 9}]})
        check(session("state", token)["recipes"] == before_invalid_recipes,
              "Every invalid batch preserves all retained drafts, including its otherwise valid first recipe")
        session("preview", token, enabled=True)
        session("solo", token, enabled=True)
        cancelled = session("cancel", token)
        check(cancelled["completionState"] == "cancelled" and cancelled["solo"] is False and
              document() == baseline and history() == baseline_history,
              "Cancel drains actual preview and restores exact baseline without history")
        begun = session("begin", pattern=pattern, channels=[first, second, untouched], leaseMs=120000)
        token = begun["operation"]
        session("update", token, recipes=[every, euclidean])
        session("solo", token, enabled=True)
        committed = session("commit", token)
        check(committed["completionState"] == "completed" and committed["solo"] is False and
              document()["channels"] == baseline["channels"],
              "Commit reaches actual completion and restores temporary audition before creating history")
        composed = document()
        check(composed != baseline, "Composed score is actually published")
        command("edit.undo")
        check(document() == baseline, "One exact Composer Undo restores score/loops/source IDs")
        command("edit.redo")
        check(document() == composed, "One exact Composer Redo restores identical generated IDs")
        # The third Channel and dormant source note stay exact; selected recipes alone change.
        check([n for n in composed["notes"] if n["channel"] == untouched or n["beat"] >= 8] ==
              [n for n in baseline["notes"] if n["channel"] == untouched or n["beat"] >= 8],
              "Only dirty selected parts are applied; unrelated Channel and out-of-cycle data retain exact identities")
        pcm = export("composed.wav")
        saved = attempt / "composed.vibra"
        project_job("save", path=str(saved), overwrite=False)
        project_job("new", template="empty", discardChanges=True)
        project_job("open", path=str(saved), discardChanges=True)
        check(document() == composed and export("reopened.wav") == pcm,
              "Actual save/reopen preserves score/loop model and every exported PCM frame")

        project_job("new", template="empty", discardChanges=True)
        current = data("vibera_state")
        pattern = current["selectedPattern"]
        a, b, ghost = [row["id"] for row in current["channels"][:3]]
        command("project.tempo", value=120)
        command("pattern.length", pattern=pattern, length=8)
        command("clip.add", pattern=pattern, beat=0, length=8)
        # Independent explicit score: no production mask or loop expansion helper produces this reference.
        for owner, beats, velocity in ((a, (.25, 1.25, 2.25, 3.25, 4.25, 5.25, 6.25, 7.25), .8),
                                        (b, (.25, 1., 1.75), .8), (ghost, (7.5,), .3)):
            for beat in beats:
                command("notes.add", pattern=pattern, channel=owner, beat=beat, length=.25, pitch=60, value=velocity, step=True)
        check(export("independent-reference.wav") == pcm,
              "Composed looped multi-Channel PCM equals an independent literal unlooped score, with no excluded frames")
        lease_baseline, lease_history = document(), history()
        leased = session("begin", pattern=pattern, channels=[a], leaseMs=250)
        lease_token = leased["operation"]
        session("update", lease_token, recipes=[{"channel": a, "sequenceSteps": 16, "mode": "every", "every": 2}])
        session("preview", lease_token, enabled=True)
        session("solo", lease_token, enabled=True)
        check(document() != lease_baseline, "Actual expiring lease owns a real changed preview")
        time.sleep(.32)
        expired = session("state", lease_token)
        check(expired["completionState"] == "cancelled" and expired["solo"] is False and bool(expired["reason"]) and
              document() == lease_baseline and history() == lease_history,
              "Idle lease expiry cancels and restores actual preview baseline and history")
        refuse(TOOL, {"action": "commit", "operation": lease_token})
        command("channel.muteLock", channel=a, enabled=True)
        locked_baseline = protected()
        locked_session = session("begin", pattern=pattern, channels=[a, b], leaseMs=120000)
        locked_token = locked_session["operation"]
        refuse(TOOL, {"action": "solo", "operation": locked_token, "enabled": True})
        check(session("state", locked_token)["solo"] is False and
              session("state", locked_token)["activeChannel"] == a,
              "Locked Solo refuses without changing pending audition or selected draft")
        session("select", locked_token, channel=b)
        session("solo", locked_token, enabled=True)
        current_channels = {row["id"]: row for row in document()["channels"]}
        check(current_channels[a]["muted"] is False and current_channels[b]["muted"] is False,
              "Audition of another Channel preserves the opening audible mute-locked peer")
        session("commit", locked_token)
        check(document() == locked_baseline["document"] and history() == locked_baseline["history"] and
              data("vibera_state")["projectIdentity"] == locked_baseline["projectIdentity"],
              "Solo-only Accept restores opening document, identity, history and dirty state; audition revisions remain truthful")
        report["pcmExported"] = True
        report["passed"] = True
    except BaseException as error:
        report.update(error=str(error), traceback=traceback.format_exc())
    finally:
        try:
            if client is not None:
                client.close()
        except BaseException as error:
            report["passed"] = False
            report["closeError"] = str(error)
        if client is not None:
            report["exitCode"] = client.process.returncode
            (attempt / "stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
        report.update(finished=True, elapsedSeconds=time.monotonic() - started)
        retain()
    print(json.dumps({key: report.get(key) for key in ("passed", "finished", "elapsedSeconds", "error", "attemptDirectory")}))
    return 0 if report["passed"] and report.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
