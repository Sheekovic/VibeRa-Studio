"""Fast selector checks; no build, package, native input or provider launches."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("dev_select", ROOT / "scripts/dev-select.py")
selector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(selector)


class SelectionTests(unittest.TestCase):
    def plan(self, *paths, **kwargs):
        return selector.select(ROOT, [{"path": p, "status": " M"} for p in paths], **kwargs)

    def test_actual_test_inventory_and_target_dependencies(self):
        tests = selector.cmake_tests(ROOT)
        self.assertGreaterEqual(len(tests), 40)
        self.assertEqual(tests["mixer_controls_audio"]["targets"], {"VibeRaTests"})
        self.assertEqual(tests["mixer_controls_mcp"]["targets"], {"VibeRa"})
        self.assertEqual(tests["isolated_parameters"]["targets"], {"VibeRaTests", "VibeRaParameterFixture"})
        self.assertEqual(tests["automation_foundation"]["targets"], {"AutomationTests"})
        self.assertEqual(tests["mixer_dynamic_waveform"]["targets"],
                         {"VibeRaTests", "VibeRaClockWaveformFixture", "VibeRaMixerLatencyFixture"})

    def test_mixer_selects_live_semantic_history_ui_and_persistence(self):
        plan = self.plan("src/StudioMixerControls.cpp")
        self.assertFalse(plan["full"])
        self.assertEqual(set(plan["categories"]), {"document", "audio", "ui", "semantic", "persistence"})
        self.assertTrue({"mixer_controls_mcp", "mixer_controls_gui", "mixer_controls_audio",
                         "mixer_routing_mcp", "mixer_routing_gui", "musical_acceptance",
                         "callback_timing", "audio_playlist_live"} <= set(plan["testNames"]))
        self.assertNotIn("coproducer_context", plan["testNames"])
        self.assertTrue(plan["nativeSuggested"])
        self.assertIn("callback_timing", plan["timingSensitiveTests"])

    def test_practical_effect_slots_select_component_and_shared_semantics(self):
        for path in ("src/MixerEffectSlots.h", "src/MixerEffectsContracts.h",
                     "tests/MixerEffectSlotsAcceptance.inc"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertIn("mixer_effect_slots_gui", plan["testNames"])
                self.assertIn("mixer_effect_slots_mcp", plan["testNames"])
                self.assertIn("mixer_effects_verifier", plan["testNames"])
                self.assertIn("mixer_effect_opening", plan["testNames"])
                self.assertIn("mixer_effect_opening_unsupported", plan["testNames"])
                self.assertIn("mcp_semantics", plan["testNames"])
                self.assertIn("VibeRa", plan["buildTargets"])

    def test_plugin_worker_selects_real_host_contract_groups(self):
        plan = self.plan("src/PluginWorker.cpp", "src/PluginIPC.h")
        self.assertIn("plugins", plan["categories"])
        self.assertTrue({"isolated_parameters", "plugin_parameters_gui", "plugin_catalogue_gui",
                         "automation_foundation", "callback_timing", "mixer_latency_risk",
                           "mixer_sidechain_transport", "mixer_sidechain_graph", "mixer_bypass_transition",
                           "mixer_dynamic_latency", "mixer_default_dynamic_latency", "mixer_dynamic_waveform", "compensation_history", "plugin_latency_mcp"} <= set(plan["testNames"]))
        self.assertIn("VibeRaParameterFixture", plan["buildTargets"])

    def test_public_host_and_packet_contracts_select_full_source(self):
        for path in ("src/PluginProtocol.h", "src/PluginBuses.h", "src/PluginHost.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["full"])
                self.assertEqual(set(plan["testNames"]), set(selector.cmake_tests(ROOT)))

    def test_transport_experiments_keep_both_real_worker_paths(self):
        for path in ("tests/PluginTurnaround512Acceptance.h", "tests/PluginTurnaroundEffectsAcceptance.h",
                     "src/PluginWorker.cpp", "src/PluginAutomationStream.cpp"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue({"mixer_worker_turnaround", "mixer_effect_turnaround", "mixer_dynamic_waveform"} <= set(plan["testNames"]))
                self.assertTrue({"VibeRaTests", "VibeRaInstrumentFixture", "VibeRaParameterFixture",
                                 "VibeRaMixerLatencyFixture"} <= set(plan["buildTargets"]))
                self.assertFalse(plan["releaseQualification"])

    def test_chain_and_routing_coordinator_select_actual_native_and_model_checks(self):
        for path in ("src/HostWetMix.h", "src/MixerChainControls.h", "tests/PluginHostWetMixAcceptance.h"):
            with self.subTest(path=path):
                selected = self.plan(path)
                self.assertTrue({"plugin_host_wet_mix", "mixer_chain_controls", "mixer_chain_live_export", "mixer_effect_slots_mcp"}
                                <= set(selected["testNames"]))
                self.assertIn("VibeRaMixerLatencyFixture", selected["buildTargets"])
        for path in ("src/ChannelRoutingCoordinator.h", "tests/ChannelRoutingCoordinatorAcceptance.h"):
            with self.subTest(path=path):
                self.assertTrue({"mixer_routing_coordinator", "controller_prepared_history", "mixer_routing_lifetime"}
                                <= set(self.plan(path)["testNames"]))

    def test_document_model_and_dispatcher_are_shared_full(self):
        for path in ("src/Project.cpp", "src/Commands.h", "src/Editors.h", "src/McpContracts.h", "tests/test_dev_selection.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["full"])
                self.assertEqual(set(plan["testNames"]), set(selector.cmake_tests(ROOT)))

    def test_unknown_and_shared_build_inputs_are_conservative(self):
        for path in ("src/BrandNewSubsystem.cpp", "new-system/config.json", "CMakeLists.txt",
                     "scripts/build.ps1", "vendor/pin.txt", "patches/engine.patch"):
            with self.subTest(path=path):
                self.assertTrue(self.plan(path)["full"])

    def test_direct_script_change_runs_its_registered_test(self):
        plan = self.plan("scripts/verify-note-stretch-mcp.py")
        self.assertIn("note_stretch_mcp", plan["testNames"])
        self.assertIn("note_stretch_gui", plan["testNames"])

    def test_prepared_history_includes_its_actual_manager_and_controller_checks(self):
        for path in ("tests/ControllerPreparedHistoryAcceptance.h", "tests/HistoryDestinationAcceptance.h",
                     "tests/PreparedHistoryMutationAcceptance.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue({"document", "semantic", "persistence", "ui"} <= set(plan["categories"]))
                self.assertTrue({"controller_prepared_history", "history_destination_admission",
                                 "prepared_history_mutation", "musical_acceptance", "mcp_semantics"}
                                <= set(plan["testNames"]))

    def test_prepared_timeline_selects_actual_ownership_and_history_audio_checks(self):
        for path in ("src/PreparedTimeline.h", "src/LiveTimeline.cpp", "tests/PreparedTimelineAcceptance.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertTrue({"document", "audio", "semantic", "persistence", "ui"} <= set(plan["categories"]))
                self.assertTrue({"prepared_timeline_publication", "controller_prepared_history",
                                 "history_destination_admission", "musical_acceptance", "callback_timing", "mixer_dynamic_waveform"}
                                <= set(plan["testNames"]))
                self.assertIn("VibeRaTests", plan["buildTargets"])
                self.assertFalse(plan["releaseQualification"])
        for path in ("tests/ControllerPreparedHistoryAcceptance.h", "tests/HistoryDestinationAcceptance.h"):
            self.assertIn("prepared_timeline_publication", self.plan(path)["testNames"])
        test = selector.cmake_tests(ROOT)["prepared_timeline_publication"]
        self.assertEqual(test["targets"], {"VibeRaTests"})

    def test_retained_take_and_history_ui_select_actual_recovery_scenarios(self):
        for path in ("src/MidiRecording.h", "src/StudioMidiTakeRecoveryAcceptance.cpp",
                     "tests/MidiTakeRecoveryAcceptance.h", "src/StudioUiHistoryAcceptance.cpp"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue({"midi_take_recovery", "midi_take_recovery_gui", "history_ui_recovery"}
                                <= set(plan["testNames"]))
                self.assertTrue({"VibeRaTests", "VibeRa"} <= set(plan["buildTargets"]))
                self.assertFalse(plan["releaseQualification"])
        tests = selector.cmake_tests(ROOT)
        self.assertEqual(tests["midi_take_recovery"]["targets"], {"VibeRaTests"})
        self.assertEqual(tests["midi_take_recovery_gui"]["targets"], {"VibeRa"})
        self.assertEqual(tests["history_ui_recovery"]["targets"], {"VibeRa"})

    def test_retained_context_driver_selects_its_actual_native_graph_experiment(self):
        for path in ("src/RetainedContextFixture.h", "src/RetainedContextFixture.inc", "tests/DetachedGraphPluginAcceptance.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertIn("mixer_retained_context", plan["testNames"])
                self.assertFalse(plan["releaseQualification"])
                self.assertTrue({"VibeRaTests", "VibeRaMixerLatencyFixture"} <= set(plan["buildTargets"]))
        self.assertEqual(selector.cmake_tests(ROOT)["mixer_retained_context"]["targets"],
                         {"VibeRaTests", "VibeRaMixerLatencyFixture"})

    def test_fixed_initialisation_selects_real_lifecycle_and_musical_adoption(self):
        plan = self.plan("tests/FixedInitialisationLifecycleAcceptance.h")
        self.assertTrue({"fixed_initialisation_lifecycle", "mixer_retained_context"} <= set(plan["testNames"]))
        self.assertEqual(selector.cmake_tests(ROOT)["fixed_initialisation_lifecycle"]["targets"], {"VibeRaTests"})

    def test_partial_effect_and_concurrent_graph_select_actual_worker_branches(self):
        for path in ("src/RemoteEffect.h", "tests/RemoteEffectRuntimeAcceptance.h",
                     "tests/ConcurrentRetainedContextAcceptance.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertTrue({"mixer_effect_runtime", "mixer_retained_context",
                                 "mixer_concurrent_retained_context"} <= set(plan["testNames"]))
                self.assertTrue({"VibeRaTests", "VibeRaParameterFixture", "VibeRaMixerLatencyFixture"}
                                <= set(plan["buildTargets"]))
        self.assertEqual(selector.cmake_tests(ROOT)["mixer_effect_runtime"]["targets"],
                         {"VibeRaTests", "VibeRaParameterFixture", "VibeRaMixerLatencyFixture"})

    def test_structural_command_preparation_selects_real_history_oracles(self):
        plan = self.plan("tests/ControllerStructuralPreparationAcceptance.h")
        self.assertFalse(plan["full"])
        self.assertTrue({"mixer_command_preparation", "controller_prepared_history",
                         "history_destination_admission"} <= set(plan["testNames"]))
        self.assertEqual(selector.cmake_tests(ROOT)["mixer_command_preparation"]["targets"], {"VibeRaTests"})

    def test_instrument_runtime_requires_genuine_native_instrument_fixture(self):
        for path in ("tests/InstrumentRuntimeAcceptance.inc", "tests/InstrumentFixture.cpp"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertIn("mixer_instrument_runtime", plan["testNames"])
                self.assertIn("VibeRaInstrumentFixture", plan["buildTargets"])
        self.assertEqual(selector.cmake_tests(ROOT)["mixer_instrument_runtime"]["targets"],
                         {"VibeRaTests", "VibeRaInstrumentFixture"})

    def test_graph_contracts_select_actual_native_and_control_acceptance(self):
        for path in ("tests/GraphOutputRoutingAcceptance.h", "tests/AdoptedGraphLatencyAcceptance.h",
                     "src/GraphControlRuntime.h", "tests/GuideRuntimeAcceptance.inc"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertIn("mixer_structural_graph_contracts", plan["testNames"])
                self.assertIn("VibeRaTests", plan["buildTargets"])
        self.assertEqual(selector.cmake_tests(ROOT)["mixer_structural_graph_contracts"]["targets"],
                         {"VibeRaTests"})

    def test_native_stages_forward_inverse_and_ports_select_audio_and_plugins(self):
        for path in ("src/InverseStageResidual.h", "src/ForwardStageDrain.h",
                     "tests/ForwardStageDrainAcceptance.h", "tests/OutputStageDifferenceAcceptance.h",
                     "tests/StableBusStageAcceptance.h", "tests/MixerAudioPortAcceptance.h",
                     "tests/AppAudioPortMidiOwnershipAcceptance.h", "tests/NativeMidiIngressDspAcceptance.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertEqual(set(plan["categories"]), {"audio", "plugins"})
                self.assertTrue({"mixer_stopped_effect_inverse_sdk", "mixer_stopped_effect_forward_sdk",
                                 "mixer_forward_stage_drain", "mixer_output_stage_difference",
                                 "mixer_audio_port", "mixer_app_midi_ownership", "callback_timing"}
                                <= set(plan["testNames"]))
                self.assertTrue({"mixer_bounded_midi_ownership", "mixer_native_midi_ingress", "mixer_app_audio_port"}
                                <= set(plan["testNames"]))
                self.assertIn("VibeRaTests", plan["buildTargets"])
                self.assertFalse(any("Browser" in row["reason"] for row in plan["reasons"]))

    def test_native_guard_units_and_verifiers_select_conservative_full_source(self):
        for path in ("tests/test_dev_stage_difference_guard.py", "tests/test_dev_forward_guard.py",
                     "tests/test_dev_send_block_guard.py", "tests/test_dev_local_source_guard.py",
                     "scripts/verify-stage-difference-guard.py", "scripts/verify-forward-drain-guard.py",
                     "scripts/verify-summing-properties-guard.py", "scripts/verify-physical-callback-guard.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["full"])
                self.assertTrue({"audio", "plugins"} <= set(plan["categories"]))
                self.assertEqual(set(plan["testNames"]), set(selector.cmake_tests(ROOT)))
                self.assertFalse(any("Browser" in row["reason"] for row in plan["reasons"]))

    def test_genuine_browser_and_tag_words_keep_browser_contracts(self):
        for path in ("src/BrowserTags.h", "tests/BrowserTagTests.h", "src/TagIndex.h",
                     "src/tag_index.h", "src/tags.h", "src/TagsEditor.h", "src/TAG_INDEX.h",
                     "scripts/verify-browser-tags.py"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertEqual(set(plan["categories"]), {"ui", "persistence", "semantic"})
                self.assertTrue(all("Browser" in row["reason"] for row in plan["reasons"]))

    def test_stage_substrings_and_unrecognized_source_stay_conservative(self):
        for path in ("src/Backstage.cpp", "tests/StageToolsAcceptance.h", "src/StageTagger.h",
                     "src/NewUnknownSubsystem.cpp"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertTrue(plan["full"])
                self.assertEqual(set(plan["testNames"]), set(selector.cmake_tests(ROOT)))
                self.assertFalse(any("Browser" in row["reason"] for row in plan["reasons"]))

    def test_zero_history_is_native_queue_provenance_not_document_history(self):
        for path in ("tests/ZeroHistoryCreationAcceptance.h", "src/CompensationHistory.h",
                     "src/MidiCompensationHistory.h", "tests/CompensationHistoryTests.cpp",
                     "tests/MidiCompensationHistoryTests.h", "src/midi_compensation_history.h",
                     "src\\MidiCompensationHistory.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertEqual(set(plan["categories"]), {"audio", "plugins"})
                self.assertTrue({"mixer_zero_history_creation", "mixer_forward_stage_drain",
                                 "mixer_audio_port", "mixer_stopped_effect_inverse_sdk",
                                 "mixer_app_midi_ownership",
                                 "mixer_bounded_midi_ownership", "mixer_native_midi_ingress", "mixer_app_audio_port",
                                 "mixer_warm_source_relocation", "mixer_stopped_effect_native",
                                 "mixer_stopped_effect_ordinary_forward",
                                 "plugin_dry_history",
                                 "plugin_dry_history_host",
                                 "mixer_instrument_packet_boundary", "compensation_history",
                                 "bounded_midi_compensation", "mixer_dynamic_waveform"} <= set(plan["testNames"]))
                self.assertIn("VibeRaTests", plan["buildTargets"])
        # Genuine document history keeps its distinct command/UI ownership.
        document = self.plan("tests/ControllerPreparedHistoryAcceptance.h")
        self.assertEqual(set(document["categories"]), {"document", "semantic", "persistence", "ui"})

    def test_actual_control_and_instrument_graph_sources_select_their_runtime_branches(self):
        for path, expected in (("tests/ControlRuntimeAcceptance.h", "mixer_control_runtime"),
                               ("tests/RetainedWaveContextAcceptance.h", "mixer_wave_context"),
                               ("src/RetainedWaveContextFixture.inc", "mixer_wave_context"),
                               ("src/RetainedInstrumentContextFixture.inc", "mixer_instrument_context"),
                               ("tests/RetainedInstrumentContextAcceptance.h", "mixer_instrument_context")):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertIn(expected, plan["testNames"])
        self.assertEqual(selector.cmake_tests(ROOT)["mixer_instrument_context"]["targets"],
                         {"VibeRaTests", "VibeRaInstrumentFixture"})

    def test_routing_lifetime_and_actual_packet_boundaries_are_not_omitted(self):
        for path in ("src/ChannelRouting.inc", "tests/RoutingLifetimeAcceptance.h",
                     "tests/RetainedInstrumentApplicationRoutingAcceptance.h"):
            with self.subTest(path=path):
                plan = self.plan(path)
                self.assertFalse(plan["full"])
                self.assertIn("plugins", plan["categories"])
                self.assertTrue({"mixer_instrument_context", "mixer_instrument_packet_boundary",
                                 "mixer_routing_lifetime", "mixer_wave_context"} <= set(plan["testNames"]))

    def test_docs_protected_research_and_empty_selection_do_not_build(self):
        for paths in ((), ("docs/MIXER_CONTROLS.md",), ("Reference/", "evidance/", "--browser-search-check/"),
                      ("Reference/Mixer Concept.png", "evidance/autotune/report.json")):
            with self.subTest(paths=paths):
                plan = self.plan(*paths)
                self.assertEqual(plan["testNames"], [])
                self.assertEqual(plan["buildTargets"], [])
                self.assertEqual(plan["ctestRegex"], "a^")
                self.assertFalse(plan["releaseQualification"])

    def test_full_and_additional_exact_test_overrides(self):
        self.assertEqual(set(self.plan(full=True)["testNames"]), set(selector.cmake_tests(ROOT)))
        plan = self.plan("docs/STATUS.md", additional=["coproducer_context"])
        self.assertEqual(plan["testNames"], ["coproducer_context"])
        self.assertEqual(plan["buildTargets"], ["CoProducerTests"])
        with self.assertRaisesRegex(ValueError, "Unknown --additional-test"):
            self.plan(additional=["typo"])

    def test_regex_is_anchored_exact_and_handles_spaced_paths(self):
        import re
        plan = self.plan("src/My New Subsystem.cpp")
        self.assertIn("src/My New Subsystem.cpp", plan["changedFiles"])
        self.assertTrue(re.fullmatch(plan["ctestRegex"], "mcp_semantics"))
        self.assertIsNone(re.fullmatch(plan["ctestRegex"], "mcp_semantics_wrong"))

    def test_registration_drift_fails_closed(self):
        original = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory(prefix="vibera selector contracts ") as directory:
            root = Path(directory)
            cmake = root / "CMakeLists.txt"
            cmake.write_text(original.replace("NAME callback_timing ", "NAME renamed_callback_timing "), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Required test registrations missing"):
                selector.select(root, [], full=True)
            cmake.write_text(original + '\nadd_test(NAME "unsupported quoted name" COMMAND python check.py)\n', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unsupported/duplicate"):
                selector.cmake_tests(root)

    def test_git_staged_unstaged_new_and_rename_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="vibera selector ") as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.run(["git", "-c", f"safe.directory={root.as_posix()}", *args],
                                      cwd=root, check=True, capture_output=True)
            git("init")
            for name in ("staged.cpp", "unstaged.cpp", "old name.cpp", "both.cpp"):
                (root / name).write_text("baseline\n", encoding="utf-8")
            git("add", ".")
            git("-c", "user.name=Selector Test", "-c", "user.email=selector@example.invalid",
                "commit", "-m", "owned disposable fixture")
            (root / "staged.cpp").write_text("staged\n", encoding="utf-8")
            (root / "both.cpp").write_text("staged\n", encoding="utf-8")
            git("add", "staged.cpp", "both.cpp")
            (root / "unstaged.cpp").write_text("unstaged\n", encoding="utf-8")
            (root / "both.cpp").write_text("staged and unstaged\n", encoding="utf-8")
            git("mv", "old name.cpp", "renamed with spaces.cpp")
            (root / "new with spaces.cpp").write_text("new\n", encoding="utf-8")
            (root / "Reference").mkdir()
            (root / "Reference" / "do not enumerate.png").write_bytes(b"fixture")
            changes = selector.git_changes(root)
            statuses = {x["path"]: x["status"] for x in changes}
            self.assertEqual(statuses["staged.cpp"], "M ")
            self.assertEqual(statuses["unstaged.cpp"], " M")
            self.assertEqual(statuses["both.cpp"], "MM")
            self.assertEqual(statuses["new with spaces.cpp"], "??")
            self.assertIn("old name.cpp", statuses)
            self.assertIn("renamed with spaces.cpp", statuses)
            self.assertIn("Reference/", statuses)
            self.assertNotIn("Reference/do not enumerate.png", statuses)


if __name__ == "__main__":
    unittest.main()
