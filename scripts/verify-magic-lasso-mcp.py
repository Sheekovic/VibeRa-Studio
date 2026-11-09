"""Magic lasso note selection through MCP (isolated --mcp-test instance).

vibera_selection action lasso closes [beat, pitch] points into a path and selects the selected
Channel's notes whose start lies inside, using the same function as the Piano roll lasso: a box
around two notes picks exactly those, a triangle picks only the note inside its slanted edge, and
fewer than three points is refused.

usage: python scripts/verify-magic-lasso-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/magic-lasso"
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
    call = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True)
    data = lambda tool, args=None: call(tool, args).get("structuredContent", {})
    data("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    state = data("vibera_state")
    channel, pattern = state["rack"]["rows"][0]["id"], state["selectedPattern"]
    for beat, pitch in ((0, 60), (2, 64), (4, 67), (6, 72)):
        data("vibera_command", {"command": "notes.add", "args": {"channel": channel, "pattern": pattern, "pitch": pitch,
                                                                  "beat": beat, "length": 1, "value": .8}})
    notes = {(n["beat"], n["pitch"]): n["id"] for n in data("vibera_inspect", {"entity": "notes"})["items"]}
    selected = lambda: sorted(data("vibera_selection", {"action": "state", "target": "notes"})["ids"])

    data("vibera_selection", {"action": "lasso", "target": "notes", "points": [[-.5, 58], [3, 58], [3, 66], [-.5, 66]]})
    check(selected() == sorted([notes[(0, 60)], notes[(2, 64)]]), "A box around two notes selects exactly them")
    # Triangle (beat, pitch) (3, 62) (3, 74) (8, 62): (4, 67.5) is under its slanted edge, (6, 72.5) above it.
    data("vibera_selection", {"action": "lasso", "target": "notes", "points": [[3, 62], [3, 74], [8, 62]]})
    check(selected() == [notes[(4, 67)]], "A triangle selects only the note inside its slanted edge")
    refused = call("vibera_selection", {"action": "lasso", "target": "notes", "points": [[0, 0], [1, 1]]})
    check(refused.get("isError"), "Fewer than three points is refused")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
