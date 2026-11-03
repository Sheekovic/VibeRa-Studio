"""Actual source-bound sampler tempo stdio workflow; original PCM, no hardware.

Usage: verify-sampler-tempo-mcp.py ABSOLUTE_APP ABSOLUTE_OWNED_OUTPUT
Uses the existing provider-neutral client in disposable --mcp-test mode. Export
is actual offline PCM; no physical callback, native input or listening claim.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import time
import traceback
import wave
from datetime import datetime, timezone
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
RATE, FRAMES = 48000, 8 * 48000


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
    # Reuse the already qualified ordinary/source/package output admission;
    # importing this guarded module does not execute its application main().
    paths = load_module("sampler_tempo_output_admission", ROOT / "scripts/verify-plugin-io-mcp.py")
    output = paths.owned_output_path(sys.argv[2], app)
    paths.ordinary(app, False)
    output.mkdir(parents=True, exist_ok=True)
    attempt = output / ("attempt-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    attempt.mkdir(exist_ok=False)
    receipt = {"passed": False, "finished": False, "checks": [], "operations": [], "responseCount": 0,
               "nativeInput": False, "hardware": False, "listening": False, "liveAudio": False,
               "scope": "Original pulse/silence PCM; actual shared tempo analysis, history, persistence and offline export",
               "application": str(app), "applicationSha256": digest(app),
               "scriptSha256": digest(Path(__file__)), "outputAdmissionSha256": digest(ROOT / "scripts/verify-plugin-io-mcp.py")}
    started = time.monotonic()
    client = None

    def check(condition, message):
        if not condition:
            raise AssertionError(message)
        receipt["checks"].append(message)

    def retain():
        text = json.dumps(receipt, indent=2, ensure_ascii=False) + "\n"
        (attempt / "verification.json").write_text(text, encoding="utf-8")
        (output / "verification.json").write_text(text, encoding="utf-8")

    try:
        pulse, silence = attempt / "original-120bpm-pulses.wav", attempt / "original-silence.wav"
        replacement = attempt / "original-6s-150bpm-pulses.wav"
        pulse_bytes = bytearray()
        for frame in range(FRAMES):
            phase = frame % (RATE // 2)
            # Sixteen original short positive triangular pulses at literal0.5s
            # spacing. No provider assets or fitted runtime timing parameters.
            value = int(10000 * max(0., 1. - abs(phase - 240) / 240.)) if phase < 480 else 0
            pulse_bytes.extend(struct.pack("<h", value))
        for path, data in ((pulse, pulse_bytes), (silence, bytes(FRAMES * 2))):
            with wave.open(str(path), "wb") as wav:
                wav.setparams((1, 2, RATE, FRAMES, "NONE", "not compressed"))
                wav.writeframes(data)
        with wave.open(str(replacement), "wb") as wav:
            wav.setparams((1, 2, RATE, 6 * RATE, "NONE", "not compressed"))
            wav.writeframes(b"".join(struct.pack("<h", int(10000 * max(0., 1. - abs(frame % 19200 - 240) / 240.)))
                                    for frame in range(6 * RATE)))
        receipt["originalSources"] = {str(path): digest(path) for path in (pulse, silence, replacement)}
        client_path = app.parent / "skills/vibera-studio/scripts/stdio_client.py"
        if not client_path.is_file():
            client_path = ROOT / "skills/vibera-studio/scripts/stdio_client.py"
        module = load_module("sampler_tempo_neutral_client", client_path)
        client = module.VibeRaClient(app)
        receipt.update(pid=client.process.pid, clientSha256=digest(client_path), version=client.server.get("version"))

        def call(tool, args=None, allow_failure=False):
            response = client.call(tool, args or {}, allow_failure=True)
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": tool, "args": args or {}, "response": response}, ensure_ascii=False) + "\n")
            receipt["responseCount"] += 1
            if response.get("isError") and not allow_failure:
                raise AssertionError("Unexpected refusal: " + str(response))
            return response["structuredContent"]

        state = lambda: call("vibera_state")

        def command(name, **args):
            result = call("vibera_command", {"command": name, "args": args})
            check(result.get("completionState") in (None, "completed"), "Actual ordinary command completes: " + name)
            return result

        def tempo(action, allow_failure=False, **args):
            return call("vibera_samplerTempo", {"action": action, **args}, allow_failure=allow_failure)

        def wait_tempo(admission, expected="completed"):
            token = admission["operation"]
            current, began = admission, time.monotonic()
            try:
                while current["completionState"] == "pending" and time.monotonic() - began < 15:
                    time.sleep(.01)
                    current = tempo("job", operation=token, allow_failure=expected == "failed")
                    check(current["operation"] == token, "Analysis observation retains the exact session token")
                check(current["completionState"] == expected, "Actual tempo terminal " + expected)
                return current
            finally:
                receipt["operations"].append({"initial": admission, "terminal": current, "seconds": time.monotonic() - began})
                retain()

        def wait_prepared(channel, seconds=8.):
            began, current = time.monotonic(), None
            while time.monotonic() - began < 15:
                current = call("vibera_samplerTempo", {"action": "state", "channel": channel}, True)
                if current.get("completionState") == "completed":
                    check(current["channel"] == channel and current["sourceSeconds"] == seconds and
                          current["method"] == "resample" and current["changesPitch"] is True,
                          "Actual exact current prepared source duration and explicit resampling scope: " + str(seconds))
                    return current
                check(current.get("error", {}).get("code") == "samplerTempo.unavailable",
                      "Readiness refuses only genuine not-yet-prepared source")
                if time.monotonic() - began > .1 and not state()["pendingAudioUpdate"]:
                    raise AssertionError("Current source remains unavailable with no automatic preparation pending; no mutation retry: " + str(current))
                time.sleep(.02)
            raise AssertionError("Prepared current sampler source did not become available: " + str(current))

        def settings(channel):
            return next(row["sampler"] for row in state()["channels"] if row["id"] == channel)

        def unchanged_snapshot():
            current = state()
            return {key: current[key] for key in ("projectIdentity", "revision", "dirty", "undoAvailable", "redoAvailable", "tempo", "channels")}

        def refuse(args, code):
            before = unchanged_snapshot()
            reply = client.call("vibera_samplerTempo", args, allow_failure=True)
            with (attempt / "responses.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({"tool": "vibera_samplerTempo", "args": args, "response": reply}) + "\n")
            receipt["responseCount"] += 1
            check(reply.get("isError") is True and reply["structuredContent"]["error"]["code"] == code and
                  unchanged_snapshot() == before, "Typed refusal preserves exact settings/history: " + code)

        tools = {row["name"]: row for row in client.request("tools/list", {})["tools"]}
        tool = tools["vibera_samplerTempo"]
        schema = tool["inputSchema"]
        check(schema["additionalProperties"] is False and schema["required"] == ["action"] and
              set(schema["properties"]["action"]["enum"]) == {"state", "capture", "detect", "job", "cancel", "apply"} and
              schema["properties"]["sourceBpm"]["minimum"] == 20 and schema["properties"]["sourceBpm"]["maximum"] == 400 and
              schema["properties"]["beats"]["minimum"] == .0625 and schema["properties"]["bars"]["type"] == "integer",
              "Discovery exposes typed source-bound analysis/confirmation and BPM/quarter-note/bar units")
        (attempt / "tempo-tool.json").write_text(json.dumps(tool, indent=2) + "\n", encoding="utf-8")
        created = call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
        check(created["completionState"] == "completed", "Genuine disposable empty project created")
        initial = state()
        check(len(initial["channels"]) >= 2 and initial["tempo"] == 120, "Known empty-project channels and literal120 project tempo")
        channel, silent_channel = [row["id"] for row in initial["channels"][:2]]
        pattern = initial["selectedPattern"]
        for row in initial["channels"]:
            if row["id"] != channel:
                command("channel.mute", channel=row["id"], enabled=True)
        command("channel.source", channel=channel, text=str(pulse))
        command("channel.source", channel=silent_channel, text=str(silence))
        wait_prepared(channel)
        wait_prepared(silent_channel)
        baseline = unchanged_snapshot()

        analysis = tempo("detect", channel=channel)
        check(analysis["completionState"] in ("pending", "completed") and analysis["applied"] is False,
              "Actual detection may finish before observation but never applies implicitly")
        detected = wait_tempo(analysis)
        check(abs(detected["estimateBpm"] - 120) < 1 and detected["phase"] == "confirmation" and
              detected["applied"] is False and detected["documentChanged"] is False and unchanged_snapshot() == baseline,
              "Original literal120BPM PCM is analyzed without editing before explicit apply")
        cancelled = wait_tempo(tempo("cancel", operation=detected["operation"]), "cancelled")
        check(cancelled["applied"] is False and unchanged_snapshot() == baseline,
              "Completed analysis can be explicitly cancelled without document mutation")
        refuse({"action": "apply", "operation": cancelled["operation"], "sourceBpm": 120}, "samplerTempo.cancelled")
        in_progress = tempo("detect", channel=channel)
        receipt["cancelAdmissionPhase"] = in_progress["completionState"]
        wait_tempo(tempo("cancel", operation=in_progress["operation"]), "cancelled")
        check(unchanged_snapshot() == baseline, "Pending-or-already-completed analysis cancellation retains real unchanged history")
        quiet = wait_tempo(tempo("detect", channel=silent_channel, allow_failure=True), "failed")
        check(quiet["error"] == "samplerTempo.noEstimate" and quiet["estimateBpm"] == 0 and quiet["applied"] is False,
              "Actual silence has no invented rhythmic estimate")
        before_override = settings(silent_channel)
        manual = tempo("apply", operation=quiet["operation"], sourceBpm=120, fit=True)
        check(manual["completionState"] == "completed" and manual["applied"] and "error" not in manual and
              "error" not in tempo("job", operation=manual["operation"]),
              "Explicit manual override after no-estimate reports actual successful commit without failure error")
        command("edit.undo")
        check(settings(silent_channel) == before_override, "Manual override is exactly one Undo")

        captured = tempo("capture", channel=channel)
        check(captured["operation"] in tempo("state", channel=channel)["operations"],
              "Read-only Channel state discovers retained UI/shared session tokens")
        check(captured["completionState"] == "completed" and captured["phase"] == "confirmation" and not captured["applied"],
              "Manual capture is a completed settings witness, not an edit")
        command("channel.sampler.sourceTempo", channel=channel, value=100)
        refuse({"action": "apply", "operation": captured["operation"], "sourceBpm": 120}, "samplerTempo.stale")
        command("edit.undo")
        source_witness = tempo("capture", channel=channel)
        command("channel.source", channel=channel, text=str(replacement))
        wait_prepared(channel, 6.)
        replacement_analysis = wait_tempo(tempo("detect", channel=channel))
        check(abs(replacement_analysis["estimateBpm"] - 150) < 1,
              "No-device source replacement analyzes actual6s150BPM PCM, never old8s120BPM bank")
        refuse({"action": "apply", "operation": source_witness["operation"], "sourceBpm": 120}, "samplerTempo.stale")
        command("edit.undo")
        wait_prepared(channel)

        valid = tempo("capture", channel=channel)["operation"]
        for extra in ({"sourceBpm": 120, "beats": 8}, {"sourceBpm": True}, {"sourceBpm": 19},
                      {"bars": 0}, {"bars": 1.5}, {"beats": 2049}, {"sourceBpm": 120, "unexpected": 1}):
            refuse({"action": "apply", "operation": valid, **extra}, "invalid_arguments")
        refuse({"action": "apply", "operation": valid, "beats": .0625}, "samplerTempo.range")
        refuse({"action": "apply", "operation": "sampler-tempo-not-owned", "sourceBpm": 120}, "samplerTempo.unknownOperation")

        detected = wait_tempo(tempo("detect", channel=channel))
        before, revision = settings(channel), state()["revision"]
        applied = tempo("apply", operation=detected["operation"], fit=True)
        after = settings(channel)
        check(applied["phase"] == "applied" and applied["applied"] is True and applied["documentChanged"] is True and
              after["sourceTempo"] == detected["estimateBpm"] and after["fitTempo"] is True and state()["revision"] == revision + 1,
              "Explicit detected BPM plus fit commits one atomic canonical history edge")
        command("edit.undo")
        check(settings(channel) == before, "One Undo restores both original BPM and fit settings exactly")
        command("edit.redo")
        check(settings(channel) == after, "One Redo restores exact detected BPM and fit")
        refuse({"action": "apply", "operation": applied["operation"], "sourceBpm": 120}, "samplerTempo.alreadyApplied")
        command("channel.sampler.range", channel=channel, beat=.25, length=.75)
        command("channel.sampler.sourceTempo", channel=channel, value=100)
        current = wait_prepared(channel)
        check(current["sourceSeconds"] == 8 and current["trimmedSeconds"] == 4,
              "Prepared source and exact trimmed quarter-to-three-quarter length are distinguished")
        length_capture = tempo("capture", channel=channel)
        beats = tempo("apply", operation=length_capture["operation"], beats=8)
        check(beats["sourceBpm"] == 120 and beats["fit"] is True and settings(channel)["sourceTempo"] == 120,
              "Eight quarter notes in literal four trimmed seconds gives120BPM and enables fit")
        command("channel.sampler.fitTempo", channel=channel, enabled=False)
        bars_capture = tempo("capture", channel=channel)
        command("project.meter", numerator=3, denominator=4)
        before, revision = settings(channel), state()["revision"]
        bars = tempo("apply", operation=bars_capture["operation"], bars=2)
        after = settings(channel)
        check(bars["sourceBpm"] == 90 and bars["fit"] is True and after["sourceTempo"] == 90 and
              state()["revision"] == revision + 1, "Two CURRENT3/4 bars are six quarters in trimmed4s, exactly90BPM")
        command("edit.undo")
        check(settings(channel) == before, "Bars fit is one atomic BPM/fit Undo after meter change")
        command("edit.redo")
        check(settings(channel) == after, "Bars fit Redo restores exact90BPM and enabled fit")
        reported = wait_prepared(channel)
        check(reported["sourceBpm"] == 90 and reported["fit"] is True and reported["trimmedSeconds"] == 4,
              "Current prepared-source readback agrees with canonical90BPM fitting and exact trim")
        noop_before = unchanged_snapshot()
        noop = tempo("apply", operation=tempo("capture", channel=channel)["operation"], sourceBpm=90, fit=True)
        check(noop["applied"] is True and noop["documentChanged"] is False and unchanged_snapshot() == noop_before,
              "Explicit unchanged tempo apply is acknowledged without hidden history")
        expired = tempo("capture", channel=channel)["operation"]
        for _ in range(32):
            check(tempo("capture", channel=channel)["completionState"] == "completed", "Bounded finished confirmation record")
        refuse({"action": "job", "operation": expired}, "samplerTempo.unknownOperation")

        command("pattern.length", pattern=pattern, length=16)
        command("notes.add", pattern=pattern, channel=channel, beat=0, length=.25, pitch=60, value=.8)
        command("clip.add", pattern=pattern, beat=0, length=16)

        def render(name):
            path = attempt / name
            result = call("vibera_project", {"action": "export", "path": str(path), "overwrite": False})
            check(result["completionState"] == "pending", "Actual offline export acceptance is pending")
            completed = client.wait_job(result)
            receipt["operations"].append({"tool": "vibera_project", "terminal": completed})
            with wave.open(str(path), "rb") as wav:
                check((wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()) == (RATE, 2, 3, FRAMES),
                      "Actual16-quarter120BPM Song export is exactly8s stereo24bit")
                raw = wav.readframes(FRAMES)
            left = [int.from_bytes(raw[i:i + 3], "little", signed=True) / 8388608 for i in range(0, len(raw), 6)]
            for pulse_index in range(8):
                lo, hi = max(0, int((pulse_index * .375 - .005) * RATE)), int((pulse_index * .375 + .02) * RATE)
                check(max(abs(value) for value in left[lo:hi]) > .005,
                      "Independent fitted pulse occurs at literal3/8-second spacing: " + str(pulse_index))
            check(max(abs(value) for value in left[int(3.02 * RATE):]) == 0,
                  "Trimmed4s resampled at120/90 finishes before3.02s, without fitted phase or duration")
            return raw

        original_pcm = render("fitted-original.wav")
        project = attempt / "sampler-tempo.vibra"
        client.wait_job(call("vibera_project", {"action": "save", "path": str(project), "overwrite": False}))
        xml = ET.parse(project).getroot()
        selected = next(row for row in xml.findall(".//CHANNEL") if int(row.get("id", "0")) == channel)
        persisted = selected.find("SAMPLER")
        check(persisted is not None and float(persisted.get("sourceTempo")) == 90 and
              persisted.get("fitTempo") == "1" and float(persisted.get("start")) == .25 and float(persisted.get("end")) == .75,
              "Actual project bytes persist exact BPM, fitting and trimmed source settings")
        saved_settings, identity = settings(channel), state()["projectIdentity"]
        reopen_witness = tempo("capture", channel=channel)["operation"]
        client.wait_job(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
        wait_prepared(channel)
        check(state()["projectIdentity"] != identity and settings(channel) == saved_settings,
              "Actual Open restores exact sampler settings in a new source session")
        refuse({"action": "apply", "operation": reopen_witness, "sourceBpm": 100}, "samplerTempo.stale")
        check(render("fitted-reopened.wav") == original_pcm, "Recovered actual offline PCM is byte-exact after Save/Open")
        check(all(digest(path) == receipt["originalSources"][str(path)] for path in (pulse, silence, replacement)),
              "All tempo workflows preserve original source files byte-for-byte")
        receipt["passed"] = True
    except Exception as failure:
        receipt["error"] = str(failure)
        receipt["traceback"] = traceback.format_exc()
    finally:
        if client is not None:
            try:
                client.close()
                receipt["exitCode"] = client.process.returncode
            except Exception as failure:
                receipt["passed"] = False
                receipt["closeError"] = str(failure)
            (attempt / "application-stderr.txt").write_text("".join(client.stderr), encoding="utf-8")
        receipt["finished"] = True
        receipt["seconds"] = time.monotonic() - started
        retain()
    print(json.dumps(receipt, ensure_ascii=False))
    return 0 if receipt["passed"] and receipt.get("exitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
