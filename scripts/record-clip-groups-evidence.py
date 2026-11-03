"""Bind the Playlist grouping increment to exact passing inputs; legacy gaps stay deferred."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-log", required=True)
parser.add_argument("--native-output", required=True)
args = parser.parse_args()
read = lambda p: json.loads((root/p).read_text(encoding="utf-8-sig"))
sha = lambda p: hashlib.sha256((root/p).read_bytes()).hexdigest()


def write(path, value):
    (root/path).write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")


app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/clip_groups_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 38 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
assert (root/"build/windows/clip_groups_gui/clip-groups-gui.txt").read_text().startswith("PASS")
assert (root/"build/windows/acceptance/clip-groups-core.txt").read_text().startswith("PASS")
live = (root/"build/windows/acceptance/clip-groups-live.txt").read_text()
assert len(live.splitlines()) == 2
for row in live.splitlines():
    assert all(token in row for token in ["overruns=0", "silentLeft=0", "silentRight=0", "allocations=0",
                                         "destructions=0", "graphReplacements=0", "deviceStarts=0", "deviceStops=0"])
    assert int(re.search(r"loops=(\d+)", row)[1]) >= 2
native = read(str(Path(args.native_output)/"process.json"))
assert native["passed"] and native["exitCode"] == 0 and not native["forcedClose"] and native["applicationSha256"] == sha(app)
assert "PASS owned OS Shift+G/Alt+G" in (root/args.native_output/"verification.txt").read_text()
log = root/args.source_log
raw = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root/"CMakeLists.txt").read_text(), re.M))
assert log.stat().st_mtime >= (root/app).stat().st_mtime
inputs = {i["path"]: i["sha256"] for i in read("build/build-inputs.json")}
for path in ["scripts/verify-clip-groups-mcp.py", "scripts/record-clip-groups-evidence.py", "scripts/verify-grouping-desktop.py",
             "skills/vibera-studio/SKILL.md", "skills/vibera-studio/references/playlist-groups.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[path] = sha(path)
assert all(sha(path) == digest for path, digest in inputs.items()), "Acceptance inputs changed"
commands = ["clip.group", "clip.ungroup", "playlist.grouping", "clip.move", "clip.duplicate", "clip.delete", "clip.mute", "clip.slice", "clip.slip"]
cases = ["Actual JUCE contextual Shift+G/Alt+G, grouping toolbar, member selection/drag and exact Undo/Redo behavior",
         "Owned native OS shortcuts, member drag, bypass/individual drag, Ctrl+S/Z/Y and actual saved group; app-only capture",
         "Heterogeneous Pattern/Audio/Automation identity, rigid boundary clamp, mixed-unit common resize/mute/slice and stable linked-source copies",
         "Independent duplicate groups, union/bypassed regroup, deletion pruning and exact macro order/context/one Undo/typed override persistence",
         "Strict schema16 with genuine old-file migration and atomic malformed/missing/masquerading membership refusal",
         "Real Pattern/Song repeated move/duplicate/delete/ungroup/history and natural loops; explicit out-of-range shrink wraps counted separately",
         "Held sample/MIDI PCM, callback deadline/heap/lifetime, retained Edit/context/device/cache; streamed wave graph refreshes explicitly reported",
         "Real semantic typed discovery, target/readback pagination, leased gesture cancel, invalid input, save/reopen, exact WAV and actual async file failure"]
receipt = "docs/mcp/CLIP_GROUPS_EVIDENCE.json"
write(receipt, dict(passed=True, applicationSha256=sha(app), sourceInputs=inputs, checks=cases,
                    sourceSuites=f"{summary[1]}/{summary[1]}", mcpChecks=mcp["checks"], nativeInput=args.native_output,
                    listening=False, physicalMidi=False,
                    scope="Heterogeneous Playlist placement grouping; exact FL regroup/duplicate/resize policy and complete PL-01 remain unqualified"))
verification = dict(status="passed", receipt=receipt, inputs=inputs, cases=cases)
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [o for o in inventory["operations"] if o["id"] != "playlist.clipGroups"]
inventory["operations"].append(dict(id="playlist.clipGroups", name="Playlist mixed placement grouping",
    source="src/Editors.h", canonical="Controller Clip commands and shared Studio effective selection/grouping behavior/history",
    mcp="vibera_command", additionalInterfaces=["vibera_editor", "vibera_selection", "vibera_gesture", "vibera_inspect"], commands=commands,
    readback=["vibera_state:clipGroups/clipGroupingEnabled/effectiveSelectedClips", "vibera_inspect:clipGroups/clipGroupMembers/clips", "vibera_state:pendingAudioUpdate"],
    skill="skills/vibera-studio/references/playlist-groups.md", verification=verification))
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
spec = importlib.util.spec_from_file_location("coverage_gate", root/"scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if source == "src/Editors.h" and "playlistGrouping.onClick" in text:
        reviewed[entry["surface"]] = dict(sourceSha256=entry["sourceSha256"], operations=commands, mcp="vibera_command",
            additionalInterfaces=["vibera_editor", "vibera_selection", "vibera_gesture"], readback=["vibera_state:clipGroups/effectiveSelectedClips"],
            skill="skills/vibera-studio/references/playlist-groups.md", verification=verification)
write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded scoped Playlist grouping evidence; full inherited MCP gate remains incomplete")
