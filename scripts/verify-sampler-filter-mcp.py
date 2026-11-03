"""Actual application sampler-filter commands, history, files and musical PCM."""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import time
import wave
import uuid
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
app = Path(sys.argv[1])
paths_spec = importlib.util.spec_from_file_location("filter_output_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
paths = importlib.util.module_from_spec(paths_spec)
paths_spec.loader.exec_module(paths)
output_root = paths.owned_output_path(sys.argv[2], app)
paths.ordinary(app, False)
output_root.mkdir(parents=True, exist_ok=True)
output = output_root / ("attempt-" + uuid.uuid4().hex)
output.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location("neutral", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
receipt = dict(passed=False, finished=False, checks=0, output=str(output),
               applicationSha256=hashlib.sha256(app.read_bytes()).hexdigest(),
               nativeInput=False, listening=False, hardware=False)
began = time.monotonic()
client = None


def check(ok, message):
    if not ok:
        raise AssertionError(message)
    receipt["checks"] += 1


try:
    # Original independent two-frequency signal; the unrelated route is a third
    # pure tone. Frequencies land on exact analysis bins, avoiding FFT libraries.
    sources = [output / "original-filter-tone.wav", output / "original-other-route.wav"]
    for index, path in enumerate(sources):
        with wave.open(str(path), "wb") as wav:
            wav.setparams((1, 2, 48000, 48000, "NONE", "not compressed"))
            frequencies = (220, 6000) if index == 0 else (750,)
            wav.writeframes(b"".join(struct.pack("<h", round(5000 * sum(
                math.sin(2 * math.pi * frequency * frame / 48000) for frequency in frequencies)))
                for frame in range(48000)))
    client = module.VibeRaClient(app)
    data = client.data
    state = lambda: data("vibera_state")
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    pattern = state()["selectedPattern"]
    first, second = [channel["id"] for channel in state()["channels"][:2]]
    settings = lambda channel=first: next(c["sampler"] for c in state()["channels"] if c["id"] == channel)
    for channel, source, pan in [(first, sources[0], -1), (second, sources[1], 1)]:
        command("channel.source", channel=channel, text=str(source))
        command("channel.pan", channel=channel, value=pan)
        command("notes.add", pattern=pattern, channel=channel, beat=0, length=3, pitch=60, value=.7)
    command("clip.add", pattern=pattern, beat=0, length=4)
    check((settings()["filterType"], settings()["filterCutoff"], settings()["filterResonance"],
           settings()["filterBypassed"]) == (1, 1, 0, True), "Legacy/default sampler remains dry")

    def render(name):
        path = output / name
        accepted = data("vibera_project", dict(action="export", path=str(path), overwrite=True))
        check(accepted["completionState"] == "pending", "Export acceptance is not completion")
        completed = client.wait_job(accepted)
        check(completed["completionState"] == "completed", "Export reached actual terminal success")
        with wave.open(str(path), "rb") as wav:
            check((wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) == (48000, 2, 3),
                  "Actual 48k stereo24-bit WAV")
            pcm = wav.readframes(wav.getnframes())
        return [int.from_bytes(pcm[i:i+3], "little", signed=True) / 8388608
                for i in range(0, len(pcm), 6)], b"".join(pcm[i:i+3] for i in range(3, len(pcm), 6)), pcm

    def magnitude(pcm, frequency):
        start, end = 4800, 38400
        real = sum(pcm[i] * math.cos(2 * math.pi * frequency * i / 48000) for i in range(start, end))
        imag = sum(pcm[i] * math.sin(2 * math.pi * frequency * i / 48000) for i in range(start, end))
        return 2 * math.hypot(real, imag) / (end-start)

    baseline, other, _ = render("default-dry.wav")
    cutoff = math.log(1000 / 20, 1000)
    command("channel.sampler.filterCutoff", channel=first, value=cutoff)
    low, low_other, low_bytes = render("low-pass.wav")
    check(magnitude(low, 220) > .8 * magnitude(baseline, 220), "LP retains musical bass")
    check(magnitude(low, 6000) < .04 * magnitude(baseline, 6000), "LP removes high partial")
    check(low_other == other, "Unrelated route PCM stays byte-identical")
    check(abs(settings()["filterQ"] - 2**-.5) < 1e-12 and not settings()["filterBypassed"],
          "Readback exposes actual Q and enabled filter")
    command("channel.sampler.filterType", channel=first, value=3)
    high, high_other, _ = render("high-pass.wav")
    check(magnitude(high, 220) < .06 * magnitude(baseline, 220), "HP removes bass")
    check(magnitude(high, 6000) > .9 * magnitude(baseline, 6000), "HP retains high partial")
    check(high_other == other, "Mode changes leave unrelated route intact")
    command("edit.undo")
    check(settings()["filterType"] == 1, "Type change has one Undo")
    command("edit.redo")
    check(settings()["filterType"] == 3, "Type change has exact Redo")
    command("edit.undo")
    project = output / "sampler-filter.vibra"
    before_save = {key: settings()[key] for key in ("filterType", "filterCutoff", "filterResonance")}
    client.wait_job(data("vibera_project", dict(action="save", path=str(project), overwrite=True)))
    persisted = next(row.find("SAMPLER") for row in ET.parse(project).findall("./CHANNELS/CHANNEL")
                     if int(row.get("id")) == first)
    check(float(persisted.get("filterCutoff")).hex() == cutoff.hex() and
          int(persisted.get("filterType")) == 1 and float(persisted.get("filterResonance")) == 0,
          "Project XML preserves exact submitted binary64 cutoff and type/resonance")
    client.wait_job(data("vibera_project", dict(action="open", path=str(project), discardChanges=True)))
    check(all(settings()[key] == value for key, value in before_save.items()),
          "Actual project save/reopen preserves exact pre-save MCP wire readback")
    _, _, reopened = render("reopened-filter.wav")
    check(reopened == low_bytes, "Saved project reproduces exact filtered stereo PCM")

    for name in ("filterType", "filterCutoff", "filterResonance"):
        definition = data("vibera_commands", dict(command="channel.sampler."+name, includeSchemas=True))["operations"][0]
        schema = definition["arguments"]
        check(definition["supported"] and schema["additionalProperties"] is False and
              set(schema["required"]) == {"channel", "value"}, "Closed discoverable target/value contract")
        check(schema["properties"]["value"]["maximum"] == (5 if name == "filterType" else 1),
              "Discoverable canonical range")
        invalids = [-1, 6, True, "0.5"] if name == "filterType" else [-.1, 1.1, True, "0.5"]
        if name == "filterType":
            invalids.append(1.5)
        for value in invalids:
            revision = state()["revision"]
            refused = client.call("vibera_command", dict(command="channel.sampler."+name,
                                  args=dict(channel=first, value=value)), allow_failure=True)
            check(refused.get("isError") and refused["structuredContent"].get("error") and
                  state()["revision"] == revision, "Invalid filter input refuses atomically")

    data("vibera_rack", dict(action="select", ids=[first], activeChannel=first))
    before = {key: settings()[key] for key in ("filterType", "filterCutoff", "filterResonance")}
    command("transport.record")
    lease = data("vibera_gesture", dict(action="begin", domain="samplerFilter"))["operation"]
    for name, value in [("filterType", 2), ("filterCutoff", .4), ("filterResonance", .2)]:
        pending = data("vibera_gesture", dict(action="update", operation=lease,
                       command="channel.sampler."+name, args=dict(channel=first, value=value)))
        check(pending["completionState"] == "pending" and state()["recordArmed"],
              "Continuous filter preview retains armed MIDI and truthful pending state")
    refused = client.call("vibera_gesture", dict(action="update", operation=lease,
                          command="channel.sampler.normalize", args=dict(channel=first, enabled=True)),
                          allow_failure=True)
    check(refused.get("isError") and refused["structuredContent"]["error"]["code"] == "gesture_domain",
          "Filter lease refuses unrelated edits")
    check(data("vibera_gesture", dict(action="commit", operation=lease))["completionState"] == "completed",
          "Preview reaches actual commit")
    command("transport.record")
    command("edit.undo")
    check(all(settings()[key] == value for key, value in before.items()), "Whole filter gesture has one exact Undo")
    command("edit.redo")
    check([settings()[key] for key in before] == [2, .4, .2], "Whole filter gesture has exact Redo")
    command("edit.undo")
    for expiry in (False, True):
        lease = data("vibera_gesture", dict(action="begin", domain="samplerFilter", leaseMs=250))["operation"]
        data("vibera_gesture", dict(action="update", operation=lease,
             command="channel.sampler.filterCutoff", args=dict(channel=first, value=.2)))
        if expiry:
            time.sleep(.45)
        result = data("vibera_gesture", dict(action="state" if expiry else "cancel", operation=lease))
        check(result["completionState"] == "cancelled" and
              all(settings()[key] == value for key, value in before.items()),
              "Cancel/real lease expiry restores exact filter state")

    profile_before = data("vibera_macros", dict(action="state"))
    recipe = dict(id="macro.acceptance-sampler-filter", name="Disposable filter setting", steps=[
        dict(action="channel.sampler.filterType", currentContext=False, parameters=dict(channel=first, value=2)),
        dict(action="channel.sampler.filterCutoff", currentContext=False, parameters=dict(channel=first, value=.3)),
        dict(action="channel.sampler.filterResonance", currentContext=False, parameters=dict(channel=first, value=.1))])
    created = data("vibera_macros", dict(action="create", generation=profile_before["generation"], macro=recipe))
    check(created["profileMatchesMemory"], "Macro persisted to isolated profile")
    ran = data("vibera_macros", dict(action="run", generation=created["generation"], id=recipe["id"]))
    check(ran["completionState"] == "completed" and [settings()[key] for key in before] == [2, .3, .1],
          "Actual saved macro executes canonical filter operations")
    command("edit.undo")
    check(all(settings()[key] == value for key, value in before.items()), "Filter macro one exact Undo")
    current = data("vibera_macros", dict(action="state"))
    data("vibera_macros", dict(action="remove", generation=current["generation"], id=recipe["id"]))

    profile = state()["controllerProfile"]
    mapped = dict(profile, bindings=[dict(action="channel.sampler.filterCutoff", label="Filter cutoff",
        port=1, channel=16, kind=0, number=74, layer=0, mode=1, mixer=-1, minimum=0, maximum=1,
        pickup=False, banked=False, enabled=True, nativeArea=0)])
    data("vibera_controller", dict(action="configure", profile=mapped))
    data("vibera_controller", dict(action="inject", port=1, kind=0, channel=16, number=74, value=64))
    check(abs(settings()["filterCutoff"] - 64/127) < 1e-12, "Virtual CC reaches canonical normalized cutoff")
    command("edit.undo")
    check(settings()["filterCutoff"] == before["filterCutoff"], "Controller filter change exact Undo")
    data("vibera_controller", dict(action="configure", profile=profile))
    corrupt = output / "corrupt.vibra"
    corrupt.write_text("Not a VibeRa project", encoding="utf-8")
    failed_job = client.call("vibera_project", dict(action="open", path=str(corrupt), discardChanges=True),
                             allow_failure=True)["structuredContent"]
    check(failed_job["completionState"] in ("pending", "failed"), "Corrupt open never reports success")
    receipt["failedProjectInitiallyPending"] = failed_job["completionState"] == "pending"
    failure_deadline = time.monotonic() + 40
    while failed_job["completionState"] == "pending" and time.monotonic() < failure_deadline:
        time.sleep(.03)
        failed_job = client.call("vibera_project", dict(action="job", operation=failed_job["operation"]),
                                 allow_failure=True)["structuredContent"]
    receipt["failedProjectJob"] = failed_job
    check(failed_job["completionState"] == "failed" and failed_job.get("error", {}).get("code"),
          "Corrupt project reaches actual typed terminal failure, not a timeout")
    check(all(settings()[key] == value for key, value in before.items()), "Actual file-job failure preserves filter/project")
    blocked = output / "not-a-directory"
    blocked.write_text("Original disposable failure fixture", encoding="utf-8")
    failed_export = client.call("vibera_project", dict(action="export", path=str(blocked / "rejected.wav"),
                                overwrite=False), allow_failure=True)["structuredContent"]
    check(failed_export["completionState"] == "pending", "Failed export starts as an actual pending job")
    failure_deadline = time.monotonic() + 40
    while failed_export["completionState"] == "pending" and time.monotonic() < failure_deadline:
        time.sleep(.03)
        failed_export = client.call("vibera_project", dict(action="job", operation=failed_export["operation"]),
                                    allow_failure=True)["structuredContent"]
    receipt["failedExportJob"] = failed_export
    check(failed_export["completionState"] == "failed" and failed_export.get("error", {}).get("code"),
          "Actual asynchronous render failure reaches typed terminal state")
    check(not (blocked / "rejected.wav").exists() and blocked.read_text(encoding="utf-8") ==
          "Original disposable failure fixture" and all(settings()[key] == value for key, value in before.items()),
          "Failed render preserves destination blocker and complete filter state")
    receipt["passed"] = True
except Exception as error:
    receipt["error"] = str(error)
finally:
    if client:
        try:
            client.close()
            receipt["exitCode"] = client.process.returncode
        except Exception as error:
            receipt["passed"] = False
            receipt["closeError"] = str(error)
    receipt["finished"] = True
    receipt["seconds"] = time.monotonic()-began
    (output / "verification.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
    (output_root / "verification.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
print(json.dumps(receipt))
raise SystemExit(0 if receipt["passed"] else 1)
