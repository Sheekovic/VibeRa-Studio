"""Original actual stdio automation state/history/persistence checks; no UI/audio claim."""
from pathlib import Path
import importlib.util
import json
import sys
import time
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main():
    if len(sys.argv) != 3 or not Path(sys.argv[1]).is_absolute(): return 2
    app = Path(sys.argv[1]).resolve(strict=True)
    helper = load("automation_owned_output", ROOT/"scripts/verify-mixer-effects-mcp.py")
    helper.OWNER = {"schemaVersion": 1, "owner": "VibeRa verify-mixer-automation-mcp"}
    helper.OWNER_FILE = ".vibera-mixer-automation-mcp-owner.json"
    output, attempt = helper.prepare_output(sys.argv[2])
    checks = []; client = None; started = time.monotonic()
    report = dict(passed=False, finished=False, checks=checks, application=str(app),
        applicationSha256=helper.digest(app), scriptSha256=helper.digest(Path(__file__)),
        attemptDirectory=str(attempt), nativeInput=False, listening=False, hardware=False,
        audio=False, macroReplay=False)
    def check(value, message):
        if not value: raise AssertionError(message)
        checks.append(message)
    try:
        module = load("automation_neutral_client", ROOT/"skills/vibera-studio/scripts/stdio_client.py")
        client = module.VibeRaClient(app); d = client.data
        cmd = lambda name, **args: d("vibera_command", dict(command=name, args=args))
        state = lambda: d("vibera_state")
        inspect = lambda entity, **args: d("vibera_inspect", dict(entity=entity, **args))
        def snapshot():
            sources = inspect("automationSources")["items"]
            return dict(sources=sources,
                points=[inspect("automationPoints", source=s["id"])["items"] for s in sources],
                links=[inspect("automationLinks", source=s["id"])["items"] for s in sources],
                initial=inspect("automationInitialValues")["items"])
        for operation in ("automation.create", "automation.link", "automation.range", "automation.init", "automation.init.remove"):
            contract = d("vibera_commands", dict(command=operation, includeSchemas=True))["operations"][0]
            check(contract["supported"] and contract["macroAvailable"] and
                  contract["arguments"]["properties"]["automationControl"]["enum"] == ["gain", "pan", "stereoSeparation", "send"] and
                  contract["arguments"]["properties"]["automationSendTarget"]["type"] == "integer" and
                  contract["arguments"]["properties"]["automationSendTarget"]["minimum"] == 0,
                  "Actual typed discoverable target control " + operation)
        initial = snapshot()
        result = cmd("automation.create", mixer=1, automationControl="pan", beat=1, length=4, lane=2)
        check(result["completionState"] == "completed" and result["completionScope"] == "canonicalAutomationTarget" and
              result["dspSettlingAcknowledged"] is False, "Canonical completion never claims DSP or heard acknowledgement")
        source = inspect("automationSources")["items"][0]["id"]
        points = inspect("automationPoints", source=source)["items"]
        check([p["value"] for p in points] == [.5, .5] and
              inspect("automationLinks", source=source)["items"][0] == dict(kind=1, owner=1, parameter=0, minimum=-1, maximum=1),
              "Centre source points normalize to0.5 and signed pan range retains stable owner")
        created = snapshot(); cmd("edit.undo")
        check(snapshot() == initial, "Actual source create is one exact Undo")
        cmd("edit.redo"); check(snapshot() == created, "Actual source create exact Redo")
        cmd("automation.link", target=source, mixer=1, automationControl="stereoSeparation")
        cmd("automation.range", target=source, mixer=1, automationControl="stereoSeparation", beat=1, length=-1)
        cmd("automation.point.move", target=points[0]["id"], beat=0, value=0)
        cmd("automation.point.move", target=points[1]["id"], beat=4, value=1)
        links = inspect("automationLinks", source=source)["items"]
        check(links == [dict(kind=1, owner=1, parameter=0, minimum=-1, maximum=1),
                        dict(kind=5, owner=1, parameter=0, minimum=1, maximum=-1)],
              "Source links independently identify pan and separation including inverted units")
        for control in ("pan", "stereoSeparation"):
            cmd("automation.create", mixer=2, automationControl=control, beat=0, length=2)
        page = inspect("automationSources", offset=0, limit=1); ids = []
        while True:
            ids.extend(item["id"] for item in page["items"])
            if page["nextOffset"] is None: break
            page = inspect("automationSources", offset=page["nextOffset"], limit=1)
        check(len(ids) == 3 and len(set(ids)) == 3 and ids == [s["id"] for s in snapshot()["sources"]],
              "Actual paginated source discovery never loses later target sources")
        first = inspect("automationPoints", source=source, offset=0, limit=1)
        second = inspect("automationPoints", source=source, offset=first["nextOffset"], limit=1)
        check(first["total"] == 2 and second["nextOffset"] is None and
              [first["items"][0]["value"], second["items"][0]["value"]] == [0, 1],
              "Actual point pagination preserves normalized musical endpoints")
        original = snapshot()
        d("vibera_batch", dict(commands=[dict(command="automation.init", args=dict(mixer=1, automationControl="pan", value=-.5)),
            dict(command="automation.init", args=dict(mixer=1, automationControl="stereoSeparation", value=.25))]))
        batched = snapshot(); cmd("edit.undo")
        check(snapshot() == original, "Pan+separation batch has one exact Undo")
        cmd("edit.redo"); check(snapshot() == batched, "Batch exact Redo")
        cmd("edit.undo")
        failed = client.call("vibera_batch", dict(commands=[dict(command="automation.init", args=dict(mixer=1, automationControl="pan", value=.25)),
            dict(command="automation.range", args=dict(target=source, mixer=1, automationControl="pan", beat=1.1, length=0))]), allow_failure=True)
        check(failed.get("isError") and snapshot() == original and not state()["gestureActive"],
              "Later invalid target-domain batch step rolls back every earlier update and ownership")
        cmd("edit.redo"); check(snapshot() == batched, "Failed batch preserves original exact Redo")
        cmd("edit.undo")
        for args in (dict(mixer=1, automationControl="send", value=0), dict(mixer=999, automationControl="pan", value=0),
                     dict(mixer=1, automationControl="pan", value=1.00001), dict(mixer=1, automationControl=True, value=0)):
            before = snapshot(); revision = state()["revision"]
            error = client.call("vibera_command", dict(command="automation.init", args=args), allow_failure=True)
            check(error.get("isError") and error.get("structuredContent", {}).get("error", {}).get("code") and
                  snapshot() == before and state()["revision"] == revision, "Typed target refusal is structured and atomic " + str(args))
        token = d("vibera_gesture", dict(action="begin", domain="document"))["operation"]
        for amount in (-.5, .75):
            d("vibera_gesture", dict(action="update", operation=token, command="automation.init",
                args=dict(mixer=1, automationControl="pan", value=amount)))
        check(d("vibera_gesture", dict(action="cancel", operation=token))["completionState"] == "cancelled" and snapshot() == original,
              "Actual owned continuous initialization cancel restores complete source/init state")
        path = attempt/"typed-automation.vibra"
        client.wait_job(d("vibera_project", dict(action="save", path=str(path), overwrite=False)))
        saved = snapshot()
        client.wait_job(d("vibera_project", dict(action="new", discardChanges=True)))
        client.wait_job(d("vibera_project", dict(action="open", path=str(path), discardChanges=True)))
        check(snapshot() == saved, "Actual asynchronous save/new/open preserves typed automation sources, points, links and init values")
        cmd("automation.unlink", target=source, mixer=1, automationControl="pan")
        check(inspect("automationLinks", source=source)["items"] == [links[1]] and
              any(v["kind"] == 1 and v["owner"] == 1 for v in inspect("automationInitialValues")["items"]),
              "Typed unlink preserves other control links and independent initialization")
        cmd("automation.init.remove", mixer=1, automationControl="pan")
        check(not any(v["kind"] == 1 and v["owner"] == 1 for v in inspect("automationInitialValues")["items"]) and
              any(v["kind"] == 5 and v["owner"] == 1 for v in inspect("automationInitialValues")["items"]),
              "RemoveInit targets only selected kind/owner; separation initialization survives")
        item = next(row for row in inspect("mixer")["items"] if row["index"] == 1)
        target = next(value for value in item["automationTargets"] if value["kind"] == "pan")
        check(target["manual"] == 0 and target["initializedValue"] is None and target["units"] == "bipolarPan",
              "Actual target readback distinguishes saved manual fallback from absent initialization")
        cmd("edit.undo"); cmd("edit.undo")
        check(snapshot() == saved, "Both typed removal operations have exact independent Undo")
        # Required public send workflow; old pan/separation oracles above stay
        # unchanged. Exact stable pairs, normalized source points and real jobs.
        cmd("mixer.route", mixer=1, target=3, enabled=True)
        cmd("mixer.send", mixer=1, target=3, value=.25)
        send_baseline = snapshot()
        result = cmd("automation.create", mixer=1, automationControl="send", automationSendTarget=3,
                     beat=0, length=2, lane=3)
        check(result["completionState"] == "completed" and not result["dspSettlingAcknowledged"],
              "Public send create has truthful canonical completion")
        new_source = snapshot()["sources"][-1]["id"]
        send_links = inspect("automationLinks", source=new_source)["items"]
        send_points = inspect("automationPoints", source=new_source)["items"]
        check(send_links == [dict(kind=6, owner=1, parameter=3, minimum=0, maximum=2)] and
              [point["value"] for point in send_points] == [.125, .125],
              "Actual send target and source normalization use route gain, not fader or selection")
        send_created = snapshot()
        cmd("edit.undo"); check(snapshot() == send_baseline, "Actual send create one exact Undo")
        cmd("edit.redo"); check(snapshot() == send_created, "Actual send create exact Redo")
        cmd("automation.link", target=new_source, mixer=1, automationControl="send", automationSendTarget=0)
        cmd("automation.range", target=new_source, mixer=1, automationControl="send", automationSendTarget=0,
            beat=2, length=0)
        pair_links = inspect("automationLinks", source=new_source)["items"]
        check(pair_links == [send_links[0], dict(kind=6, owner=1, parameter=0, minimum=2, maximum=0)],
              "Master0 and non-Master send links retain independent exact ranges")
        mixer = next(row for row in inspect("mixer")["items"] if row["index"] == 1)
        target = next(item for item in mixer["automationTargets"] if item["kind"] == "send" and item["destinationInsert"] == 3)
        check(target["sourceInsert"] == 1 and target["manual"] == .25 and new_source in target["linkedSources"],
              "Actual send discovery/readback identifies pair, manual state and linked source")
        before_refusal, before_revision = snapshot(), state()["revision"]
        refusal = client.call("vibera_command", dict(command="mixer.route", args=dict(mixer=1, target=3, enabled=False)), allow_failure=True)
        check(refusal.get("isError") and "automation.routeReferenced" in str(refusal) and
              snapshot() == before_refusal and state()["revision"] == before_revision,
              "Referenced send removal is a structured atomic refusal")
        for bad in (dict(mixer=1, automationControl="send", automationSendTarget=-1),
                    dict(mixer=1, automationControl="send", automationSendTarget=1),
                    dict(mixer=1, automationControl="send", automationSendTarget=True),
                    dict(mixer=1, automationControl="send", automationSendTarget=2),
                    dict(mixer=1, automationControl="pan", automationSendTarget=3)):
            error = client.call("vibera_command", dict(command="automation.init", args=bad), allow_failure=True)
            check(error.get("isError") and snapshot() == before_refusal and state()["revision"] == before_revision,
                  "Invalid send pair/foreign metadata preserves current music " + str(bad))
        token = d("vibera_gesture", dict(action="begin", domain="document"))["operation"]
        d("vibera_gesture", dict(action="update", operation=token, command="automation.range",
            args=dict(target=new_source, mixer=1, automationControl="send", automationSendTarget=3, beat=.5, length=1.5)))
        check(d("vibera_gesture", dict(action="cancel", operation=token))["completionState"] == "cancelled" and
              snapshot() == before_refusal, "Actual send range gesture cancel restores exact pair and history baseline")
        send_file = attempt/"public-send-automation.vibra"
        client.wait_job(d("vibera_project", dict(action="save", path=str(send_file), overwrite=False)))
        send_saved = snapshot()
        client.wait_job(d("vibera_project", dict(action="new", discardChanges=True)))
        client.wait_job(d("vibera_project", dict(action="open", path=str(send_file), discardChanges=True)))
        reopened_mixer = next(row for row in inspect("mixer")["items"] if row["index"] == 1)
        check(snapshot() == send_saved and any(edge["target"] == 3 and edge["gain"] == .25 for edge in reopened_mixer["routes"]),
              "Real asynchronous save/new/open preserves both public send keys, points, ranges and manual route")
        report["passed"] = True
    except Exception as failure: report["error"] = str(failure)
    finally:
        if client:
            try: client.close(); report["exitCode"] = client.process.returncode
            except Exception as failure: report["passed"] = False; report["closeError"] = str(failure)
            (attempt/"stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
        report.update(finished=True, seconds=time.monotonic()-started)
        payload = json.dumps(report, indent=2)+"\n"
        (attempt/"verification.json").write_text(payload, encoding="utf-8")
        (output/"verification.json").write_text(payload, encoding="utf-8")
    print(json.dumps({key: report.get(key) for key in ("passed", "finished", "seconds", "error", "attemptDirectory")}))
    return 0 if report["passed"] and report.get("exitCode") == 0 else 1
if __name__ == "__main__": raise SystemExit(main())
