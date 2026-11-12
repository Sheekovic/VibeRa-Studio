"""The Tools menu items through MCP (isolated --mcp-test instance).

Each item of the main Tools menu runs a registered operation; this drives those operations the way the
menu does and reads the result back: window items (CoProducer, Performance, Drum pads, chord progression),
workspace layout items, step record and MIDI recording replace, the score log, the template / backup
folder items (a test instance reports the folder without starting the file manager), update settings and
the update dialogs' refusals when nothing is available. Master pitch, Wait for input, the Recording filter
and the Audio logger are checked by master_pitch_mcp and record_filter_mcp; diagnostics, loudness, the
Chord Track, missing samples and Last tweaked by their own tests. No update check contacts the network here.

usage: python scripts/verify-tools-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/tools-menu"
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
    raw = lambda tool, args: client.call(tool, args, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args or {}).get("structuredContent", {})
    state = lambda: call("vibera_state")
    command = lambda name: call("vibera_command", {"command": name})
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})

    # Windows and panels.
    shown = state().get("coProducerShown")
    command("view.coProducer")
    check(state().get("coProducerShown") is (not shown), "CoProducer panel toggles (view.coProducer)")
    command("view.coProducer")
    check(state().get("performanceShown") is False, "The Performance window starts closed")
    check(command("view.performance").get("completionState") == "completed" and state().get("performanceShown") is True,
          "Performance mode opens its window (view.performance)")
    check(command("view.drumPads").get("completionState") == "completed", "Drum pads opens (view.drumPads)")

    # Workspace layout items.
    for name, mode in (("workspace.tileBalanced", 1), ("workspace.tileFocused", 2), ("workspace.free", 0)):
        command(name)
        check(state().get("autoTileMode") == mode, f"{name} sets the tiling mode {mode}")
    check(command("view.restoreLayout").get("completionState") == "completed", "Restore docked workspace layout runs")

    # Recording options.
    before = state().get("midiRecordReplace")
    check(isinstance(before, bool), "MIDI recording replace reads back")
    command("transport.recordReplace")
    check(state().get("midiRecordReplace") is (not before), "transport.recordReplace toggles replace / overdub")
    command("transport.recordReplace")
    check(state().get("midiRecordReplace") is before, "A second toggle restores it")
    check(command("score.clear").get("completionState") == "completed", "Clear log runs (score.clear)")

    # Folder items: the folder is reported; the outcome follows whether it exists.
    folders = state().get("folders", {})
    check(bool(folders.get("templates")) and bool(folders.get("backups")), f"Template and backup folders read back ({folders})")
    for name, key in (("project.openTemplatesFolder", "templates"), ("project.openBackupsFolder", "backups")):
        expected = "completed" if Path(folders[key]).is_dir() else "failed"
        check(command(name).get("completionState") == expected, f"{name} reports {expected} for its folder")

    # Step record, chord progression window, detached tiling: each reports a real outcome.
    for name in ("transport.stepRecord", "transport.stepRest", "transport.stepRecord", "view.chordProgression",
                 "workspace.tileDetached", "score.dump"):
        outcome = command(name).get("completionState")
        check(outcome in ("completed", "failed"), f"{name} reports its outcome ({outcome})")

    # Detach to another monitor / dock back (view.editorWindow).
    check(not raw("vibera_editor", {"editor": 3, "action": "detach"}).get("isError"), "Detach Mixer runs (view.editorWindow)")
    check(not raw("vibera_editor", {"editor": 3, "action": "dock"}).get("isError"), "and docks back")

    # Diagnostics and templates.
    report = output / "diagnostics.txt"
    raw("vibera_project", {"action": "diagnostics", "path": str(report), "overwrite": True})
    check(report.is_file() and report.stat().st_size > 100, "Export diagnostics writes the report (project.diagnostics)")
    template = "tools-menu-" + uuid.uuid4().hex[:8]
    saved = raw("vibera_project", {"action": "saveTemplate", "template": template})
    check(not saved.get("isError"), "Save as template writes a template (project.saveTemplate)")
    for _ in range(50):  # The save is an asynchronous job.
        if template in call("vibera_project", {"action": "templates"}).get("templates", []):
            break
        time.sleep(.1)
    check(template in call("vibera_project", {"action": "templates"}).get("templates", []), "The template is listed")
    opened = raw("vibera_project", {"action": "new", "template": template, "discardChanges": True})
    check(not opened.get("isError"), "New from template opens it (project.new)")
    for candidate in Path(state()["folders"]["templates"]).glob(template + ".*"):
        candidate.unlink()  # The test's own template only.

    # Updates: settings and the dialogs' refusals (nothing is checked or downloaded here).
    status = call("vibera_update", {"action": "status"})
    check("phase" in status and isinstance(status.get("includePreview"), bool), f"Update status reads back ({status.get('phase')})")
    flipped = not status["includePreview"]
    check(call("vibera_update", {"action": "settings", "includePreview": flipped}).get("includePreview") is flipped,
          "app.update.settings changes Include preview releases")
    check(call("vibera_update", {"action": "settings", "includePreview": status["includePreview"]}).get("includePreview")
          is status["includePreview"], "and restores it")
    check(raw("vibera_update", {"action": "settings"}).get("isError"), "settings without a field is refused")
    not_available = raw("vibera_update", {"action": "showOffer"})
    check(not_available.get("isError") and "notAvailable" in json.dumps(not_available), "showOffer is refused with no release available")
    not_ready = raw("vibera_update", {"action": "offerInstall"})
    check(not_ready.get("isError") and "notReady" in json.dumps(not_ready), "offerInstall is refused with no download ready")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
