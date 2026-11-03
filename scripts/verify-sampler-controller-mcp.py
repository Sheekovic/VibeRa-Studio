"""Actual project-owned sampler MIDI CC Learn/link stdio acceptance.

Usage: verify-sampler-controller-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
Only disposable --mcp-test performs diagnostic MIDI injection. No hardware,
native input, listening, provider DSP, callback timing or live-audio claim.
"""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import time
import traceback
import uuid
import wave
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / "scripts/verify-plugin-io-mcp.py").is_file()
            and (p / "skills/vibera-studio/scripts/stdio_client.py").is_file())
LINK_FIELDS = ("id", "channel", "parameter", "port", "midiChannel", "cc", "layer",
               "minimum", "maximum", "pickup", "enabled", "label")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if len(sys.argv) != 3 or not Path(sys.argv[1]).is_absolute():
        print(__doc__, file=sys.stderr)
        return 2
    app = Path(sys.argv[1])
    admission = load("sampler_controller_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
    admission.ordinary(app, False)
    output_root = admission.owned_output_path(sys.argv[2], app)
    summary = output_root / "verification.json"
    if summary.exists() or summary.is_symlink():
        admission.ordinary(summary, False)
    output = output_root / ("attempt-" + uuid.uuid4().hex)
    output.mkdir(parents=True, exist_ok=False)
    client_module = load("sampler_controller_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
    support = load("sampler_controller_support", ROOT / "scripts/dev_support.py")
    dependencies = [ROOT / "scripts/verify-plugin-io-mcp.py", ROOT / "scripts/dev_support.py",
                    ROOT / "skills/vibera-studio/scripts/stdio_client.py"]
    report = dict(passed=False, finished=False, checks=[], operations=[], refusals=[],
                  attemptDirectory=str(output), application=str(app), applicationSha256=digest(app),
                  scriptSha256=digest(Path(__file__)), dependencies={str(p.relative_to(ROOT)): digest(p)
                                                                     for p in dependencies},
                  hardware=False, nativeInput=False, listening=False, liveAudio=False,
                  callbackTiming=False, providerDsp=False, preArmQueueQualified=False,
                  scope="Actual canonical stdio/CONTROL-injected MIDI/document persistence; no audio qualification")
    client = None
    started = time.monotonic()

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        report["checks"].append(label)

    try:
        worker = app.parent / "VibeRa Plugin Worker.exe"
        admission.ordinary(worker, False)
        report["workerSha256"] = digest(worker)
        client = client_module.VibeRaClient(app)
        report["ownedProcess"] = dict(pid=client.process.pid, server=client.server)
        data = client.data
        state = lambda: data("vibera_state")
        command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
        configuration = lambda: data("vibera_controller", dict(action="state"))
        controllers = lambda: configuration()["samplerControllers"]
        links = lambda: [{key: row[key] for key in LINK_FIELDS} for row in controllers()["links"]]

        def job(arguments, failure=False):
            raw = client.call("vibera_project", arguments, allow_failure=failure)
            admitted = raw["structuredContent"]
            terminal = admitted
            deadline = time.monotonic() + 40  # Existing VibeRaClient.wait_job bound.
            while terminal["completionState"] == "pending" and time.monotonic() < deadline:
                time.sleep(.03)
                terminal = client.call("vibera_project", dict(action="job", operation=terminal["operation"]),
                                       allow_failure=failure)["structuredContent"]
            report["operations"].append(dict(action=arguments["action"], admitted=admitted, terminal=terminal))
            check(terminal["completionState"] == ("failed" if failure else "completed"),
                  "Actual project terminal outcome: " + arguments["action"])
            if failure:
                check(terminal.get("error", {}).get("code"), "Failed file job has a structured cause")
            return terminal

        def content():
            s = state()
            source_rows = {row["id"]: row for row in
                           data("vibera_inspect", dict(entity="channels", offset=0, limit=128))["items"]}
            return dict(links=[{k: row[k] for k in LINK_FIELDS} for row in s["samplerControllers"]["links"]],
                        notes=data("vibera_inspect", dict(entity="notes", pattern=pattern, offset=0, limit=128))["items"],
                        channels=[{k: c[k] for k in ("id", "name", "gain", "pan", "muted")}
                                  | dict(source=source_rows[c["id"]]["source"],
                                         modulation=c["sampler"]["modulation"]) for c in s["channels"]])

        def retained():
            s = state()
            return dict(content=content(), project=s["projectIdentity"], revision=s["revision"],
                        history={key: s[key] for key in ("dirty", "undoAvailable", "redoAvailable", "gestureActive")},
                        profile=s["controllerProfile"])

        def refuse(tool, args, prefix=None):
            before = retained()
            response = client.call(tool, args, allow_failure=True)
            code = response.get("structuredContent", {}).get("error", {}).get("code")
            report["refusals"].append(dict(tool=tool, arguments=args, response=response))
            check(response.get("isError") and code and (prefix is None or code.startswith(prefix)),
                  "Actual typed refusal: " + str(code))
            check(retained() == before, "Refusal preserves actual values/links/history/revision/global profile")
            return response

        def inject(cc, value, port=1, midi_channel=3, failure=False):
            args = dict(action="inject", port=port, channel=midi_channel, kind=0, number=cc, value=value)
            if failure:
                return refuse("vibera_controller", args, "controller.samplerUnavailable")
            return data("vibera_controller", args)

        job(dict(action="new", discardChanges=True))
        s = state()
        channel, other = s["channels"][0]["id"], s["channels"][1]["id"]
        pattern = s["selectedPattern"]
        command("notes.add", pattern=pattern, channel=other, beat=.25, length=.5, pitch=67, value=.75)
        modulation = lambda owner=channel: next(c["sampler"]["modulation"] for c in state()["channels"] if c["id"] == owner)
        manual = lambda field, owner=channel, target="volume": next(r[field] for r in modulation(owner) if r["target"] == target)
        make_link = lambda parameter, cc, **overrides: dict(id=0, channel=channel, parameter=parameter,
            port=1, midiChannel=3, cc=cc, layer=0, minimum=0, maximum=1, pickup=False,
            enabled=True, label="Original sampler CC " + str(cc)) | overrides

        listed = client.request("tools/list", {})["tools"]
        controller_tool = next(tool for tool in listed if tool["name"] == "vibera_controller")
        report["controllerDiscovery"] = controller_tool
        check(set(("samplerLearnStart", "samplerLearnState", "samplerLearnAccept", "samplerLearnCancel"))
              <= set(controller_tool["inputSchema"]["properties"]["action"]["enum"]),
              "Actual discovery exposes all owned pending Learn lifecycle actions")
        for suffix in ("create", "update", "remove"):
            op = data("vibera_commands", dict(command="channel.sampler.controller." + suffix,
                                               includeSchemas=True))["operations"][0]
            check(op["supported"] and op["documentEdit"] and op["macroAvailable"] is False and
                  op["arguments"]["additionalProperties"] is False,
                  "Canonical typed non-macro project mapping discovery: " + suffix)
            if suffix != "remove":
                schema = op["arguments"]["properties"]["controllerLink"]
                check(set(schema["required"]) == set(LINK_FIELDS) and
                      len(schema["properties"]["parameter"]["enum"]) == 88 and
                      0 in schema["properties"]["parameter"]["enum"] and
                      32 not in schema["properties"]["parameter"]["enum"],
                      "Closed complete link schema retains code0 and excludes Pan envelope")

        # Diagnostic session configuration is intentionally separate from project data.
        # No setBinding/prefs save, hardware opening or user-profile mutation occurs.
        initial_profile = configuration()["controllerProfile"]
        global_binding = dict(action="channel.gain", label="Original shadowed global CC", port=1, channel=3,
                              kind=0, number=21, layer=0, mode=1, mixer=-1, minimum=0, maximum=1,
                              pickup=False, banked=False, enabled=True, nativeArea=0)
        profile = dict(initial_profile, profile="Original sampler CC acceptance", input="", output="",
                       bindings=[global_binding])
        global_profile = data("vibera_controller", dict(action="configure", profile=profile))["controllerProfile"]
        data("vibera_select", dict(pattern=pattern, channel=other, scope=7))
        inject(21, 0)
        check(next(c["gain"] for c in state()["channels"] if c["id"] == other) == 0,
              "Actual global CONTROL-injected CC works before project precedence")
        command("channel.gain", channel=other, value=.5)
        period = make_link(17, 21, minimum=.01, maximum=64)
        before = content()
        result = command("channel.sampler.controller.create", controllerLink=period)
        first = links()[-1]
        check(result.get("completionState") == "completed" and first["channel"] == channel and
              state()["selectedChannel"] == other,
              "Typed create targets payload Channel even while another Channel is selected")
        after = content()
        command("edit.undo")
        check(content() == before, "One link creation has exact model Undo")
        command("edit.redo")
        check(content() == after, "Exact Redo restores same link ID and destination")
        changed = dict(first, label="Updated while another Channel selected")
        command("channel.sampler.controller.update", target=first["id"], controllerLink=changed)
        check(links() == [changed], "Typed update also keeps fixed owner independent of selected Channel")
        generation = controllers()["effective"]["projectionGeneration"]
        # Destination navigation canonically opens/reuses the local sampler window.
        # channel.openInstrument remains an explicitly inherited adapter gap.
        command("sampler.modulation.pitch")
        check(next(w for w in state()["samplerWindows"] if w["channel"] == other)["modulationTarget"] == "pitch",
              "Actual other-Channel sampler page navigation is reflected in readback")
        inject(21, 64)
        expected_period = .01 + (64 / 127) * 63.99
        check(math.isclose(manual("lfoPeriod"), expected_period, rel_tol=0, abs_tol=1e-12) and
              manual("lfoPeriod", other) != expected_period and
              next(c["gain"] for c in state()["channels"] if c["id"] == other) == .5,
              "Fixed project mapping applies literal physical time only to captured target, shadowing global")
        effective = controllers()["effective"]
        check(effective["capacity"] == 1024 and effective["globalLimit"] == effective["projectLimit"] == 512 and
              effective["globalRows"][0]["shadowedByProjectLinks"] == [first["id"]] and
              effective["projectRows"][0]["slot"] >= 512 and effective["projectionGeneration"] == generation,
              "Readback retains global prefix/shadow and stable project projection across scalar values")
        command("channel.sampler.controller.create", controllerLink=make_link(17, 22, minimum=.01, maximum=64))
        check(len(links()) == 2 and {row["channel"] for row in links()} == {channel},
              "Two independent physical sources may target the same parameter")
        conflict = make_link(17, 21, channel=other, minimum=.01, maximum=64)
        refuse("vibera_command", dict(command="channel.sampler.controller.create", args=dict(controllerLink=conflict)),
               "samplerController.sourceConflict")
        refuse("vibera_command", dict(command="channel.sampler.controller.create",
               args=dict(controllerLink=conflict, replacements=[links()[1]["id"]])))
        before = content()
        command("channel.sampler.controller.create", controllerLink=conflict, replacements=[first["id"]])
        check(len(links()) == 2 and all(r["id"] != first["id"] for r in links()),
              "Explicit exact project conflict replacement displaces only named source owner")
        command("edit.undo")
        check(content() == before, "Replacement Undo restores exact original link identities and values")
        command("edit.redo")
        command("edit.undo")
        revision = state()["revision"]
        command("channel.sampler.controller.update", target=changed["id"], controllerLink=changed)
        check(state()["revision"] == revision, "Identical metadata update is a real no-op")

        # Code0 is a valid boolean; identifying CC must not edit the target.
        command("channel.sampler.modulation.enabled", channel=channel, text="volume", enabled=False)
        before = retained()
        started_learn = data("vibera_controller", dict(action="samplerLearnStart", targetChannel=channel,
                                                       samplerParameter=0, projectIdentity=state()["projectIdentity"]))
        token = started_learn["operation"]
        report["operations"].append(dict(action="samplerLearnStart", admitted=started_learn))
        check(started_learn["completionState"] == "pending" and not started_learn["captured"] and
              retained() == before, "Actual arm is pending and makes no document/history edit")
        refuse("vibera_controller", dict(action="samplerLearnAccept", token=token), "controller.capturePending")
        # These performance-port messages are deliberately not eligible for CC Learn.
        for velocity in (100, 0):
            data("vibera_controller", dict(action="inject", port=0, channel=3, kind=1, number=67, value=velocity))
        check(not data("vibera_controller", dict(action="samplerLearnState", token=token))["captured"],
              "Performance note messages never identify CC Learn; no audibility claim")
        for cc in (64, 120, 123):
            inject(cc, 0, port=0)
        pending = data("vibera_controller", dict(action="samplerLearnState", token=token))
        check(not pending["captured"], "Performance sustain/panic cannot be captured as project controls")
        inject(24, 127)
        identified = data("vibera_controller", dict(action="samplerLearnState", token=token))
        check(identified["completionState"] == "pending" and identified["captured"] and
              identified["candidate"]["cc"] == 24 and not manual("enabled") and retained() == before,
              "First code0 CC identification stays pending without applying its value")
        inject(25, 0)
        check(data("vibera_controller", dict(action="samplerLearnState", token=token))["candidate"]["cc"] == 24
              and retained() == before, "Second CC never retargets or applies pending candidate")
        accepted = data("vibera_controller", dict(action="samplerLearnAccept", token=token, pickup=False))
        report["operations"].append(dict(action="samplerLearnAccept", terminal=accepted))
        check(accepted["completionState"] == "completed" and accepted["documentChanged"] and
              accepted["projectUndoable"] and accepted["effectiveApplied"] and not accepted["saved"] and
              accepted["candidate"]["parameter"] == 0 and not manual("enabled"),
              "Explicit acceptance commits one dirty document link, without capture replay or disk claim")
        learned = content()
        command("edit.undo")
        check(content() == before["content"], "Learn Accept has exactly one document Undo")
        command("edit.redo")
        check(content() == learned, "Learn Redo restores exact stable row")
        inject(24, 127)
        check(manual("enabled") is True, "Accepted code0 quantizes CC127 to boolean true")
        inject(24, 0)
        check(manual("enabled") is False, "Accepted code0 quantizes CC0 to boolean false")
        command("channel.sampler.controller.create", controllerLink=make_link(19, 25, maximum=2))
        for cc_value, expected in ((0, 0), (64, 1), (127, 2)):
            inject(25, cc_value)
            check(manual("lfoShape") == expected, "Shape follows independent half-up physical quantization: " + str(cc_value))
        command("channel.sampler.modulation.sustain", channel=channel, text="volume", value=.5)
        command("channel.sampler.controller.create", controllerLink=make_link(6, 27, pickup=True))
        generation = controllers()["effective"]["projectionGeneration"]
        unchanged = state()["revision"]
        inject(27, 0)
        check(manual("sustain") == .5 and state()["revision"] == unchanged,
              "Uncaught pickup preserves parameter and document history")
        inject(27, 64)
        check(math.isclose(manual("sustain"), 64 / 127, rel_tol=0, abs_tol=1e-12),
              "Pickup catches actual current value instead of jumping at first extreme")
        inject(27, 127)
        check(manual("sustain") == 1 and controllers()["effective"]["projectionGeneration"] == generation,
              "Caught pickup and scalar revisions retain effective projection generation")

        conflict_before = content()
        pending = data("vibera_controller", dict(action="samplerLearnStart", targetChannel=channel, samplerParameter=0))
        inject(21, 127)
        captured = data("vibera_controller", dict(action="samplerLearnState", token=pending["operation"]))
        check(captured["conflictingProjectLinks"] == [first["id"]],
              "Learn reports the actual displaced project source owner")
        refuse("vibera_controller", dict(action="samplerLearnAccept", token=pending["operation"], pickup=False),
               "samplerController.sourceConflict")
        check(data("vibera_controller", dict(action="samplerLearnState", token=pending["operation"]))["completionState"] == "pending",
              "Refused Learn conflict retains owned pending candidate for explicit confirmation")
        replaced = data("vibera_controller", dict(action="samplerLearnAccept", token=pending["operation"],
                                                  pickup=False, replacements=[first["id"]]))
        report["operations"].append(dict(action="samplerLearnAcceptReplacement", terminal=replaced))
        check(replaced["completionState"] == "completed" and replaced["replacedProjectLinks"] == [first["id"]],
              "Explicit Learn replacement reports the exact displaced project link")
        command("edit.undo")
        check(content() == conflict_before, "Learn replacement Undo restores full original target/configuration")

        cancelled_before = retained()
        pending = data("vibera_controller", dict(action="samplerLearnStart", targetChannel=channel, samplerParameter=19))
        inject(30, 64)
        cancelled = data("vibera_controller", dict(action="samplerLearnCancel", token=pending["operation"]))
        report["operations"].append(dict(action="samplerLearnCancel", terminal=cancelled))
        check(cancelled["completionState"] == "cancelled" and not cancelled["documentChanged"] and
              retained() == cancelled_before, "Captured cancellation preserves document, history and exact values")
        refuse("vibera_controller", dict(action="samplerLearnAccept", token=pending["operation"]))
        pending = data("vibera_controller", dict(action="samplerLearnStart", targetChannel=channel, samplerParameter=17))
        command("channel.gain", channel=other, value=.625)
        stale_response = client.call("vibera_controller",
                                     dict(action="samplerLearnState", token=pending["operation"]),
                                     allow_failure=True)
        stale = stale_response["structuredContent"]
        report["operations"].append(dict(action="samplerLearnStateStale", terminal=stale))
        check(stale_response.get("isError"), "Failed owned Learn state retains actual MCP error status")
        check(stale["completionState"] == "failed" and stale["error"]["code"] == "controller.staleLearn",
              "Actual canonical revision change invalidates pending Learn")
        refuse("vibera_controller", dict(action="samplerLearnAccept", token=pending["operation"]), "controller.staleLearn")
        pending = data("vibera_controller", dict(action="samplerLearnStart", targetChannel=other, samplerParameter=0))
        deleted = data("vibera_rack", dict(action="delete", ids=[other]))
        delete_admitted = copy.deepcopy(deleted)
        delete_deadline = time.monotonic() + 40
        while deleted["completionState"] == "pending" and time.monotonic() < delete_deadline:
            time.sleep(.03)
            deleted = client.call("vibera_rack", dict(action="job", operation=deleted["operation"]),
                                  allow_failure=True)["structuredContent"]
        report["operations"].append(dict(action="channelDelete", admitted=delete_admitted, terminal=deleted))
        check(deleted["completionState"] == "completed" and
              all(c["id"] != other for c in state()["channels"]),
              "Actual canonical asynchronous Channel deletion reaches terminal document removal")
        refuse("vibera_controller", dict(action="samplerLearnAccept", token=pending["operation"]), "controller.staleLearn")
        command("edit.undo")

        invalid = [dict(period, parameter=32), dict(period, parameter=True), dict(period, minimum=64, maximum=.01),
                   dict(period, pickup="true"), dict(period, port=0, cc=64), dict(period, unexpected=1)]
        for row in invalid:
            refuse("vibera_command", dict(command="channel.sampler.controller.create", args=dict(controllerLink=row)))
        refuse("vibera_command", dict(command="channel.sampler.controller.remove", args=dict(target=999999999)))
        refuse("vibera_controller", dict(action="samplerLearnStart", targetChannel=channel, samplerParameter=32))
        refuse("vibera_controller", dict(action="samplerLearnStart", targetChannel=channel, samplerParameter=0, extra=True))
        profile_before_macro = data("vibera_macros", dict(action="state"))
        for suffix in ("create", "update", "remove"):
            recipe = dict(id="macro.sampler-cc-refused." + uuid.uuid4().hex, name="Must refuse project mapping payload",
                          steps=[dict(action="channel.sampler.controller." + suffix, currentContext=False,
                                      parameters=dict(target=first["id"]) if suffix == "remove" else
                                      dict(controllerLink=period))])
            refuse("vibera_macros", dict(action="create", generation=profile_before_macro["generation"], macro=recipe),
                   "macro.unsupported")
        profile_after_macro = data("vibera_macros", dict(action="state"))
        check(profile_before_macro["generation"] == profile_after_macro["generation"] and
              profile_before_macro["macros"] == profile_after_macro["macros"],
              "Unsupported mapping macros do not mutate actual preference recipes")
        remove_id = next(r["id"] for r in links() if r["cc"] == 22)
        before = content()
        command("channel.sampler.controller.remove", target=remove_id)
        check(all(r["id"] != remove_id for r in links()), "Canonical remove deletes only exact stable project row")
        command("edit.undo")
        check(content() == before, "Remove Undo restores exact mapping identity/configuration")
        command("edit.redo")
        command("edit.undo")
        check(configuration()["controllerProfile"] == global_profile, "All project edits leave global profile byte-value unchanged")

        saved = output / "sampler-controller-links.vibra"
        job(dict(action="save", path=str(saved), overwrite=False))
        saved_hash, saved_content = digest(saved), content()
        xml = ET.parse(saved)
        check(int(xml.getroot().get("version")) == support.project_schema(ROOT) and
              len(xml.findall("./SAMPLER_CONTROLLER_LINKS/LINK")) == len(links()),
              "Actual saved schema contains all project-owned stable CC rows")
        # Valid ordinary Audio Clip role preserves persisted links as dormant.
        original_wave = output / "original-role-fixture.wav"
        with wave.open(str(original_wave), "wb") as wav:
            wav.setparams((1, 2, 48000, 48000, "NONE", "not compressed"))
            wav.writeframes(b"".join(struct.pack("<h", (i * 97) % 12001 - 6000) for i in range(48000)))
        report["originalRoleFixtureSha256"] = digest(original_wave)
        dormant = ET.parse(saved)
        row = next(c for c in dormant.findall("./CHANNELS/CHANNEL") if int(c.get("id")) == channel)
        row.set("source", str(original_wave))
        row.set("audioClip", "1")
        row.set("audioDuration", "1")
        dormant_file = output / "dormant-sampler-links.vibra"
        dormant.write(dormant_file, encoding="utf-8", xml_declaration=True)
        job(dict(action="open", path=str(dormant_file), discardChanges=True))
        check(all(not r["available"] for r in controllers()["links"] if r["channel"] == channel) and
              all(r["reservesSource"] for r in controllers()["effective"]["projectRows"] if r["channel"] == channel),
              "Real reopened unsupported role retains unavailable project sources and reservations")
        inject(21, 0, failure=True)
        check(configuration()["controllerProfile"] == global_profile,
              "Dormant reserved source cannot fall through into unrelated global mapping")
        job(dict(action="open", path=str(saved), discardChanges=True))
        check(content() == saved_content, "Actual Open restores all exact persisted controls/links/manual values")
        stale_project = state()["projectIdentity"]
        pending = data("vibera_controller", dict(action="samplerLearnStart", targetChannel=channel, samplerParameter=0))
        job(dict(action="new", discardChanges=True))
        refuse("vibera_controller", dict(action="samplerLearnAccept", token=pending["operation"],
                                         projectIdentity=stale_project), "stale_project")
        check(not links() and configuration()["controllerProfile"] == global_profile,
              "New project owns no prior fixed-ID mapping and retains global profile")
        job(dict(action="open", path=str(saved), discardChanges=True))
        check(content() == saved_content, "Save/new/Open restores exact project links with unchanged global configuration")
        data("vibera_select", dict(pattern=pattern, channel=other, scope=7))
        inject(21, 127)
        check(manual("lfoPeriod") == 64 and next(c["gain"] for c in state()["channels"] if c["id"] == other) == .625,
              "Actual reopened CC still reaches fixed Channel/code and shadows global source")
        invalid_xml = ET.parse(saved)
        invalid_xml.find("./SAMPLER_CONTROLLER_LINKS/LINK").set("parameter", "32")
        malformed = output / "invalid-project-link.vibra"
        invalid_xml.write(malformed, encoding="utf-8", xml_declaration=True)
        before = retained()
        job(dict(action="open", path=str(malformed), discardChanges=True), failure=True)
        check(retained() == before and digest(saved) == saved_hash,
              "Actual failed file Open retains incumbent document/history and original saved file")
        check(configuration()["controllerProfile"] == global_profile,
              "Durability/role/stale/failed-file workflows never rewrite global preference mappings")
        report["applicationSha256After"] = digest(app)
        report["workerSha256After"] = digest(worker)
        report["scriptSha256After"] = digest(Path(__file__))
        report["dependenciesAfter"] = {str(p.relative_to(ROOT)): digest(p) for p in dependencies}
        report["inputsStable"] = (report["applicationSha256After"] == report["applicationSha256"] and
                                 report["workerSha256After"] == report["workerSha256"] and
                                 report["scriptSha256After"] == report["scriptSha256"] and
                                 report["dependenciesAfter"] == report["dependencies"])
        check(report["inputsStable"], "Actual app/worker/verifier/client/admission inputs stayed identical")
        report["passed"] = True
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
    finally:
        if client:
            try:
                client.close()
                report["ownedProcessExit"] = client.process.returncode
                if client.process.returncode != 0:
                    report["error"] = "Owned application exited with code " + str(client.process.returncode)
                    report["passed"] = False
            except Exception as error:
                report["closeError"] = str(error)
                report["passed"] = False
        report["finished"] = True
        report["seconds"] = time.monotonic() - started
        encoded = json.dumps(report, indent=2) + "\n"
        (output / "verification.json").write_text(encoded, encoding="utf-8")
        summary.write_text(encoded, encoding="utf-8")
    print(json.dumps(dict(passed=report["passed"], checks=len(report["checks"]), output=str(output),
                          error=report.get("error")), indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
