"""Reissue reviewed UI-surface receipts whose original files (old build/ folders) are gone.

Each group of operations maps to the CTest cases that exercise it. The script refuses unless
the given complete dev.ps1 -Full log passed every registered test, including every mapped one;
it then writes a docs/mcp/<GROUP>_EVIDENCE.json receipt in the coverage format (current source
hashes, exact application identity, the passing tests as checks) and points the inventory
entries at it. Durable docs/mcp receipts are not removed by build cleanup.

usage: python scripts/reissue-surface-receipts.py --source-log build/dev/<run>/affected-tests.log
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dev_support import content_identity  # noqa: E402

GROUPS = {
    "build/mixer-unmute-qualified-focused-20261019/verification.json":
        ("MIXER_UNMUTE", ["mixer_controls_gui", "mixer_controls_mcp", "channel_solo_mcp"]),
    "build/sampler-envelope-20261021/automation-verification.json":
        ("SAMPLER_AUTOMATION", ["sampler_automation_mcp", "automation_clip_gui", "sampler_modulation_mcp"]),
    "build/sampler-envelope-20261021/verification.json":
        ("SAMPLER_ENVELOPE", ["sampler_envelope", "sampler_envelope_mcp"]),
    "build/sampler-envelope-20261021/midi-coverage-verification.json":
        ("SAMPLER_CONTROLLER", ["sampler_controller_mcp", "sampler_controller_links", "controller_mapping_gui"]),
    "build/sampler-voice-modes-20261021/verification.json":
        ("SAMPLER_VOICE_MODES", ["sampler_voice_modes", "sampler_voice_modes_mcp"]),
    "build/sampler-note-modes-20261021/mcp-review-proof.json":
        ("SAMPLER_NOTE_MODES", ["sampler_note_modes", "sampler_note_modes_mcp", "piano_note_modes_gui"]),
}

parser = argparse.ArgumentParser()
parser.add_argument("--source-log", required=True)
parser.add_argument("--rerun-log", action="append", default=[],
                    help="log of a failed test re-run alone (repeatable); see scripts/test_run_evidence.py")
args = parser.parse_args()
sys.path.insert(0, str(ROOT / "scripts"))
from test_run_evidence import complete_passing_run  # noqa: E402
passed, rerun_note = complete_passing_run(ROOT, args.source_log, args.rerun_log)
exe = ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
application = hashlib.sha256(exe.read_bytes()).hexdigest()

inventory_path = ROOT / "docs/mcp/SURFACE_INVENTORY.json"
inventory = json.loads(inventory_path.read_text(encoding="utf-8-sig"))
report = {}
for old, (name, tests) in GROUPS.items():
    missing = [t for t in tests if t not in passed]
    if missing:
        sys.exit(f"Refused: {name} maps to tests that did not pass: {missing}")
    ops = [op for op in inventory["operations"] if (op.get("verification") or {}).get("receipt") == old]
    if not ops:
        continue
    mode = ops[0]["verification"].get("sourceHashMode", "raw")
    names = sorted({n for op in ops for n in op["verification"].get("inputs", {})} | {op["source"] for op in ops})
    names = [n for n in names if (ROOT / n).is_file()]

    def digest(n):
        return content_identity(ROOT / n)["contentSha256"] if mode == "text-lf-v1" else hashlib.sha256((ROOT / n).read_bytes()).hexdigest()

    inputs = {n: digest(n) for n in names}
    receipt = ROOT / f"docs/mcp/{name}_EVIDENCE.json"
    receipt.write_text(json.dumps(dict(
        passed=True, applicationSha256=application, sourceInputs=inputs, sourceHashMode=mode,
        checks=[f"ctest {t} passed in {args.source_log}" for t in tests],
        replaces=old, scope="Reissued from a complete passing regression; the original receipt folder no longer exists"),
        indent=2) + "\n", encoding="utf-8")
    for op in ops:
        op["verification"]["receipt"] = receipt.relative_to(ROOT).as_posix()
        op["verification"]["inputs"] = inputs
    report[name] = len(ops)
inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report))
