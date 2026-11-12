"""Mixer organisation menu items without their own test, through MCP (isolated --mcp-test instance).

The Mixer's selection / group menu runs registered operations. Creating, renaming, colouring, muting and
soloing groups are checked by mixer_organisation_mcp; this drives the rest and reads the Mixer back:
Select all inserts, Rename selected, colour selected / Follow theme, Ungroup selected, Solo group with
connected inputs, Restore group solo and Dissolve display group. Every edit is one Undo step.

usage: python scripts/verify-mixer-organisation-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/mixer-organisation-menu"
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
    organisation = lambda: call("vibera_mixer", {"action": "state"})["organisation"]
    inserts = lambda: {m["index"]: m for m in call("vibera_inspect", {"entity": "mixer", "limit": 200})["items"]}
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})

    everything = raw("vibera_mixer", {"action": "selectAll"})
    selected = organisation()["selected"]
    check(not everything.get("isError") and len(selected) == len(inserts()), f"Select all inserts selects every insert ({len(selected)})")

    call("vibera_mixer", {"action": "selectMany", "ids": [1, 2], "active": 1})
    names = [inserts()[1]["name"], inserts()[2]["name"]]
    renamed = result("mixer.selection.rename", ids=[1, 2], text="Bus")
    after = [inserts()[1]["name"], inserts()[2]["name"]]
    check(renamed.get("completionState") == "completed" and after != names and all(n.startswith("Bus") for n in after),
          f"Rename selected names both inserts (mixer.selection.rename) {after} {json.dumps(renamed.get('error'))[:100]}")
    done("edit.undo")
    check([inserts()[1]["name"], inserts()[2]["name"]] == names, "Rename selected is one Undo step")
    colours = lambda: [inserts()[1].get("colour"), inserts()[2].get("colour")]
    plain = colours()
    check(done("mixer.selection.colour", ids=[1, 2], value=0xFF2E8BC0) == "completed" and colours() == [0xFF2E8BC0, 0xFF2E8BC0],
          f"Colour selected sets both colours (mixer.selection.colour) {colours()}")
    check(done("mixer.selection.colour", ids=[1, 2], value=0) == "completed" and colours() == [0, 0], "Follow theme clears them")
    check(done("mixer.selection.colour", ids=[1, 2], value=0x002E8BC0) == "failed", "A transparent colour is refused")

    groups = lambda: organisation()["groups"]
    check(done("mixer.group.create", ids=[1, 2, 3], text="Drum bus") == "completed" and len(groups()) == 1, "A group of three inserts")
    group = groups()[0]["id"]
    members = lambda: sorted(groups()[0].get("members", [])) if groups() else []
    check(members() == [1, 2, 3], f"Its members read back ({members()})")
    check(done("mixer.group.assign", ids=[3], mixerGroup="") == "completed" and members() == [1, 2],
          f"Ungroup selected removes insert 3 from the group (mixer.group.assign) {members()}")
    check(done("mixer.group.soloConnected", mixerGroup=group, enabled=True) == "completed", "Solo group with connected inputs (mixer.group.soloConnected)")
    check(done("mixer.group.delete", mixerGroup=group) == "failed", "A group that owns the solo cannot be dissolved")
    check(done("mixer.group.unsolo", mixerGroup=group) == "completed", "Restore group solo")
    check(done("mixer.group.delete", mixerGroup=group) == "completed" and groups() == [] and len(inserts()) == len(selected),
          "Dissolve display group removes the group and keeps every insert (mixer.group.delete)")
    done("edit.undo")
    check(len(groups()) == 1, "Dissolve is one Undo step")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
