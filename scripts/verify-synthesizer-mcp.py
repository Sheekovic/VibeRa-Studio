"""Actual native instrument/project/history/export/editor workflow; no listening claim."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import traceback
import wave
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True


def main():
    app, output = [Path(x).resolve() for x in sys.argv[1:]]
    if not output.is_relative_to(ROOT / "build/windows"):
        raise ValueError("Use an owned build/windows output directory")
    output = output / ("attempt-" + str(time.time_ns()))
    output.mkdir(parents=True)
    os.environ["VIBERA_SYNTH_USER_DIRECTORY"] = str(output / "user")
    spec = importlib.util.spec_from_file_location("synth_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    command_spec = importlib.util.spec_from_file_location("synth_command_driver", ROOT / "scripts/verify-installed-mcp.py")
    command_driver = importlib.util.module_from_spec(command_spec)
    command_spec.loader.exec_module(command_driver)
    library_path = ROOT / "assets/synthesizer/library.json"
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(passed=False, checks=[], jobs=[], audio=[], errors=[], commandObservations=[], telemetry=[],
                  applicationSha256=digest(app), workerSha256=digest(app.parent / "VibeRa Plugin Worker.exe"),
                  verifierSha256=digest(Path(__file__)), librarySha256=digest(library_path),
                  listening=False, hardware=False, nativeInput=False, liveDspTelemetry=False,
                  telemetryScope="Prepared startup only: ordinary --mcp-test has no virtual callback driver. Live source/voice checks belong to the native-meter desktop fixture.")
    receipt = output / "verification.json"
    receipt.write_text(json.dumps(report, indent=2), encoding="utf-8")
    started = time.monotonic()
    client = None

    def check(condition, name):
        if not condition:
            raise AssertionError(name)
        report["checks"].append(name)

    def call(tool, args=None, failure=False):
        result = client.call(tool, args or {}, allow_failure=failure)
        with (output / "calls.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(dict(tool=tool, args=args, result=result)) + "\n")
        return result["structuredContent"]

    def wait(tool, admission, timeout=45):
        result = admission
        began = time.monotonic()
        while result.get("completionState") == "pending":
            if time.monotonic() - began > timeout:
                raise AssertionError("Timed out " + str(result))
            time.sleep(.03)
            result = call(tool, dict(action="job", operation=admission["operation"]))
        report["jobs"].append(result)
        check(result.get("completionState") == "completed", "actual terminal completion: " + str(result))
        return result

    def settled(plugin):
        began = time.monotonic()
        while time.monotonic() - began < 30:
            state = call("vibera_state")
            rows = call("vibera_plugin", dict(action="list"))["items"]
            found = [r for r in rows if r["id"] == plugin]
            if not state["pendingAudioUpdate"] and len(found) == 1 and found[0]["available"]:
                return found[0]
            time.sleep(.04)
        raise AssertionError("Native instrument preparation failed: " + str(state.get("status")))

    def parameters(plugin):
        row = settled(plugin)
        # Existing async refresh owns an actual current scalar snapshot.
        refreshed = call("vibera_plugin", dict(action="refresh", plugin=plugin, instance=row["instance"]))
        wait("vibera_plugin", refreshed)
        page = call("vibera_synth", dict(action="parameters", plugin=plugin))
        return row, page

    def command(name, args=None):
        # A terminal export commits its file before the host's deferred audio
        # projection is republished. Observe that prerequisite before history;
        # never retry a refused mutation or guess a preparation sleep.
        deadline = time.monotonic() + 30
        while call("vibera_state")["pendingAudioUpdate"]:
            if time.monotonic() >= deadline:
                raise AssertionError("Projection did not settle before " + name)
            time.sleep(.03)
        result = call("vibera_command", dict(command=name, args=args or {}))
        return command_driver.await_command_result(result, lambda tool, request: call(tool, request),
            lambda observation: report["commandObservations"].append(dict(command=name, **observation)))

    def refuse(args, description):
        result = client.call("vibera_synth", args, allow_failure=True)
        error = result.get("structuredContent", {}).get("error", {})
        check(result.get("isError") is True and bool(error.get("code")), description)
        report["errors"].append(dict(case=description, code=error["code"]))
        return error

    def patch(plugin, preset):
        row, page = parameters(plugin)
        return wait("vibera_plugin", call("vibera_synth", dict(action="loadPreset", plugin=plugin,
            instance=row["instance"], generation=page["generation"], preset=preset["id"])))

    def expect_patch(plugin, preset, description):
        _, page = parameters(plugin)
        expected = {}
        for spec, value in zip(discovery["parameters"], preset["settings"]):
            fraction = (value - spec["minimum"]) / (spec["maximum"] - spec["minimum"])
            expected[spec["id"]] = min(1., max(0., fraction)) ** spec["skew"]
        actual = {p["id"]: p["normalized"] for p in page["items"]}
        check(len(expected) == 64 and actual.keys() == expected.keys() and
              all(abs(actual[key] - value) < 2e-5 for key, value in expected.items()), description)

    def step_patch(plugin, action, current, expected):
        row, page = parameters(plugin)
        request = dict(action=action, plugin=plugin, instance=row["instance"], generation=page["generation"])
        if current is not None:
            request["preset"] = current["id"]
        terminal = wait("vibera_plugin", call("vibera_synth", request))
        expect_patch(plugin, expected, "filtered " + action + " restores every scalar of " + expected["id"])
        return terminal

    def expect_telemetry(plugin, preset, voices=None, timeout=10):
        deadline = time.monotonic() + timeout
        observed = {}
        while time.monotonic() < deadline:
            observed = call("vibera_synth", dict(action="status", plugin=plugin)).get("synthesizer", {})
            if (observed.get("available") is True and observed.get("presetId") == preset["id"] and
                    observed.get("sourceKind") == preset["sourceKind"] and
                    (voices is None or (observed.get("activeVoices", -1) > 0) == voices)):
                break
            time.sleep(.04)
        else:
            raise AssertionError("Prepared startup telemetry was not published: " + str(observed))
        check(type(observed.get("activeVoices")) is int and 0 <= observed["activeVoices"] <= 64 and
              observed.get("contentReady") is True and
              observed.get("sampleAvailable") is bool(preset["soundSource"]) and
              observed.get("parameterError") == "" and bool(observed.get("loadStatus")) and
              bool(observed.get("scope")), "prepared startup source/voice/content metadata for " + preset["id"])
        report["telemetry"].append(observed)
        return observed

    def render(name):
        path = output / (name + ".wav")
        wait("vibera_project", call("vibera_project", dict(action="export", path=str(path), overwrite=False)), 90)
        with wave.open(str(path), "rb") as wav:
            check(wav.getnchannels() == 2 and wav.getsampwidth() == 3 and wav.getframerate() == 48000,
                  "actual stereo 48kHz/24-bit export " + name)
            frames = wav.getnframes()
            check(frames == 192000, "bounded eight-beat/four-second fixture " + name)
            pcm = wav.readframes(frames)
        samples = [int.from_bytes(pcm[i:i+3], "little", signed=True) for i in range(0, len(pcm), 3)]
        peak = max(map(abs, samples), default=0)
        check(peak > 100, "nonzero real instrument PCM " + name)
        check(peak < 8388607, "export retains headroom " + name)
        report["audio"].append(dict(name=name, frames=frames, peak24=peak, sha256=hashlib.sha256(pcm).hexdigest()))
        return pcm

    try:
        client = module.VibeRaClient(app)
        report.update(pid=client.process.pid, version=client.server["version"])
        tools = client.request("tools/list", {})["tools"]
        check(any(t["name"] == "vibera_synth" for t in tools), "typed synth discovery installed")
        semantic_actions = {
            "discover":"list", "browser.state":"browser", "browser.filter":"filter", "browser.favorite":"favorite",
            "editor.navigate":"navigate", "preset.load":"loadPreset", "preset.previous":"previous", "preset.next":"next",
            "user.list":"listUserPresets", "user.save":"saveUserPreset", "user.load":"loadUserPreset",
            "parameters":"parameters", "parameter.set":"set", "preview.on":"previewOn", "preview.off":"previewOff",
            "preview.panic":"panic", "editor.open":"openEditor", "editor.close":"closeEditor"}
        for suffix, action in semantic_actions.items():
            entries=call("vibera_commands",dict(command="synth."+suffix,includeSchemas=True))["operations"]
            check(len(entries)==1 and entries[0]["semanticTool"]=="vibera_synth" and
                  entries[0]["semanticAction"]==action and
                  entries[0]["arguments"]["properties"]["action"]["const"]==action and
                  entries[0]["arguments"]["additionalProperties"] is False and
                  entries[0]["macroAvailable"] is False,
                  "registry-derived typed Synth capability: "+suffix)

        discovery = call("vibera_synth", dict(action="list", genre="All Genres", family="All", search="", favoritesOnly=False))
        # BEFORE any preference or user-file mutation, require actual isolation.
        check(Path(discovery["userDirectory"]).resolve() == output / "user", "owned user directory honored before writes")
        presets = discovery["presets"]
        check(len(presets) == 77 and len({p["id"] for p in presets}) == 77, "all 77 distinct stable factory IDs")
        check(all(g in discovery["genres"] for g in ["Pop", "Jazz", "Funk", "Hip-Hop", "R&B", "Electronic", "Rock", "Cinematic", "Arabic"]), "all nine genre groups")
        check(discovery["separateVST3"] is False, "native engine does not require external VST3")
        families = {"Piano", "Electric Piano", "Violin", "Strings", "Bass", "Guitar", "Brass", "Woodwinds",
                    "Synth Leads", "Pads", "Plucks", "Drums / Percussion", "Arabic / World"}
        check(set(discovery["families"]) == families | {"All"} and {p["family"] for p in presets} == families,
              "all thirteen required families have actual presets")
        kinds = {kind: sum(p["sourceKind"] == kind for p in presets) for kind in ["SAMPLE", "SAMPLE + SYNTH", "SYNTHESIS"]}
        check(kinds == {"SAMPLE": 20, "SAMPLE + SYNTH": 1, "SYNTHESIS": 56}, "honest sample/hybrid/oscillator counts")
        maps = {instrument["id"] for instrument in json.loads(library_path.read_text(encoding="utf-8"))["instruments"]}
        check(len(maps) == 20 and {p["soundSource"] for p in presets if p["sourceKind"] != "SYNTHESIS"} == maps and
              all(not p["soundSource"] for p in presets if p["sourceKind"] == "SYNTHESIS"), "every recorded-source claim resolves to the real library")
        specs = {p["key"]: p for p in discovery["parameters"]}
        check(len(specs) == 64 and len({p["id"] for p in specs.values()}) == 64 and
              all(p["minimum"] <= p["default"] <= p["maximum"] for p in specs.values()), "all stable parameter keys, IDs and ranges")
        for genre in ["Pop", "Jazz", "Funk", "Hip-Hop", "R&B", "Electronic", "Rock", "Cinematic", "Arabic"]:
            filtered_rows = call("vibera_synth", dict(action="list", genre=genre, family="All", search="", favoritesOnly=False))["presets"]
            check(filtered_rows and all(genre in p["genres"] for p in filtered_rows), "actual genre filtering " + genre)
        for family in families:
            filtered_rows = call("vibera_synth", dict(action="list", genre="All Genres", family=family, search="", favoritesOnly=False))["presets"]
            check(filtered_rows and all(p["family"] == family for p in filtered_rows), "actual instrument family filtering " + family)
        refuse(dict(action="list", genre="Not a genre"), "unknown transient genre is refused")
        refuse(dict(action="filter", family="Not an instrument"), "unknown persistent family is refused")
        sampled = next(p for p in presets if p["sourceKind"] == "SAMPLE" and "piano" in p["soundSource"])
        synthesized = next(p for p in presets if p["sourceKind"] == "SYNTHESIS" and p["family"] == "Bass")
        hybrid = next(p for p in presets if p["sourceKind"] == "SAMPLE + SYNTH")
        call("vibera_synth", dict(action="favorite", preset=sampled["id"], enabled=True))
        favorites = call("vibera_synth", dict(action="list", genre="Favorites", family="All", search=""))["presets"]
        check(any(p["id"] == sampled["id"] for p in favorites), "native favorite preference service")
        call("vibera_synth", dict(action="favorite", preset=sampled["id"], enabled=False))
        check(not call("vibera_synth", dict(action="list", genre="Favorites"))["presets"], "favorite removal readback")
        call("vibera_synth", dict(action="filter", genre="Pop", family="All", search="velvet"))
        filtered = call("vibera_synth", dict(action="browser"))
        check(filtered["browser"]["search"] == "velvet", "composed persistent browser filters")
        call("vibera_synth", dict(action="filter", genre="All Genres", family="All", search=""))
        navigation = dict(effectPage=4, octave=-1, scrollX=17, scrollY=29, presetScrollY=121,
                          selectedPresetId=hybrid["id"])
        navigation_revision = call("vibera_state")["revision"]
        navigated = call("vibera_synth", dict(action="navigate", **navigation))["browser"]
        persisted = json.loads((output / "user/browser.json").read_text(encoding="utf-8-sig"))
        check(all(navigated[key] == value and persisted[key] == value for key, value in navigation.items()),
              "native editor page/octave/workspace/list-scroll/highlight preferences are durably saved")
        reread = call("vibera_synth", dict(action="browser"))["browser"]
        check(all(reread[key] == value for key, value in navigation.items()) and
              call("vibera_state")["revision"] == navigation_revision,
              "fresh browser readback retains navigation without a score history revision")
        for invalid in [dict(effectPage=5), dict(octave=-2), dict(presetScrollY=-1),
                        dict(selectedPresetId="owned-unknown-factory-id")]:
            refuse(dict(action="navigate", **invalid), "invalid navigation is refused: " + str(invalid))
        check(call("vibera_synth", dict(action="browser"))["browser"] == reread and
              json.loads((output / "user/browser.json").read_text(encoding="utf-8-sig")) == persisted,
              "refused navigation retains every persisted browser preference")
        report["browserNavigation"] = dict(requested=navigation, persisted=persisted, revision=navigation_revision,
                                           scope="Durable preferences; native viewport adoption is separate component acceptance")
        call("vibera_synth", dict(action="navigate", effectPage=0, octave=0, scrollX=0, scrollY=0,
                                  presetScrollY=0, selectedPresetId=""))
        fixture = ET.parse(ROOT / "examples/First Light.vibra")
        for channel in fixture.findall("./CHANNELS/CHANNEL"):
            channel.set("muted", "1")
        for pattern in fixture.findall("./PATTERNS/PATTERN"):
            for note in list(pattern):
                if note.tag == "NOTE":
                    pattern.remove(note)
        placements = fixture.find("./PLACEMENTS")
        for placement in list(placements)[1:]:
            placements.remove(placement)
        placements[0].set("beat", "0.0")
        placements[0].set("length", "8.0")
        source = output / "source.vibra"
        fixture.write(source, encoding="utf-8", xml_declaration=True)
        wait("vibera_project", call("vibera_project", dict(action="open", path=str(source), discardChanges=True)))
        created = call("vibera_synth", dict(action="create"))
        channel, plugin = created["channel"], created["plugin"]
        row, page = parameters(plugin)
        check(len(page["items"]) == len(discovery["parameters"]), "all exposed parameters have native MCP IDs")
        expect_telemetry(plugin, presets[0], voices=False)
        # Each exposed setter is exercised independently through the canonical
        # native ACK/history service, not inferred from loading a whole patch.
        report["parameterCases"] = []
        for parameter in discovery["parameters"]:
            if parameter["key"] == "factory-preset":
                continue  # The coupled source identity is exercised by full loads.
            row, page = parameters(plugin)
            baseline = {p["id"]: p["normalized"] for p in page["items"]}
            target = parameter["maximum"] if baseline[parameter["id"]] == 0 else parameter["minimum"]
            expected = 1 if target == parameter["maximum"] else 0
            terminal = wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin,
                instance=row["instance"], generation=page["generation"], key=parameter["key"], value=target)))
            _, changed = parameters(plugin)
            observed = {p["id"]: p["normalized"] for p in changed["items"]}
            check(terminal["historyCommitted"] and observed[parameter["id"]] == expected and
                  all(observed[key] == value for key, value in baseline.items() if key != parameter["id"]),
                  "individual parameter ACK/readback with unchanged neighbours: " + parameter["key"])
            report["parameterCases"].append(dict(key=parameter["key"], nativeValue=target,
                                                  normalized=expected, historyCommitted=True))
        check(len(report["parameterCases"]) == 63, "all63 exposed scalar setter branches qualified")
        # Each write above commits captured canonical history. Replay one exact
        # scalar edge here; cross-bank, user-sound and automation history are
        # exercised separately below and all46 scalar rewrites in core tests.
        command("edit.undo")
        _, restored = parameters(plugin)
        check({p["id"]: p["normalized"] for p in restored["items"]} == baseline,
              "final individual setter exact Undo preserves all other edited parameters")
        patch(plugin,presets[0])
        pattern = call("vibera_state")["selectedPattern"]
        for beat, pitch, velocity in [(0, 60, .4), (1, 64, .7), (2, 67, .95)]:
            command("notes.add", dict(pattern=pattern, channel=channel, beat=beat, length=.7, pitch=pitch, value=velocity))
        filtered_bass = call("vibera_synth", dict(action="list", genre="R&B", family="Bass", search="SYNTHESIS",
                                                 favoritesOnly=False))["presets"]
        check(len(filtered_bass) >= 2, "composed genre/family/source search provides distinct navigation patches")
        first_step, last_step = filtered_bass[0], filtered_bass[-1]
        for preset in [first_step, last_step]:
            call("vibera_synth", dict(action="favorite", preset=preset["id"], enabled=True))
        limited = call("vibera_synth", dict(action="filter", genre="R&B", family="Bass", search="SYNTHESIS",
                                           favoritesOnly=True))["presets"]
        check([p["id"] for p in limited] == [first_step["id"], last_step["id"]],
              "previous/next navigation uses the actual ordered composed favorites filter")
        patch(plugin, first_step)
        check(step_patch(plugin, "next", first_step, last_step)["historyCommitted"],
              "filtered next commits one real full-patch history operation")
        check(step_patch(plugin, "previous", last_step, first_step)["historyCommitted"],
              "filtered previous reverses the exact filtered order")
        step_patch(plugin, "previous", first_step, last_step)
        step_patch(plugin, "next", sampled, first_step)
        call("vibera_synth", dict(action="navigate", selectedPresetId=last_step["id"]))
        check(not step_patch(plugin, "next", None, first_step)["historyCommitted"],
              "omitted current ID uses shared row highlight and reports an identical-patch no-op")
        call("vibera_synth", dict(action="filter", search="owned-no-matching-sound"))
        row, page = parameters(plugin)
        before_empty = {p["id"]: p["normalized"] for p in page["items"]}
        error = refuse(dict(action="next", plugin=plugin, instance=row["instance"], generation=page["generation"],
                            preset=first_step["id"]), "empty filtered navigation is refused")
        check(error["code"] == "synth.emptyFilter", "empty filtered navigation has a precise structured outcome")
        _, page = parameters(plugin)
        check({p["id"]: p["normalized"] for p in page["items"]} == before_empty,
              "empty-filter refusal preserves the entire actual native patch")
        for preset in [first_step, last_step]:
            call("vibera_synth", dict(action="favorite", preset=preset["id"], enabled=False))
        call("vibera_synth", dict(action="filter", genre="All Genres", family="All", search="", favoritesOnly=False))
        call("vibera_synth", dict(action="navigate", selectedPresetId=""))
        # The default is Velvet Upright. Move away first so the changed-patch
        # history assertion does not manufacture an Undo edge for a true no-op.
        patch(plugin, synthesized)
        patch(plugin, hybrid)
        hybrid_pcm = render("recorded-synthesis-layer")
        row, page = parameters(plugin)
        loaded = call("vibera_synth", dict(action="loadPreset", plugin=plugin, instance=row["instance"], generation=page["generation"], preset=sampled["id"]))
        terminal = wait("vibera_plugin", loaded)
        check(terminal["historyCommitted"], "whole factory patch is one acknowledged captured Undo operation")
        piano = render("sampled-piano")
        check(not patch(plugin, sampled)["historyCommitted"], "identical patch is an honest no-op with no extra Undo edge")
        row, page = parameters(plugin)
        loaded = call("vibera_synth", dict(action="loadPreset", plugin=plugin, instance=row["instance"], generation=page["generation"], preset=synthesized["id"]))
        wait("vibera_plugin", loaded)
        bass = render("synthesized-bass")
        check(piano != bass and hybrid_pcm != piano and hybrid_pcm != bass, "recorded/synthesized/hybrid sources render different actual PCM")
        command("edit.undo")
        settled(plugin)
        check(render("undo-piano") == piano, "whole patch Undo restores exact sampled sound")
        command("edit.redo")
        settled(plugin)
        check(render("redo-bass") == bass, "whole patch Redo restores exact synthesized sound")
        row, page = parameters(plugin)
        unchanged = {p["id"]: p["normalized"] for p in page["items"]}
        refuse(dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="master-db", value=100),
               "out-of-range native value is refused")
        refuse(dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="factory-preset", value=0),
               "source identity cannot substitute for a whole-patch load")
        refuse(dict(action="loadPreset", plugin=plugin, instance="stale owned fixture", generation=page["generation"], preset=sampled["id"]),
               "stale runtime instance refuses patch mutation")
        _, page = parameters(plugin)
        check({p["id"]: p["normalized"] for p in page["items"]} == unchanged, "failed admissions preserve every actual native scalar")

        row, page = parameters(plugin)
        admission = call("vibera_synth", dict(action="loadPreset", plugin=plugin, instance=row["instance"], generation=page["generation"], preset=sampled["id"]))
        cancellation = client.call("vibera_plugin", dict(action="cancel", operation=admission["operation"]), allow_failure=True)
        cancelled = cancellation.get("structuredContent", {})
        if cancellation.get("isError"):
            check(cancelled.get("error", {}).get("code") == "too_late", "dispatched cancellation returns structured too-late refusal")
            wait("vibera_plugin", admission)
            report["patchCancellation"] = "too_late; original completion observed"
        elif cancelled.get("completionState") == "completed":
            check(cancelled["operation"] == admission["operation"], "completed patch keeps exact cancellation observation owner")
            report["patchCancellation"] = "already completed before cancellation request"
        else:
            check(cancelled.get("completionState") == "cancelled" and not cancelled.get("nativeApplied"),
                  "undispatched cancellation completes without native mutation")
            report["patchCancellation"] = "cancelled before dispatch"
        patch(plugin, synthesized)
        # Device-free MCP preserves a deferred document projection. Play here
        # starts new device preparation, so it cannot qualify a live switch.
        # The real 48k/256 native-device host and desktop fixtures own that test.
        report["playingSwitchScope"] = "No device-free playback claim; actual native host/desktop callback fixtures qualify live sound and source changes."
        patch(plugin, synthesized)
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="master-db", value=-18)))
        quieter = render("master-edited")
        check(quieter != bass, "native-unit master edit changes actual export")
        row = settled(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="saveUserPreset", plugin=plugin, instance=row["instance"], name="MCP sound")))
        user_file = output / "user/MCP sound.vrsynth"
        check(user_file.is_file(), "user preset saved to isolated writable content directory")
        saved_bytes = user_file.read_bytes()
        user_library = call("vibera_synth", dict(action="listUserPresets"))
        check(len(user_library["presets"]) == 1 and not user_library["errors"],
              "actual user library lists the independently saved sound without errors")
        saved_user = user_library["presets"][0]
        user_id = saved_user["id"]
        check(len(user_id) == 32 and saved_user["factory"] is False and saved_user["name"] == "MCP sound" and
              saved_user["basePreset"] == synthesized["id"] and saved_user["source"] == "SYNTHESIS" and
              saved_user["family"] == synthesized["family"] and saved_user["sampleAsset"] == "" and
              saved_user["macros"] == synthesized["macroMapping"] and Path(saved_user["path"]).resolve() == user_file,
              "user discovery exposes stable own identity, source, base and macro metadata")
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="master-db", value=-9)))
        louder = render("before-user-load")
        row, page = parameters(plugin)
        overwrite_values = {p["id"]: p["normalized"] for p in page["items"]}
        error = refuse(dict(action="saveUserPreset", plugin=plugin, instance=row["instance"], name="MCP sound"),
                       "existing user preset requires explicit overwrite")
        check(error["code"] == "synth.overwriteRequired" and user_file.read_bytes() == saved_bytes,
              "overwrite refusal retains the exact preceding user file")
        _, page = parameters(plugin)
        check({p["id"]: p["normalized"] for p in page["items"]} == overwrite_values,
              "overwrite refusal does not mutate current instrument scalars")
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="loadUserPreset", plugin=plugin, instance=row["instance"], generation=page["generation"], name="MCP sound")))
        check(render("loaded-user-preset") == quieter, "actual user-preset load restores exact saved sound")
        command("edit.undo")
        settled(plugin)
        check(render("undo-user-preset") == louder, "user-preset Undo restores preceding actual sound")
        command("edit.redo")
        settled(plugin)
        check(render("redo-user-preset") == quieter, "user-preset Redo restores saved actual sound")
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="master-db", value=-9)))
        row = settled(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="saveUserPreset", plugin=plugin, instance=row["instance"],
                                                       name="MCP sound", overwrite=True)))
        overwritten = call("vibera_synth", dict(action="listUserPresets"))
        check(not overwritten["errors"] and len(overwritten["presets"]) == 1 and
              overwritten["presets"][0]["id"] == user_id and user_file.read_bytes() != saved_bytes,
              "explicit overwrite changes actual saved state while preserving stable user identity")
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="master-db", value=-18)))
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="loadUserPreset", plugin=plugin, instance=row["instance"],
                                                       generation=page["generation"], preset=user_id)))
        check(render("overwritten-user-id") == louder, "stable-ID load reads the actual explicitly overwritten sound")
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="master-db", value=-18)))
        row = settled(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="saveUserPreset", plugin=plugin, instance=row["instance"],
                                                       name="MCP sound", overwrite=True)))
        nested_user = output / "user/Owned nested library/Renamed sound.vrsynth"
        nested_user.parent.mkdir()
        user_file.rename(nested_user)
        nested_library = call("vibera_synth", dict(action="listUserPresets"))
        check(not nested_library["errors"] and len(nested_library["presets"]) == 1 and
              nested_library["presets"][0]["id"] == user_id and
              Path(nested_library["presets"][0]["path"]).resolve() == nested_user,
              "managed nested file relocation retains the independent stable user ID")
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"], generation=page["generation"], key="master-db", value=-9)))
        row, page = parameters(plugin)
        loaded_user = wait("vibera_plugin", call("vibera_synth", dict(action="loadUserPreset", plugin=plugin,
            instance=row["instance"], generation=page["generation"], preset=user_id)))
        check(loaded_user["historyCommitted"] and render("nested-user-id") == quieter,
              "stable-ID load resolves a nested managed file and commits its actual full patch")
        report["userPresetIdentity"] = dict(id=user_id, nestedPath=str(nested_user), sha256=digest(nested_user))
        row, page = parameters(plugin)
        refuse(dict(action="loadUserPreset", plugin=plugin, instance=row["instance"], generation=page["generation"],
                    preset=user_id, name="MCP sound"), "ambiguous user ID and basename are refused")
        refuse(dict(action="loadUserPreset", plugin=plugin, instance=row["instance"], generation=page["generation"],
                    preset="0" * 32), "unknown stable user ID is refused")
        (output / "user/Malformed.vrsynth").write_bytes(b"Owned deliberately malformed preset")
        row, page = parameters(plugin)
        unchanged = {p["id"]: p["normalized"] for p in page["items"]}
        refuse(dict(action="loadUserPreset", plugin=plugin, instance=row["instance"], generation=page["generation"], name="Malformed"),
               "malformed user preset refuses before any native mutation")
        _, page = parameters(plugin)
        check({p["id"]: p["normalized"] for p in page["items"]} == unchanged, "malformed user-file load retains every scalar")

        row, page = parameters(plugin)
        master_id = specs["master-db"]["id"]
        initialized = call("vibera_plugin", dict(action="automate", plugin=plugin, instance=row["instance"], generation=page["generation"], parameter=master_id, automation="init"))
        check(wait("vibera_plugin", initialized)["historyCommitted"], "Synth initialization uses acknowledged canonical automation history")
        initials = call("vibera_inspect", dict(entity="automationInitialValues"))["items"]
        check(any(p["owner"] == plugin and p["parameter"] == master_id for p in initials), "initialized native parameter has actual project readback")
        command("edit.undo")
        settled(plugin)
        check(not any(p["owner"] == plugin and p["parameter"] == master_id for p in call("vibera_inspect", dict(entity="automationInitialValues"))["items"]),
              "initialization Undo removes that exact native parameter declaration")
        command("edit.redo")
        row, page = parameters(plugin)
        wait("vibera_plugin", call("vibera_plugin", dict(action="automate", plugin=plugin, instance=row["instance"], generation=page["generation"], parameter=master_id, automation="removeInit")))
        save = output / "restored.vibra"
        wait("vibera_project", call("vibera_project", dict(action="save", path=str(save), overwrite=False)))
        settled(plugin)
        wait("vibera_project", call("vibera_project", dict(action="open", path=str(save), discardChanges=True)))
        settled(plugin)
        check(render("reopened") == quieter, "project save/reopen preserves exact sound, samples and effects")
        row = settled(plugin)
        opened = wait("vibera_plugin", call("vibera_synth", dict(action="openEditor", plugin=plugin, instance=row["instance"])))
        check(opened.get("windowAtCompletion", {}).get("visible") is True, "native synthesizer window is actually acknowledged visible")
        closed = wait("vibera_plugin", call("vibera_synth", dict(action="closeEditor", plugin=plugin, instance=row["instance"])))
        check(closed.get("windowAtCompletion", {}).get("visible") is False, "native synthesizer close is actually acknowledged")
        reopened_editor = wait("vibera_plugin", call("vibera_synth", dict(action="openEditor", plugin=plugin, instance=row["instance"])))
        check(reopened_editor.get("windowAtCompletion", {}).get("visible") is True,
              "same native processor reopens after acknowledged editor destruction")
        wait("vibera_plugin", call("vibera_synth", dict(action="closeEditor", plugin=plugin, instance=row["instance"])))
        preview_error = refuse(dict(action="previewOn", plugin=plugin, note=60, velocity=90),
                               "device-free preview refuses unavailable audio")
        check(preview_error["code"] == "synth.previewRefused" and bool(preview_error.get("message")),
              "device-free preview gives an actionable structured refusal rather than claiming a DSP note")
        call("vibera_synth", dict(action="panic", plugin=plugin))
        report["previewScope"] = "Device-free refusal and idempotent panic only; live preview on/off is tested by the native virtual-device fixture."
        command("channel.mute", dict(channel=channel, enabled=True))
        command("edit.undo")
        settled(plugin)
        check(render("after-mute-undo") == quieter, "Channel mute history retains native sound")
        check(digest(library_path) == report["librarySha256"], "factory manifest remains unchanged after user/browser operations")
        patch(plugin,synthesized)
        host_fixture=output / "host-fixture.vibra"
        wait("vibera_project",call("vibera_project",dict(action="save",path=str(host_fixture),overwrite=False)))
        report["nativeHostFixture"]=dict(path=str(host_fixture),sha256=digest(host_fixture),preset=synthesized["id"])
        # Independent existing-Channel assignment through the generic semantic
        # command, after retaining the earlier Synth host fixture unchanged.
        assignment_started = time.monotonic()
        replacement = call("vibera_commands", dict(
            command="plugin.instrument.replace", includeSchemas=True))["operations"]
        check(len(replacement) == 1 and replacement[0]["supported"] is True and
              replacement[0]["mapping"] == "vibera_command" and
              replacement[0]["arguments"]["properties"]["channel"]["type"] == "integer" and
              replacement[0]["arguments"]["properties"]["text"]["type"] == "string",
              "existing-Channel assignment has a discovered canonical semantic command schema")
        descriptor = call("vibera_synth", dict(action="descriptor"))["descriptor"]
        description = ET.fromstring(descriptor)
        check(description.get("format") == "VibeRa" and
              description.get("file") == "vibera.instrument.synthesizer" and
              description.get("isInstrument") == "1",
              "Synth assignment uses its actual canonical native instrument descriptor")
        wait("vibera_project", call("vibera_project", dict(
            action="open", path=str(source), discardChanges=True)))
        channels = call("vibera_inspect", dict(entity="channels"))["items"]
        target = next(c["id"] for c in channels
                      if c["source"] == "builtin:bass" and not c.get("plugin") and not c["audioClip"])
        assignment_pattern = call("vibera_state")["selectedPattern"]
        command("channel.mute", dict(channel=target, enabled=False))
        for beat, pitch, velocity in [(0, 60, .4), (1, 64, .7), (2, 67, .95)]:
            command("notes.add", dict(pattern=assignment_pattern, channel=target,
                                     beat=beat, length=.7, pitch=pitch, value=velocity))
        before_channels = call("vibera_inspect", dict(entity="channels"))["items"]
        before_notes = call("vibera_inspect", dict(entity="notes", pattern=assignment_pattern))["items"]
        before_pcm = render("assignment-before-builtin")

        def assignment_document(label):
            path = output / ("assignment-" + label + ".vibra")
            wait("vibera_project", call("vibera_project", dict(action="save", path=str(path), overwrite=False)))
            return ET.tostring(ET.parse(path).getroot(), encoding="utf-8")

        before_document = assignment_document("before")
        before_revision = call("vibera_state")["revision"]
        command("plugin.instrument.replace", dict(channel=target, text=descriptor))
        after_channels = call("vibera_inspect", dict(entity="channels"))["items"]
        assigned = next(c for c in after_channels if c["id"] == target)
        assigned_plugin = assigned["plugin"]
        preceding = next(c for c in before_channels if c["id"] == target)
        preserved = lambda row: {k: v for k, v in row.items() if k not in ("plugin", "name")}
        check(len(after_channels) == len(before_channels) and assigned_plugin > 0 and
              assigned["name"] == description.get("name") and preserved(assigned) == preserved(preceding) and
              [c for c in after_channels if c["id"] != target] ==
              [c for c in before_channels if c["id"] != target],
              "semantic Synth assignment preserves existing Channel ID, sampler role, routing and unrelated Channels")
        assigned_notes = call("vibera_inspect", dict(entity="notes", pattern=assignment_pattern))["items"]
        # These five fields describe the current instrument's capabilities, not
        # persisted Note data. Every other field, including future readback
        # fields and unrelated Channels, must remain exactly unchanged.
        sampler_scope = "Local sampler; model state, check pendingAudioUpdate separately"
        instrument_scope = "Pan/Fine stored only; native/VST3 Pan/Fine unsupported. Shift uses score scheduling"
        local_modulation_scope = ("Original local sampler composition: clamp(global + 2*(note-0.5),0,1). "
            "Mod X offsets cutoff; Mod Y offsets resonance. Filter Off is a no-op. Source metadata, not a voice/DSP ACK.")
        instrument_modulation_scope = "Mod X/Y stored only; native/VST3 and Audio Clip per-note sampler modulation unsupported."
        target_notes = [n for n in before_notes if n["channel"] == target]
        check(bool(target_notes) and all(n["noteExpressionSupported"] is True and
              n["noteExpressionScope"] == sampler_scope and n["noteModulationSupported"] is True and
              n["noteModulationScope"] == local_modulation_scope and
              n["noteModulationAcknowledged"] is None and n["transitionSupported"] is True and
              n["transitionAcknowledged"] is None for n in target_notes),
              "assignment baseline exposes actual local-sampler expression capabilities without a fake DSP ACK")
        expected_assigned_notes = [dict(n, noteExpressionSupported=False, noteExpressionScope=instrument_scope,
            noteModulationSupported=False, noteModulationScope=instrument_modulation_scope,
            transitionSupported=False)
                                   if n["channel"] == target else dict(n) for n in before_notes]
        report["assignmentNoteReadback"] = dict(before=before_notes, assigned=assigned_notes,
            instrumentDerivedFields=["noteExpressionSupported", "noteExpressionScope",
                                     "noteModulationSupported", "noteModulationScope", "transitionSupported"])
        check(assigned_notes == expected_assigned_notes,
              "semantic Synth assignment preserves every score note field and updates only target instrument capabilities")
        assigned_row, assigned_page = parameters(assigned_plugin)
        check(assigned_row["instrument"] is True and assigned_row["channel"] == target and
              bool(assigned_row["instance"]) and len(assigned_page["items"]) == 64,
              "semantic Synth assignment owns a ready actual native worker with all46 readable scalars")
        expect_patch(assigned_plugin, presets[0], "assigned Synth starts with the exact complete native default patch")
        expect_telemetry(assigned_plugin, presets[0], voices=False)
        check(call("vibera_state")["revision"] == before_revision + 1,
              "semantic Synth assignment commits exactly one canonical project revision")
        after_pcm = render("assignment-native-synth")
        check(after_pcm != before_pcm, "assigned native Synth renders different real PCM from the preceding builtin bass")
        after_document = assignment_document("native")
        command("edit.undo")
        check(call("vibera_inspect", dict(entity="channels"))["items"] == before_channels and
              call("vibera_inspect", dict(entity="notes", pattern=assignment_pattern))["items"] == before_notes and
              not any(p["id"] == assigned_plugin for p in call("vibera_plugin", dict(action="list"))["items"]),
              "one assignment Undo restores the exact preceding Channel/score and removes the assigned native plugin")
        check(render("assignment-undo-builtin") == before_pcm,
              "one assignment Undo restores exact preceding builtin instrument PCM")
        check(assignment_document("undo") == before_document,
              "one assignment Undo restores exact complete saved musical project state")
        command("edit.redo")
        redone_row, redone_page = parameters(assigned_plugin)
        check(call("vibera_inspect", dict(entity="channels"))["items"] == after_channels and
              call("vibera_inspect", dict(entity="notes", pattern=assignment_pattern))["items"] == assigned_notes and
              redone_row["channel"] == target and redone_row["available"] is True and
              {p["id"]: p["normalized"] for p in redone_page["items"]} ==
              {p["id"]: p["normalized"] for p in assigned_page["items"]},
              "one assignment Redo restores the exact stable plugin ID, Channel/score and all46 native scalars")
        check(render("assignment-redo-synth") == after_pcm,
              "one assignment Redo restores exact assigned native Synth PCM")
        check(assignment_document("redo") == after_document,
              "one assignment Redo restores exact complete assigned project state")
        report["existingChannelAssignment"] = dict(
            command="plugin.instrument.replace", channel=target, plugin=assigned_plugin,
            descriptorSha256=hashlib.sha256(descriptor.encode("utf-8")).hexdigest(),
            beforeRevision=before_revision, assignedRevision=before_revision + 1,
            initialInstance=assigned_row["instance"], redoneInstance=redone_row["instance"],
            beforePcmSha256=hashlib.sha256(before_pcm).hexdigest(),
            assignedPcmSha256=hashlib.sha256(after_pcm).hexdigest(),
            seconds=time.monotonic() - assignment_started,
            scope="Actual semantic assignment onto a pre-existing builtin Channel; worker/scalars, canonical history, full saved state and offline PCM. No native input, hardware or listening claim.")
        report["passed"] = True
    except BaseException as error:
        report["error"] = repr(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        close_error = None
        if client is not None:
            try:
                client.close()
                report["closedGracefully"] = True
            except Exception as error:
                report["passed"] = False
                report["closedGracefully"] = False
                report["closeError"] = repr(error)
                close_error = error
            (output / "stderr.log").write_text("".join(client.stderr), encoding="utf-8")
        report["seconds"] = time.monotonic() - started
        receipt.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(dict(passed=report["passed"], output=str(output), checks=len(report["checks"]), seconds=report["seconds"])))
        if close_error is not None and "error" not in report:
            raise close_error


if __name__ == "__main__":
    main()
