"""Qualify only current Loop/live-extent behavior; preserve inherited MCP gaps."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-log",required=True)
parser.add_argument("--native-output",required=True)
args=parser.parse_args()
read=lambda p:json.loads((root/p).read_text(encoding="utf-8-sig"))
sha=lambda p:hashlib.sha256((root/p).read_bytes()).hexdigest()
def write(p,data):(root/p).write_text(json.dumps(data,indent=2)+"\n",encoding="utf-8")
app="build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp=read("build/windows/loop_bounds_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"]>=41 and mcp["exitCode"]==0 and mcp["applicationSha256"]==sha(app)
assert (root/"build/windows/sampler_gestures_gui/loop-bounds-mcp-gui.txt").read_text(encoding="utf-8").startswith("PASS")
concurrent=(root/"build/windows/acceptance/loop-bounds-concurrent.txt").read_text(encoding="utf-8")
assert all(token in concurrent for token in ["overruns=0","silentHeld=0","allocations=0","destructions=0"])
live=(root/"build/windows/acceptance/loop-bounds-live.txt").read_text(encoding="utf-8")
assert live.count("overruns=0")==4 and live.count("silentInput=0")==4 and live.count("loops=2")==4
native=(root/args.native_output/"native-loop-bounds-verification.txt").read_text(encoding="utf-8-sig")
assert sha(app).upper() in native and "PASS native Loop" in native and "exitCode=0; forcedClose=False" in native
log=root/args.source_log;raw=log.read_bytes()
summary=re.search(r"100% tests passed, 0 tests failed out of (\d+)",raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1])==len(re.findall(r"^add_test\(",(root/"CMakeLists.txt").read_text(encoding="utf-8"),re.M))
assert log.stat().st_mtime>=(root/app).stat().st_mtime
inputs={i["path"]:i["sha256"] for i in read("build/build-inputs.json")}
for path in ["scripts/verify-loop-bounds-mcp.py","scripts/record-loop-bounds-evidence.py","scripts/verify-pattern-desktop.ps1",
             "skills/vibera-studio/SKILL.md","skills/vibera-studio/references/looping.md","skills/vibera-studio/scripts/stdio_client.py"]:
 inputs[path]=sha(path)
assert all(sha(p)==h for p,h in inputs.items()),"Acceptance inputs changed"
commands=["transport.loop","pattern.length","clip.move","notes.add"]
cases=["real component Loop callback and shared typed MCP runtime action with idempotent setter and explicit completion scope",
 "requested/applied quarter-beat readback; missing audio context is explicitly unavailable/null",
 "manual/note-driven Pattern growth, short/in-range/out-of-range shrink and exact source-ID/history restoration",
 "whole-Song arrangement end follows placement extent; optional Loop does not change established mode defaults",
 "48k256/44k128 Pattern/Song repeated edit/Undo/Redo and natural boundaries with held MIDI/preview and retained resources",
 "actual concurrent audio thread versus control edits: callback deadline, held PCM and instrumented allocation/destruction",
 "active MIDI-take Loop refusal preserves held capture/model/armed state; structural length finalization remains an explicit limitation",
 "owned native Loop glyph, Rack2bar menu, Save, exact Undo/Redo and optional whole-Song looping",
 "real stdio virtual controller mapping, invalid input, project round trip, finite one-pass WAV and deferred file failure",
 "existing full source regressions retained; native plugin internals/loop regions/advanced Rack loops remain open"]
receipt="docs/mcp/LOOP_BOUNDS_EVIDENCE.json"
write(receipt,dict(passed=True,applicationSha256=sha(app),sourceInputs=inputs,checks=cases,sourceSuites=summary[1]+"/"+summary[1],
 mcpChecks=mcp["checks"],nativeInput="Owned synthetic OS input; not human physical input",listening=False,physicalMidi=False,
 scope="Whole-range transport Loop and immediate live extents; full FL looping/continuous record retiming remain incomplete"))
verification=dict(status="passed",receipt=receipt,inputs=inputs,cases=cases)
inventory=read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"]=[o for o in inventory["operations"] if o["id"]!="transport.loopBounds"]
inventory["operations"].append(dict(id="transport.loopBounds",name="Whole-range Loop and playback-safe musical extents",
 source="src/Studio.cpp",canonical="Shared transport.loop application action plus canonical document commands and control-thread publisher",
 mcp="vibera_transport",additionalInterfaces=["vibera_command"],commands=commands,
 readback=["vibera_state:audio.loopBounds","vibera_state:pendingAudioUpdate","vibera_inspect:patterns/clips"],
 skill="skills/vibera-studio/references/looping.md",verification=verification))
write("docs/mcp/SURFACE_INVENTORY.json",inventory)
spec=importlib.util.spec_from_file_location("coverage_gate",root/"scripts/mcp-coverage.py")
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
reviewed=read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
 source,line,_=entry["surface"].split(":")
 text=(root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
 if source=="src/Studio.cpp" and "loopButton.onClick" in text:
  reviewed[entry["surface"]]=dict(sourceSha256=entry["sourceSha256"],operations=commands,mcp="vibera_transport",
    additionalInterfaces=["vibera_command"],readback=["vibera_state:audio.loopBounds"],
    skill="skills/vibera-studio/references/looping.md",verification=verification)
write("docs/mcp/REVIEWED_SURFACES.json",reviewed)
print("Recorded new Loop increment; shared legacy handlers and full MCP gate remain unqualified")
