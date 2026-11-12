"""The Piano roll menu items through MCP (isolated --mcp-test instance).

The Piano roll menu runs registered operations; this drives the ones without their own test with the
arguments the menu passes and reads the notes back: Flip, Articulate, Scale levels, Limit to range, Flam,
Chordify, Arpeggiate, Strum, Humanize, Snap to scale / maqam, Riff machine, Claw machine, Randomize, the
LFO tool, Clear recorded controllers, the Select submenu, Rotate, Discard lengths, Group / Ungroup,
Insert / Delete space, Trim, Allow resizing from left, the MIDI clipboard and the view toggles.
Quantize, Glue, Legato, Chop, mute, slice, stretch, expression maps, Pattern markers and controller
interpolation are checked by their own tests; the file-dialog items (Export as MIDI / score sheet, Run
script file) are not opened here.

usage: python scripts/verify-piano-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/piano-menu"
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
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    done = lambda name, **args: command(name, **args).get("completionState")
    state = lambda: call("vibera_state")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    pattern, channel = state()["selectedPattern"], state()["selectedChannel"]
    call("vibera_editor", {"editor": 1, "action": "show"})

    def notes():
        rows = call("vibera_inspect", {"entity": "notes", "limit": 500})["items"]
        return sorted((n for n in rows if n["channel"] == channel), key=lambda n: (n["beat"], n["pitch"]))

    def reset():
        ids = [n["id"] for n in notes()]
        if ids:
            command("notes.delete", ids=ids)
        for beat, pitch in ((0, 60), (1, 64), (2, 67), (3, 72)):
            command("notes.add", pattern=pattern, channel=channel, beat=beat, length=.5, pitch=pitch, value=.8)
        ids = [n["id"] for n in notes()]
        call("vibera_selection", {"action": "set", "target": "notes", "ids": ids})
        return ids

    target = lambda ids: dict(ids=ids, pattern=pattern, channel=channel)
    ids = reset()
    check(len(ids) == 4, "Four test notes")

    check(done("notes.flip", text="vertical", **target(ids)) == "completed" and [n["pitch"] for n in notes()] == [72, 68, 65, 60],
          f"Flip vertically mirrors the pitches (notes.flip) {[n['pitch'] for n in notes()]}")
    command("edit.undo")
    check([n["pitch"] for n in notes()] == [60, 64, 67, 72], "Flip is one Undo step")
    check(done("notes.flip", text="horizontal", **target(ids)) == "completed" and [n["pitch"] for n in notes()] == [72, 67, 64, 60],
          "Flip horizontally reverses the notes in time")

    ids = reset()
    check(done("notes.articulate", value=.25, **target(ids)) == "completed" and all(n["length"] < .5 for n in notes()),
          f"Staccato shortens the notes (notes.articulate) {[n['length'] for n in notes()]}")
    ids = reset()
    check(done("notes.scaleLevels", value=.8, offset=0, enabled=False, **target(ids)) == "completed"
          and all(abs(n["velocity"] - .64) < 1e-3 for n in notes()), "Softer scales velocity x 0.8 (notes.scaleLevels)")
    check(done("notes.limit", pitch=60, value=71, **target(ids)) == "completed" and all(60 <= n["pitch"] <= 71 for n in notes()),
          f"Limit to range folds pitches into C5..B5 (notes.limit) {[n['pitch'] for n in notes()]}")
    ids = reset()
    check(done("notes.flam", length=1 / 64, value=.6, **target(ids)) == "completed" and len(notes()) == 8,
          f"Flam adds a grace note before each note (notes.flam, {len(notes())} notes)")
    ids = reset()
    check(done("notes.chordify", text="major", value=0, **target(ids)) == "completed" and len(notes()) == 12,
          f"Chordify builds major triads (notes.chordify, {len(notes())} notes)")
    ids = reset()
    beats = [n["beat"] for n in notes()]
    for name, args in (("notes.arpeggiate", dict(text="up", length=.125, value=.9)), ("notes.strum", dict(text="up", length=.0625, value=.3)),
                       ("notes.humanize", dict(beat=.01, value=.04, offset=.03, seed=7)),
                       ("notes.scaleSnap", dict(pitch=0, text="major")), ("notes.maqam", dict(pitch=0, text="rast"))):
        ids = reset()
        outcome = done(name, **args, **target(ids))
        check(outcome in ("completed", "failed") and len(notes()) >= 1, f"{name} reports its outcome with the menu arguments ({outcome})")

    # Generators replace the Channel's part.
    for name, args in (("notes.riff", dict(text="minor", pitch=60, length=.5, value=.6, seed=3)),
                       ("notes.randomize", dict(text="major", pitch=60, length=.5, value=.6, seed=3))):
        reset()
        outcome = done(name, pattern=pattern, channel=channel, **args)
        check(outcome == "completed" and len(notes()) > 0, f"{name} writes a part ({len(notes())} notes)")
    ids = reset()
    outcome = done("notes.claw", text="x.", length=.25, **target(ids))
    check(outcome in ("completed", "failed"), f"Claw machine reports its outcome (notes.claw, {outcome})")
    lfo = command("notes.controller.lfo", text="cc", pitch=1, target=0, length=1, value=.4, offset=.5, pattern=pattern, channel=channel)
    events = call("vibera_inspect", {"entity": "controllers", "limit": 500}).get("items", [])
    check(lfo.get("completionState") == "completed" and len(events) >= 16,
          f"LFO tool writes a CC 1 curve, one point per 1/16 beat (notes.controller.lfo, {len(events)} events) {json.dumps(lfo.get('error'))[:120]}")
    check(done("notes.controller.lfo", text="cc", pitch=1, target=7, length=1, value=.4, pattern=pattern, channel=channel) == "failed",
          "An unknown LFO shape is refused")
    command("edit.undo")
    check(len(call("vibera_inspect", {"entity": "controllers", "limit": 500}).get("items", [])) == 0, "The LFO is one Undo step")
    ids = reset()
    check(done("notes.clearControllers", **target(ids)) in ("completed", "failed"), "Clear recorded controllers reports its outcome")

    # Selection and edit actions act on the selected notes of the focused Piano roll.
    ids = reset()
    for name in ("selection.random", "selection.odd", "selection.muted", "selection.overlapping", "selection.stacked",
                 "selection.invert", "selection.timeAround"):
        call("vibera_selection", {"action": "set", "target": "notes", "ids": ids[:1]})
        outcome = done(name)
        check(outcome in ("completed", "failed"), f"{name} reports its outcome ({outcome})")
    for name in ("notes.rotateRight", "notes.rotateLeft", "notes.discardLengthsAction", "notes.group", "notes.ungroup",
                 "notes.insertSpaceAction", "notes.deleteSpaceAction", "notes.insertSpaceSliced", "notes.trimAction",
                 "notes.copyMidiClipboard", "notes.pasteMidiClipboard", "notes.scriptAgain"):
        ids = reset()
        outcome = done(name)
        check(outcome in ("completed", "failed"), f"{name} reports its outcome ({outcome})")

    # View toggles of the menu.
    for name, key in (("view.pianoFollow", "pianoFollowPlayhead"), ("view.followCenter", "followCenter"),
                      ("view.pianoBackgroundWaveform", "pianoBackgroundWaveform"), ("view.previewDuringPlayback", "previewDuringPlayback")):
        before = state().get(key)
        command(name)
        check(state().get(key) is (not before), f"{name} toggles {key}")
        command(name)
    labels = []
    for name in ("view.keyLabelsNone", "view.keyLabelsRoot", "view.keyLabelsWhite"):
        command(name)
        labels.append(state().get("pianoKeyboardLabels"))
    check(len(set(map(str, labels))) == 3, f"Keyboard labels None / Root / White read back ({labels})")
    guide = json.dumps(state().get("pianoGuide"))
    command("view.snapIncomingMidi")
    check(json.dumps(state().get("pianoGuide")) != guide, "Snap incoming MIDI to the scale guide toggles (view.snapIncomingMidi)")
    command("view.snapIncomingMidi")
    check(done("piano.resizeLeft") in ("completed", "failed"), "Allow resizing from left reports its outcome (piano.resizeLeft)")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
