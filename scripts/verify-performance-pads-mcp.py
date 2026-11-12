"""Performance mode pads and scenes through MCP (isolated --mcp-test instance, no audio device).

The Performance window's pads, pad menu and Scenes menu run registered operations; this drives them and
reads state.performance and the Pattern rows back: launch / stop a pad (performance.padN), assign a
Pattern to a pad, launch quantum and Follow action of a pad's Pattern, save and launch a scene, and stop
all. Without an audio device nothing plays: launching only places the performance clips. Pattern
management's Delete empty unused Patterns is checked here too.

usage: python scripts/verify-performance-pads-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/performance-pads"
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
    result = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    done = lambda name, **args: result(name, **args).get("completionState")
    state = lambda: call("vibera_state")
    performance = lambda: state()["performance"]
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    done("pattern.add")  # The demo has one Pattern; pads 2 and 5 need more.
    done("pattern.add")
    patterns = [p["id"] for p in state()["patterns"]]
    check(len(patterns) >= 2 and performance()["launched"] == [] and performance()["pads"] == [0] * 16,
          f"Performance state reads back: nothing launched, default pads ({len(patterns)} Patterns)")

    # Pads launch into the Song: Pattern mode refuses, Song mode launches the n-th Pattern.
    call("vibera_transport", {"action": "pattern"})
    check(done("performance.pad1") == "failed" and performance()["launched"] == [], "In Pattern mode a pad is refused (Performance launches into the Song)")
    call("vibera_transport", {"action": "song"})
    done("performance.pad1")
    check(performance()["launched"] == [patterns[0]], f"Pad 1 launches the first Pattern (performance.pad1) {performance()['launched']}")
    done("performance.pad2")
    check(sorted(performance()["launched"]) == sorted(patterns[:2]), "Pad 2 adds the second Pattern")
    done("performance.pad1")
    check(performance()["launched"] == [patterns[1]], "Pressing pad 1 again stops its Pattern")

    # Scenes: save the launched Patterns, stop all, launch the scene again.
    check(done("performance.saveScene", value=3, ids=performance()["launched"]) == "completed"
          and performance()["scenes"][2] == [patterns[1]], "Save launched Patterns as scene 3 (performance.saveScene)")
    check(done("performance.saveScene", value=9, ids=[patterns[0]]) == "failed", "Scene 9 is refused")
    done("performance.stopAll")
    check(performance()["launched"] == [], "Stop all clears the launched Patterns (performance.stopAll)")
    done("performance.scene3")
    check(performance()["launched"] == [patterns[1]], "Launch scene 3 launches its Patterns (performance.scene3)")
    done("performance.stopAll")

    # Pad menu: assign a Pattern, launch quantum, Follow action.
    check(done("performance.assignPad", value=5, pattern=patterns[1]) == "completed" and performance()["pads"][4] == patterns[1],
          "Assign Pattern to pad 5 (performance.assignPad)")
    done("performance.pad5")
    check(performance()["launched"] == [patterns[1]], "Pad 5 now launches the assigned Pattern")
    done("performance.stopAll")
    check(done("performance.assignPad", value=5, pattern=0) == "completed" and performance()["pads"][4] == 0, "Default resets the pad")
    check(done("performance.assignPad", value=17, pattern=patterns[0]) == "failed", "Pad 17 is refused")
    row = lambda: next(p for p in state()["patterns"] if p["id"] == patterns[0])
    check(done("pattern.launchQuantum", pattern=patterns[0], value=1) == "completed" and row()["launchQuantum"] == 1,
          "Launch quantum: one beat (pattern.launchQuantum)")
    check(done("pattern.followAction", pattern=patterns[0], text="next", value=2) == "completed"
          and row()["followAction"] == "next" and row()["followAfter"] == 2, "Follow action: next after 2 plays (pattern.followAction)")
    check(done("pattern.followAction", pattern=patterns[0], text="sideways", value=2) == "failed", "An unknown Follow action is refused")
    done("pattern.followAction", pattern=patterns[0], text="", value=1)
    check(row()["followAction"] == "", "Follow action off")

    # Pattern management: delete empty unused Patterns.
    done("pattern.add")
    before = len(state()["patterns"])
    purged = done("pattern.deleteEmpty")
    check(purged in ("completed", "failed") and len(state()["patterns"]) <= before,
          f"Delete empty unused Patterns reports its outcome (pattern.deleteEmpty, {purged}, {before} -> {len(state()['patterns'])})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
