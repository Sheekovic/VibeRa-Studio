"""Interface appearance through MCP (isolated --mcp-test instance; owned appearance file).

vibera_appearance set applies a preset, a font and single colours with the same theme calls as
Settings > Appearance, reads back live, and save:true writes the isolated appearance profile.
Preset default keeps the font; reset restores palette and font. Unknown presets, fonts, roles and
malformed colours are refused without changing anything. vibera_ai configure sets the ACE-Step
models (aceDit/aceLm) and refuses unknown ones.

usage: python scripts/verify-appearance-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/appearance"
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

    start = call("vibera_appearance", {"action": "state"})
    check(len(start.get("colours", {})) == 24 and "default" in start.get("presets", []), "state lists 24 roles and presets")
    check(start.get("saved") is False, "A fresh test instance has no saved appearance")
    defaults = dict(start["colours"])
    role = "Workspace background"
    check(role in defaults, "The workspace role is listed")
    contrast = [p for p in start["presets"] if p != "default"][0]
    after = call("vibera_appearance", {"action": "set", "preset": contrast})
    check(after["colours"] != defaults, "A preset replaces the palette")
    after = call("vibera_appearance", {"action": "set", "colours": {role: "ff123456"}, "save": True})
    check(after["colours"][role].lower() == "ff123456", "One colour reads back")
    check(after.get("saved") is True, "save:true writes the appearance profile")
    for bad in ({"preset": "Nope"}, {"font": "No Such Font 123"}, {"colours": {"Nope": "ff000000"}},
                {"colours": {role: "12345"}}):
        result = raw("vibera_appearance", dict(action="set", **bad))
        check(result.get("isError"), "Refused: " + json.dumps(bad))
    check(call("vibera_appearance", {"action": "state"})["colours"][role].lower() == "ff123456",
          "Refusals leave the palette")
    back = call("vibera_appearance", {"action": "set", "preset": "default"})
    check(back["colours"] == defaults and back["font"] == start["font"], "preset default restores the palette")
    reset = call("vibera_appearance", {"action": "reset"})
    check(reset["colours"] == defaults and reset["font"] == start["font"], "reset restores palette and font")

    ai = call("vibera_ai", {"action": "state"})
    dit = ai.get("music", {}).get("aceRecommended", {}).get("dit") or "acestep-v15-turbo"
    configured = call("vibera_ai", {"action": "configure", "music": {"aceDit": dit, "aceLm": "none"}})
    check(configured.get("music", {}).get("aceDit") == dit and configured["music"].get("aceLm") == "none",
          "configure sets the ACE-Step models")
    check(raw("vibera_ai", {"action": "configure", "music": {"aceDit": "not-a-model"}}).get("isError"),
          "An unknown ACE-Step model is refused")
    call("vibera_ai", {"action": "configure", "music": {"aceDit": "", "aceLm": ""}})
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
