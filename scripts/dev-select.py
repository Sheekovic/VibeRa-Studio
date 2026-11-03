#!/usr/bin/env python3
"""Read-only, fail-conservative affected-test selection. Never builds or packages."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


# These are coverage groups, not a second inventory of all CTest registrations.
# Names are validated against CMake on every invocation; missing coverage fails.
GROUPS = {
    "synthesizer": {"vibera_synth_engine", "vibera_synth_processor", "vibera_synth_mcp", "vibera_synth_content", "vibera_synth_host"},
    "document": {"mixer_command_preparation", "prepared_timeline_publication", "musical_acceptance", "mcp_semantics", "gui_workflow", "history_destination_admission", "prepared_history_mutation", "controller_prepared_history", "number_serialization_safety", "midi_take_recovery", "midi_take_recovery_gui", "history_ui_recovery"},
    "audio": {"prepared_timeline_publication", "musical_acceptance", "callback_timing", "audio_playlist_live",
              "mixer_controls_audio", "mixer_routing_mcp", "automation_foundation", "compensation_history", "bounded_midi_compensation", "mixer_graph_publication", "mixer_ready_graph", "mixer_structural_graph_contracts", "mixer_wave_context", "mixer_guide_runtime", "mixer_control_runtime", "mixer_instrument_context", "mixer_instrument_packet_boundary", "mixer_routing_lifetime", "mixer_concurrent_retained_context", "mixer_effect_runtime", "mixer_instrument_runtime", "midi_take_recovery", "midi_take_recovery_gui"},
    "plugins": {"musical_acceptance", "isolated_parameters", "plugin_parameters_gui",
                "plugin_catalogue_gui", "mixer_effect_slots_gui", "mixer_effect_slots_mcp", "mixer_effects_verifier", "mixer_effect_opening", "mixer_effect_opening_unsupported", "automation_foundation", "mcp_semantics",
                "mixer_latency_risk", "mixer_sidechain_transport", "mixer_bypass_transition", "mixer_sidechain_graph",
                "mixer_dynamic_latency", "mixer_default_dynamic_latency", "compensation_history", "bounded_midi_compensation", "mixer_graph_publication", "mixer_ready_graph", "mixer_structural_graph_contracts", "mixer_wave_context", "mixer_guide_runtime", "mixer_control_runtime", "mixer_instrument_context", "mixer_instrument_packet_boundary", "mixer_routing_lifetime", "mixer_effect_runtime", "mixer_instrument_runtime", "mixer_retained_context", "mixer_concurrent_retained_context", "plugin_latency_mcp"},
    "ui": {"mixer_effect_opening", "mixer_effect_opening_unsupported", "mixer_effect_slots_gui", "mixer_effect_slots_mcp", "mixer_effects_verifier", "gui_workflow", "controller_mapping_gui", "midi_take_recovery_gui", "history_ui_recovery"},
    "semantic": {"mcp_semantics", "mcp_coverage_detector", "mixer_effect_slots_mcp", "mixer_effects_verifier", "midi_take_recovery_gui"},
    "persistence": {"musical_acceptance", "mcp_semantics", "midi_interchange_gui",
                    "audio_playlist_gui", "midi_take_recovery", "midi_take_recovery_gui"},
}
for category in ("document", "audio", "plugins", "ui", "semantic", "persistence"):
    GROUPS[category].update({"step_composition", "step_composition_mcp", "step_composition_gui",
                             "note_release_worker"})
    GROUPS[category].update({"pattern_extent", "pattern_extent_mcp", "pattern_channel_loops", "pattern_channel_loops_mcp"})
    GROUPS[category].add("sampler_tempo_mcp")
    GROUPS[category].add("sampler_controller_links")
    GROUPS[category].add("sampler_controller_mcp")
    GROUPS[category].update({"sampler_voice_modes", "sampler_voice_modes_mcp", "sampler_note_modes", "sampler_note_modes_mcp", "piano_note_modes_gui"})
    GROUPS[category].add("channel_appearance_mcp")
    GROUPS[category].add("channel_scoped_solo_mcp")
    GROUPS[category].update({"mixer_insert_order", "mixer_insert_order_gui", "mixer_insert_order_mcp"})
for category in ("audio", "plugins"):
    GROUPS[category].add("mixer_plugin_recovery")
    GROUPS[category].add("mixer_finite_effect_baseline")
    GROUPS[category].add("mixer_dynamic_waveform")
    GROUPS[category].add("mixer_dormant_sampler_runtime")
    GROUPS[category].add("mixer_local_source_initial")
    GROUPS[category].add("mixer_native_identity")
    GROUPS[category].add("mixer_summing_properties")
    GROUPS[category].add("mixer_prepared_source_hosts")
    GROUPS[category].update({"mixer_native_identity_sparse_pattern", "mixer_native_identity_sparse_song",
                             "mixer_native_identity_129_pattern", "mixer_native_identity_129_song"})
    GROUPS[category].add("mixer_pcm_receipts")
    GROUPS[category].update({"mixer_worker_turnaround", "mixer_effect_turnaround", "mixer_routing_coordinator", "mixer_chain_controls", "plugin_host_wet_mix", "mixer_chain_live_export"})
GROUPS["document"].update({"mixer_chain_controls", "mixer_routing_coordinator"})
for category in GROUPS:
    GROUPS[category].add("mixer_chain_data")
    GROUPS[category].add("mixer_chain_file_jobs")
    GROUPS[category].add("mixer_chain_workflow")
    GROUPS[category].add("mixer_chain_mcp")
    GROUPS[category].update({"mixer_organisation", "mixer_macro_configuration", "mixer_organisation_gui", "mixer_organisation_mcp"})
    GROUPS[category].update({"mixer_native_inverse_manager", "mixer_native_inverse_controller", "mixer_stereo_controls", "mixer_stereo_gui", "mixer_stereo_mcp", "mixer_stereo_native"})
    GROUPS[category].update({"mixer_automation_core", "mixer_automation_mcp"})
    GROUPS[category].add("mixer_automation_native")
    GROUPS[category].add("mixer_automation_gui")
    GROUPS[category].update({"mixer_automation_take_core", "mixer_automation_take_gui",
                             "mixer_native_scalar_core", "mixer_native_scalar_gui"})
for category in ("audio", "plugins"):
    GROUPS[category].add("mixer_output_stage_difference")
    GROUPS[category].add("mixer_warm_source_relocation")
    GROUPS[category].add("mixer_zero_history_creation")
    GROUPS[category].add("mixer_stopped_effect_forward_sdk")
    GROUPS[category].add("mixer_stopped_effect_inverse_sdk")
    GROUPS[category].add("mixer_audio_port")
    GROUPS[category].add("mixer_app_midi_ownership")
    GROUPS[category].add("mixer_bounded_midi_ownership")
    GROUPS[category].add("mixer_native_midi_ingress")
    GROUPS[category].add("mixer_app_audio_port")
    GROUPS[category].add("mixer_initial_app_audio_ports")
    GROUPS[category].add("mixer_initial_app_active_fx_ports")
    GROUPS[category].add("mixer_app_zero_history")
    GROUPS[category].add("mixer_zero_stage_exchange")
    GROUPS[category].add("mixer_initial_source_joint_route")
    GROUPS[category].add("mixer_zero_channel_delay")
    GROUPS[category].add("mixer_dynamic_instrument_latency")
    GROUPS[category].add("mixer_stream_pcm")
    GROUPS[category].add("mixer_fixed_source_pcm_epoch")
    GROUPS[category].add("mixer_source_render_time")
    GROUPS[category].add("mixer_initial_source_epoch_fence")
    GROUPS[category].add("mixer_initial_stable_track_midi")
    GROUPS[category].add("mixer_forward_stage_drain")
    GROUPS[category].add("mixer_initial_app_source")
    GROUPS[category].add("mixer_initial_app_source_controls")
    GROUPS[category].add("mixer_send_automation_native")
for category in ("document", "audio", "plugins", "persistence"):
    GROUPS[category].update({"mixer_stopped_effect_command", "mixer_stopped_effect_plan", "mixer_stopped_effect_native",
                             "mixer_stopped_effect_ordinary_forward", "plugin_dry_history",
                             "plugin_dry_history_host"})
for category in ("document", "audio", "plugins", "ui", "semantic", "persistence"):
    GROUPS[category].update({"controller_routing_capture", "studio_channel_routing",
                             "studio_channel_routing_provider", "studio_channel_routing_same_config",
                             "deferred_device_candidate_fault"})
for category in ("audio", "plugins"):
    GROUPS[category].update({"physical_burst_leaf", "mixer_physical_worker", "mixer_prepared_transport",
                            "mixer_prepared_transport_coverage", "mixer_native_source_admission"})
    GROUPS[category].add("mixer_stable_bus_stages")
for category in ("document", "plugins", "ui", "semantic", "persistence"):
    GROUPS[category].add("mixer_plugin_save_capture")
GROUPS["persistence"].add("mixer_chain_controls")
for category in ("document", "audio", "ui", "semantic", "persistence"):
    GROUPS[category].update({"sampler_filter", "sampler_filter_mcp", "sampler_modulation", "sampler_modulation_mcp",
                            "sampler_envelope", "sampler_envelope_mcp", "sampler_automation_mcp"})
for category in ("document", "ui", "semantic"):
    GROUPS[category].update({"mixer_target_choices_gui", "mixer_target_choices"})
SHARED = {"cmakelists.txt", "cmakepresets.json", "dependencies.lock.json",
          "src/project.h", "src/project.cpp", "src/commands.h", "src/commands.cpp",
          "src/audioengine.h", "src/audioengine.cpp", "src/studio.h", "src/studio.cpp",
          "src/main.cpp", "src/editors.h", "src/studiomcp.cpp", "src/mcpcontracts.h",
          "src/pluginprotocol.h", "src/pluginbuses.h", "src/pluginhost.h",
          "src/mcpschema.h", "tests/acceptance.cpp", "tests/test_dev_selection.py"}
PROTECTED = {"reference", "evidance", "--browser-search-check"}


def git_changes(root: Path) -> list[dict]:
    # -z avoids quoting/escaping paths with spaces, tabs, unicode and newlines.
    # normal reports untracked directories once; never enumerate research trees.
    result = subprocess.run(
        ["git", "-c", f"safe.directory={root.as_posix()}", "status", "--porcelain=v1",
         "-z", "--untracked-files=normal"], cwd=root, check=True, capture_output=True)
    fields = result.stdout.decode("utf-8", errors="surrogateescape").split("\0")
    changes, index = [], 0
    while index < len(fields) and fields[index]:
        record = fields[index]
        index += 1
        if len(record) < 4 or record[2] != " ":
            raise ValueError("Invalid Git porcelain record; refusing incomplete selection")
        status, path = record[:2], record[3:]
        changes.append({"path": path, "status": status})
        if "R" in status or "C" in status:
            if index >= len(fields) or not fields[index]:
                raise ValueError("Missing rename/copy source in Git status")
            changes.append({"path": fields[index], "status": status, "renameSource": True})
            index += 1
    return changes


def cmake_tests(root: Path) -> dict[str, dict]:
    # Current authoritative file uses modern add_test(NAME ... COMMAND ...).
    # CMake integration changes are broad, never narrowly classified by parser.
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    text = re.sub(r"(?m)^\s*#.*$", "", text)
    executables = set(re.findall(r"(?:juce_add_(?:gui|console)_app|add_executable)\s*\(\s*([A-Za-z0-9_]+)", text))
    tests = {}
    for match in re.finditer(r"add_test\s*\(\s*NAME\s+(\w+)\s+COMMAND\s+([^\n]+(?:\n(?!\s*(?:add_|set_|if|endif|#))[^\n]+)*)", text):
        name, command = match.groups()
        # Stop at the closing parenthesis of the add_test, not at generator refs.
        command = command.split("\nset_tests_properties", 1)[0]
        targets = set(re.findall(r"\$<TARGET_FILE:([A-Za-z0-9_]+)>", command))
        direct = re.match(r"([A-Za-z0-9_]+)\b", command)
        if direct and direct.group(1) in executables:
            targets.add(direct.group(1))
        scripts = re.findall(r'(?:scripts|tests)/[^\r\n"$<>]*?\.(?:pyw|py|ps1|psm1|psd1|mjs|cjs|js|bat|cmd)(?=["\s)])', command)
        tests[name] = {"targets": targets, "scripts": scripts}
    if not tests or len(tests) != len(re.findall(r"\badd_test\s*\(", text)):
        raise ValueError("Unsupported/duplicate CTest registration; refusing incomplete selection")
    return tests


def classify(path: str) -> tuple[set[str], str, bool]:
    p = path.replace("\\", "/").removeprefix("./").lower()
    first = p.split("/", 1)[0]
    if first in PROTECTED:
        return set(), "Protected reference/research work preserved; no recursive scan", False
    if first in {"build", "dist", ".git"}:
        return set(), "Generated output is not a source change", False
    if p in SHARED or first in {"vendor", "patches", ".github"} or p.startswith("cmake/"):
        return set(GROUPS), "Shared model/dispatcher/build/dependency infrastructure: full source regression", True
    if p.startswith("scripts/") and not p.startswith(("scripts/verify-", "scripts/record-", "scripts/coproducer/")):
        return set(GROUPS), "Build/test orchestration or unknown script: full source regression", True
    if first == "docs" or p in {"agents.md", "readme.md", "license", ".gitignore"}:
        return set(), "Documentation/policy only; development preflight still applies", False
    if p.startswith("skills/"):
        return {"semantic"}, "Provider-neutral skill/discovery contract", False
    if p.startswith(("src/synth/", "assets/synthesizer/")) or Path(p).name in {
            "synthenginetests.cpp", "synthplugintests.cpp", "synthhostacceptance.h", "studiosynth.inc",
            "verify-synthesizer-mcp.py", "verify-synthesizer-installed.py", "verify-synthesizer-desktop.py"}:
        return {"synthesizer"}, "Native Synthesizer engine/content/editor and canonical musical workflow", False
    if p.startswith("assets/"):
        return {"ui"}, "Application visual assets require UI inspection", False
    if p.startswith("scripts/record-"):
        return {"semantic"}, "Verification receipt generation and semantic coverage", False
    if p.startswith("scripts/coproducer/") or "coproducer" in p:
        return {"document", "semantic", "persistence"}, "CoProducer document/job and runtime contract", False
    if p.startswith(("src/", "tests/", "scripts/verify-")):
        name = Path(p).name
        if ((p.startswith("tests/test_dev_") and name.endswith("_guard.py")) or
                (p.startswith("scripts/verify-") and name.endswith("-guard.py"))):
            return set(GROUPS), "Native/build guard implementation: conservative full source regression", True
        native_name = name.replace("_", "").replace("-", "")
        if "compensationhistory" in native_name:
            return {"audio", "plugins"}, "Audio/MIDI compensation delay queues and retained DSP lifetime", False
        if any(token in native_name for token in ("inversestageresidual", "forwardstagedrain",
                                                  "outputstagedifference", "stablebusstage", "audioport",
                                                  "nativemidiingress")):
            return {"audio", "plugins"}, "Native audio stage, finite transition and audio-port contracts", False
        if p == "tests/zerohistorycreationacceptance.h":
            return {"audio", "plugins"}, "Native compensation queue creation/provenance, not document Undo history", False
        if any(x in name for x in ("plugin", "worker", "parameter", "scanner", "latency", "sidechain", "buslayout", "fixture", "remoteeffect", "retainedcontext", "instrumentruntime", "instrumentcontext", "wavecontext", "instrumentapplicationrouting", "routinglifetime", "channelrouting")):
            return {"plugins", "audio", "persistence", "semantic"}, "Plugin ownership/protocol/state and audio integration", False
        if any(x in name for x in ("mixer", "automation", "livetimeline", "preparedtimeline", "fixedinitialisation", "callback", "samplecache", "liveaudio", "livenote", "samplervoice", "guideruntime", "controlruntime", "graphoutputrouting")):
            return {"audio", "document", "semantic", "persistence", "ui"}, "Live routing/DSP/automation and control/history integration", False
        if any(x in name for x in ("mcp", "schema", "macro")):
            return {"semantic", "document"}, "Semantic command/discovery/macro contract", False
        # Tag is a word, including CamelCase and underscore/hyphen spellings.
        # A substring search incorrectly treats Stage/Backstage as Browser code.
        original_stem = Path(path.replace("\\", "/")).stem
        words = {word.lower() for word in re.findall(
            r"[A-Z]+(?=[A-Z][a-z]|[^A-Za-z]|$)|[A-Z]?[a-z]+|[0-9]+", original_stem)}
        if any(x in name for x in ("browser", "collection", "query")) or words & {"tag", "tags"}:
            return {"ui", "persistence", "semantic"}, "Browser search, UI and persisted collections", False
        if "history" in name or "structuralpreparation" in name:
            return {"document", "semantic", "persistence", "ui"}, "Actual history admission/ownership, commands and persistence", False
        if any(x in name for x in ("native", "window", "workspace", "theme", "setting", "customization", "controller")):
            return {"ui", "semantic", "persistence"}, "Custom UI/gesture/settings and command bindings", False
        if any(x in name for x in ("record", "import", "export", "render", "waveform", "audioclip", "file", "async", "job", "recovery")):
            return {"persistence", "audio", "semantic", "ui"}, "File/job/recovery behavior and audio/persistence integration", False
        if any(x in name for x in ("note", "piano", "score", "pattern", "rack", "channel", "step", "swing", "groove", "quantiz", "clip", "playlist", "sampler", "midi", "selection", "gesture")):
            return {"document", "audio", "ui", "semantic", "persistence"}, "Musical editing, history, live scheduling and persistence", False
    return set(GROUPS), "Unknown source path: conservative full source regression", True


def select(root: Path, changes: list[dict], full=False, additional=()) -> dict:
    tests = cmake_tests(root)
    missing = set().union(*GROUPS.values()) - tests.keys()
    if missing:
        raise ValueError(f"Required test registrations missing: {', '.join(sorted(missing))}")
    unknown = set(additional) - tests.keys()
    if unknown:
        raise ValueError(f"Unknown --additional-test: {', '.join(sorted(unknown))}")
    categories, reasons, chosen = set(), [], set(additional)
    broad = full
    for change in changes:
        groups, reason, conservative = classify(change["path"])
        categories.update(groups)
        broad |= conservative
        reasons.append({"path": change["path"], "categories": sorted(groups), "reason": reason})
        # Direct registered verifier edits always rerun that exact test.
        p = change["path"].replace("\\", "/")
        chosen.update(n for n, spec in tests.items() if p in spec["scripts"])
    if broad:
        chosen.update(tests)
        categories.update(GROUPS)
    else:
        for category in categories:
            chosen.update(GROUPS[category])
        # Add complete feature families to the cross-cutting baselines. Do not
        # run every unrelated editor test for a local Mixer control adjustment.
        families = {
            "routingcoordinator": ("mixer_routing_coordinator", "controller_prepared_history", "mixer_routing_lifetime"),
            "chaincontrols": ("mixer_chain_controls", "plugin_host_wet_mix", "mixer_chain_live_export", "mixer_effect_slots_mcp", "mixer_effect_slots_gui"),
            "hostwetmix": ("plugin_host_wet_mix", "mixer_chain_controls", "mixer_effect_slots_mcp"),
            "turnaround": ("mixer_worker_turnaround", "mixer_effect_turnaround"),
            "instrumentapplicationrouting": ("mixer_instrument_context", "mixer_instrument_packet_boundary", "mixer_instrument_runtime"),
            "routinglifetime": ("mixer_routing_lifetime", "mixer_wave_context", "mixer_instrument_context", "mixer_instrument_packet_boundary"),
            "channelrouting": ("mixer_routing_lifetime", "mixer_wave_context", "mixer_instrument_context", "mixer_instrument_packet_boundary", "mixer_structural_graph_contracts", "mixer_control_runtime", "mixer_guide_runtime", "mixer_effect_runtime"),
            "instrumentcontext": ("mixer_instrument_context", "mixer_instrument_packet_boundary", "mixer_instrument_runtime"),
            "controlruntime": ("mixer_control_runtime", "mixer_structural_graph_contracts", "mixer_wave_context", "mixer_controls_audio"),
            "guideruntime": ("mixer_guide_runtime", "musical_acceptance"),
            "graphoutputrouting": ("mixer_structural_graph_contracts", "mixer_wave_context", "mixer_guide_runtime", "mixer_control_runtime", "mixer_instrument_context", "mixer_retained_context", "mixer_concurrent_retained_context"),
            "adoptedgraphlatency": ("mixer_structural_graph_contracts", "mixer_wave_context", "mixer_guide_runtime", "bounded_midi_compensation"),
            "graphcontrolruntime": ("mixer_structural_graph_contracts", "mixer_wave_context", "mixer_guide_runtime", "mixer_control_runtime", "mixer_instrument_context", "mixer_controls_audio"),
            "fixedinitialisation": ("fixed_initialisation_lifecycle", "mixer_retained_context", "mixer_concurrent_retained_context"),
            "retainedcontext": ("mixer_retained_context", "mixer_concurrent_retained_context"), "detachedgraph": ("mixer_retained_context", "mixer_concurrent_retained_context"),
            "instrumentruntime": ("mixer_instrument_runtime", "mixer_retained_context", "mixer_concurrent_retained_context"),
            "remoteeffect": ("mixer_effect_runtime", "mixer_retained_context", "mixer_concurrent_retained_context", "mixer_sidechain_"),
            "structuralpreparation": ("mixer_command_preparation", "controller_prepared_history", "history_destination_admission"),
            "mixer": ("mixer_",), "browser": ("browser_", "midi_interchange_"),
            "plugin": ("plugin_", "isolated_parameters"), "parameter": ("plugin_", "isolated_parameters"),
            "automation": ("automation_", "isolated_parameters", "plugin_parameters_"),
            "coproducer": ("coproducer_",), "sampler": ("sampler_", "normalize_"),
            "normalize": ("normalize_", "sampler_"), "channel": ("channel_", "rack_", "step_"),
            "rack": ("rack_", "step_", "channel_"), "step": ("step_", "rack_"),
            "swing": ("swing_",), "groove": ("groove_", "quantizer_"),
            "quantiz": ("quantizer_", "groove_"), "stretch": ("note_stretch_",),
            "slice": ("note_slice_",), "mute": ("note_mute_",),
            "clipgroup": ("clip_groups_",), "clip-group": ("clip_groups_",),
            "playlist": ("audio_playlist_", "clip_groups_"), "audioclip": ("audio_playlist_",),
            "loopbound": ("loop_bounds_",), "loop-bound": ("loop_bounds_",),
            "midi": ("midi_interchange_", "controller_mapping_", "midi_take_"),
            "record": ("midi_take_",),
            "history": ("history_", "controller_prepared_history", "prepared_history_mutation", "prepared_timeline_publication"),
            "controller": ("controller_mapping_",),
        }
        for change in changes:
            if not classify(change["path"])[0]:
                continue
            name = Path(change["path"].replace("\\", "/")).name.lower()
            for token, prefixes in families.items():
                if token in name:
                    chosen.update(n for n in tests if n.startswith(prefixes))
    targets = set().union(*(tests[n]["targets"] for n in chosen)) if chosen else set()
    return {"schemaVersion": 1, "changedFiles": sorted({x["path"] for x in changes}),
            "fileStatus": changes, "categories": sorted(categories), "reasons": reasons,
            "buildTargets": sorted(targets), "testNames": sorted(chosen),
            # CTest uses POSIX regular expressions, not Python's noncapturing
            # groups/lookahead. The runner additionally rejects zero tests.
            "ctestRegex": "^(" + "|".join(re.escape(n) for n in sorted(chosen)) + ")$" if chosen else "a^",
            "nativeSuggested": "ui" in categories,
            "timingSensitiveTests": sorted(chosen & {"callback_timing", "mixer_controls_audio", "audio_playlist_live", "midi_take_recovery_gui"}),
            "full": broad, "requestedFull": full, "additionalTests": sorted(set(additional)),
            "releaseQualification": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--files", nargs="*", help="Explicit paths instead of Git status (test/review override)")
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--additional-test", action="append", default=[])
    parser.add_argument("--format", choices=("json", "text"), default="json")
    args = parser.parse_args()
    try:
        changes = git_changes(args.repo.resolve()) if args.files is None else [{"path": p, "status": "explicit"} for p in args.files]
        plan = select(args.repo.resolve(), changes, args.full, args.additional_test)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(json.dumps({"error": "selection_failed", "message": str(exc)}), file=sys.stderr)
        return 2
    if args.format == "json":
        print(json.dumps(plan, indent=2, ensure_ascii=True))
    else:
        for item in plan["reasons"]:
            print(f"{item['path']}: {item['reason']}")
        print("Categories:", ", ".join(plan["categories"]) or "none")
        print("Build targets:", ", ".join(plan["buildTargets"]) or "none")
        print("Tests:", ", ".join(plan["testNames"]) or "none")
        print("Native inspection suggested:", plan["nativeSuggested"])
        print("Development selection does not qualify a release.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
