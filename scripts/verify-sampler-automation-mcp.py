"""Actual sampler Automation Clip stdio/history/file/PCM acceptance.

Usage: verify-sampler-automation-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
Uses disposable --mcp-test and the existing executable-derived output admission.
No native input, audio-device timing, listening or third-party response claim.
"""
from __future__ import annotations

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
ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "scripts/verify-plugin-io-mcp.py").is_file()
            and (parent / "skills/vibera-studio/scripts/stdio_client.py").is_file())


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
    admission = load("sampler_automation_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
    admission.ordinary(app, False)
    output_root = admission.owned_output_path(sys.argv[2], app)
    summary_path = output_root / "verification.json"
    if summary_path.exists() or summary_path.is_symlink():
        admission.ordinary(summary_path, False)
    output = output_root / ("attempt-" + uuid.uuid4().hex)
    output.mkdir(parents=True, exist_ok=False)
    client_module = load("sampler_automation_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
    schema_module = load("sampler_automation_schema", ROOT / "scripts/dev_support.py")
    report = dict(passed=False, finished=False, checks=[], operations=[], pcmOracles=[],
                  attemptDirectory=str(output), application=str(app), applicationSha256=digest(app),
                  scriptSha256=digest(Path(__file__)), nativeInput=False, listening=False,
                  hardware=False, liveAudio=False, pcmQualified=False, macroReplay=False,
                  dependencies={str(path.relative_to(ROOT)): digest(path) for path in (
                      ROOT / "scripts/verify-plugin-io-mcp.py", ROOT / "scripts/dev_support.py",
                      ROOT / "skills/vibera-studio/scripts/stdio_client.py")})
    began = time.monotonic()
    client = None
    owned_macro = None

    def check(value, label):
        if not value:
            raise AssertionError(label)
        report["checks"].append(label)

    try:
        # Original fixture; no installed sample or proprietary asset.
        source = output / "original-sampler-automation-tone.wav"
        with wave.open(str(source), "wb") as wav:
            wav.setparams((1, 2, 48000, 96000, "NONE", "not compressed"))
            wav.writeframes(b"".join(struct.pack("<h", round(6000 * math.sin(2 * math.pi * 1000 * i / 48000)))
                                     for i in range(96000)))
        report["fixtureSha256"] = digest(source)
        client = client_module.VibeRaClient(app)
        data = client.data
        state = lambda: data("vibera_state")
        command = lambda name, **args: data("vibera_command", dict(command=name, args=args))

        def job(arguments):
            accepted = data("vibera_project", arguments)
            terminal = client.wait_job(accepted)
            report["operations"].append(dict(action=arguments["action"], admitted=accepted, terminal=terminal))
            check(terminal["completionState"] == "completed", "Actual terminal project job: " + arguments["action"])
            return terminal

        def failed_job(arguments, require_pending=False):
            accepted = client.call("vibera_project", arguments, allow_failure=True)
            result = accepted["structuredContent"]
            observed_pending = result["completionState"] == "pending"
            check(result["completionState"] in ("pending", "failed") and result.get("operation"),
                  "Negative file operation exposes its actual tracked pending/failed state")
            if require_pending:
                check(observed_pending, "Negative export really enters asynchronous work")
            deadline = time.monotonic() + 40  # Same bound as the real client's positive job helper.
            while result["completionState"] == "pending" and time.monotonic() < deadline:
                time.sleep(.03)
                result = client.call("vibera_project", dict(action="job", operation=result["operation"]),
                                     allow_failure=True)["structuredContent"]
            report["operations"].append(dict(action=arguments["action"], admitted=accepted, terminal=result,
                                               observedPending=observed_pending))
            check(result["completionState"] == "failed" and result.get("error", {}).get("code"),
                  "Negative asynchronous job reports its real structured terminal failure")

        def inspect(entity, **args):
            items = []
            offset = 0
            while True:
                page = data("vibera_inspect", dict(entity=entity, offset=offset, limit=128, **args))
                items.extend(page["items"])
                if page["nextOffset"] is None:
                    return items
                if page["nextOffset"] <= offset:
                    raise AssertionError("Non-advancing canonical inspection pagination")
                offset = page["nextOffset"]

        def snapshot():
            sources = inspect("automationSources")
            return dict(sources=sources,
                        points=[inspect("automationPoints", source=s["id"]) for s in sources],
                        links=[inspect("automationLinks", source=s["id"]) for s in sources],
                        initial=inspect("automationInitialValues"), clips=inspect("clips"))

        job(dict(action="new", discardChanges=True))
        channel, pattern = state()["selectedChannel"], state()["selectedPattern"]
        command("channel.source", channel=channel, text=str(source))
        command("project.tempo", value=120)
        command("notes.add", channel=channel, pattern=pattern, beat=0, length=3, pitch=60, value=.7)
        command("clip.add", pattern=pattern, beat=0, length=4, lane=0)
        settings = lambda: next(c["sampler"] for c in state()["channels"] if c["id"] == channel)
        manual = lambda: settings()["modulation"]

        def retained_document():
            s = state()
            return dict(project=s["projectIdentity"], revision=s["revision"],
                        history={key: s[key] for key in ("dirty", "undoAvailable", "redoAvailable", "gestureActive")},
                        selected=[s["selectedChannel"], s["selectedPattern"]],
                        notes=inspect("notes", pattern=pattern), automation=snapshot(), manual=manual())

        contracts = {}
        for suffix in ("create", "init", "init.remove", "link", "unlink", "range"):
            discovery = data("vibera_commands", dict(command="automation.sampler." + suffix, includeSchemas=True))
            operation = discovery["operations"][0]
            schema = operation["arguments"]
            codes = schema["properties"]["samplerParameter"]["enum"]
            check(operation["supported"] and not schema["additionalProperties"] and
                  {"channel", "samplerParameter"}.issubset(schema["required"]) and
                  len(codes) == len(set(codes)) == 88 and 0 in codes and 32 not in codes and 31 not in codes,
                  "Actual closed supported sampler schema and explicit valid code0: " + suffix)
            contracts[suffix] = operation
        metadata = discovery["samplerAutomationContract"]
        check(metadata["kind"] == 7 and metadata["validPairs"] == 88 and metadata["targetStride"] == 32 and
              metadata["parameterEncoding"] == "destinationCode*32+fieldCode" and
              [r["id"] for r in metadata["destinations"]] == ["volume", "pan", "cutoff", "resonance", "pitch"] and
              next(r for r in metadata["fields"] if r["id"] == "attack")["maximum"] == 64 and
              next(r for r in metadata["fields"] if r["id"] == "amount")["minimum"] == -1 and
              {"minimum", "maximum"}.issubset(contracts["range"]["arguments"]["required"]),
              "Discovery identifies physical field ranges, destination codes and kind7")
        report["samplerAutomationContract"] = metadata

        for parameter, value in (("attack", 0), ("hold", 0), ("decay", 0), ("sustain", 1), ("amount", 1)):
            command("channel.sampler.modulation." + parameter, channel=channel, text="volume", value=value)
        command("channel.sampler.modulation.enabled", channel=channel, text="volume", enabled=True)
        expected_manual = manual()

        def render(name):
            path = output / name
            accepted = data("vibera_project", dict(action="export", path=str(path), overwrite=False))
            check(accepted["completionState"] == "pending", "WAV acceptance is pending, not DSP completion")
            terminal = client.wait_job(accepted)
            report["operations"].append(dict(action="export", path=str(path), admitted=accepted, terminal=terminal))
            with wave.open(str(path), "rb") as wav:
                check(terminal["completionState"] == "completed" and
                      (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 3, 48000),
                      "Actual completed stereo24/48k exported WAV")
                return wav.readframes(wav.getnframes())

        dry = render("manual-full-sustain.wav")
        before_create = snapshot()
        result = command("automation.sampler.create", channel=channel, samplerParameter=0, beat=0, length=4, lane=1)
        check(result["completionState"] == "completed" and result["completionScope"] == "canonicalAutomationTarget"
              and result["dspSettlingAcknowledged"] is False, "Canonical target completion makes no DSP acknowledgement")
        enabled_source = inspect("automationSources")[-1]["id"]
        check(inspect("automationLinks", source=enabled_source) ==
              [dict(kind=7, owner=channel, parameter=0, minimum=0, maximum=1)] and
              [p["value"] for p in inspect("automationPoints", source=enabled_source)] == [1, 1],
              "Create code0 retains stable Channel owner and normalized enabled endpoints")
        created = snapshot()
        command("edit.undo")
        check(snapshot() == before_create, "Create removes source, placement and initialization in one exact Undo")
        command("edit.redo")
        check(snapshot() == created, "Create exact Redo restores source/point/placement identities")
        command("automation.sampler.create", channel=channel, samplerParameter=6, beat=0, length=4, lane=2)
        sustain_source = inspect("automationSources")[-1]["id"]
        navigation_before = retained_document()
        # Familiar F5 action toggles; do not hide an already visible Playlist.
        if not next(e for e in state()["editors"] if e["index"] == 2)["visible"]:
            command("view.playlist")
        data("vibera_picker", dict(action="visible", visible=True))
        data("vibera_picker", dict(action="type", kind="automation"))
        data("vibera_picker", dict(action="filter", text=""))
        data("vibera_picker", dict(action="groupFilter", text=""))
        picker = data("vibera_picker", dict(action="select", ids=[sustain_source]))
        navigation_after = retained_document()
        navigation_editors = state()["editors"]
        report["linkedSourceNavigation"] = dict(picker=picker, editors=navigation_editors,
                                                   documentUnchanged=navigation_after == navigation_before)
        check(picker["selection"] == [sustain_source] and picker["kind"] == "automation" and picker["visible"]
              and next(e for e in navigation_editors if e["index"] == 2)["visible"]
              and navigation_after == navigation_before,
              "Semantic linked-source navigation uses actual Picker and Playlist without changing music/history/MIDI target")
        points = inspect("automationPoints", source=sustain_source)
        check([p["beat"] for p in points] == [0, 4] and [p["value"] for p in points] == [1, 1] and
              all(p["shape"] == 0 and p["tension"] == 0 for p in points),
              "Sustain create uses two normalized linear musical endpoints")

        command("automation.sampler.init", channel=channel, samplerParameter=67, value=.25)
        check(dict(kind=7, owner=channel, parameter=67, value=.25) in inspect("automationInitialValues"),
              "Init stores a physical quarter-second cutoff attack, not a normalized64-second fraction")
        before_link = snapshot()
        command("automation.sampler.link", target=sustain_source, channel=channel, samplerParameter=67)
        check(dict(kind=7, owner=channel, parameter=67, minimum=0, maximum=64) in
              inspect("automationLinks", source=sustain_source), "Existing source link exposes full physical attack endpoints")
        default_range = snapshot()
        command("automation.sampler.range", target=sustain_source, channel=channel, samplerParameter=67,
                minimum=.125, maximum=.5)
        check(dict(kind=7, owner=channel, parameter=67, minimum=.125, maximum=.5) in
              inspect("automationLinks", source=sustain_source), "Explicit physical minimum/maximum retain their units")
        changed_range = snapshot()
        command("edit.undo")
        check(snapshot() == default_range, "Physical endpoint range edit has exact Undo")
        command("edit.redo")
        check(snapshot() == changed_range, "Physical endpoint range edit has exact Redo")
        command("automation.sampler.unlink", target=sustain_source, channel=channel, samplerParameter=67)
        check(snapshot() == before_link, "Unlink removes only selected target and preserves independent initialization")
        command("automation.sampler.init.remove", channel=channel, samplerParameter=67)
        check(not any(v["parameter"] == 67 and v["kind"] == 7 for v in inspect("automationInitialValues")),
              "Delete initialization removes only its exact sampler key")
        command("edit.undo")
        check(snapshot() == before_link, "Delete initialization has exact Undo")

        original = snapshot()
        token = data("vibera_gesture", dict(action="begin", domain="document"))["operation"]
        for low, high in ((1, 0), (.5, .5)):
            data("vibera_gesture", dict(action="update", operation=token, command="automation.sampler.range",
                 args=dict(target=sustain_source, channel=channel, samplerParameter=6, minimum=low, maximum=high)))
        check(data("vibera_gesture", dict(action="cancel", operation=token))["completionState"] == "cancelled"
              and snapshot() == original and not state()["gestureActive"],
              "Owned physical range drag permits inverted/constant values and exact Cancel")

        invalid = [
            ("init", dict(channel=channel, value=0)),
            ("init", dict(channel=channel, samplerParameter=True, value=0)),
            ("init", dict(channel=channel, samplerParameter=.5, value=0)),
            ("init", dict(channel=channel, samplerParameter="0", value=0)),
            ("init", dict(channel=channel, samplerParameter=31, value=0)),
            ("init", dict(channel=channel, samplerParameter=32, value=0)),
            ("init", dict(channel=999999999, samplerParameter=0, value=0)),
            ("init", dict(channel=channel, samplerParameter=0, value=.5)),
            ("init", dict(channel=channel, samplerParameter=19, value=1.5)),
            ("init", dict(channel=channel, samplerParameter=6, value=1.01)),
            ("init", dict(channel=channel, samplerParameter=6, value=True)),
            ("init", dict(channel=channel, samplerParameter=6, value=.5, surprise=0)),
            ("create", dict(channel=channel, samplerParameter=0, length=0)),
            ("range", dict(channel=channel, samplerParameter=6, target=sustain_source, minimum=0, maximum=1.1)),
            ("range", dict(channel=channel, samplerParameter=6, target=999999999, minimum=0, maximum=1)),
        ]
        for suffix, arguments in invalid:
            before = retained_document()
            error = client.call("vibera_command", dict(command="automation.sampler." + suffix, args=arguments),
                                allow_failure=True)
            check(error.get("isError") and error.get("structuredContent", {}).get("error", {}).get("code") and
                  retained_document() == before, "Structured atomic target refusal: " + suffix + " " + json.dumps(arguments, sort_keys=True))

        def move(a, b):
            for point, value in zip(points, (a, b)):
                command("automation.point.move", target=point["id"], beat=point["beat"], value=value)

        move(.25, .25)
        quarter = render("constant-quarter-sustain.wav")
        move(.25, .75)
        ramp = render("linear-sustain-ramp.wav")
        begin, end = 12000, 60000  # .25..1.25s: settled attack, before the1.5s note-off.

        def left(pcm):
            if len(pcm) < end * 6:
                raise AssertionError("WAV ends before independent sustain observation window")
            return [int.from_bytes(pcm[i:i+3], "little", signed=True) / 8388608
                    for i in range(begin * 6, end * 6, 6)]

        baseline = left(dry)
        energy = sum(x*x for x in baseline)
        check(energy > .01, "Original exported reference has nonzero numerical energy in the declared window")
        for name, pcm, gain, tolerance in (
                ("constantQuarter", quarter, lambda frame: .25, .001),
                ("linearRamp", ramp, lambda frame: .25 + .25 * frame / 48000, .003)):
            expected = math.sqrt(sum((x * gain(frame))**2 for frame, x in enumerate(baseline, begin)) / energy)
            measured = math.sqrt(sum(x*x for x in left(pcm)) / energy)
            report["pcmOracles"].append(dict(name=name, expectedRmsRatio=expected, actualRmsRatio=measured,
                tolerance=tolerance, beginFrame=begin, endFrame=end, sourceRate=48000,
                oracle="Independent physical gain over actual baseline PCM; no automation consumer helper"))
            check(abs(measured - expected) < tolerance, "Actual WAV RMS follows independent physical sustain oracle: " + name)
        check(manual() == expected_manual and inspect("notes", pattern=pattern)[0]["beat"] == 0,
              "Song curves leave manual fallback and raw note geometry unchanged")
        report["pcmQualified"] = True

        # Public profile17 fields; zero code must survive persisted recipe encoding.
        check(all(c["macroAvailable"] for c in contracts.values()), "All six target commands advertise actual macro support")
        macro_state = data("vibera_macros", dict(action="state"))
        owned_macro = "macro.sampler-automation." + uuid.uuid4().hex
        recipe = dict(id=owned_macro, name="Original sampler Automation Clip acceptance", steps=[
            dict(action="automation.sampler.init", currentContext=False,
                 parameters=dict(channel=channel, samplerParameter=0, value=0)),
            dict(action="automation.sampler.range", currentContext=False,
                 parameters=dict(target=sustain_source, channel=channel, samplerParameter=6, minimum=.5, maximum=.5))])
        # Exercise the actual public profile-decoding path, without writing a user
        # profile file or inventing an unsupported raw-profile import endpoint.
        malformed_parameters = [
            dict(channel=channel, samplerParameter=0),  # Omitted physical init must not default to0.
            dict(channel=channel + .5, samplerParameter=0, value=0),
            dict(channel=str(channel), samplerParameter=0, value=0),
            dict(channel=True, samplerParameter=0, value=0),
            dict(channel=channel, samplerParameter=.5, value=0),
            dict(channel=channel, samplerParameter=True, value=0),
        ]
        for parameters in malformed_parameters:
            malformed = dict(id=owned_macro, name=recipe["name"], steps=[dict(
                action="automation.sampler.init", currentContext=False, parameters=parameters)])
            current = data("vibera_macros", dict(action="state"))
            before = retained_document()
            refused = client.call("vibera_macros", dict(action="create", generation=current["generation"],
                                                       macro=malformed), allow_failure=True)
            after = data("vibera_macros", dict(action="state"))
            check(refused.get("isError") and refused.get("structuredContent", {}).get("error", {}).get("code") and
                  after["generation"] == current["generation"] and after["macros"] == current["macros"] and
                  retained_document() == before,
                  "Actual malformed saved-recipe admission preserves disk generation and document: " + json.dumps(parameters, sort_keys=True))
        saved_recipe = data("vibera_macros", dict(action="create", generation=macro_state["generation"], macro=recipe))
        check(saved_recipe["profileVersion"] == 17 and saved_recipe["configurationCommitted"] and
              saved_recipe["profileMatchesMemory"], "Actual typed target recipe commits profile17 to disk and memory")
        readback = data("vibera_macros", dict(action="state", id=owned_macro))
        saved_steps = readback["macros"][0]["steps"]
        check(saved_steps[0]["parameters"]["samplerParameter"] == 0 and
              saved_steps[1]["parameters"]["minimum"] == saved_steps[1]["parameters"]["maximum"] == .5 and
              "beat" not in saved_steps[1]["parameters"], "Saved profile17 readback preserves explicit code0 and physical public range fields")
        before_macro = snapshot()
        ran = data("vibera_macros", dict(action="run", generation=readback["generation"], id=owned_macro))
        after_macro = snapshot()
        check(ran["completionState"] == "completed" and ran["dspSettlingAcknowledged"] is False and
              dict(kind=7, owner=channel, parameter=0, value=0) in after_macro["initial"] and
              inspect("automationLinks", source=sustain_source) ==
              [dict(kind=7, owner=channel, parameter=6, minimum=.5, maximum=.5)],
              "Actual saved macro applies initialization and range through the canonical runner")
        command("edit.undo")
        check(snapshot() == before_macro, "Two-target macro is one exact Undo")
        command("edit.redo")
        check(snapshot() == after_macro, "Two-target macro exact Redo")
        command("edit.undo")
        current = data("vibera_macros", dict(action="state"))
        removed = data("vibera_macros", dict(action="remove", generation=current["generation"], id=owned_macro))
        check(removed["configurationCommitted"] and removed["profileMatchesMemory"] and
              all(m["id"] != owned_macro for m in removed["macros"]), "Only the owned saved test recipe is removed and persisted")
        owned_macro = None
        report["macroReplay"] = True

        project = output / "sampler-automation.vibra"
        job(dict(action="save", path=str(project), overwrite=False))
        project_hash = digest(project)
        saved = snapshot()
        saved_notes = inspect("notes", pattern=pattern)
        saved_manual = manual()
        xml = ET.parse(project)
        check(int(xml.getroot().get("version")) == schema_module.project_schema(ROOT) and
              any(link.get("kind") == "7" and link.get("parameter") == "0" for link in xml.findall("./AUTOMATION/SOURCES/SOURCE/LINKS/LINK")),
              "Actual current-schema file retains kind7 and genuine parameter0")
        job(dict(action="new", discardChanges=True))
        job(dict(action="open", path=str(project), discardChanges=True))
        check(snapshot() == saved and inspect("notes", pattern=pattern) == saved_notes and manual() == saved_manual,
              "Actual save/new/reopen preserves source/point/placement/note IDs and manual fallback")
        check(render("reopened-ramp.wav") == ramp, "Actual reopened project renders byte-identical automated PCM")

        # Explicit compatibility oracle: these Automation targets were introduced
        # in38. This fixture contains no newer project-controller link metadata.
        legacy = ET.parse(project)
        check(legacy.getroot().find("SAMPLER_CONTROLLER_LINKS") is None,
              "Legacy38 fixture has no newer project-controller metadata")
        legacy.getroot().set("version", "38")
        legacy_path = output / "sampler-automation-legacy38.vibra"
        legacy.write(legacy_path, encoding="utf-8", xml_declaration=True)
        report["legacy38FixtureSha256"] = digest(legacy_path)
        job(dict(action="open", path=str(legacy_path), discardChanges=True))
        check(snapshot() == saved and inspect("notes", pattern=pattern) == saved_notes and manual() == saved_manual,
              "Actual schema38 reopen retains Automation identities and manual sampler values")
        check(render("legacy38-ramp.wav") == ramp, "Schema38 compatibility retains byte-identical automated PCM")

        for fault in ("physical-range", "unsupported-code", "legacy-smuggling"):
            tree = ET.parse(project)
            link = next(x for x in tree.findall("./AUTOMATION/SOURCES/SOURCE/LINKS/LINK") if x.get("parameter") == "6")
            if fault == "physical-range":
                link.set("maximum", "64")  # Sustain physical domain is0..1, not all-property union.
            elif fault == "unsupported-code":
                link.set("parameter", "32")  # Pan has no envelope.
            else:
                tree.getroot().set("version", "37")
            path = output / ("invalid-" + fault + ".vibra")
            tree.write(path, encoding="utf-8", xml_declaration=True)
            before = retained_document()
            failed_job(dict(action="open", path=str(path), discardChanges=True))
            check(retained_document() == before, "Malformed asynchronous open preserves complete current document: " + fault)

        blocked = output / "blocked.wav"
        blocked.mkdir(exist_ok=False)
        marker = blocked / "owned-marker.txt"
        marker.write_text("Preserve the existing owned directory and this file.\n", encoding="utf-8")
        marker_hash = digest(marker)
        before = retained_document()
        failed_job(dict(action="export", path=str(blocked), overwrite=True), require_pending=True)
        check(blocked.is_dir() and digest(marker) == marker_hash and retained_document() == before,
              "Actual failed export preserves existing target contents and live document")
        check(digest(project) == project_hash and digest(source) == report["fixtureSha256"],
              "Failed file jobs preserve original saved project and source WAV bytes")
        report["passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        report["traceback"] = traceback.format_exc()
    finally:
        if client:
            if owned_macro:
                try:
                    current = client.data("vibera_macros", dict(action="state"))
                    if any(m["id"] == owned_macro for m in current["macros"]):
                        client.data("vibera_macros", dict(action="remove", generation=current["generation"], id=owned_macro))
                except Exception as failure:
                    report["cleanupError"] = str(failure)
                    report["passed"] = False
            try:
                client.close()
                report["ownedProcessExit"] = client.process.returncode
            except Exception as failure:
                report["closeError"] = str(failure)
                report["passed"] = False
        report["finished"] = True
        report["elapsedSeconds"] = time.monotonic() - began
        encoded = json.dumps(report, indent=2) + "\n"
        (output / "verification.json").write_text(encoded, encoding="utf-8")
        summary_path.write_text(encoded, encoding="utf-8")
    print(json.dumps(dict(passed=report["passed"], checks=len(report["checks"]), output=str(output),
                          error=report.get("error")), indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
