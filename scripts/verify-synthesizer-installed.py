"""Installed native Synthesizer/content acceptance; no user-library writes.

Usage: python scripts/verify-synthesizer-installed.py ABS_APP ABS_BUILD_WINDOWS_OUTPUT
Run against staging, final, or a moved owned installation. Source/full MCP, native
input, live callbacks, listening, and hardware acceptance remain separate checks.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import sys
import time
import traceback
import wave
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def ordinary(path, directory=False):
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise ValueError("Refuse reparse input/output: " + str(path))
    if not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)):
        raise ValueError("Unexpected input/output type: " + str(path))


def safe_path(raw):
    path = Path(raw)
    if not path.is_absolute() or any(p in (".", "..") for p in raw.replace("\\", "/").split("/")):
        raise ValueError("Require absolute paths without traversal")
    for ancestor in reversed((path, *path.parents)):
        if ancestor.exists():
            ordinary(ancestor, ancestor.is_dir())
    return path.resolve()


def content_file(root, relative):
    if not isinstance(relative, str) or not relative or "\\" in relative:
        raise ValueError("Malformed content-relative path")
    path = Path(relative)
    if path.is_absolute() or any(part in (".", "..") for part in relative.split("/")) or ":" in relative:
        raise ValueError("Content path escapes its installation")
    result = safe_path(str(root / path))
    if not result.is_relative_to(root):
        raise ValueError("Content path escapes its installation")
    ordinary(result)
    return result


def file_snapshot(root):
    """Read-only bounded user-content snapshot; do not create the directory."""
    if not root.exists():
        return {"exists": False, "directories": [], "files": {}}
    ordinary(root, True)
    directories, files, total = [], {}, 0
    pending = [root]
    while pending:
        directory = pending.pop()
        for path in sorted(directory.iterdir()):
            ordinary(path, path.is_dir())
            relative = path.relative_to(root).as_posix()
            if path.is_dir():
                directories.append(relative)
                pending.append(path)
            else:
                size = path.stat().st_size
                total += size
                if len(files) >= 10000 or total > 256 * 1024 * 1024:
                    raise RuntimeError("User-content snapshot exceeds bounded verification budget")
                files[relative] = {"bytes": size, "sha256": digest(path)}
    return {"exists": True, "directories": sorted(directories), "files": files}


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def native_xml(encoded):
    """Decode VibeRa/JUCE's original little-bit opaque-state representation."""
    match = re.fullmatch(r"([0-9]{1,8})\.([.A-Za-z0-9+]*)", encoded)
    if not match:
        raise ValueError("Malformed opaque native state")
    length, text = int(match[1]), match[2]
    if not 0 < length <= 262144 or len(text) != (length * 8 + 5) // 6:
        raise ValueError("Native state length mismatch")
    alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    binary, accumulator, bits = bytearray(), 0, 0
    for character in text:
        accumulator |= alphabet.index(character) << bits
        bits += 6
        if bits >= 8:
            binary.append(accumulator & 255)
            accumulator >>= 8
            bits -= 8
    if accumulator or len(binary) != length or len(binary) < 10:
        raise ValueError("Noncanonical native state padding")
    magic, xml_length = struct.unpack_from("<II", binary)
    if magic != 0x21324356 or xml_length != len(binary) - 9 or binary[-1] != 0:
        raise ValueError("Invalid native binary XML envelope")
    payload = bytes(binary[8:-1]).decode("utf-8")
    if "\0" in payload or "<!DOCTYPE" in payload or "<!ENTITY" in payload:
        raise ValueError("Unexpected native XML content")
    return ET.fromstring(payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app")
    parser.add_argument("output")
    args = parser.parse_args()
    app, output_root = safe_path(args.app), safe_path(args.output)
    allowed = ROOT / "build/windows"
    if output_root == allowed or not output_root.is_relative_to(allowed):
        raise ValueError("Output must be an owned directory below repository build/windows")
    if output_root.is_relative_to(app.parent):
        raise ValueError("Verification output must remain outside the inspected installation")
    output = output_root / ("attempt-" + str(time.time_ns()))
    output.mkdir(parents=True, exist_ok=False)
    receipt = output / "verification.json"
    started = time.monotonic()
    report = dict(schemaVersion=1, passed=False, application=str(app), output=str(output),
                  checks=[], jobs=[], audio=[], commandObservations=[], telemetry=[], timings={},
                  listening=False, hardware=False, nativeInput=False, liveDspTelemetry=False,
                  scope="Installed native preparation, acknowledged control edits, real offline PCM and portable project state. No user browser/preset writes or editor opens; live callback/native-input/listening qualification is separate.")
    client = None
    user_root = None
    user_before = None
    content_before = None
    previous_override = os.environ.pop("VIBERA_SYNTH_USER_DIRECTORY", None)

    def write_report():
        receipt.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def check(condition, description):
        if not condition:
            raise AssertionError(description)
        report["checks"].append(description)

    def call(tool, request=None):
        began = time.monotonic()
        record = dict(tool=tool, args=request or {})
        try:
            result = client.call(tool, request or {}, allow_failure=True)
            record["result"] = result
            if result.get("isError"):
                raise RuntimeError(str(result.get("structuredContent", result)))
            data = result["structuredContent"]
            if not isinstance(data, dict):
                raise TypeError("Expected structured MCP object for " + tool)
            return data
        except BaseException as error:
            record["error"] = repr(error)
            raise
        finally:
            record["seconds"] = time.monotonic() - began
            with (output / "calls.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(record) + "\n")

    def wait(tool, admission, timeout=45):
        began = time.monotonic()
        current, token, polls = admission, admission.get("operation"), 0
        try:
            while current.get("completionState") == "pending":
                if not token or time.monotonic() - began >= timeout:
                    raise AssertionError("Job timed out or lacks operation: " + str(current))
                time.sleep(.03)
                current = call(tool, dict(action="job", operation=token))
                polls += 1
                if current.get("operation") != token or type(current.get("operation")) is not type(token):
                    raise AssertionError("Job observation changed operation ownership")
            check(current.get("completionState") == "completed", "actual terminal completion via " + tool)
            return current
        finally:
            report["jobs"].append(dict(tool=tool, initial=admission, terminal=current, polls=polls,
                                       seconds=time.monotonic() - began))

    def settled(plugin):
        began = time.monotonic()
        while time.monotonic() - began < 30:
            state = call("vibera_state")
            found = [row for row in call("vibera_plugin", dict(action="list"))["items"] if row["id"] == plugin]
            if not state["pendingAudioUpdate"] and len(found) == 1 and found[0]["available"]:
                workers = state.get("audio", {}).get("pluginWorkers", [])
                check(bool(workers) and all(worker.get("alive") is True for worker in workers),
                      "owned native workers are alive after preparation")
                return found[0]
            time.sleep(.04)
        raise AssertionError("Installed native preparation failed: " + str(state.get("status")))

    def parameters(plugin):
        row = settled(plugin)
        wait("vibera_plugin", call("vibera_plugin", dict(action="refresh", plugin=plugin, instance=row["instance"])))
        page = call("vibera_synth", dict(action="parameters", plugin=plugin))
        check(len(page["items"]) == 64 and len({p["id"] for p in page["items"]}) == 64,
              "fresh native catalogue exposes all 64 distinct scalars")
        return row, page

    def values(page):
        return {item["id"]: item["normalized"] for item in page["items"]}

    def normalized(spec, value):
        return min(1., max(0., (value - spec["minimum"]) / (spec["maximum"] - spec["minimum"]))) ** spec["skew"]

    def patch(plugin, preset):
        row, page = parameters(plugin)
        terminal = wait("vibera_plugin", call("vibera_synth", dict(action="loadPreset", plugin=plugin,
            instance=row["instance"], generation=page["generation"], preset=preset["id"])))
        check(terminal.get("historyCommitted") is True, "changed whole patch has acknowledged history: " + preset["id"])
        _, page = parameters(plugin)
        expected = {spec["id"]: normalized(spec, value) for spec, value in zip(discovery["parameters"], preset["settings"])}
        actual = values(page)
        check(len(preset["settings"]) == 64 and actual.keys() == expected.keys() and
              all(abs(actual[key] - value) < 2e-5 for key, value in expected.items()),
              "whole-patch ACK and fresh 64-scalar readback: " + preset["id"])
        return page

    def prepared_telemetry(plugin):
        # Ordinary --mcp-test has no virtual callback driver. This mailbox is a
        # prepared startup snapshot, then the last processed block; it cannot
        # prove that a subsequent control patch has run on live audio.
        began = time.monotonic()
        while time.monotonic() - began < 10:
            telemetry = call("vibera_synth", dict(action="status", plugin=plugin)).get("synthesizer", {})
            if telemetry.get("available") is True and telemetry.get("contentReady") is True:
                break
            time.sleep(.04)
        check(telemetry.get("available") is True and telemetry.get("contentReady") is True and
              telemetry.get("sampleAvailable") is True and telemetry.get("parameterError") == "" and
              bool(telemetry.get("scope")), "initial native worker prepared the installed recorded content")
        report["telemetry"].append(dict(**telemetry, verificationScope="Initial prepared startup only; subsequent patch DSP is verified by actual offline PCM."))

    def command(name, request):
        result = call("vibera_command", dict(command=name, args=request))
        return command_driver.await_command_result(result, lambda tool, args: call(tool, args),
            lambda observation: report["commandObservations"].append(dict(command=name, **observation)))

    def render(name):
        began = time.monotonic()
        path = output / (name + ".wav")
        wait("vibera_project", call("vibera_project", dict(action="export", path=str(path), overwrite=False)), 90)
        with wave.open(str(path), "rb") as wav:
            check((wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) == (2, 3, 48000, 192000),
                  "actual four-second stereo 48kHz/24-bit export: " + name)
            pcm = wav.readframes(wav.getnframes())
        samples = [int.from_bytes(pcm[i:i + 3], "little", signed=True) for i in range(0, len(pcm), 3)]
        peak = max(map(abs, samples), default=0)
        tail = samples[-48000:]  # Last half-second, two channels; measured cut-tail contract.
        check(100 < peak < 8388607, "real nonzero PCM with headroom: " + name)
        report["audio"].append(dict(name=name, path=str(path), frames=192000, peak24=peak,
            sha256=hashlib.sha256(pcm).hexdigest(), fileSha256=digest(path),
            tailWindowSeconds=.5, tailPeak24=max(map(abs, tail), default=0),
            tailRms=math.sqrt(sum(value * value for value in tail) / len(tail)) / 8388608.,
            tailEnergy=sum((value / 8388608.) ** 2 for value in tail), seconds=time.monotonic() - began,
            tailScope="Measured final half-second within the existing four-second export boundary; no arbitrary tail extension claim."))
        return pcm

    def installed_content():
        manifest_path = app.parent / "Content/Synthesizer/library.json"
        root = manifest_path.parent
        ordinary(root, True)
        manifest = json.loads(content_file(root, "library.json").read_text(encoding="utf-8-sig"))
        check(manifest.get("schemaVersion") == 1 and manifest.get("license") == "CC0-1.0", "installed CC0 library schema")
        instruments, sources = manifest["instruments"], manifest["sources"]
        check(len(instruments) == 20 and len({row["id"] for row in instruments}) == 20, "20 installed recorded instrument maps")
        check(len(sources) == 3 and {row["id"] for row in sources} == {"vsco2ce", "vcsl", "freepats-guitar"},
              "all three original source notices are required")
        verified = {"library.json": digest(manifest_path)}
        samples = {}

        def verify(relative, expected):
            path = content_file(root, relative)
            actual = digest(path)
            check(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected) is not None and actual == expected,
                  "installed content hash: " + relative)
            verified[relative] = actual
            return path

        for source in sources:
            check(source["license"] == "CC0-1.0", "compatible source license: " + source["id"])
            verify(source["licensePath"], source["licenseSha256"])
        for instrument in instruments:
            verify(instrument["sourceMapping"], instrument["sourceMappingSha256"])
            check(bool(instrument["regions"]), "nonempty installed map: " + instrument["id"])
            for region in instrument["regions"]:
                relative = region["sample"]
                signature = (region["sha256"], region["sourceBytes"])
                if relative in samples:
                    check(samples[relative] == signature, "consistent shared sample provenance: " + relative)
                    continue
                samples[relative] = signature
                path = verify(relative, region["sha256"])
                check(path.stat().st_size == region["sourceBytes"], "installed sample byte size: " + relative)
                check(region["sourceId"] in {source["id"] for source in sources}, "sample source has bundled notice: " + relative)
        actual_samples = {path.relative_to(root).as_posix() for path in (root / "samples").rglob("*") if path.is_file()}
        check(len(samples) == 123 and actual_samples == samples.keys(), "all 123 installed sample files match the manifest exactly")
        notice = app.parent / "licenses/SYNTHESIZER_SOUND_SOURCES.md"
        ordinary(notice)
        verified["../../licenses/SYNTHESIZER_SOUND_SOURCES.md"] = digest(notice)
        return manifest, dict(path=str(root), manifestSha256=verified["library.json"], relativeToApplication="Content/Synthesizer",
                              maps=len(instruments), sampleFiles=len(samples), sampleBytes=sum(value[1] for value in samples.values()),
                              verifiedFiles=verified, distributedNoticeSha256=digest(notice))

    write_report()
    try:
        report["phase"] = "installed-inputs"
        ordinary(app)
        worker = app.parent / "VibeRa Plugin Worker.exe"
        skill_client = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        example = app.parent / "Examples/Midnight Session.vibra"
        helper = ROOT / "scripts/verify-installed-mcp.py"
        for name, path in [("application", app), ("worker", worker), ("verifier", Path(__file__)),
                           ("skillClient", skill_client), ("commandObserver", helper), ("example", example)]:
            ordinary(path)
            report[name + "Identity"] = dict(path=str(path), sha256=digest(path))
        content_started = time.monotonic()
        manifest, content_before = installed_content()
        report["runtimeContent"] = content_before
        report["timings"]["contentValidation"] = time.monotonic() - content_started
        check(bool(os.environ.get("APPDATA")), "Windows standard AppData directory is available")
        user_root = safe_path(str(Path(os.environ["APPDATA"]) / "VibeRa Studio/Synthesizer"))
        user_before = file_snapshot(user_root)
        report["userContent"] = dict(path=str(user_root), before=user_before, overrideRemoved=previous_override is not None,
                                     policy="Read-only; no filter/favorite/navigation/user-preset/editor requests.")
        module = load_module("vibera_installed_synth_client", skill_client)
        command_driver = load_module("vibera_installed_synth_commands", helper)
        report["phase"] = "discovery"
        write_report()
        client = module.VibeRaClient(app)
        report.update(pid=client.process.pid, version=client.server["version"])
        check(any(tool["name"] == "vibera_synth" for tool in client.request("tools/list", {})["tools"]),
              "installed typed native Synthesizer service")
        discovery = call("vibera_synth", dict(action="list", genre="All Genres", family="All", search="", favoritesOnly=False))
        check(Path(discovery["userDirectory"]).resolve() == user_root, "actual service uses standard AppData rather than a development override")
        presets = discovery["presets"]
        check(len(presets) == 81 and len({preset["id"] for preset in presets}) == 81, "all 81 distinct installed factory patches")
        check(discovery["separateVST3"] is False, "first-class native instrument requires no external VST3")
        kinds = {kind: sum(preset["sourceKind"] == kind for preset in presets) for kind in ("SAMPLE", "SAMPLE + SYNTH", "SYNTHESIS")}
        check(kinds == {"SAMPLE": 20, "SAMPLE + SYNTH": 1, "SYNTHESIS": 56}, "installed 20 recorded / one hybrid / 56 synthesized patch inventory")
        map_ids = {instrument["id"] for instrument in manifest["instruments"]}
        check({preset["soundSource"] for preset in presets if preset["sourceKind"] != "SYNTHESIS"} == map_ids and
              all(not preset["soundSource"] for preset in presets if preset["sourceKind"] == "SYNTHESIS"),
              "every installed recorded-source claim resolves to a bundled map")
        check(len(discovery["parameters"]) == 64 and len({spec["id"] for spec in discovery["parameters"]}) == 64,
              "64 stable installed parameter IDs")
        report["discovery"] = dict(factoryCount=len(presets), sourceKinds=kinds, parameterCount=64,
                                    genres=discovery["genres"], families=discovery["families"])
        sampled = next(preset for preset in presets if preset["sourceKind"] == "SAMPLE" and "piano" in preset["soundSource"])
        synthesized = next(preset for preset in presets if preset["sourceKind"] == "SYNTHESIS" and preset["family"] == "Bass")
        hybrid = next(preset for preset in presets if preset["sourceKind"] == "SAMPLE + SYNTH")
        report["phase"] = "disposable-project"
        fixture = ET.parse(example)
        fixture.getroot().set("tempo", "120.0")
        for channel in fixture.findall("./CHANNELS/CHANNEL"):
            channel.set("muted", "1")
        for pattern in fixture.findall("./PATTERNS/PATTERN"):
            for note in list(pattern):
                if note.tag == "NOTE":
                    pattern.remove(note)
        placements = fixture.find("./PLACEMENTS")
        check(placements is not None and len(placements) > 0, "packaged built-in example has a usable placement")
        for placement in list(placements)[1:]:
            placements.remove(placement)
        placements[0].set("beat", "0.0")
        placements[0].set("length", "8.0")
        placements[0].set("offset", "0.0")
        source = output / "source.vibra"
        fixture.write(source, encoding="utf-8", xml_declaration=True)
        wait("vibera_project", call("vibera_project", dict(action="open", path=str(source), discardChanges=True)))
        old_channels = {row["id"] for row in call("vibera_inspect", dict(entity="channels"))["items"]}
        admission = call("vibera_synth", dict(action="create"))
        command_driver.await_command_result(admission, lambda tool, args: call(tool, args),
            lambda observation: report["commandObservations"].append(dict(command="vibera_synth.create", **observation)))
        added = [row for row in call("vibera_inspect", dict(entity="channels"))["items"] if row["id"] not in old_channels]
        check(len(added) == 1 and bool(added[0].get("plugin")), "canonical native create adds exactly one real instrument channel")
        channel, plugin = added[0]["id"], added[0]["plugin"]
        parameters(plugin)
        prepared_telemetry(plugin)
        report.update(channel=channel, plugin=plugin)
        pattern = call("vibera_state")["selectedPattern"]
        for beat, pitch, velocity in [(0, 60, .4), (1, 64, .7), (2, 67, .95)]:
            command("notes.add", dict(pattern=pattern, channel=channel, beat=beat, length=.7, pitch=pitch, value=velocity))
        report["phase"] = "actual-native-pcm"
        patch(plugin, synthesized)
        synth_pcm = render("synthesized-bass")
        patch(plugin, sampled)
        sampled_pcm = render("recorded-piano")
        patch(plugin, hybrid)
        hybrid_pcm = render("recorded-synthesis-layer")
        check(len({hashlib.sha256(pcm).hexdigest() for pcm in (synth_pcm, sampled_pcm, hybrid_pcm)}) == 3,
              "sampled, synthesized and hybrid sources produce distinct real PCM")
        row, page = parameters(plugin)
        before_edit = values(page)
        master = next(spec for spec in discovery["parameters"] if spec["key"] == "master-db")
        target = -18. if abs(before_edit[master["id"]] - normalized(master, -18.)) > 1e-5 else -12.
        terminal = wait("vibera_plugin", call("vibera_synth", dict(action="set", plugin=plugin, instance=row["instance"],
            generation=page["generation"], key="master-db", value=target)))
        check(terminal.get("historyCommitted") is True, "installed native-unit master edit has acknowledged history")
        _, page = parameters(plugin)
        saved_values = values(page)
        expected = dict(before_edit)
        expected[master["id"]] = normalized(master, target)
        check(all(abs(saved_values[key] - value) < 2e-5 for key, value in expected.items()),
              "fresh scalar edit readback changes exactly the requested native-unit value")
        baseline = render("edited-hybrid")
        check(baseline != hybrid_pcm, "acknowledged master edit changes actual offline sound")
        report["phase"] = "portable-save-reopen"
        project = output / "portable.vibra"
        wait("vibera_project", call("vibera_project", dict(action="save", path=str(project), overwrite=False)))
        project_text = project.read_text(encoding="utf-8-sig")
        project_xml = ET.fromstring(project_text)
        nodes = [node for node in project_xml.findall(".//PLUGIN") if node.get("id") == str(plugin)]
        check(len(nodes) == 1, "saved project retains exactly the native instrument identity")
        descriptor = ET.fromstring(nodes[0].get("descriptor", ""))
        check(descriptor.tag == "PLUGIN" and descriptor.get("format") == "VibeRa" and
              descriptor.get("file") == "vibera.instrument.synthesizer" and descriptor.get("isInstrument") == "1",
              "saved descriptor is the original native built-in identity")
        state_xml = native_xml(nodes[0].get("state", ""))
        check(state_xml.tag == "VIBERA_SYNTHESIZER" and set(state_xml.attrib) == {"schemaVersion", "preset"} and
              state_xml.get("schemaVersion") == "1" and state_xml.get("preset") == hybrid["id"],
              "native state persists schema and stable factory identity")
        state_values = {int(node.get("id")): float(node.get("value")) for node in state_xml.findall("PARAM")}
        for sidecar in state_xml.findall("VibeRaParameterValues"):
            state_values.update({int(node.get("id")): float(node.get("value")) for node in sidecar.findall("PARAM")})
        check(state_values.keys() == saved_values.keys() and all(abs(state_values[key] - value) < 2e-5 for key, value in saved_values.items()),
              "native saved state contains every acknowledged scalar")
        portable_text = (project_text + ET.tostring(state_xml, encoding="unicode")).replace("\\", "/").casefold()
        forbidden = [str(ROOT), str(app.parent / "Content"), str(app.parent)]
        check(all(path.replace("\\", "/").casefold() not in portable_text for path in forbidden),
              "project/native state has no absolute content, installation or source-checkout path")
        report["portableProject"] = dict(path=str(project), sha256=digest(project), nativePreset=hybrid["id"], parameterCount=len(state_values))
        settled(plugin)
        wait("vibera_project", call("vibera_project", dict(action="open", path=str(project), discardChanges=True)))
        _, page = parameters(plugin)
        check(values(page) == saved_values, "save/reopen preserves exact current native scalar readback")
        check(render("reopened-hybrid") == baseline, "save/reopen restores exact real PCM from installed content and effect state")
        report["phase"] = "complete"
        report["passed"] = True
    except BaseException as error:
        report["error"] = repr(error)
        report["traceback"] = traceback.format_exc()
    finally:
        if client is not None:
            try:
                client.close()
                report["closedGracefully"] = True
            except Exception as error:
                report.update(passed=False, closedGracefully=False, closeError=repr(error))
            (output / "stderr.log").write_text("".join(client.stderr), encoding="utf-8")
        try:
            if user_before is not None:
                user_after = file_snapshot(user_root)
                report["userContent"]["after"] = user_after
                check(user_after == user_before, "standard user content and preferences remain byte-for-byte unchanged")
            if content_before is not None:
                for relative, expected_hash in content_before["verifiedFiles"].items():
                    path = ((app.parent / "Content/Synthesizer") / relative).resolve()
                    check(digest(path) == expected_hash, "installed factory/notice bytes remain unchanged: " + relative)
            for name in ("application", "worker", "verifier", "skillClient", "commandObserver", "example"):
                identity = report.get(name + "Identity")
                if identity:
                    check(digest(Path(identity["path"])) == identity["sha256"], "verification input identity remains fixed: " + name)
        except BaseException as error:
            report["passed"] = False
            report["postflightError"] = repr(error)
            report["postflightTraceback"] = traceback.format_exc()
        if previous_override is not None:
            os.environ["VIBERA_SYNTH_USER_DIRECTORY"] = previous_override
        report["seconds"] = time.monotonic() - started
        write_report()
        print(json.dumps(dict(passed=report["passed"], output=str(output), phase=report.get("phase"),
                              checks=len(report["checks"]), seconds=report["seconds"], error=report.get("error", report.get("postflightError")))))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
