"""Workspace window placement and splitters through MCP (isolated --mcp-test instance).

vibera_editor move floats an editor (or the Browser, editor 4) at the given workspace bounds, the
same as dragging its header; layout sets the splitter sizes with the splitters' own limits
(out-of-range values clamp like dragging). The Browser accepts only placement actions, and a
detached window must be docked before it can be moved.

usage: python scripts/verify-workspace-layout-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/workspace-layout"
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
    raw = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args).get("structuredContent", {})
    editor = lambda **args: raw("vibera_editor", args)
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})

    state = call("vibera_state")
    check(len(state.get("editorBounds", [])) == 5 and "rackWidth" in state.get("workspaceLayout", {}),
          "state reads back window bounds and splitter sizes")
    check(not editor(editor=1, action="move", bounds=[120, 90, 640, 360]).get("isError"), "move floats the Piano roll")
    check(call("vibera_state")["editorBounds"][1][2:] == [640, 360], "The Piano roll has the requested size")
    check(editor(editor=1, action="move", bounds=[0, 0, 20, 20]).get("isError"), "A tiny window is refused")
    check(not editor(editor=4, action="move", bounds=[40, 40, 260, 400]).get("isError"), "move floats the Browser")
    check(editor(editor=4, action="maximize").get("isError"), "The Browser refuses editor-only actions")

    editor(editor=0, action="layout", rackWidth=500, browserWidth=300, mixerHeight=999)
    layout = call("vibera_state")["workspaceLayout"]
    check(layout["rackWidth"] == 500 and layout["browserWidth"] == 300, "layout sets splitter sizes")
    check(layout["mixerHeight"] == 360, "Out-of-range sizes clamp like the splitter")
    check(editor(editor=0, action="layout", rackWidth="wide").get("isError"), "A non-number is refused")

    editor(editor=1, action="detach")
    check(editor(editor=1, action="move", bounds=[100, 100, 500, 300]).get("isError"), "A detached window cannot move")
    editor(editor=1, action="dock")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
