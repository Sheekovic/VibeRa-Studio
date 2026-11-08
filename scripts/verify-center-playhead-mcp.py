"""Center (Shift+0) through MCP (isolated --mcp-test instance).

view.centerPlayhead scrolls the focused editor once so the playhead sits mid-view: after seeking
to beat 64 the Playlist view starts before 64 and the playhead is inside the view, and the
Piano roll does the same when it has focus.

usage: python scripts/verify-center-playhead-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/center-playhead"
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
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    call("vibera_transport", {"action": "song"})
    call("vibera_transport", {"action": "seek", "beat": 64})
    before = call("vibera_state")
    check(before.get("playlistScrollBeat", -1) == 0, "The Playlist view starts at the Song start")
    call("vibera_select", {"scope": 3})
    result = call("vibera_command", {"command": "view.centerPlayhead"})
    after = call("vibera_state")
    scroll = after.get("playlistScrollBeat", -1)
    check(0 < scroll < 64, f"Center scrolls the Playlist so beat 64 is mid-view (view starts at {scroll}) {str(result)[:80]}")
    call("vibera_select", {"scope": 2})
    call("vibera_command", {"command": "view.centerPlayhead"})
    piano = call("vibera_state").get("pianoScrollBeat", -1)
    check(0 < piano < 64, f"Center works in the Piano roll too (view starts at {piano})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
