"""Keyboard shortcuts, controller mapping removal and Browser/CoProducer toggles through MCP
(isolated --mcp-test instance; owned test profiles, never the user's).

vibera_macros bindShortcut/unbindShortcut/resetShortcuts edit the shortcut profile with the same
Customization calls as Settings > Shortcuts, guarded by the configuration generation; a conflicting
key is refused. vibera_controller removeBinding deletes a mapping row through the same guarded
service as Settings > Controllers Remove. view.browser / view.coProducer toggle their panels.

usage: python scripts/verify-shortcuts-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/shortcuts"
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
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})

    # Shortcuts.
    state = call("vibera_macros", {"action": "state", "includeShortcuts": True})
    shortcuts = state.get("shortcuts")
    check(isinstance(shortcuts, list) and shortcuts, "state lists the shortcuts")
    k = ord("K")
    free = not any(s["key"] == k and s["ctrl"] and s["alt"] and s["scope"] == 0 for s in shortcuts)
    check(free, "Ctrl+Alt+K is free in the global scope")
    bound = call("vibera_macros", {"action": "bindShortcut", "generation": state["generation"],
                                   "shortcut": {"command": "view.mixer", "scope": 0, "key": k, "ctrl": True, "alt": True}})
    check(bound.get("configurationCommitted") is True, "bindShortcut commits: " + json.dumps(bound)[:200])
    rows = [s for s in bound["shortcuts"] if s["command"] == "view.mixer" and s["scope"] == 0]
    check(any(s["key"] == k and s["ctrl"] and s["alt"] and not s["shift"] for s in rows), "The new key reads back")
    check(len(rows) == 1, "Without alternate the command keeps only the new key")
    stale = raw("vibera_macros", {"action": "bindShortcut", "generation": state["generation"],
                                  "shortcut": {"command": "view.rack", "scope": 0, "key": k, "ctrl": True, "alt": True}})
    check(stale.get("isError"), "A stale generation is refused")
    conflict = raw("vibera_macros", {"action": "bindShortcut", "generation": bound["generation"],
                                     "shortcut": {"command": "view.rack", "scope": 0, "key": k, "ctrl": True, "alt": True}})
    check(conflict.get("isError") and "conflict" in json.dumps(conflict), "A key used by another command is refused")
    alt = call("vibera_macros", {"action": "bindShortcut", "generation": bound["generation"],
                                 "shortcut": {"command": "view.mixer", "scope": 0, "key": ord("J"), "ctrl": True,
                                              "alt": True, "alternate": True}})
    rows = [s for s in alt["shortcuts"] if s["command"] == "view.mixer" and s["scope"] == 0]
    check(len(rows) == 2, "alternate keeps both keys")
    removed = call("vibera_macros", {"action": "unbindShortcut", "generation": alt["generation"],
                                     "shortcut": {"command": "view.mixer", "scope": 0}})
    check(not [s for s in removed["shortcuts"] if s["command"] == "view.mixer" and s["scope"] == 0],
          "unbind removes the command's shortcuts in that scope")
    reset = call("vibera_macros", {"action": "resetShortcuts", "generation": removed["generation"]})
    check(sorted(map(json.dumps, reset["shortcuts"])) == sorted(map(json.dumps, shortcuts)),
          "reset restores the original shortcuts")
    check(raw("vibera_macros", {"action": "bindShortcut", "generation": reset["generation"],
                                "shortcut": {"command": "no.such.command", "scope": 0, "key": k}}).get("isError"),
          "An unknown command is refused")

    # Profile export / import.
    profile = output / "profile.json"
    exported = call("vibera_macros", {"action": "exportProfile", "path": str(profile), "generation": reset["generation"]})
    check(profile.is_file() and exported.get("configurationCommitted") is False, "exportProfile writes the file")
    check(raw("vibera_macros", {"action": "exportProfile", "path": str(profile), "generation": exported["generation"]}).get("isError"),
          "exportProfile refuses to overwrite without overwrite:true")
    changed = call("vibera_macros", {"action": "bindShortcut", "generation": exported["generation"],
                                     "shortcut": {"command": "view.mixer", "scope": 0, "key": k, "ctrl": True, "alt": True}})
    check(any(s["key"] == k and s["command"] == "view.mixer" for s in changed["shortcuts"]), "A shortcut changed after export")
    check(raw("vibera_macros", {"action": "importProfile", "path": str(profile),
                                "generation": exported["generation"]}).get("isError"), "importProfile needs the current generation")
    imported = call("vibera_macros", {"action": "importProfile", "path": str(profile), "generation": changed["generation"]})
    check(imported.get("configurationCommitted") is True and
          sorted(map(json.dumps, imported["shortcuts"])) == sorted(map(json.dumps, shortcuts)),
          "importProfile restores the exported shortcuts")
    check(raw("vibera_macros", {"action": "importProfile", "path": str(output / "missing.json"),
                                "generation": imported["generation"]}).get("isError"), "A missing file is refused")

    # Controller mapping removal.
    ctl = call("vibera_controller", {"action": "state"})
    count = len(ctl["controllerProfile"]["bindings"])
    binding = {"action": "transport.play", "label": "Removal test", "port": 0, "channel": 0, "kind": 0, "number": 20,
               "layer": 0, "mode": 0, "mixer": -1, "minimum": 0, "maximum": 1, "pickup": False, "banked": False,
               "enabled": False, "nativeArea": 0}
    added = call("vibera_controller", {"action": "setBinding", "index": count, "binding": binding,
                                       "expectedProfile": ctl["controllerProfileIdentity"]})
    check(len(added["controllerProfile"]["bindings"]) == count + 1, "A mapping is appended")
    check(raw("vibera_controller", {"action": "removeBinding", "index": count,
                                    "expectedProfile": ctl["controllerProfileIdentity"]}).get("isError"),
          "removeBinding with a stale profile is refused")
    check(raw("vibera_controller", {"action": "removeBinding", "index": count + 1,
                                    "expectedProfile": added["controllerProfileIdentity"]}).get("isError"),
          "removeBinding past the last row is refused")
    after = call("vibera_controller", {"action": "removeBinding", "index": count,
                                       "expectedProfile": added["controllerProfileIdentity"]})
    rows = after["controllerProfile"]["bindings"]
    check(len(rows) == count and not any(r.get("label") == "Removal test" for r in rows), "removeBinding deletes the row")

    # Panels.
    before = call("vibera_state")
    for action, field in (("view.browser", "browserShown"), ("view.coProducer", "coProducerShown")):
        call("vibera_command", {"command": action})
        check(call("vibera_state").get(field) == (not before[field]), action + " toggles " + field)
        call("vibera_command", {"command": action})
        check(call("vibera_state").get(field) == before[field], action + " toggles back")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
