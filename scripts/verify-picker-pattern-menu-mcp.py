"""Picker Pattern menu through MCP (isolated --mcp-test instance, no audio device).

The Picker's Pattern context menu runs registered operations; this drives them and reads the Patterns
back: Group selected / Ungroup selected, Rename / colour, Transpose, Clone selected Patterns, Delete
selected Patterns and Delete Patterns not in Playlist. Every edit is one Undo step. Render and replace
has its own test (pattern_render_replace_mcp); Export Pattern WAV opens a native file dialog and is not
driven here.

usage: python scripts/verify-picker-pattern-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/picker-pattern-menu"
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
    rows = lambda: {p["id"]: p for p in call("vibera_inspect", {"entity": "patterns", "limit": 512})["items"]}
    pitches = lambda pattern: sorted(n["pitch"] for n in call("vibera_inspect", {"entity": "notes", "pattern": pattern, "limit": 512})["items"])
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    first = next(iter(rows()))
    done("pattern.add")
    second = next(i for i in rows() if i != first)

    # Group selected... / Ungroup selected.
    check(done("pattern.group", ids=[first, second], text="Verse") == "completed"
          and [rows()[first]["group"], rows()[second]["group"]] == ["Verse", "Verse"], "Group selected puts both Patterns in the filter group (pattern.group)")
    check(done("pattern.group", ids=[second], text="") == "completed" and rows()[second]["group"] == "" and rows()[first]["group"] == "Verse",
          "Ungroup selected clears only the chosen Pattern")
    check(done("pattern.group", ids=[first, 999999], text="X") == "failed" and rows()[first]["group"] == "Verse", "A missing Pattern is refused and nothing changes")
    done("edit.undo")
    done("edit.undo")
    check(rows()[first]["group"] == "" and rows()[second]["group"] == "", "Grouping and ungrouping are one Undo step each")

    # Rename / colour...
    name = rows()[first]["name"]
    check(done("pattern.properties", pattern=first, text="Hook", value=0xFF2E8BC0) == "completed" and rows()[first]["name"] == "Hook",
          "Rename / colour names the Pattern (pattern.properties)")
    check(done("pattern.properties", pattern=first, text="Hook", value=0x002E8BC0) == "failed", "A transparent Pattern colour is refused")
    done("edit.undo")
    check(rows()[first]["name"] == name, "Rename / colour is one Undo step")

    # Transpose...
    before = pitches(first)
    check(len(before) > 0, f"The demo Pattern has notes ({len(before)})")
    check(done("pattern.transpose", pattern=first, channel=0, value=12) == "completed" and pitches(first) == [p + 12 for p in before],
          "Transpose moves every note up an octave (pattern.transpose)")
    check(done("pattern.transpose", pattern=first, channel=0, value=127) == "failed" and pitches(first) == [p + 12 for p in before],
          "A transpose outside MIDI pitches is refused and nothing changes")
    check(done("pattern.transpose", pattern=second, channel=0, value=1) == "failed", "An empty Pattern has nothing to transpose")
    done("edit.undo")
    check(pitches(first) == before, "Transpose is one Undo step")

    # Clone selected Patterns / Delete selected Patterns.
    count = len(rows())
    check(done("pattern.cloneSelected", ids=[first, second]) == "completed" and len(rows()) == count + 2, "Clone selected Patterns adds two Patterns (pattern.cloneSelected)")
    clones = [i for i in rows() if i not in (first, second)]
    check(sorted(rows()[c]["noteCount"] for c in clones) == sorted([rows()[first]["noteCount"], rows()[second]["noteCount"]]), "The clones carry the notes")
    check(done("pattern.deleteSelected", ids=clones) == "completed" and sorted(rows()) == sorted([first, second]),
          "Delete selected Patterns removes them (pattern.deleteSelected)")
    done("edit.undo")
    check(len(rows()) == count + 2, "Delete selected is one Undo step")
    done("pattern.deleteSelected", ids=clones)

    # Delete Patterns not in Playlist.
    placed = {c.get("source") for c in call("vibera_inspect", {"entity": "clips", "limit": 512})["items"]}
    purged = done("pattern.deleteUnplaced")
    left = sorted(rows())
    check(purged == "completed" and second not in left and all(i in placed for i in left) and len(left) >= 1,
          f"Delete Patterns not in Playlist keeps only placed Patterns (pattern.deleteUnplaced, {len(left)} left)")
    done("edit.undo")
    check(second in rows(), "Delete Patterns not in Playlist is one Undo step")

    # Delete empty unused Patterns: the second Pattern has no notes and no placement.
    check(rows()[second]["noteCount"] == 0 and done("pattern.deleteEmpty") == "completed" and second not in rows() and first in rows(),
          "Delete empty unused Patterns removes the empty unplaced Pattern (pattern.deleteEmpty)")
    done("edit.undo")
    check(second in rows(), "Delete empty unused Patterns is one Undo step")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
