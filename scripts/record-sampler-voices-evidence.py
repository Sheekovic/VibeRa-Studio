"""Bind the new sampler voice increment only; legacy MCP gaps stay deferred."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-log", required=True)
parser.add_argument("--native-output", required=True)
args = parser.parse_args()
read = lambda name: json.loads((root/name).read_text(encoding="utf-8-sig"))
sha = lambda name: hashlib.sha256((root/name).read_bytes()).hexdigest()
def write(name, value):
    (root/name).write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")
app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/sampler_voices_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 62 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["sampler-voices-gui.txt", "sampler-gesture-verification.txt"]:
    assert (root/"build/windows/sampler_gestures_gui"/name).read_text(encoding="utf-8").startswith("PASS")
log = root/args.source_log
raw = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root/"CMakeLists.txt").read_text(), re.M))
assert log.stat().st_mtime >= (root/app).stat().st_mtime
native = (root/args.native_output/"native-sampler-voices-verification.txt").read_text(encoding="utf-8-sig")
assert sha(app).upper() in native and "PASS native Cut" in native and "exitCode=0; forcedClose=False" in native
inputs = {item["path"]:item["sha256"] for item in read("build/build-inputs.json")}
for name in ["scripts/verify-sampler-voices-mcp.py", "scripts/record-sampler-voices-evidence.py",
             "scripts/verify-pattern-desktop.ps1", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/sampler-voices.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == value for name,value in inputs.items())
commands = ["channel.sampler."+name for name in ["cut", "cutBy", "cutItself", "maxPoly"]]
cases = ["canonical numeric validation, independent group allocation and exact Undo/Redo",
         "actual component controls bound to original Channel even after active selection changes",
         "owned native OS text entry/button/Rack context action and continuous increment-button drag/one Undo/Redo, exact history and project Save",
         "schema14 round trip, actual older migration and malformed/masquerading data rejection",
         "actual WAV cross-hat configured release, step-only self-cut and Max-poly overlap",
         "shared score/MIDI/typing/preview99 limit and bounded128 Auto, oldest stealing",
         "live ordinal and dense gate overflow regression, no resurrection on publication",
         "Pattern/Song repeated settings/history edits, captured release/held MIDI/recording and retained graph/device/sample identities",
         "typed semantic discovery, invalid-input structured failures, recording-safe grouped preview/cancel/real expiry",
         "virtual controller CC/button canonical changes and exact Undo; no physical hardware claim"]
receipt = "docs/mcp/SAMPLER_VOICES_EVIDENCE.json"
write(receipt, dict(passed=True, applicationSha256=sha(app), sourceInputs=inputs,
    checks=cases, sourceSuites=summary[1]+"/"+summary[1], mcpChecks=mcp["checks"],
    nativeInput="Owned synthetic OS input; not human physical input", listening=False, physicalMidi=False,
    scope="Sample/builtin Cut/Cut-by/step-self-cut/shared polyphony only; native plugin internals and Mono/portamento unsupported"))
verification = dict(status="passed", receipt=receipt, inputs=inputs, cases=cases)
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [x for x in inventory["operations"] if x["id"] != "sampler.voices"]
inventory["operations"].append(dict(id="sampler.voices", name="Sampler cut groups, step self-cut and shared polyphony",
    source="src/InstrumentWindow.h", canonical="Shared Controller commands/history and fixed-target voice gesture",
    mcp="vibera_command", additionalInterfaces=["vibera_gesture", "vibera_controller"], commands=commands,
    readback=["vibera_state:channels.sampler", "vibera_gesture:state", "vibera_state:audio"],
    skill="skills/vibera-studio/references/sampler-voices.md", verification=verification))
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
spec = importlib.util.spec_from_file_location("coverage_gate",root/"scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
source = (root/"src/InstrumentWindow.h").read_text().splitlines()
start = next(i for i,text in enumerate(source,1) if 'voiceNames{' in text)
end = next(i for i,text in enumerate(source,1) if 'setTempo.setComponentID' in text)
for entry in gate.inventory()["customSurfaceCandidates"]:
    file,line,_ = entry["surface"].split(":")
    if file == "src/InstrumentWindow.h" and start < int(line) < end:
        reviewed[entry["surface"]] = dict(sourceSha256=entry["sourceSha256"],operations=commands,
            mcp="vibera_command",additionalInterfaces=["vibera_gesture"],
            readback=["vibera_state:channels.sampler", "vibera_gesture:state"],
            skill="skills/vibera-studio/references/sampler-voices.md",verification=verification)
# Shared legacy Rack/context handlers remain unreviewed; the dedicated new action
# is qualified by the explicit operation above, not a blanket handler waiver.
write("docs/mcp/REVIEWED_SURFACES.json",reviewed)
print("Recorded sampler voice increment; full inherited MCP gate remains incomplete")
