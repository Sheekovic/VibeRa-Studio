"""Recording filter (Notes) through MCP (isolated --mcp-test instance, no audio device).

transport.recordNotes toggles whether Record opens a MIDI note take (state.recordNotes). Without
an audio device a MIDI take cannot start: with Notes on, Record + Play enters the take path,
which refuses and disarms Record; with Notes off the take path is skipped and Record stays armed.
That difference shows the filter decides whether a note take is attempted. Not a hardware test.

usage: python scripts/verify-record-filter-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/record-filter"
output = base / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    call = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})
    state = lambda: call("vibera_state")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    check(state().get("recordNotes") is True, "Notes recording is on by default")

    def record_then_play():
        call("vibera_command", {"command": "transport.record"})
        armed_before = state().get("recordArmed")
        call("vibera_transport", {"action": "play"})
        armed_after = state().get("recordArmed")
        call("vibera_transport", {"action": "stop"})
        if state().get("recordArmed"):
            call("vibera_command", {"command": "transport.record"})  # Disarm for the next round.
        return armed_before, armed_after

    before, after = record_then_play()
    check(before is True and after is False, f"Notes on: the MIDI take path is entered and refused without a device ({before}->{after})")

    call("vibera_command", {"command": "transport.recordNotes"})
    check(state().get("recordNotes") is False, "transport.recordNotes turns Notes off")
    before, after = record_then_play()
    check(before is True and after is True, f"Notes off: no MIDI take is attempted, Record stays armed ({before}->{after})")
    check(not state().get("recordingTake"), "No take is open with Notes off")

    call("vibera_command", {"command": "transport.recordNotes"})
    check(state().get("recordNotes") is True, "transport.recordNotes turns Notes back on")

    # Wait for input (Tools menu): the option toggles and reads back; it never starts a take by itself.
    check(state().get("waitForInput") is False and state().get("waitingForInput") is False, "Wait for input is off by default")
    call("vibera_command", {"command": "transport.waitForInput"})
    check(state().get("waitForInput") is True and state().get("waitingForInput") is False,
          "transport.waitForInput turns the option on without opening a take")
    call("vibera_command", {"command": "transport.waitForInput"})
    check(state().get("waitForInput") is False, "transport.waitForInput turns it off again")

    # Wait for input options: Only when recording off makes plain Play wait for a note; Play or Stop cancels.
    check(state().get("waitOnlyWhenRecording") is True and state().get("waitRespectFilter") is True, "Wait for input options read back their defaults")
    call("vibera_command", {"command": "transport.waitForInput"})
    call("vibera_transport", {"action": "play"})
    waited = state().get("waitingForInput")
    call("vibera_transport", {"action": "stop"})
    check(waited is False, "Only when recording on: plain Play does not wait")
    call("vibera_command", {"command": "transport.waitOnlyWhenRecording"})
    check(state().get("waitOnlyWhenRecording") is False, "transport.waitOnlyWhenRecording turns the option off")
    call("vibera_transport", {"action": "play"})
    check(state().get("waitingForInput") is True and state().get("playing") is False, "Only when recording off: Play waits for a note instead of starting")
    call("vibera_transport", {"action": "stop"})
    check(state().get("waitingForInput") is False, "Stop cancels the wait")
    call("vibera_transport", {"action": "play"})
    call("vibera_command", {"command": "transport.waitOnlyWhenRecording"})
    check(state().get("waitingForInput") is False and state().get("waitOnlyWhenRecording") is True, "Turning the option back on ends the wait")
    call("vibera_command", {"command": "transport.waitRespectFilter"})
    check(state().get("waitRespectFilter") is False, "transport.waitRespectFilter turns the option off")
    call("vibera_command", {"command": "transport.waitRespectFilter"})
    call("vibera_command", {"command": "transport.waitForInput"})
    check(state().get("waitRespectFilter") is True and state().get("waitForInput") is False, "Both options and Wait for input are restored")

    # Record to step sequencer: the option toggles and reads back (the conversion is a native check).
    check(state().get("recordToSteps") is False, "Record to step sequencer is off by default")
    call("vibera_command", {"command": "transport.recordToSteps"})
    check(state().get("recordToSteps") is True, "transport.recordToSteps turns the option on")
    call("vibera_command", {"command": "transport.recordToSteps"})
    check(state().get("recordToSteps") is False, "transport.recordToSteps turns it off again")

    # Audio logger: without an audio device it is refused truthfully; save refuses while off.
    logger = state().get("audioLogger", {})
    check(logger.get("enabled") is False and logger.get("recordedSeconds") == 0, "Audio logger reads back off")
    raw = lambda tool, args: client.call(tool, args, allow_failure=True)
    refused = raw("vibera_project", {"action": "audioLogger", "enabled": True, "minutes": 1})
    check(refused.get("isError") and "noDevice" in json.dumps(refused), "Enabling without an audio device is refused (audioLog.noDevice)")
    check(raw("vibera_project", {"action": "audioLoggerSave", "path": str(output / "log.wav")}).get("isError"),
          "Saving while the logger is off is refused")
    check(raw("vibera_project", {"action": "audioLogger", "enabled": True, "minutes": 9}).get("isError"),
          "More than 5 minutes is refused")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
