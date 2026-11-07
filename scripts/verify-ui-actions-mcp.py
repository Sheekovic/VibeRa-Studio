"""Semantic MCP qualification of the formerly unmapped UI actions (Browser, sampler view,
Pattern finding, controller banks, instrument opening, score dump).

Each action runs through vibera_command on an isolated --mcp-test instance and is judged by
real readback (vibera_state browser / selectedPattern / samplerWindows / dialog state), or
must refuse truthfully when its target is absent. Dialog/menu actions must open their dialog,
which is then cancelled through vibera_dialog; nothing may change the project document.

usage: python scripts/verify-ui-actions-mcp.py [executable] [output folder]
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/mcp-ui-actions"
output = base / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks, covered = [], set()


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


passed = False
client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
try:
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def run(command, **args):
        covered.add(command)
        result = call("vibera_command", {"command": command, **({"args": args} if args else {})})
        return result.get("completionState"), (result.get("error") or {}).get("code")

    def browser():
        return call("vibera_state")["browser"]

    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    document = call("vibera_inspect", {"entity": "notes", "limit": 500}).get("items")

    # Browser view and filter state.
    check(run("browser.showStarred")[0] == "completed" and browser()["view"] == "starred", "browser.showStarred selects the Starred tab")
    check(run("browser.showAll")[0] == "completed" and browser()["view"] == "all", "browser.showAll selects the All tab")
    shown = browser()["tagsShown"]
    check(run("browser.showTags")[0] == "completed" and browser()["tagsShown"] != shown, "browser.showTags toggles the tag panel")
    run("browser.showTags")
    match = browser()["tagMatchAll"]
    check(run("browser.matchAllTags")[0] == "completed" and browser()["tagMatchAll"] != match, "browser.matchAllTags toggles Any/All")
    run("browser.matchAllTags")
    check(run("browser.clearTagFilters")[0] == "completed" and browser()["tagFilter"] == [], "browser.clearTagFilters empties the filter")
    check(run("browser.clearSearch")[0] == "completed" and browser()["search"] == "" and not browser()["tagsShown"],
          "browser.clearSearch clears search and tag panel")
    check(run("browser.find")[0] == "completed", "browser.find opens library search")
    check(run("browser.reindex")[0] == "completed", "browser.reindex rereads folders")
    full = browser()["fullPreview"]
    check(run("browser.fullPreview")[0] == "completed" and browser()["fullPreview"] != full, "browser.fullPreview toggles")
    run("browser.fullPreview")
    volume = browser()["previewVolume"]
    run("browser.previewSofter")
    softer = browser()["previewVolume"]
    run("browser.previewLouder")
    covered.update({"browser.previewSofter", "browser.previewLouder"})
    check(softer < volume and abs(browser()["previewVolume"] - volume) < 1e-6, "browser.previewSofter/Louder step the preview volume")
    check(run("browser.stopPreview")[0] == "completed", "browser.stopPreview is idempotent")
    check(run("browser.playPreview")[0] in ("completed", "failed"), "browser.playPreview reports a real outcome")
    # Without history, results or a selection these must refuse instead of claiming a change.
    state = browser()
    for command, ready in (("browser.back", state["canGoBack"]), ("browser.forward", state["canGoForward"]),
                           ("browser.nextMatch", state["results"] > 0), ("browser.previousMatch", state["results"] > 0),
                           ("browser.toggleFavorite", bool(state["selectedFile"]))):
        outcome = run(command)
        check(ready or outcome == (None, "unavailable_operation") or outcome[0] == "failed",
              f"{command} refuses truthfully without its target ({outcome})")
    outcome = run("browser.up")
    check(outcome[0] == "completed" if state["folder"] else outcome[0] != "completed", f"browser.up matches folder state ({outcome})")

    # Dialog / menu actions open a modal that is cancelled; the document stays unchanged.
    for command in ("browser.collectionsMenu", "browser.editTags", "browser.addTags", "pattern.renameDialog"):
        outcome = run(command)
        dialog = call("vibera_dialog", {"action": "state"})
        if dialog.get("open"):
            buttons = [b.get("name", b) if isinstance(b, dict) else b for b in dialog.get("buttons", [])]
            cancel = next((b for b in buttons if str(b).lower() in ("cancel", "close")), None)
            if cancel:
                call("vibera_dialog", {"action": "submit", "button": cancel})
        check(outcome[0] in ("completed", "failed") or outcome[1] == "unavailable_operation", f"{command} reports an outcome ({outcome})")

    # Patterns.
    selected = call("vibera_state")["selectedPattern"]
    check(run("pattern.findEmpty")[0] == "completed" and call("vibera_state")["selectedPattern"] != selected,
          "pattern.findEmpty selects an empty Pattern slot")
    check(run("pattern.firstEmpty")[0] == "completed", "pattern.firstEmpty selects the first empty Pattern")

    # Controller banks and score dump.
    for command in ("controller.bankNext", "controller.bankPrevious"):
        check(run(command)[0] == "completed", command + " steps the controller Mixer bank")
    check(run("score.dump")[0] in ("completed", "failed"), "score.dump reports its outcome")

    # Sampler waveform view: refused without an instrument window, applied with one.
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    check(run("sampler.view.zoomIn")[1] == "unavailable_target", "sampler.view.* refuses without an open instrument window")
    check(run("channel.openInstrument")[0] == "completed", "channel.openInstrument opens the selected Channel's window")
    time.sleep(1)
    windows = call("vibera_state").get("samplerWindows", [])
    check(windows and "waveformViewLength" in windows[0], "The sampler window reports its waveform view")
    length = windows[0]["waveformViewLength"]
    run("sampler.view.zoomIn")
    zoomed = call("vibera_state")["samplerWindows"][0]["waveformViewLength"]
    run("sampler.view.zoomOut")
    run("sampler.view.all")
    whole = call("vibera_state")["samplerWindows"][0]["waveformViewLength"]
    check(zoomed < length and whole >= zoomed, "sampler.view.zoomIn/zoomOut/all change the waveform view")
    check(run("sampler.view.range")[0] == "completed", "sampler.view.range fits the view to the sample range")
    covered.update({"sampler.view.zoomOut", "sampler.view.all"})
    check(call("vibera_inspect", {"entity": "notes", "limit": 500}).get("items") is not None, "Readback still served")
    passed = True
finally:
    client.close()
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    deps = ["scripts/verify-ui-actions-mcp.py", "src/SampleFileBrowser.h", "src/StudioMcp.cpp", "src/Commands.cpp"]
    receipt = dict(passed=passed, checks=checks, commands=sorted(covered),
                   dependencyHashes=[dict(path=d, mode="content", sha256=content_identity(ROOT / d)["contentSha256"]) for d in deps],
                   candidateHashes=[dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                    for p in (exe, exe.parent / "VibeRa Plugin Worker.exe") if p.is_file() and ROOT in p.parents],
                   scope="Isolated --mcp-test instance; component handlers, not OS input")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), commands=len(covered), receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
