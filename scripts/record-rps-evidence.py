"""Bind the Rack/Pattern/sampler software rows to one complete source regression.

Reads a passed `dev.ps1 -Full` CTest log, checks that every registered test ran and
passed on binaries built before the run, and writes one structured receipt that the
module gate verifies: per-row test results, content identities of the source/test/
skill inputs and exact identities of the candidate binaries. Software rows only:
listening, physical hardware and human FL familiarity stay separate and unclaimed.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

from dev_support import content_identity

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-log", required=True, help="affected-tests.log of a dev.ps1 -Full run")
parser.add_argument("--output", default="build/module-qualification/rps-current/verification.json")
args = parser.parse_args()

log = root / args.source_log
text = log.read_text(encoding="utf-8-sig", errors="replace")
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", text)
registered = re.findall(r"^\s*add_test\(NAME (\S+)", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M)
assert summary and int(summary[1]) == len(registered), "The run must pass every registered test"
results = {m[1]: float(m[2]) for m in re.finditer(r"Test +#\d+: (\S+) \.+ +Passed +([\d.]+) sec", text)}
assert set(results) == set(registered), "Every registered test needs a Passed line"

# Row -> tests that carry its behaviour (docs/modules/RPS-REVIEW.md lists the scope).
rows = {
    "RPS-A": ["gui_workflow", "musical_acceptance", "channel_management_mcp", "studio_channel_routing",
              "studio_channel_routing_provider", "studio_channel_routing_same_config", "rps_gaps_mcp"],
    "RPS-B": ["channel_appearance_mcp", "channel_management_mcp", "channel_solo_mcp", "channel_scoped_solo_mcp",
              "rack_groups_mcp", "automation_source_clone", "automation_source_clone_mcp"],
    "RPS-C": ["step_composition", "step_composition_gui", "step_composition_mcp", "step_fill_mcp",
              "step_velocity_mcp", "swing_mcp", "groove_mcp", "quantizer_mcp"],
    "RPS-D": ["pattern_extent", "pattern_extent_mcp"],
    "RPS-E": ["pattern_channel_loops", "pattern_channel_loops_mcp", "live_burn_gui"],
    "RPS-F": ["pattern_management", "clip_groups_gui", "clip_groups_mcp", "performance_gui"],
    "RPS-G": ["sampler_fade", "sampler_gestures_gui", "normalize_mcp", "slicer"],
    "RPS-H": ["sampler_filter", "sampler_filter_mcp", "sampler_envelope", "sampler_envelope_mcp",
              "sampler_modulation", "sampler_modulation_mcp", "sampler_automation_mcp", "sampler_controller_links",
              "sampler_controller_mcp"],
    "RPS-I": ["sampler_voice_modes", "sampler_voice_modes_mcp", "sampler_voices_mcp", "sampler_note_modes",
              "sampler_note_modes_mcp", "piano_note_modes_gui"],
    "RPS-J": ["sampler_tempo_mcp", "sampler_fade", "rps_gaps_mcp", "audio_stretch"],
    "RPS-K": ["channel_preset", "rps_gaps_mcp"],
    "RPS-L": ["history_ui_recovery", "midi_take_recovery", "midi_take_recovery_gui", "number_serialization_safety",
              "musical_acceptance"],
    "RPS-M": ["mcp_semantics", "rps_gaps_mcp", "mcp_coverage_detector"],
}
for row, tests in rows.items():
    missing = [t for t in tests if t not in results]
    assert not missing, row + " names unregistered tests: " + ", ".join(missing)

# Candidate binaries: every built executable, all older than the run that tested them.
total = re.search(r"Total Test time \(real\) = +([\d.]+) sec", text)
assert total, "The log must report its total test time"
run_start = log.stat().st_mtime - float(total[1])
candidates = sorted({p for p in (root / "build/windows").glob("*_artefacts/Release/**/*.exe")} |
                    {p for p in (root / "build/windows").glob("*_artefacts/Release/**/*.dll")})
assert candidates, "No candidate binaries"
for binary in candidates:
    assert binary.stat().st_mtime <= run_start, binary.name + " was rebuilt after the regression started"
rel = lambda p: p.relative_to(root).as_posix()
dependencies = []
for pattern in ["src/**/*.h", "src/**/*.cpp", "tests/**/*.h", "tests/**/*.cpp", "tests/**/*.py", "scripts/verify-*.py",
                "skills/vibera-studio/**/*.md", "CMakeLists.txt"]:
    for path in sorted(root.glob(pattern)):
        if path.is_file():
            dependencies.append(dict(path=rel(path), mode="content", sha256=content_identity(path)["contentSha256"]))
receipt = dict(
    format=1, passed=True, softwareCandidateOnly=True,
    requirements={row: dict(tests={t: results[t] for t in tests},
                            provenance="Complete " + summary[1] + "-case dev.ps1 -Full regression " + rel(log.parent))
                  for row, tests in rows.items()},
    unclaimed=["listening", "physical MIDI/audio hardware and latency", "native mixed-monitor/DPI", "human FL familiarity"],
    sourceLog=dict(path=rel(log), sha256=hashlib.sha256(log.read_bytes()).hexdigest()),
    dependencyHashes=dependencies,
    candidateHashes=[dict(path=rel(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in candidates],
)
output = root / args.output
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(receipt, indent=1) + "\n", encoding="utf-8")
digest = hashlib.sha256(output.read_bytes()).hexdigest()
ledger_path = root / "docs/modules/RACK-PATTERN-SAMPLER.json"
ledger = json.loads(ledger_path.read_text(encoding="utf-8-sig"))
for item in ledger["requirements"]:
    if item["id"] in rows:
        item["status"] = "verified"
        # Earlier scoped receipts stay on record; they bind older inputs, so the gate
        # checks only the current complete receipt.
        prior = item.get("priorEvidence", []) + [e for e in item.get("evidence", []) if e.get("path") != rel(output)]
        if prior:
            item["priorEvidence"] = prior
        item["evidence"] = [dict(path=rel(output), sha256=digest)]
ledger_path.write_text(json.dumps(ledger, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(json.dumps(dict(rows=len(rows), tests=len(results), dependencies=len(dependencies), candidates=len(candidates),
                      receipt=rel(output), sha256=digest)))
