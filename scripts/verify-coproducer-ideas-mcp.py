"""CoProducer MIDI ideas through MCP (isolated --mcp-test instance, local Magenta provider).

vibera_ai idea* actions drive the docked panel's own controls and handlers: ideaConfigure sets the
model, destination and region (refusing unknown values), ideaGenerate generates locally without
changing the project, ideaAccept places the chosen candidate as one Undo step through
coproducer.accept, and Undo removes it. Buttons that are disabled in the panel are refused
(ideaAccept before any candidate, ideaGenerate while busy).

usage: python scripts/verify-coproducer-ideas-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/coproducer-ideas"
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
    idea = lambda action, **args: raw("vibera_ai", dict(action=action, **args))

    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    call("vibera_transport", {"action": "pattern"})
    channels = call("vibera_inspect", {"entity": "channels"})["items"]
    bass = next(c["id"] for c in channels if c.get("source") == "builtin:bass")

    def wait_idle(seconds=150):
        deadline = time.monotonic() + seconds
        state = idea("ideaState")["structuredContent"]
        while state["busy"] and time.monotonic() < deadline:
            time.sleep(.25)
            state = idea("ideaState")["structuredContent"]
        return state

    state = wait_idle()
    check(bass in state["destinations"], "ideaState lists the bass Channel as a destination")
    check(idea("ideaAccept").get("isError"), "Accept with no candidates is refused")
    check(idea("ideaConfigure", destination=999999).get("isError"), "An unknown destination is refused")
    check(idea("ideaConfigure", model="multitrack", family="Banjo").get("isError"), "A family the model lacks is refused")
    state = idea("ideaConfigure", model="multitrack", family="Bass", variation="New idea")["structuredContent"]
    check(state["model"] == "multitrack" and state["family"] == "Bass" and "Idea variation" in state["variations"],
          "MusicVAE with the Bass family reads back")
    state = idea("ideaConfigure", model="melody", destination=bass, start=0, bars=1, seed=7, context=[bass])["structuredContent"]
    check(state["model"] == "melody" and state["destination"] == bass and state["bars"] == 1 and state["seed"] == 7,
          "MelodyRNN, destination, region and seed read back")
    check(state["contextMode"] == "custom" and state["context"] == [bass], "Custom context reads back")

    before = call("vibera_state")
    revision = before.get("revision")
    result = idea("ideaGenerate")
    check(not result.get("isError"), "ideaGenerate starts: " + json.dumps(result)[:200])
    state = wait_idle()
    (output / "generation.json").write_text(json.dumps(state, indent=2), encoding="utf-8")
    check(state["candidates"] >= 1, "Local generation produced candidates: " + state["summary"][:200])
    check(call("vibera_state").get("revision") == revision, "Generation leaves the project unchanged")
    notes_before = len(call("vibera_inspect", {"entity": "notes", "limit": 512}).get("items", []))
    accepted = idea("ideaAccept", candidate=0)
    check(not accepted.get("isError"), "ideaAccept places the candidate: " + json.dumps(accepted)[:200])
    check(call("vibera_state").get("revision") != revision, "Accept changes the project")
    call("vibera_command", {"command": "edit.undo"})
    check(call("vibera_state").get("revision") is not None, "Undo runs")
    after_undo = len(call("vibera_inspect", {"entity": "notes", "limit": 512}).get("items", []))
    check(after_undo == notes_before, "Undo removes the accepted part")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
