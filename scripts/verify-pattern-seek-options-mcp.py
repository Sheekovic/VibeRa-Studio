"""FL manual seventh pass through MCP: Patterns menu and transport options (isolated --mcp-test instance).

pattern.selectInPlaylist selects every Playlist clip of the selected Pattern; pattern.randomColour gives
the selected Pattern a new opaque colour as one Undo step; transport.rememberSeek makes the first Stop
return to where Play was pressed instead of the Song start (Stop again goes to the start). The instance
has no audio device, so Play does not start: the remembered position is the one at the Play press.
transport.disarmOnStop and transport.recordStartsPlayback are the recording options of the same manual
page, view.timeDisplay switches the time display to minutes:seconds (state.positionSeconds) and
pattern.findEmptyNoNaming is Find next empty without the name field.

usage: python scripts/verify-pattern-seek-options-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/pattern-seek-options"
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
    state = lambda: call("vibera_state")
    items = lambda entity: call("vibera_inspect", {"entity": entity, "limit": 500}).get("items", [])
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    first = state()["selectedPattern"]

    # Select in playlist.
    command("clip.add", pattern=first, beat=0, lane=0, length=4)
    command("clip.add", pattern=first, beat=8, lane=1, length=4)
    command("pattern.add")
    second = max(p["id"] for p in items("patterns"))
    command("clip.add", pattern=second, beat=4, lane=0, length=4)
    call("vibera_select", {"pattern": first})
    expected = sorted(c["id"] for c in items("clips") if c["kind"] == "pattern" and c.get("source") == first)
    check(len(expected) == 2, "Two clips of the first Pattern and one of the second")
    check(command("pattern.selectInPlaylist").get("completionState") == "completed"
          and sorted(call("vibera_selection", {"action": "state", "target": "clips"})["ids"]) == expected,
          "Select in playlist selects exactly the Pattern's clips (pattern.selectInPlaylist)")
    command("pattern.add")
    unused = max(p["id"] for p in items("patterns"))
    call("vibera_select", {"pattern": unused})
    check(command("pattern.selectInPlaylist").get("completionState") == "failed"
          and call("vibera_selection", {"action": "state", "target": "clips"})["ids"] == [],
          "A Pattern without clips selects nothing and says so")

    # Random colour.
    call("vibera_select", {"pattern": first})
    colour = lambda: next(p for p in state()["patterns"] if p["id"] == first)["colour"]  # ARGB hex, or "auto".
    name = next(p for p in items("patterns") if p["id"] == first)["name"]
    before = colour()
    check(command("pattern.randomColour").get("completionState") == "completed" and colour() != before and colour().lower().startswith("ff") and len(colour()) == 8,
          f"Random colour gives the Pattern an opaque colour (pattern.randomColour, {before} -> {colour()})")
    check(next(p for p in items("patterns") if p["id"] == first)["name"] == name, "The Pattern keeps its name")
    command("edit.undo")
    check(colour() == before, "Random colour is one Undo step")

    # Remember seek time.
    transport = lambda action, **a: raw("vibera_transport", dict(action=action, **a))
    position = lambda: state()["positionBeat"]
    check(state().get("rememberSeekTime") is False, "Remember seek time is off by default")
    transport("seek", beat=8)
    transport("play")
    transport("stop")
    check(abs(position()) < 1e-9, "Off: Stop returns to the Song start")
    command("transport.rememberSeek")
    check(state().get("rememberSeekTime") is True, "transport.rememberSeek turns the option on")
    transport("seek", beat=8)
    transport("play")
    transport("stop")
    check(abs(position() - 8) < 1e-6, f"On: Stop returns to where Play was pressed (beat {position()})")
    transport("stop")
    check(abs(position()) < 1e-9, "Stop again goes to the Song start")
    command("transport.rememberSeek")
    check(state().get("rememberSeekTime") is False, "The option turns off again")

    # Recording options: Disarm on stop and Recording starts playback.
    armed = lambda: state().get("recordArmed")
    check(state().get("disarmOnStop") is True and state().get("recordStartsPlayback") is False, "Recording options read back their defaults")
    command("transport.record")
    check(armed() is True, "Record arms")
    transport("stop")
    check(armed() is False, "Disarm on stop on: Stop turns Record off")
    command("transport.disarmOnStop")
    command("transport.record")
    transport("stop")
    check(state().get("disarmOnStop") is False and armed() is True, "Disarm on stop off: Record stays armed after Stop")
    command("transport.record")
    check(armed() is False, "Record disarms by hand")
    command("transport.disarmOnStop")
    command("transport.recordStartsPlayback")
    check(state().get("recordStartsPlayback") is True, "transport.recordStartsPlayback turns the option on")
    command("transport.record")
    # Without an audio device the Play this triggers cannot open a take, which disarms Record:
    # that refusal shows Record pressed Play (with the option off Record simply stays armed).
    check(armed() is False, "Recording starts playback: Record also presses Play (the take path runs and is refused without a device)")
    transport("stop")
    command("transport.recordStartsPlayback")
    command("transport.record")
    check(armed() is True, "Option off: Record only arms")
    command("transport.record")

    # Time display and position in seconds.
    check(state().get("timeDisplay") == "beats", "Time is shown as beats by default")
    command("view.timeDisplay")
    check(state().get("timeDisplay") == "seconds", "view.timeDisplay switches to minutes:seconds")
    call("vibera_transport", {"action": "song"})
    transport("seek", beat=8)
    tempo = state()["tempo"]
    check(abs(state()["positionSeconds"] - 8 * 60 / tempo) < 1e-6, f"Beat 8 at {tempo} BPM reads back as {8 * 60 / tempo:.3f} s")
    command("view.timeDisplay")
    transport("stop")

    # Find next empty (no naming).
    call("vibera_select", {"pattern": first})
    found = command("pattern.findEmptyNoNaming").get("completionState")
    chosen = state()["selectedPattern"]
    used = {c["source"] for c in items("clips") if c["kind"] == "pattern"}
    check(found == "completed" and chosen != first and not [n for n in items("notes") if n.get("pattern") == chosen],
          f"Find next empty (no naming) selects an empty Pattern (pattern.findEmptyNoNaming, {first} -> {chosen})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
