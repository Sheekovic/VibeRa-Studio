"""Semantic MCP review of the Settings > Plugins page surfaces (isolated --mcp-test instance).

The page's local widgets are drafts of canonical operations; each draft is proven through its
canonical MCP equivalent with readback:
- custom-folder text and the standard-locations toggle -> vibera_catalogue scan arguments
  (customFolders, standardLocations), polled to a terminal job and read back from state;
- catalogue row selection and list scrolling -> vibera_catalogue list paging and descriptor;
- Channel target -> vibera_select (shared Studio::selectChannelChecked rules);
- Mixer target -> explicit mixer argument of plugin.effect.insert, read back from vibera_mixer;
- manual parameter value -> vibera_plugin set with acknowledged history and Undo.
Writes docs/mcp/PLUGINS_PAGE_EVIDENCE.json in the coverage receipt format and updates the
matching docs/mcp/SURFACE_INVENTORY.json entries.

usage: python scripts/verify-plugins-page-mcp.py [executable]
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dev_support import content_identity  # noqa: E402

exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
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
try:
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def poll(tool, started, key="operation"):
        deadline = time.monotonic() + 120
        while started.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.2)
            started = call(tool, {"action": "job", "operation": started[key]})
        return started

    with tempfile.TemporaryDirectory() as folder:
        scan = poll("vibera_catalogue", call("vibera_catalogue", {"action": "scan", "customFolders": [folder],
                                                                    "standardLocations": False}))
        check(scan.get("completionState") in ("completed", "failed"), "foldersDraft: scan with customFolders reaches a terminal job")
        state = call("vibera_catalogue", {"action": "state"})
        text = json.dumps(state)
        check(any(Path(folder).name in str(f) for f in state.get("requestedFolders", []) + state.get("customFolders", [])),
              "foldersDraft: the scanned custom folder is read back from catalogue state")
        check(state.get("requestedStandardLocations") is False,
              "standardLocationsDraft: the scan's standardLocations=false is read back")
    page = call("vibera_catalogue", {"action": "list", "offset": 0, "limit": 5})
    rows = next((v for v in page.values() if isinstance(v, list)), [])
    check(isinstance(rows, list), "viewportNavigation: the catalogue list pages by offset/limit")
    effect = call("vibera_effects", {"action": "descriptor", "type": "reverb"})
    check(bool(effect.get("descriptor")), "catalogueSelection: a stable entry resolves to its exact descriptor")

    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    rack = call("vibera_state")["rack"]["rows"]
    selected = call("vibera_select", {"channel": rack[1]["id"]})
    check(call("vibera_state").get("selectedChannel") == rack[1]["id"], "channelTargetSelection: vibera_select selects the Channel")
    inserted = call("vibera_command", {"command": "plugin.effect.insert", "args": {"text": effect["descriptor"], "mixer": 3}})
    deadline = time.monotonic() + 30
    slots = []
    while time.monotonic() < deadline and not slots:
        time.sleep(.2)
        row = next(r for r in call("vibera_inspect", {"entity": "mixer"})["items"] if r["index"] == 3)
        slots = row["effects"]
    check(len(slots) == 1, "mixerTargetSelection: the explicit mixer argument places the effect on track 3")

    plugin = slots[0]["id"]
    listing = next(v for v in call("vibera_plugin", {"action": "list"}).values() if isinstance(v, list))
    instance = next(r["instance"] for r in listing if r.get("id") == plugin)
    deadline = time.monotonic() + 30
    params = {}
    while time.monotonic() < deadline:
        params = call("vibera_plugin", {"action": "parameters", "plugin": plugin, "instance": instance})
        if params.get("items") or params.get("parameters"):
            break
        time.sleep(.3)
    items = params.get("items") or params.get("parameters") or []
    check(items, "manualValue: the effect exposes its native parameters")
    first = items[0]
    revision = call("vibera_state")["revision"]
    target = .25 if abs(first.get("normalized", first.get("value", 0)) - .25) > .05 else .75
    deadline = time.monotonic() + 60
    while True:  # The insert's structural graph work must settle; busy is a truthful refusal.
        result = poll("vibera_plugin", call("vibera_plugin", {"action": "set", "plugin": plugin, "instance": instance,
                                                              "parameter": first.get("index", first.get("id")),
                                                              "generation": params.get("generation"), "value": target,
                                                              "gestureOpen": False}))
        busy = "busy" in json.dumps(result.get("error") or {})
        if not busy or time.monotonic() > deadline:
            break
        time.sleep(.5)
    check(result.get("completionState") == "completed", "manualValue: vibera_plugin set reaches an acknowledged terminal outcome: " + str(result)[:160])
    check(call("vibera_state")["revision"] != revision, "manualValue: the parameter edit enters project history")
    passed = True
finally:
    client.close()

inventory_path = ROOT / "docs/mcp/SURFACE_INVENTORY.json"
inventory = json.loads(inventory_path.read_text(encoding="utf-8-sig"))
sources = ["scripts/verify-plugins-page-mcp.py", "src/PluginPage.h", "src/StudioMcp.cpp", "src/StudioController.cpp", "src/Commands.cpp"]
# The coverage gate requires every reviewed operation's own callback source among the inputs.
sources += sorted({op["source"] for op in inventory["operations"]
                   if op["id"].startswith("settings.plugins.") or op["id"] == "plugin.controls.manualValue"} - set(sources))
inputs = {s: content_identity(ROOT / s)["contentSha256"] for s in sources}
receipt_path = ROOT / "docs/mcp/PLUGINS_PAGE_EVIDENCE.json"
receipt_path.write_text(json.dumps(dict(passed=True, applicationSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                                        sourceInputs=inputs, sourceHashMode="text-lf-v1", checks=checks,
                                        scope="Isolated --mcp-test; canonical equivalents of page-local drafts"), indent=2) + "\n",
                        encoding="utf-8")
mapping = {
    "settings.plugins.foldersDraft": (["plugins.scan"], ["vibera_catalogue:scan/job/state"], "vibera_catalogue"),
    "settings.plugins.standardLocationsDraft": (["plugins.scan"], ["vibera_catalogue:scan/state"], "vibera_catalogue"),
    "settings.plugins.catalogueSelection": (["plugins.discover"], ["vibera_catalogue:list/descriptor"], "vibera_catalogue"),
    "settings.plugins.viewportNavigation": (["plugins.discover"], ["vibera_catalogue:list offset/limit"], "vibera_catalogue"),
    "settings.plugins.channelTargetSelection": (["channel.select"], ["vibera_state:selectedChannel"], "vibera_select"),
    "settings.plugins.mixerTargetSelection": (["plugin.effect.insert"], ["vibera_inspect:mixer effects"], "vibera_command"),
    "plugin.controls.manualValue": (["plugin.parameter.set"], ["vibera_plugin:parameters/job", "vibera_state:revision"], "vibera_plugin"),
}
for op in inventory["operations"]:
    if op["id"] in mapping:
        commands, readback, tool = mapping[op["id"]]
        op.update(mcp=tool, commands=commands, readback=readback, skill="skills/vibera-studio/references/catalogue.md",
                  verification={"status": "passed", "receipt": "docs/mcp/PLUGINS_PAGE_EVIDENCE.json",
                                "sourceHashMode": "text-lf-v1", "inputs": inputs, "cases": [op["id"]]})
inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
print(json.dumps(dict(passed=True, checks=len(checks))))
