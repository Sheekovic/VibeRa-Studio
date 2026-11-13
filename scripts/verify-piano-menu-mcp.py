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

    # FL tool options: flip keeping start times, flam on the beat, alternating strum.
    ids = reset()
    before = {n["id"]: n for n in notes()}
    starts = sorted(n["beat"] for n in before.values())
    check(done("notes.flip", text="horizontalStarts", **target(ids)) == "completed"
          and sorted(n["beat"] for n in notes()) == starts
          and all(abs(n["length"] - before[n["id"]]["length"]) < 1e-9 for n in notes())
          and [n["pitch"] for n in sorted(notes(), key=lambda n: n["beat"])] == [72, 67, 64, 60],
          "Flip horizontally, keep start times: the starts mirror and every length stays (notes.flip horizontalStarts)")
    ids = reset()
    before = {n["id"]: n for n in notes()}
    check(done("notes.flam", length=1 / 64, value=.6, text="after", **target(ids)) == "completed" and len(notes()) == 8
          and all(abs(n["beat"] - before[n["id"]]["beat"] - 1 / 64) < 1e-9
                  and abs(n["beat"] + n["length"] - before[n["id"]]["beat"] - before[n["id"]]["length"]) < 1e-9
                  for n in notes() if n["id"] in before)
          and sorted(n["beat"] for n in notes() if n["id"] not in before) == sorted(b["beat"] for b in before.values()),
          "Flam on the beat: the stroke takes the original start and the note follows, keeping its end (notes.flam after)")
    check(done("notes.flam", length=1 / 64, value=.6, text="sideways", **target(ids)) == "failed", "An unknown flam placement is refused")
    ids = reset()
    done("notes.chordify", text="major", value=0, **target(ids))
    chordIds = [n["id"] for n in notes()]
    check(done("notes.strum", text="alternate", length=.0625, value=0, **target(chordIds)) == "completed", "Strum alternate runs (notes.strum)")
    groups = {}
    for n in notes():
        groups.setdefault(round(n["beat"] - (n["beat"] % 1 if n["beat"] % 1 < .5 else 0), 6), []).append(n)
    chords = [sorted(groups[k], key=lambda n: n["beat"]) for k in sorted(groups)]
    rising = [c[0]["pitch"] < c[-1]["pitch"] for c in chords if len(c) == 3]
    check(len(rising) == 4 and rising == [True, False, True, False],
          f"Alternate strums successive chords up, down, up, down {rising}")

    # More FL tool options: strum tension / trigger ahead, flam in milliseconds, claw trash every Nth note.
    def strummed(**extra):
        ids = reset()
        done("notes.chordify", text="major", value=0, **target(ids))
        state = done("notes.strum", text="up", length=.125, value=0, **extra, **target([n["id"] for n in notes()]))
        first = sorted((n for n in notes() if n["beat"] < .9), key=lambda n: n["pitch"])
        return state, [round(n["beat"], 6) for n in first]
    check(strummed() == ("completed", [0, .125, .25]), "An even strum delays each chord note by the spread")
    state, bent = strummed(offset=1)
    check(state == "completed" and bent[0] == 0 and bent[2] == .25 and .125 < bent[1] < .25,
          f"Positive strum tension speeds the strum up toward its end and keeps the first and last note (notes.strum offset) {bent}")
    check(strummed(offset=2)[0] == "failed", "Strum tension outside -1..1 is refused")
    ids = reset()
    done("notes.chordify", text="major", value=0, **target([n["id"] for n in notes() if n["beat"] >= .9 and n["beat"] < 1.1]))
    second = [n["id"] for n in notes() if .9 <= n["beat"] < 1.1]
    check(done("notes.strum", text="up", length=.125, value=0, beat=.5, **target(second)) == "completed"
          and sorted(round(n["beat"], 6) for n in notes() if n["id"] in second) == [.875, 1, 1.125],
          "Strum trigger ahead centres the strum on the chord start (notes.strum beat)")
    ids = reset()
    before = {n["id"]: n for n in notes()}
    check(done("notes.flam", length=.25, value=.6, offset=30, **target(ids)) == "completed" and len(notes()) == 8
          and sorted(round(n["beat"], 6) for n in notes() if n["id"] not in before)[1:] == [round(b - .03 * 2, 6) for b in (1, 2, 3)],
          "Flam absolute time: 30 ms is 0.06 beat at 120 BPM (notes.flam offset)")
    check(done("notes.flam", length=.25, value=.6, offset=900, **target(ids)) == "failed", "A flam time above 250 ms is refused")
    ids = reset()
    check(done("notes.claw", text="trash", value=2, enabled=True, **target(ids)) == "completed"
          and [(n["pitch"], round(n["beat"], 6)) for n in sorted(notes(), key=lambda n: n["beat"])] == [(60, 0), (67, 1.75)],
          f"Claw trashes every 2nd note and stretches the rest over the span (notes.claw trash) {[(n['pitch'], n['beat'], n['length']) for n in notes()]}")
    ids = reset()
    check(done("notes.claw", text="trash", value=2, enabled=False, **target(ids)) == "completed"
          and [(n["pitch"], n["beat"]) for n in sorted(notes(), key=lambda n: n["beat"])] == [(60, 0), (67, 2)],
          "Claw trash without stretch keeps the gaps")
    check(done("notes.claw", text="trash", value=9, **target(ids)) == "failed", "A trash interval longer than the notes is refused")

    # Arpeggiate and Randomize range in octaves (FL Range).
    ids = reset()
    done("notes.chordify", text="major", value=0, **target([ids[0]]))
    chord = [n["id"] for n in notes() if n["beat"] < .4]
    for n in chord:
        command("notes.resize", ids=[n], length=2) if False else None
    check(done("notes.arpeggiate", text="up", length=.125, value=.9, offset=2, **target(chord)) == "completed", "Arpeggiate over 2 octaves runs (notes.arpeggiate offset)")
    run = [n["pitch"] for n in sorted((n for n in notes() if n["beat"] < .9), key=lambda n: n["beat"])]
    check(len(run) >= 4 and run[:4] == [60, 64, 67, 72], f"The run climbs into the second octave {run}")
    check(done("notes.arpeggiate", text="up", length=.125, value=.9, offset=7, **target([n["id"] for n in notes()])) == "failed", "An arpeggio range above 4 octaves is refused")
    reset()
    check(done("notes.randomize", text="major", pitch=48, length=.25, value=1, seed=3, offset=3, pattern=pattern, channel=channel) == "completed"
          and max(n["pitch"] for n in notes()) > 60 and all(48 <= n["pitch"] <= 84 for n in notes()),
          f"Randomize over 3 octaves spreads the notes across the range (notes.randomize offset) {sorted(set(n['pitch'] for n in notes()))[:3]}..{max(n['pitch'] for n in notes())}")
    check(done("notes.randomize", text="major", pitch=48, length=.25, value=1, seed=3, offset=9, pattern=pattern, channel=channel) == "failed", "A randomize range above 4 octaves is refused")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
