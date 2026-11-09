cmake_minimum_required(VERSION 3.25)
project(VibeRa VERSION 0.2.1 LANGUAGES C CXX)
configure_file(src/VibeRaVersion.h.in generated/VibeRaVersion.h @ONLY)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(TE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/source/tracktion_engine-4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86" CACHE PATH "Pinned engine source")
set(JUCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/source/JUCE-37c894f83d379179b2070d437ccd0f1cd9af9576" CACHE PATH "Pinned JUCE source")
if(NOT EXISTS "${TE_ROOT}/modules/CMakeLists.txt" OR NOT EXISTS "${JUCE_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "Fetch pinned dependencies using scripts/fetch-dependencies.ps1 first")
endif()
add_subdirectory("${JUCE_ROOT}" juce)
add_subdirectory("${TE_ROOT}/modules" tracktion)
include(cmake/TracktionImplementation.cmake)
add_library(VibeRaCore STATIC src/NoteSlice.cpp src/NoteStretch.cpp src/MixerRouting.cpp src/Project.cpp src/Commands.cpp src/AudioEngine.cpp src/AudioInputCapture.cpp src/AudioStretch.cpp src/VocalPitch.cpp src/FlpImport.cpp src/MtsEsp.cpp src/third_party/mts-esp/libMTSMaster.cpp src/Customization.cpp src/Groove.cpp src/SampleCache.cpp src/LiveTimeline.cpp src/Automation.cpp src/AutomationState.cpp src/AutomationCommands.cpp src/PluginAutomationBlock.cpp src/PluginBinding.cpp src/PluginStateEncoding.cpp src/PluginHost.cpp src/PluginCatalog.cpp src/WorkerProcess.cpp src/MidiInput.cpp src/MidiController.cpp src/CoProducer.cpp src/CoProducerWorker.cpp)
# Signalsmith Stretch (MIT) is vendored with Tracktion; used offline for Audio Clip stretching.
target_include_directories(VibeRaCore PRIVATE "${TE_ROOT}/modules/tracktion_engine/3rd_party/signalsmith-stretch")
target_sources(VibeRaCore PRIVATE src/synth/SynthPresets.cpp)

target_include_directories(VibeRaCore PUBLIC src)
# MIT-licensed pitch-preserving stretcher bundled with the pinned engine (header-only).
target_include_directories(VibeRaCore PRIVATE "${TE_ROOT}/modules/tracktion_engine/3rd_party")
target_sources(VibeRaCore PRIVATE src/StoppedEffectWorkers.cpp src/PluginAutomationPacket.cpp src/PluginAutomationStream.cpp src/PluginAutomationPreparation.cpp src/PluginEditorClock.cpp src/WindowForeground.cpp)
target_sources(VibeRaCore PRIVATE src/EffectChain.cpp src/EffectChainPresetFiles.cpp src/ChannelPreset.cpp)
target_sources(VibeRaCore PRIVATE src/ChannelRoutingEffects.cpp)
target_sources(VibeRaCore PRIVATE src/ai/AiSettings.cpp src/ai/AiHttp.cpp src/ai/LlmClient.cpp src/ai/MusicEngines.cpp src/ai/ProducerAgent.cpp src/ai/McpLoopback.cpp src/ai/CodexAgent.cpp src/ai/ClaudeCli.cpp)
set(VIBERA_USE_CURL 0)
if(UNIX AND NOT APPLE)
  set(VIBERA_USE_CURL 1)
endif()
target_compile_definitions(VibeRaCore PUBLIC
  JUCE_USE_LAME_AUDIO_FORMAT=1
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=${VIBERA_USE_CURL} JUCE_MODAL_LOOPS_PERMITTED=0
  JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0
  JUCE_PLUGINHOST_LADSPA=0 JUCE_ASIO=0 JUCE_WASAPI=1
  TRACKTION_ENABLE_REWIRE=0 TRACKTION_ENABLE_AUTOMAP=0
  TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=0 TRACKTION_ENABLE_REX=0
  TRACKTION_ENABLE_ARA=0 TRACKTION_ENABLE_CMAJOR=0 TRACKTION_ENABLE_VIDEO=0)
target_link_libraries(VibeRaCore PUBLIC tracktion::tracktion_engine tracktion::tracktion_graph
  tracktion::tracktion_core juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography
  juce::juce_javascript) # Piano roll scripts (JUCE's QuickJS engine, vendored).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Static Core consumers need JUCE HTTPS and Tracktion's non-inline atomics.
  # Reuse JUCE's pkg-config curl target so curl's own link dependencies follow.
  target_link_libraries(VibeRaCore PUBLIC juce::pkgconfig_JUCE_CURL_LINUX_DEPS atomic)
endif()
if(WIN32)
  target_link_libraries(VibeRaCore PUBLIC dwmapi)
endif()
if(MSVC)
  # Core now owns Tracktion implementation objects. Preserve the Release crash
  # symbols previously supplied by the app/tests' own /Zi module copies.
  target_compile_options(VibeRaCore PRIVATE /W4 /bigobj $<$<CONFIG:Release>:/Zi>)
endif()
juce_add_gui_app(VibeRa PRODUCT_NAME "VibeRa Studio" COMPANY_NAME "VibeRa" VERSION "${PROJECT_VERSION}" ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/vibera-symbol-512.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/vibera-symbol-64.png")
juce_add_binary_data(VibeRaBranding SOURCES assets/vibera-symbol.svg)
target_sources(VibeRa PRIVATE src/Main.cpp src/Studio.cpp src/StudioMcp.cpp src/StudioMidi.cpp src/StudioSampler.cpp src/StudioController.cpp src/StudioAutomation.cpp src/StudioPluginParameters.cpp src/StudioBrowser.cpp src/StudioAudioTake.cpp src/StudioDrumPads.cpp src/StudioChords.cpp src/StudioDropCheck.cpp src/StudioAutosave.cpp src/StudioDiagnostics.cpp src/StudioLoudness.cpp src/StudioVocalPitch.cpp src/StudioUpdater.cpp src/StudioAudioTakeAcceptance.cpp src/VideoPlayer.cpp src/MidiClipboard.cpp)
target_include_directories(VibeRa PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_sources(VibeRa PRIVATE src/StudioPicker.cpp)

target_sources(VibeRa PRIVATE src/StudioMidiTakeRecoveryAcceptance.cpp src/StudioUiHistoryAcceptance.cpp src/StudioPluginSaveCaptureAcceptance.cpp)
target_sources(VibeRa PRIVATE src/StudioMixerChainWorkflowAcceptance.cpp)
target_sources(VibeRa PRIVATE src/StudioRack.cpp)
target_sources(VibeRa PRIVATE src/StudioSteps.cpp)
target_sources(VibeRa PRIVATE src/StudioStepCompose.cpp)
target_sources(VibeRa PRIVATE src/StudioSolo.cpp)
target_sources(VibeRa PRIVATE src/StudioChannels.cpp src/StudioAi.cpp)
target_sources(VibeRa PRIVATE src/StudioRackVelocity.cpp)
target_sources(VibeRa PRIVATE src/StudioSwing.cpp)
target_sources(VibeRa PRIVATE src/StudioQuantizer.cpp)
target_sources(VibeRa PRIVATE src/StudioClipGroups.cpp src/StudioNoteMute.cpp src/StudioNoteSlice.cpp src/StudioNoteStretch.cpp src/StudioMixer.cpp src/StudioMixerControls.cpp src/PianoModifiers.cpp)
target_link_libraries(VibeRa PRIVATE VibeRaCore VibeRaBranding)
if(MSVC)
  # Keep optimized native UI crash symbols in the one compilation cache.
  # They are diagnostics, never another runnable distribution or bundled PDB.
  target_compile_options(VibeRa PRIVATE $<$<CONFIG:Release>:/Zi>)
  target_link_options(VibeRa PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
add_custom_target(VibeRaCoProducerRuntime COMMAND ${CMAKE_COMMAND} -E make_directory
  "$<TARGET_FILE_DIR:VibeRa>/CoProducer")
foreach(providerFile model-worker.cjs magenta-runtime.cjs package.json package-lock.json fetch-magenta.mjs anticipation-worker.py anticipation_context.py)
  add_custom_command(TARGET VibeRaCoProducerRuntime POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/coproducer/${providerFile}" "$<TARGET_FILE_DIR:VibeRa>/CoProducer/${providerFile}")
endforeach()
add_dependencies(VibeRa VibeRaCoProducerRuntime)
juce_add_console_app(VibeRaTests PRODUCT_NAME "VibeRa acceptance tests")
if(MSVC)
  target_compile_options(VibeRaTests PRIVATE $<$<CONFIG:Release>:/Zi>)
  target_link_options(VibeRaTests PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()
target_sources(VibeRaTests PRIVATE tests/Acceptance.cpp)
target_link_libraries(VibeRaTests PRIVATE VibeRaCore)
juce_add_console_app(VibeRaScannerFixture PRODUCT_NAME "VibeRa scanner containment fixture")
add_executable(VibeRaCompensationTests tests/CompensationHistoryTests.cpp)
target_include_directories(VibeRaCompensationTests PRIVATE src)

# Pure history lifetime/fault checks need the real manager, not the DAW graph.
juce_add_console_app(VibeRaHistoryTests PRODUCT_NAME "VibeRa history acceptance")
target_sources(VibeRaHistoryTests PRIVATE tests/PreparedHistoryTests.cpp)
target_compile_definitions(VibeRaHistoryTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(VibeRaHistoryTests PRIVATE juce::juce_data_structures)
target_sources(VibeRaScannerFixture PRIVATE tests/ScannerFixture.cpp src/WorkerProcess.cpp)
target_include_directories(VibeRaScannerFixture PRIVATE src)
# The containment fixture needs native process ownership and PluginDescription,
# not the DAW/audio graph or GUI. Keep the actual shared WorkerProcess code.
target_compile_definitions(VibeRaScannerFixture PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0
  JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0
  JUCE_PLUGINHOST_LADSPA=0 JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_ARA=0)
target_link_libraries(VibeRaScannerFixture PRIVATE juce::juce_audio_processors_headless)
add_dependencies(VibeRaTests VibeRaScannerFixture)
add_custom_command(TARGET VibeRaTests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "$<TARGET_FILE:VibeRaScannerFixture>" "$<TARGET_FILE_DIR:VibeRaTests>/$<TARGET_FILE_NAME:VibeRaScannerFixture>")
juce_add_gui_app(VibeRaPluginWorker PRODUCT_NAME "VibeRa Plugin Worker" COMPANY_NAME "VibeRa")
target_sources(VibeRaPluginWorker PRIVATE src/PluginWorker.cpp src/PluginEditorClock.cpp src/WindowForeground.cpp)
target_sources(VibeRaPluginWorker PRIVATE src/effects/NativeEffects.cpp src/effects/NativeEffectsEditor.cpp
  src/effects/dsp/Dynamics.cpp src/effects/dsp/TimeEffects.cpp src/effects/dsp/PitchCorrect.cpp src/effects/dsp/Vocoder.cpp src/effects/dsp/StudioEffects.cpp)
target_sources(VibeRaPluginWorker PRIVATE src/synth/SynthPlugin.cpp src/synth/SynthPresets.cpp
  src/synth/SynthEngine.cpp src/synth/SynthEditor.cpp)
target_compile_definitions(VibeRaPluginWorker PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LADSPA=0 JUCE_ASIO=0)
target_link_libraries(VibeRaPluginWorker PRIVATE juce::juce_audio_utils juce::juce_cryptography)
if(WIN32)
  target_link_libraries(VibeRaPluginWorker PRIVATE dwmapi)
endif()
if(MSVC)
  target_compile_options(VibeRaPluginWorker PRIVATE /bigobj)
endif()
foreach(host VibeRa VibeRaTests)
  add_dependencies(${host} VibeRaPluginWorker)
  add_custom_command(TARGET ${host} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:VibeRaPluginWorker>" "$<TARGET_FILE_DIR:${host}>/$<TARGET_FILE_NAME:VibeRaPluginWorker>")
endforeach()
enable_testing()
add_executable(VibeRaNumberTextTests tests/NumberTextTests.cpp)
target_include_directories(VibeRaNumberTextTests PRIVATE src)
add_test(NAME project_number_text COMMAND VibeRaNumberTextTests)
set_tests_properties(project_number_text PROPERTIES TIMEOUT 30)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(VibeRaEffectsDspTests tests/effects/DspTests.cpp
  src/effects/dsp/Dynamics.cpp src/effects/dsp/TimeEffects.cpp src/effects/dsp/PitchCorrect.cpp src/effects/dsp/Vocoder.cpp src/effects/dsp/StudioEffects.cpp)
target_include_directories(VibeRaEffectsDspTests PRIVATE src/effects tests/effects)
juce_add_console_app(VibeRaNativeEffectsTests PRODUCT_NAME "VibeRa native effects acceptance")
target_sources(VibeRaNativeEffectsTests PRIVATE tests/NativeEffectsTests.cpp src/synth/SynthPresets.cpp
  src/effects/NativeEffects.cpp src/effects/NativeEffectsEditor.cpp
  src/effects/dsp/Dynamics.cpp src/effects/dsp/TimeEffects.cpp src/effects/dsp/PitchCorrect.cpp src/effects/dsp/Vocoder.cpp src/effects/dsp/StudioEffects.cpp)
target_include_directories(VibeRaNativeEffectsTests PRIVATE src tests/effects)
target_compile_definitions(VibeRaNativeEffectsTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(VibeRaNativeEffectsTests PRIVATE juce::juce_audio_utils)
add_test(NAME vibera_effects_dsp COMMAND VibeRaEffectsDspTests)
set_tests_properties(vibera_effects_dsp PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME vibera_native_effects COMMAND VibeRaNativeEffectsTests
  "${CMAKE_CURRENT_BINARY_DIR}/native-effects-acceptance")
set_tests_properties(vibera_native_effects PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME vibera_native_effect_model COMMAND VibeRaTests --native-effect-model-check)
set_tests_properties(vibera_native_effect_model PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME vibera_native_effects_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-native-effects-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/native-effects-mcp")
set_tests_properties(vibera_native_effects_mcp PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME module_release_policy COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/module-gate.py" --self-test)
set_tests_properties(module_release_policy PROPERTIES TIMEOUT 15)
add_test(NAME note_stretch_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-note-stretch-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/note_stretch_mcp")
add_test(NAME mixer_controls_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-mixer-controls-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/mixer_controls_mcp")
set_tests_properties(mixer_controls_mcp PROPERTIES TIMEOUT 90)
add_test(NAME mixer_controls_gui COMMAND VibeRa --mixer-controls-ui-check "${CMAKE_CURRENT_BINARY_DIR}/mixer_controls_gui")
set_tests_properties(mixer_controls_gui PROPERTIES TIMEOUT 30)
add_test(NAME mixer_effect_slots_gui COMMAND VibeRa --mixer-effects-ui-check "${CMAKE_CURRENT_BINARY_DIR}/mixer_effect_slots_gui")
set_tests_properties(mixer_effect_slots_gui PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_effect_slots_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-mixer-effects-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/mixer_effect_slots_mcp")
set_tests_properties(mixer_effect_slots_mcp PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_effects_verifier COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_mixer_effects_verifier.py)
set_tests_properties(mixer_effects_verifier PROPERTIES TIMEOUT 15)
add_test(NAME mixer_effect_opening COMMAND VibeRa --mixer-effects-opening-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_effect_opening" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_effect_opening PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_effect_opening_unsupported COMMAND VibeRa --mixer-effects-opening-unsupported-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_effect_opening_unsupported" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_effect_opening_unsupported PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_controls_audio COMMAND VibeRaTests --mixer-controls-check "${CMAKE_CURRENT_BINARY_DIR}/mixer_controls_audio")
set_tests_properties(mixer_controls_audio PROPERTIES TIMEOUT 90)
add_test(NAME mixer_routing_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-mixer-routing-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/mixer_routing_mcp")
set_tests_properties(mixer_routing_mcp PROPERTIES TIMEOUT 90)
add_test(NAME mixer_routing_gui COMMAND VibeRa --mixer-routing-ui-check "${CMAKE_CURRENT_BINARY_DIR}/mixer_routing_gui")
set_tests_properties(mixer_routing_gui PROPERTIES TIMEOUT 30)
set_tests_properties(note_stretch_mcp PROPERTIES TIMEOUT 90)
add_test(NAME note_stretch_gui COMMAND VibeRa --note-stretch-ui-check "${CMAKE_CURRENT_BINARY_DIR}/note_stretch_gui")
set_tests_properties(note_stretch_gui PROPERTIES TIMEOUT 30)
add_test(NAME note_slice_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-note-slice-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/note_slice_mcp")
set_tests_properties(note_slice_mcp PROPERTIES TIMEOUT 90)
add_test(NAME note_slice_gui COMMAND VibeRa --note-slice-ui-check "${CMAKE_CURRENT_BINARY_DIR}/note_slice_gui")
set_tests_properties(note_slice_gui PROPERTIES TIMEOUT 30)
add_test(NAME note_mute_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-note-mute-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/note_mute_mcp")
set_tests_properties(note_mute_mcp PROPERTIES TIMEOUT 90)
add_test(NAME note_mute_gui COMMAND VibeRa --note-mute-ui-check "${CMAKE_CURRENT_BINARY_DIR}/note_mute_gui")
set_tests_properties(note_mute_gui PROPERTIES TIMEOUT 30)
add_test(NAME clip_groups_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-clip-groups-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/clip_groups_mcp_acceptance")
set_tests_properties(clip_groups_mcp PROPERTIES TIMEOUT 90)
add_test(NAME drop_targets_gui COMMAND VibeRa --drop-targets-check "${CMAKE_CURRENT_BINARY_DIR}/drop_targets_gui")
set_tests_properties(drop_targets_gui PROPERTIES TIMEOUT 120)
add_test(NAME clip_groups_gui COMMAND VibeRa --clip-groups-ui-check "${CMAKE_CURRENT_BINARY_DIR}/clip_groups_gui")
set_tests_properties(clip_groups_gui PROPERTIES TIMEOUT 30)
add_test(NAME mcp_semantics COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mcp_acceptance.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/mcp_acceptance" "${PROJECT_VERSION}")
set_tests_properties(mcp_semantics PROPERTIES TIMEOUT 120)
add_test(NAME rack_groups_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-rack-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/rack_mcp_acceptance")
set_tests_properties(rack_groups_mcp PROPERTIES TIMEOUT 90)
add_test(NAME step_fill_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-step-fill-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/step_fill_mcp_acceptance")
set_tests_properties(step_fill_mcp PROPERTIES TIMEOUT 90)
add_test(NAME channel_solo_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-channel-solo-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/channel_solo_mcp_acceptance")
set_tests_properties(channel_solo_mcp PROPERTIES TIMEOUT 90)
add_test(NAME channel_scoped_solo_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-channel-scoped-solo-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/channel_scoped_solo_mcp_acceptance")
set_tests_properties(channel_scoped_solo_mcp PROPERTIES TIMEOUT 90)
add_test(NAME channel_management_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-channel-management-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/channel_management_mcp_acceptance")
set_tests_properties(channel_management_mcp PROPERTIES TIMEOUT 90)
add_test(NAME channel_appearance_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-channel-appearance-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/channel_appearance_mcp_acceptance")
set_tests_properties(channel_appearance_mcp PROPERTIES TIMEOUT 90)
add_test(NAME step_velocity_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-step-velocity-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/step_velocity_mcp_acceptance")
set_tests_properties(step_velocity_mcp PROPERTIES TIMEOUT 90)
add_test(NAME swing_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-swing-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/swing_mcp_acceptance")
set_tests_properties(swing_mcp PROPERTIES TIMEOUT 90)
add_test(NAME quantizer_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-quantizer-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/quantizer_mcp_acceptance")
set_tests_properties(quantizer_mcp PROPERTIES TIMEOUT 90)
add_test(NAME groove_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-groove-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/groove_mcp_acceptance")
set_tests_properties(groove_mcp PROPERTIES TIMEOUT 90)
add_test(NAME sampler_voices_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-voices-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_voices_mcp_acceptance")
set_tests_properties(sampler_voices_mcp PROPERTIES TIMEOUT 90)
add_test(NAME sampler_filter COMMAND VibeRaTests --sampler-filter-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_filter")
set_tests_properties(sampler_filter PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME sampler_filter_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-filter-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_filter_mcp")
set_tests_properties(sampler_filter_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME sampler_modulation COMMAND VibeRaTests --sampler-modulation-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_modulation")
set_tests_properties(sampler_modulation PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME sampler_modulation_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-modulation-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_modulation_mcp")
set_tests_properties(sampler_modulation_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME sampler_controller_links COMMAND VibeRaTests --sampler-controller-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_controller_links")
set_tests_properties(sampler_controller_links PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME sampler_controller_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-controller-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_controller_mcp")
set_tests_properties(sampler_controller_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME sampler_note_modes COMMAND VibeRaTests --sampler-note-modes-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_note_modes")
set_tests_properties(sampler_note_modes PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME ai_producer_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-ai-producer-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/ai_producer_mcp")
set_tests_properties(ai_producer_mcp PROPERTIES TIMEOUT 300)
add_test(NAME ai_codex_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-ai-codex-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/ai_codex_mcp")
set_tests_properties(ai_codex_mcp PROPERTIES TIMEOUT 300)
add_test(NAME installer_package COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-installer.py" "${CMAKE_CURRENT_BINARY_DIR}/installer_package")
set_tests_properties(installer_package PROPERTIES TIMEOUT 600)
add_test(NAME export_formats_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-export-formats-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/export_formats_mcp")
set_tests_properties(export_formats_mcp PROPERTIES TIMEOUT 300)
add_test(NAME audio_recording_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-audio-recording-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/audio_recording_mcp")
set_tests_properties(audio_recording_mcp PROPERTIES TIMEOUT 120)
add_test(NAME tempo_warp_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-tempo-warp-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/tempo_warp_mcp")
set_tests_properties(tempo_warp_mcp PROPERTIES TIMEOUT 180)
add_test(NAME ai_claude_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-ai-claude-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/ai_claude_mcp")
set_tests_properties(ai_claude_mcp PROPERTIES TIMEOUT 300)
add_test(NAME ai_ui_gui COMMAND VibeRa --ai-ui-check "${CMAKE_CURRENT_BINARY_DIR}/ai_ui_gui")
set_tests_properties(ai_ui_gui PROPERTIES TIMEOUT 60)
add_test(NAME ai_model COMMAND VibeRaTests --ai-check "${CMAKE_CURRENT_BINARY_DIR}/ai_model")
set_tests_properties(ai_model PROPERTIES TIMEOUT 60)
add_test(NAME live_burn_gui COMMAND VibeRa --live-burn-check
  "${CMAKE_CURRENT_BINARY_DIR}/live_burn_gui")
set_tests_properties(live_burn_gui PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME slicer COMMAND VibeRaTests --slicer-check
  "${CMAKE_CURRENT_BINARY_DIR}/slicer")
set_tests_properties(slicer PROPERTIES TIMEOUT 60)
add_test(NAME arrangements COMMAND VibeRaTests --arrangements-check
  "${CMAKE_CURRENT_BINARY_DIR}/arrangements")
set_tests_properties(arrangements PROPERTIES TIMEOUT 60)
add_test(NAME playlist_tracks COMMAND VibeRaTests --playlist-tracks-check
  "${CMAKE_CURRENT_BINARY_DIR}/playlist_tracks")
set_tests_properties(playlist_tracks PROPERTIES TIMEOUT 60)
add_test(NAME midi_import_extras COMMAND VibeRaTests --midi-import-extras-check
  "${CMAKE_CURRENT_BINARY_DIR}/midi_import_extras")
set_tests_properties(midi_import_extras PROPERTIES TIMEOUT 60)
add_test(NAME missing_media COMMAND VibeRaTests --missing-media-check
  "${CMAKE_CURRENT_BINARY_DIR}/missing_media")
set_tests_properties(missing_media PROPERTIES TIMEOUT 60)
add_test(NAME sampler_pitch_bend COMMAND VibeRaTests --sampler-pitch-bend-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_pitch_bend")
set_tests_properties(sampler_pitch_bend PROPERTIES TIMEOUT 120)
add_test(NAME song_meter COMMAND VibeRaTests --song-meter-check
  "${CMAKE_CURRENT_BINARY_DIR}/song_meter")
set_tests_properties(song_meter PROPERTIES TIMEOUT 60)
add_test(NAME audio_stretch COMMAND VibeRaTests --audio-stretch-check
  "${CMAKE_CURRENT_BINARY_DIR}/audio_stretch")
set_tests_properties(audio_stretch PROPERTIES TIMEOUT 120)
add_test(NAME tuning_render_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-tuning-render.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/tuning_render_mcp")
set_tests_properties(tuning_render_mcp PROPERTIES TIMEOUT 120)
add_test(NAME channel_tuning COMMAND VibeRaTests --channel-tuning-check
  "${CMAKE_CURRENT_BINARY_DIR}/channel_tuning")
set_tests_properties(channel_tuning PROPERTIES TIMEOUT 60)
add_test(NAME controller_recording COMMAND VibeRaTests --controller-recording-check
  "${CMAKE_CURRENT_BINARY_DIR}/controller_recording")
set_tests_properties(controller_recording PROPERTIES TIMEOUT 60)
add_test(NAME chord_progression COMMAND VibeRaTests --chord-progression-check)
set_tests_properties(chord_progression PROPERTIES TIMEOUT 60)
add_test(NAME flp_import COMMAND VibeRaTests --flp-import-check
  "${CMAKE_CURRENT_BINARY_DIR}/flp_import")
set_tests_properties(flp_import PROPERTIES TIMEOUT 60)
add_test(NAME tempo_map COMMAND VibeRaTests --tempo-map-check
  "${CMAKE_CURRENT_BINARY_DIR}/tempo_map")
set_tests_properties(tempo_map PROPERTIES TIMEOUT 120)
add_test(NAME playlist_markers COMMAND VibeRaTests --playlist-markers-check
  "${CMAKE_CURRENT_BINARY_DIR}/playlist_markers")
set_tests_properties(playlist_markers PROPERTIES TIMEOUT 60)
add_test(NAME composition_tools COMMAND VibeRaTests --composition-tools-check
  "${CMAKE_CURRENT_BINARY_DIR}/composition_tools")
set_tests_properties(composition_tools PROPERTIES TIMEOUT 60)
add_test(NAME audio_capture COMMAND VibeRaTests --audio-capture-check
  "${CMAKE_CURRENT_BINARY_DIR}/audio_capture")
set_tests_properties(audio_capture PROPERTIES TIMEOUT 60)
add_test(NAME sampler_fade COMMAND VibeRaTests --sampler-fade-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_fade")
set_tests_properties(sampler_fade PROPERTIES TIMEOUT 60)
add_test(NAME pattern_management COMMAND VibeRaTests --pattern-management-check
  "${CMAKE_CURRENT_BINARY_DIR}/pattern_management")
set_tests_properties(pattern_management PROPERTIES TIMEOUT 60)
add_test(NAME channel_preset COMMAND VibeRaTests --channel-preset-check
  "${CMAKE_CURRENT_BINARY_DIR}/channel_preset")
set_tests_properties(channel_preset PROPERTIES TIMEOUT 60)
add_test(NAME piano_note_modes_gui COMMAND VibeRa --piano-note-modes-check
  "${CMAKE_CURRENT_BINARY_DIR}/piano_note_modes_gui")
set_tests_properties(piano_note_modes_gui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME rps_gaps_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-rps-gaps-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/rps_gaps_mcp")
set_tests_properties(rps_gaps_mcp PROPERTIES TIMEOUT 120)
add_test(NAME sampler_note_modes_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-note-modes-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_note_modes_mcp")
set_tests_properties(sampler_note_modes_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME sampler_voice_modes COMMAND VibeRaTests --sampler-voice-modes-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_voice_modes")
set_tests_properties(sampler_voice_modes PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME sampler_voice_modes_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-voice-modes-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_voice_modes_mcp")
set_tests_properties(sampler_voice_modes_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME sampler_envelope COMMAND VibeRaTests --sampler-envelope-check
  "${CMAKE_CURRENT_BINARY_DIR}/sampler_envelope")
set_tests_properties(sampler_envelope PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME sampler_envelope_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-envelope-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_envelope_mcp")
set_tests_properties(sampler_envelope_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME sampler_automation_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-automation-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_automation_mcp")
set_tests_properties(sampler_automation_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME sampler_tempo_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-sampler-tempo-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/sampler_tempo_mcp_acceptance")
set_tests_properties(sampler_tempo_mcp PROPERTIES TIMEOUT 90)
add_test(NAME loop_bounds_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-loop-bounds-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/loop_bounds_mcp_acceptance")
set_tests_properties(loop_bounds_mcp PROPERTIES TIMEOUT 90)
add_test(NAME normalize_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-normalize-mcp.py" "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/normalize_mcp_acceptance")
set_tests_properties(normalize_mcp PROPERTIES TIMEOUT 90)
add_test(NAME mcp_coverage_detector COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mcp-coverage.py" --self-test)
set_tests_properties(mcp_coverage_detector PROPERTIES TIMEOUT 15)
if(WIN32)
  # Owned-process diagnostic helper; never part of the runnable distribution.
  add_executable(VibeRaCallbackProbe tests/CallbackStallSupervisor.cpp)
  target_link_libraries(VibeRaCallbackProbe PRIVATE dbghelp)
  if(MSVC)
    target_compile_options(VibeRaCallbackProbe PRIVATE /W4 $<$<CONFIG:Release>:/Zi>)
    target_link_options(VibeRaCallbackProbe PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>)
  endif()
endif()
set(VST3_SDK_ROOT "${JUCE_ROOT}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
target_include_directories(VibeRaPluginWorker PRIVATE "${VST3_SDK_ROOT}")
add_library(VibeRaInstrumentFixture MODULE tests/InstrumentFixture.cpp)
target_include_directories(VibeRaInstrumentFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaInstrumentFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
add_dependencies(VibeRaTests VibeRaInstrumentFixture)
add_library(VibeRaReleaseInstrumentFixture MODULE tests/ReleaseInstrumentFixture.cpp)
target_include_directories(VibeRaReleaseInstrumentFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaReleaseInstrumentFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
add_dependencies(VibeRaTests VibeRaReleaseInstrumentFixture)
add_test(NAME note_release_worker COMMAND VibeRaTests --note-release-worker-check
  "${CMAKE_CURRENT_BINARY_DIR}/note_release_worker" "$<TARGET_FILE:VibeRaReleaseInstrumentFixture>")
set_tests_properties(note_release_worker PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_library(VibeRaDynamicInstrumentFixture MODULE tests/DynamicInstrumentFixture.cpp)
target_include_directories(VibeRaDynamicInstrumentFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaDynamicInstrumentFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
add_dependencies(VibeRaTests VibeRaDynamicInstrumentFixture)
add_test(NAME mixer_dynamic_instrument_latency COMMAND VibeRaTests --dynamic-instrument-latency-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_dynamic_instrument_latency" "$<TARGET_FILE:VibeRaDynamicInstrumentFixture>")
set_tests_properties(mixer_dynamic_instrument_latency PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_instrument_runtime COMMAND VibeRaTests --instrument-runtime-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_instrument_runtime" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_instrument_runtime PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_library(VibeRaBusChoiceFixture MODULE tests/NativeBusChoiceFixture.cpp)
target_include_directories(VibeRaBusChoiceFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaBusChoiceFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
add_dependencies(VibeRaTests VibeRaBusChoiceFixture)
if(WIN32)
  add_dependencies(VibeRa VibeRaBusChoiceFixture)
endif()
add_test(NAME plugin_main_io_model COMMAND VibeRaTests --plugin-main-io-model-check)
set_tests_properties(plugin_main_io_model PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME plugin_main_bus_choice COMMAND VibeRaTests --plugin-bus-choice-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_main_bus_choice" "$<TARGET_FILE:VibeRaBusChoiceFixture>")
set_tests_properties(plugin_main_bus_choice PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME plugin_main_io_application COMMAND VibeRa --plugin-main-io-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_main_io_application" "$<TARGET_FILE:VibeRaBusChoiceFixture>")
set_tests_properties(plugin_main_io_application PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME plugin_io_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-plugin-io-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_io_mcp" "$<TARGET_FILE:VibeRaBusChoiceFixture>")
set_tests_properties(plugin_io_mcp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_library(VibeRaMultiOutputFixture MODULE tests/MultiOutputInstrumentFixture.cpp)
target_include_directories(VibeRaMultiOutputFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaMultiOutputFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
if(WIN32)
  add_dependencies(VibeRa VibeRaMultiOutputFixture)
endif()
add_test(NAME multi_output_routing_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-multi-output-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/multi_output_routing_mcp" "$<TARGET_FILE:VibeRaMultiOutputFixture>")
set_tests_properties(multi_output_routing_mcp PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
add_library(VibeRaMixerLatencyFixture MODULE tests/MixerLatencyFixture.cpp)
target_include_directories(VibeRaMixerLatencyFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaMixerLatencyFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
add_dependencies(VibeRaTests VibeRaMixerLatencyFixture)
if(WIN32)
  add_dependencies(VibeRa VibeRaMixerLatencyFixture)
endif()
add_test(NAME plugin_latency_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-plugin-latency-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_latency_mcp" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(plugin_latency_mcp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME mixer_latency_risk COMMAND VibeRaTests --mixer-risk-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_latency_risk" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_latency_risk PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME mixer_sidechain_transport COMMAND VibeRaTests --sidechain-transport-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_sidechain_transport" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_sidechain_transport PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_sidechain_graph COMMAND VibeRaTests --sidechain-graph-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_sidechain_graph" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_sidechain_graph PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_dynamic_latency COMMAND VibeRaTests --dynamic-latency-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_dynamic_latency" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_dynamic_latency PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_default_dynamic_latency COMMAND VibeRaTests --default-dynamic-latency-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_default_dynamic_latency" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_default_dynamic_latency PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_library(VibeRaClockWaveformFixture MODULE tests/ClockWaveformFixture.cpp)
target_include_directories(VibeRaClockWaveformFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaClockWaveformFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
add_dependencies(VibeRaTests VibeRaClockWaveformFixture)
add_test(NAME mixer_dynamic_waveform COMMAND VibeRaTests --dynamic-waveform-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_dynamic_waveform"
  "$<TARGET_FILE:VibeRaClockWaveformFixture>" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_dynamic_waveform PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_stream_pcm COMMAND VibeRaTests --plugin-stream-pcm-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stream_pcm"
  "$<TARGET_FILE:VibeRaParameterFixture>" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_stream_pcm PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_fixed_source_pcm_epoch COMMAND VibeRaTests --fixed-source-pcm-epoch-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_fixed_source_pcm_epoch")
set_tests_properties(mixer_fixed_source_pcm_epoch PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
add_test(NAME mixer_source_render_time COMMAND VibeRaTests --source-render-time-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_source_render_time")
set_tests_properties(mixer_source_render_time PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
add_test(NAME mixer_initial_source_epoch_fence COMMAND VibeRaTests --initial-source-epoch-fence-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_initial_source_epoch_fence" "$<TARGET_FILE:VibeRaDynamicInstrumentFixture>")
set_tests_properties(mixer_initial_source_epoch_fence PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_initial_stable_track_midi COMMAND VibeRaTests --initial-stable-track-midi-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_initial_stable_track_midi" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_initial_stable_track_midi PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME plugin_editor_requests COMMAND VibeRaTests --plugin-editor-requests-check)
set_tests_properties(plugin_editor_requests PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
add_test(NAME plugin_state_admission COMMAND VibeRaTests --plugin-state-admission-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_state_admission" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(plugin_state_admission PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME plugin_editor_attachment COMMAND VibeRaTests --plugin-editor-attachment-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_editor_attachment" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(plugin_editor_attachment PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME plugin_editor_deadline_timely COMMAND VibeRaTests --plugin-editor-deadline-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_editor_deadline_timely" "$<TARGET_FILE:VibeRaParameterFixture>" timely)
add_test(NAME plugin_editor_deadline_late COMMAND VibeRaTests --plugin-editor-deadline-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_editor_deadline_late" "$<TARGET_FILE:VibeRaParameterFixture>" late)
add_test(NAME plugin_editor_deadline_timeout COMMAND VibeRaTests --plugin-editor-deadline-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_editor_deadline_timeout" "$<TARGET_FILE:VibeRaParameterFixture>" timeout)
set_tests_properties(plugin_editor_deadline_timely plugin_editor_deadline_late plugin_editor_deadline_timeout
  PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
add_test(NAME plugin_editors_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-plugin-editors-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_editors_mcp" "$<TARGET_FILE:VibeRaParameterFixture>" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(plugin_editors_mcp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME compensation_history COMMAND VibeRaCompensationTests)
set_tests_properties(compensation_history PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
add_test(NAME bounded_midi_compensation COMMAND VibeRaTests --bounded-midi-check
  "${CMAKE_CURRENT_BINARY_DIR}/bounded_midi_compensation")
set_tests_properties(bounded_midi_compensation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_effect_runtime COMMAND VibeRaTests --remote-effect-runtime-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_effect_runtime" "$<TARGET_FILE:VibeRaParameterFixture>" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_effect_runtime PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_command_preparation COMMAND VibeRaTests --structural-command-preparation-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_command_preparation")
set_tests_properties(mixer_command_preparation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_concurrent_retained_context COMMAND VibeRaTests --concurrent-retained-context-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_concurrent_retained_context" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_concurrent_retained_context PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_retained_context COMMAND VibeRaTests --retained-context-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_retained_context" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_retained_context PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME fixed_initialisation_lifecycle COMMAND VibeRaTests --fixed-initialisation-check
  "${CMAKE_CURRENT_BINARY_DIR}/fixed_initialisation_lifecycle")
set_tests_properties(fixed_initialisation_lifecycle PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME prepared_timeline_publication COMMAND VibeRaTests --prepared-timeline-check
  "${CMAKE_CURRENT_BINARY_DIR}/prepared_timeline_publication")
set_tests_properties(prepared_timeline_publication PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME history_destination_admission COMMAND VibeRaTests --history-destination-check
  "${CMAKE_CURRENT_BINARY_DIR}/history_destination_admission")
set_tests_properties(history_destination_admission PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME controller_prepared_history COMMAND VibeRaTests --controller-prepared-check
  "${CMAKE_CURRENT_BINARY_DIR}/controller_prepared_history")
set_tests_properties(controller_prepared_history PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME midi_take_recovery COMMAND VibeRaTests --midi-take-recovery-check
  "${CMAKE_CURRENT_BINARY_DIR}/midi_take_recovery")
set_tests_properties(midi_take_recovery PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME midi_take_recovery_gui COMMAND VibeRa --midi-take-recovery-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/midi_take_recovery_gui")
set_tests_properties(midi_take_recovery_gui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME history_ui_recovery COMMAND VibeRa --history-ui-recovery-check
  "${CMAKE_CURRENT_BINARY_DIR}/history_ui_recovery")
set_tests_properties(history_ui_recovery PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_ready_graph COMMAND VibeRaTests --ready-graph-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_ready_graph")
set_tests_properties(mixer_ready_graph PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_stable_bus_stages COMMAND VibeRaTests --stable-bus-stage-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stable_bus_stages")
set_tests_properties(mixer_stable_bus_stages PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_structural_graph_contracts COMMAND VibeRaTests --structural-graph-contracts-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_structural_graph_contracts")
set_tests_properties(mixer_structural_graph_contracts PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_test(NAME mixer_wave_context COMMAND VibeRaTests --wave-context-check
    "${CMAKE_CURRENT_BINARY_DIR}/mixer_wave_context")
  set_tests_properties(mixer_wave_context PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_guide_runtime COMMAND VibeRaTests --guide-runtime-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_guide_runtime")
set_tests_properties(mixer_guide_runtime PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_control_runtime COMMAND VibeRaTests --control-runtime-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_control_runtime")
set_tests_properties(mixer_control_runtime PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_instrument_context COMMAND VibeRaTests --instrument-context-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_instrument_context" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_instrument_context PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_instrument_packet_boundary COMMAND VibeRaTests --instrument-context-packet-boundary-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_instrument_packet_boundary" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_instrument_packet_boundary PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_initial_app_source COMMAND VibeRaTests --initial-app-source-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_initial_app_source" "$<TARGET_FILE:VibeRaInstrumentFixture>" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_initial_app_source PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_initial_app_source_controls COMMAND VibeRaTests --initial-app-source-controls-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_initial_app_source_controls" "$<TARGET_FILE:VibeRaInstrumentFixture>" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_initial_app_source_controls PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_initial_app_audio_ports COMMAND VibeRaTests --initial-app-audio-port-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_initial_app_audio_ports" "$<TARGET_FILE:VibeRaInstrumentFixture>" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_initial_app_audio_ports PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_initial_app_active_fx_ports COMMAND VibeRaTests --initial-app-active-fx-port-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_initial_app_active_fx_ports" "$<TARGET_FILE:VibeRaInstrumentFixture>" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_initial_app_active_fx_ports PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_app_zero_history COMMAND VibeRaTests --initial-app-zero-history-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_app_zero_history" "$<TARGET_FILE:VibeRaInstrumentFixture>" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_app_zero_history PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_zero_stage_exchange COMMAND VibeRaTests --zero-stage-exchange-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_zero_stage_exchange")
set_tests_properties(mixer_zero_stage_exchange PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_initial_source_joint_route COMMAND VibeRaTests --initial-source-joint-route-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_initial_source_joint_route" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_initial_source_joint_route PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_zero_channel_delay COMMAND VibeRaTests --zero-channel-delay-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_zero_channel_delay")
set_tests_properties(mixer_zero_channel_delay PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_organisation COMMAND VibeRaTests --mixer-organisation-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_organisation")
set_tests_properties(mixer_organisation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_macro_configuration COMMAND VibeRaTests --mixer-macro-configuration-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_macro_configuration")
set_tests_properties(mixer_macro_configuration PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_organisation_gui COMMAND VibeRa --mixer-organisation-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_organisation_gui")
set_tests_properties(mixer_organisation_gui PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_organisation_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-mixer-organisation-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_organisation_mcp")
set_tests_properties(mixer_organisation_mcp PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_routing_lifetime COMMAND VibeRaTests --routing-lifetime-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_routing_lifetime")
set_tests_properties(mixer_routing_lifetime PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_worker_turnaround COMMAND VibeRaTests --worker-turnaround-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_worker_turnaround" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_worker_turnaround PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_physical_worker COMMAND VibeRaTests --physical-worker-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_physical_worker" "$<TARGET_FILE:VibeRaInstrumentFixture>" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_physical_worker PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_prepared_transport COMMAND VibeRaTests --prepared-plugin-transport-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_prepared_transport" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_prepared_transport PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_prepared_transport_coverage COMMAND VibeRaTests --prepared-plugin-transport-coverage-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_prepared_transport_coverage" "$<TARGET_FILE:VibeRaMixerLatencyFixture>" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_prepared_transport_coverage PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_native_source_admission COMMAND VibeRaTests --source-burst-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_source_admission" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_native_source_admission PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_dormant_sampler_runtime COMMAND VibeRaTests --dormant-sampler-runtime-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_dormant_sampler_runtime")
set_tests_properties(mixer_dormant_sampler_runtime PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_local_source_initial COMMAND VibeRaTests --local-source-initial-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_local_source_initial" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_local_source_initial PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_insert_order COMMAND VibeRaTests --mixer-insert-order-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_insert_order")
set_tests_properties(mixer_insert_order PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_insert_order_gui COMMAND VibeRa --mixer-insert-order-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_insert_order_gui")
set_tests_properties(mixer_insert_order_gui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_insert_order_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/verify-mixer-insert-order-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/mixer_insert_order_mcp")
set_tests_properties(mixer_insert_order_mcp PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_pcm_receipts COMMAND VibeRaTests --plugin-pcm-receipt-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_pcm_receipts" "$<TARGET_FILE:VibeRaMixerLatencyFixture>" "$<TARGET_FILE:VibeRaInstrumentFixture>")
set_tests_properties(mixer_pcm_receipts PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_native_identity_sparse_pattern COMMAND VibeRaTests --mixer-native-extended-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_identity_sparse_pattern" "offline-pattern")
set_tests_properties(mixer_native_identity_sparse_pattern PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_native_identity_sparse_song COMMAND VibeRaTests --mixer-native-extended-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_identity_sparse_song" "offline-song")
set_tests_properties(mixer_native_identity_sparse_song PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_summing_properties COMMAND VibeRaTests --summing-properties-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_summing_properties")
set_tests_properties(mixer_summing_properties PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
add_test(NAME mixer_native_inverse_manager COMMAND VibeRaTests --native-history-inverse-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_inverse_manager" manager)
add_test(NAME mixer_native_inverse_controller COMMAND VibeRaTests --native-history-inverse-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_inverse_controller" controller)
add_test(NAME mixer_stereo_gui COMMAND VibeRa --mixer-stereo-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stereo_gui")
set_tests_properties(mixer_stereo_gui PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_stereo_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-mixer-stereo-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stereo_mcp")
set_tests_properties(mixer_stereo_mcp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME mixer_stereo_controls COMMAND VibeRaTests --mixer-stereo-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stereo_controls")
add_test(NAME mixer_automation_core COMMAND VibeRaTests --mixer-automation-core-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_automation_core")
set_tests_properties(mixer_automation_core PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_automation_native COMMAND VibeRaTests --mixer-automation-native-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_automation_native")
set_tests_properties(mixer_automation_native PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_automation_gui COMMAND VibeRa --mixer-automation-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_automation_gui")
set_tests_properties(mixer_automation_gui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_automation_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-mixer-automation-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_automation_mcp")
set_tests_properties(mixer_automation_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME automation_source_clone COMMAND VibeRaTests --automation-source-clone-check
  "${CMAKE_CURRENT_BINARY_DIR}/automation_source_clone")
set_tests_properties(automation_source_clone PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME automation_source_clone_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-automation-source-clone-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/automation_source_clone_mcp")
set_tests_properties(automation_source_clone_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME pattern_channel_loops COMMAND VibeRaTests --channel-loop-check
  "${CMAKE_CURRENT_BINARY_DIR}/pattern_channel_loops")
set_tests_properties(pattern_channel_loops PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME pattern_extent COMMAND VibeRaTests --pattern-extent-check
  "${CMAKE_CURRENT_BINARY_DIR}/pattern_extent")
set_tests_properties(pattern_extent PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME pattern_extent_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-pattern-extent-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/pattern_extent_mcp")
set_tests_properties(pattern_extent_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME step_composition COMMAND VibeRaTests --step-compose-check
  "${CMAKE_CURRENT_BINARY_DIR}/step_composition")
set_tests_properties(step_composition PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE)
add_test(NAME step_composition_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-step-compose-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/step_composition_mcp")
set_tests_properties(step_composition_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME step_composition_gui COMMAND VibeRa --step-compose-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/step_composition_gui")
set_tests_properties(step_composition_gui PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE)
add_test(NAME pattern_channel_loops_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-channel-loops-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/pattern_channel_loops_mcp")
set_tests_properties(pattern_channel_loops_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_native_scalar_core COMMAND VibeRaTests --native-scalar-core-check)
set_tests_properties(mixer_native_scalar_core PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_native_scalar_gui COMMAND VibeRa --native-scalar-history-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_scalar_gui" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_native_scalar_gui PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_automation_take_core COMMAND VibeRaTests --automation-touch-core-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_automation_take_core")
set_tests_properties(mixer_automation_take_core PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_automation_take_gui COMMAND VibeRa --automation-touch-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_automation_take_gui")
set_tests_properties(mixer_automation_take_gui PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_stereo_native COMMAND VibeRaTests --mixer-stereo-native-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stereo_native")
set_tests_properties(mixer_stereo_native PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_output_stage_difference COMMAND VibeRaTests --output-stage-difference-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_output_stage_difference")
set_tests_properties(mixer_output_stage_difference PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_warm_source_relocation COMMAND VibeRaTests --warm-source-relocation-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_warm_source_relocation")
set_tests_properties(mixer_warm_source_relocation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_zero_history_creation COMMAND VibeRaTests --zero-history-creation-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_zero_history_creation")
set_tests_properties(mixer_zero_history_creation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_audio_port COMMAND VibeRaTests --mixer-audio-port-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_audio_port")
set_tests_properties(mixer_audio_port PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_app_midi_ownership COMMAND VibeRaTests --app-audio-port-midi-ownership-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_app_midi_ownership")
set_tests_properties(mixer_app_midi_ownership PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_bounded_midi_ownership COMMAND VibeRaTests --bounded-native-midi-ownership-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_bounded_midi_ownership")
set_tests_properties(mixer_bounded_midi_ownership PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_native_midi_ingress COMMAND VibeRaTests --native-midi-ingress-dsp-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_midi_ingress")
set_tests_properties(mixer_native_midi_ingress PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_app_audio_port COMMAND VibeRaTests --app-mixer-audio-port-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_app_audio_port")
set_tests_properties(mixer_app_audio_port PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_chain_data COMMAND VibeRaTests --mixer-chain-data-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_chain_data")
set_tests_properties(mixer_chain_data PROPERTIES TIMEOUT 30)
add_test(NAME mixer_chain_file_jobs COMMAND VibeRaTests --mixer-chain-file-jobs-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_chain_file_jobs")
set_tests_properties(mixer_chain_file_jobs PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_chain_workflow COMMAND VibeRa --mixer-chain-workflow
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_chain_workflow" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_chain_workflow PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_chain_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-mixer-chain-mcp.py" --prepare-seed
  "$<TARGET_FILE:VibeRa>" "$<TARGET_FILE:VibeRaParameterFixture>"
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_chain_mcp")
set_tests_properties(mixer_chain_mcp PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_finite_effect_baseline COMMAND VibeRaTests --finite-effect-baseline-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_finite_effect_baseline"
  "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_finite_effect_baseline PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_stopped_effect_forward_sdk COMMAND VibeRaTests --stopped-effect-forward-sdk-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stopped_effect_forward_sdk" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_stopped_effect_forward_sdk PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_stopped_effect_inverse_sdk COMMAND VibeRaTests --stopped-effect-inverse-sdk-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stopped_effect_inverse_sdk" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_stopped_effect_inverse_sdk PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_forward_stage_drain COMMAND VibeRaTests --forward-stage-drain-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_forward_stage_drain")
set_tests_properties(mixer_forward_stage_drain PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_send_automation_native COMMAND VibeRaTests --send-automation-native-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_send_automation_native")
set_tests_properties(mixer_send_automation_native PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
set_tests_properties(mixer_native_inverse_manager mixer_native_inverse_controller mixer_stereo_controls
  PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME plugin_dry_history_host COMMAND VibeRaTests --plugin-dry-history-host-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_dry_history_host" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(plugin_dry_history_host PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME plugin_dry_history COMMAND VibeRaTests --plugin-dry-history-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_dry_history")
set_tests_properties(plugin_dry_history PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_stopped_effect_ordinary_forward COMMAND VibeRaTests --stopped-effects-ordinary-forward-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stopped_effect_ordinary_forward" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_stopped_effect_ordinary_forward PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME mixer_stopped_effect_native COMMAND VibeRaTests --stopped-effects-native-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stopped_effect_native" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_stopped_effect_native PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME mixer_production_membership_sdk COMMAND VibeRaTests --production-membership-sdk
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_production_membership_sdk" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_production_membership_sdk PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_stopped_effect_plan COMMAND VibeRaTests --stopped-effect-plan-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_stopped_effect_plan")
set_tests_properties(mixer_stopped_effect_plan PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
add_test(NAME mixer_prepared_source_hosts COMMAND VibeRaTests --prepared-source-hosts-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_prepared_source_hosts" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_prepared_source_hosts PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_native_identity_129_pattern COMMAND VibeRaTests --mixer-native-extended-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_identity_129_pattern" "native129-pattern")
set_tests_properties(mixer_native_identity_129_pattern PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_native_identity_129_song COMMAND VibeRaTests --mixer-native-extended-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_identity_129_song" "native129-song")
set_tests_properties(mixer_native_identity_129_song PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_native_identity COMMAND VibeRaTests --mixer-native-identity-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_native_identity" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_native_identity PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_effect_turnaround COMMAND VibeRaTests --effect-turnaround-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_effect_turnaround" "$<TARGET_FILE:VibeRaParameterFixture>" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_effect_turnaround PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_routing_coordinator COMMAND VibeRaTests --routing-coordinator-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_routing_coordinator")
set_tests_properties(mixer_routing_coordinator PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_stopped_effect_command COMMAND VibeRaTests --stopped-effect-command-prepare-check)
set_tests_properties(mixer_stopped_effect_command PROPERTIES TIMEOUT 30)
add_test(NAME controller_routing_capture COMMAND VibeRaTests --controller-routing-capture-check
  "${CMAKE_CURRENT_BINARY_DIR}/controller_routing_capture")
set_tests_properties(controller_routing_capture PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_target_choices_gui COMMAND VibeRa --mixer-targets-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_target_choices_gui")
set_tests_properties(mixer_target_choices_gui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME mixer_target_choices COMMAND VibeRaTests --mixer-target-choices-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_target_choices")
set_tests_properties(mixer_target_choices PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME studio_channel_routing COMMAND VibeRa --studio-channel-routing-check
  "${CMAKE_CURRENT_BINARY_DIR}/studio_channel_routing")
set_tests_properties(studio_channel_routing PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME studio_channel_routing_provider COMMAND VibeRa --studio-channel-routing-check
  "${CMAKE_CURRENT_BINARY_DIR}/studio_channel_routing_provider" "$<TARGET_FILE:VibeRaParameterFixture>"
  "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(studio_channel_routing_provider PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME studio_channel_routing_same_config COMMAND VibeRa --studio-channel-routing-same-config-check
  "${CMAKE_CURRENT_BINARY_DIR}/studio_channel_routing_same_config" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(studio_channel_routing_same_config PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_plugin_save_capture COMMAND VibeRa --plugin-save-capture-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_plugin_save_capture" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_plugin_save_capture PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME deferred_device_candidate_fault COMMAND VibeRaTests --deferred-device-candidate-fault-check
  "${CMAKE_CURRENT_BINARY_DIR}/deferred_device_candidate_fault" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(deferred_device_candidate_fault PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_plugin_recovery COMMAND VibeRaTests --plugin-recovery-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_plugin_recovery" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_plugin_recovery PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME mixer_chain_controls COMMAND VibeRaTests --mixer-chain-controls-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_chain_controls")
set_tests_properties(mixer_chain_controls PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME plugin_host_wet_mix COMMAND VibeRaTests --plugin-wet-mix-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_host_wet_mix" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(plugin_host_wet_mix PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
add_test(NAME mixer_chain_live_export COMMAND VibeRaTests --mixer-chain-live-export-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_chain_live_export" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(mixer_chain_live_export PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME prepared_history_mutation COMMAND VibeRaHistoryTests
  "${CMAKE_CURRENT_BINARY_DIR}/prepared_history_mutation")
set_tests_properties(prepared_history_mutation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME number_serialization_safety COMMAND VibeRaHistoryTests --number-safety
  "${CMAKE_CURRENT_BINARY_DIR}/number_serialization_safety")
set_tests_properties(number_serialization_safety PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
add_test(NAME mixer_graph_publication COMMAND VibeRaTests --graph-publication-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_graph_publication")
set_tests_properties(mixer_graph_publication PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME mixer_bypass_transition COMMAND VibeRaTests --plugin-bypass-check
  "${CMAKE_CURRENT_BINARY_DIR}/mixer_bypass_transition" "$<TARGET_FILE:VibeRaMixerLatencyFixture>")
set_tests_properties(mixer_bypass_transition PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME development_workflow COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-dev-workflow.py")
set_tests_properties(development_workflow PROPERTIES TIMEOUT 60)
add_test(NAME development_fixture_profiles COMMAND "${Python3_EXECUTABLE}"
  -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_fixture_profiles.py -v)
set_tests_properties(development_fixture_profiles PROPERTIES TIMEOUT 15)
add_library(VibeRaParameterFixture MODULE tests/ParameterFixture.cpp)
target_include_directories(VibeRaParameterFixture PRIVATE "${VST3_SDK_ROOT}")
set_target_properties(VibeRaParameterFixture PROPERTIES PREFIX "" SUFFIX ".vst3")
add_dependencies(VibeRaTests VibeRaParameterFixture)
add_test(NAME isolated_parameters COMMAND VibeRaTests --parameter-check
  "${CMAKE_CURRENT_BINARY_DIR}/parameter_acceptance" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(isolated_parameters PROPERTIES TIMEOUT 90)
if(WIN32)
  add_dependencies(VibeRa VibeRaParameterFixture)
endif()
add_test(NAME plugin_parameters_gui COMMAND VibeRa --parameter-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/parameter_gui_acceptance" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(plugin_parameters_gui PROPERTIES TIMEOUT 90)
add_test(NAME plugin_wrapper_gui COMMAND VibeRa --wrapper-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_wrapper_gui_acceptance" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(plugin_wrapper_gui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME plugin_wrapper_editorless_gui COMMAND VibeRa --wrapper-editorless-ui-check
  "${CMAKE_CURRENT_BINARY_DIR}/plugin_wrapper_editorless_gui_acceptance" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(plugin_wrapper_editorless_gui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_test(NAME plugin_catalogue_gui COMMAND VibeRa --catalogue-controls-check
  "${CMAKE_CURRENT_BINARY_DIR}/catalogue_gui_acceptance" "$<TARGET_FILE:VibeRaParameterFixture>")
set_tests_properties(plugin_catalogue_gui PROPERTIES TIMEOUT 30)
juce_add_console_app(AutomationTests PRODUCT_NAME "VibeRa automation acceptance")
target_sources(AutomationTests PRIVATE tests/AutomationAcceptance.cpp)
target_link_libraries(AutomationTests PRIVATE VibeRaCore)
add_test(NAME automation_foundation COMMAND AutomationTests "${CMAKE_CURRENT_BINARY_DIR}/automation_acceptance")
set_tests_properties(automation_foundation PROPERTIES TIMEOUT 60)
juce_add_console_app(CoProducerTests PRODUCT_NAME "CoProducer context acceptance")
target_sources(CoProducerTests PRIVATE tests/CoProducerAcceptance.cpp)
target_link_libraries(CoProducerTests PRIVATE VibeRaCore)
add_test(NAME coproducer_context COMMAND CoProducerTests "${CMAKE_CURRENT_BINARY_DIR}/coproducer_acceptance")
set_tests_properties(coproducer_context PROPERTIES TIMEOUT 60)
add_test(NAME callback_timing COMMAND VibeRaTests --callback-timing-check "${CMAKE_CURRENT_BINARY_DIR}/callback_timing")
set_tests_properties(callback_timing PROPERTIES TIMEOUT 15)
add_test(NAME musical_acceptance COMMAND VibeRaTests "${CMAKE_CURRENT_BINARY_DIR}/acceptance")
add_test(NAME plugins_page_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-plugins-page-mcp.py"
  "$<TARGET_FILE:VibeRa>")
set_tests_properties(plugins_page_mcp PROPERTIES TIMEOUT 180)
add_test(NAME chord_track_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-chord-track-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/chord_track_mcp")
set_tests_properties(chord_track_mcp PROPERTIES TIMEOUT 180)
add_test(NAME modulators_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-modulators-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/modulators_mcp")
set_tests_properties(modulators_mcp PROPERTIES TIMEOUT 400)
add_test(NAME vocal_pitch_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-vocal-pitch-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/vocal_pitch_mcp")
set_tests_properties(vocal_pitch_mcp PROPERTIES TIMEOUT 300)
add_test(NAME expression_maps_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-expression-maps-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/expression_maps_mcp")
set_tests_properties(expression_maps_mcp PROPERTIES TIMEOUT 180)
add_test(NAME parallel_chains_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-parallel-chains-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/parallel_chains_mcp")
set_tests_properties(parallel_chains_mcp PROPERTIES TIMEOUT 400)
add_test(NAME playlist_tools_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-playlist-tools-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/playlist_tools_mcp")
set_tests_properties(playlist_tools_mcp PROPERTIES TIMEOUT 180)
add_test(NAME midi_scale_snap_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-midi-scale-snap-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/midi_scale_snap_mcp")
set_tests_properties(midi_scale_snap_mcp PROPERTIES TIMEOUT 120)
add_test(NAME marker_actions_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-marker-actions-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/marker_actions_mcp")
set_tests_properties(marker_actions_mcp PROPERTIES TIMEOUT 200)
add_test(NAME center_playhead_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-center-playhead-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/center_playhead_mcp")
set_tests_properties(center_playhead_mcp PROPERTIES TIMEOUT 120)
add_test(NAME controller_interpolate_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-controller-interpolate-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/controller_interpolate_mcp")
set_tests_properties(controller_interpolate_mcp PROPERTIES TIMEOUT 120)
add_test(NAME loudness_mcp COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-loudness-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/loudness_mcp")
set_tests_properties(loudness_mcp PROPERTIES TIMEOUT 180)
add_test(NAME mcp_ui_actions COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-ui-actions-mcp.py"
  "$<TARGET_FILE:VibeRa>" "${CMAKE_CURRENT_BINARY_DIR}/mcp_ui_actions")
set_tests_properties(mcp_ui_actions PROPERTIES TIMEOUT 120)
add_test(NAME browser_search COMMAND VibeRaTests --browser-search-check "${CMAKE_CURRENT_BINARY_DIR}/browser_search")
set_tests_properties(browser_search PROPERTIES TIMEOUT 40)
add_test(NAME performance_gui COMMAND VibeRa --performance-check "${CMAKE_CURRENT_BINARY_DIR}/performance_gui")
set_tests_properties(performance_gui PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME accessibility_gui COMMAND VibeRa --accessibility-check "${CMAKE_CURRENT_BINARY_DIR}/accessibility_gui")
set_tests_properties(accessibility_gui PROPERTIES TIMEOUT 60)
add_test(NAME audio_take_gui COMMAND VibeRa --audio-take-check "${CMAKE_CURRENT_BINARY_DIR}/audio_take_gui")
set_tests_properties(audio_take_gui PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
add_test(NAME browser_live_gui COMMAND VibeRa --browser-live-check "${CMAKE_CURRENT_BINARY_DIR}/browser_live_gui")
set_tests_properties(browser_live_gui PROPERTIES TIMEOUT 60)
# Complete72-stage model/render/recovery/live-edit aggregate. Match the existing
# packaged musical allowance; every internal operation and physical callback
# deadline remains independently asserted. Retain historical120s failures.
set_tests_properties(musical_acceptance PROPERTIES TIMEOUT 300)
add_test(NAME gui_workflow COMMAND VibeRa --smoke-test "${CMAKE_CURRENT_BINARY_DIR}/gui_acceptance")
set_tests_properties(gui_workflow PROPERTIES TIMEOUT 120)

add_test(NAME midi_interchange_gui COMMAND VibeRa --midi-file-check "${CMAKE_CURRENT_BINARY_DIR}/midi_gui_acceptance")
set_tests_properties(midi_interchange_gui PROPERTIES TIMEOUT 60)
# Repeat against the same owned output: intentionally unsupported Browser
# preference fixtures must remain preserved without poisoning the next run.
add_test(NAME browser_collections_repeat_gui COMMAND VibeRa --midi-file-check "${CMAKE_CURRENT_BINARY_DIR}/midi_gui_acceptance")
set_tests_properties(browser_collections_repeat_gui PROPERTIES TIMEOUT 60 DEPENDS midi_interchange_gui)
add_test(NAME audio_playlist_gui COMMAND VibeRa --audio-playlist-check "${CMAKE_CURRENT_BINARY_DIR}/audio_playlist_acceptance")
set_tests_properties(audio_playlist_gui PROPERTIES TIMEOUT 60)

# Suggested integration only. Root applies the reviewed physical-callback vendor
# family and v2 application/fixture headers first. No clean configure or fetch.
# The guard owns exact vendor/archive/payload hashes; do not duplicate constants.
if(WIN32)
  set(_physical_manifest "${CMAKE_CURRENT_SOURCE_DIR}/patches/juce-physical-callback/manifest.json")
  set(_physical_guard "${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply-physical-callback-patch.ps1")
  if(NOT EXISTS "${_physical_manifest}" OR NOT EXISTS "${_physical_guard}")
    message(FATAL_ERROR "Physical-burst leaf requires the reviewed strict physical-callback patch family")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=PSModulePath
    powershell -NoProfile -ExecutionPolicy Bypass -File "${_physical_guard}" -VerifyOnly
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _physical_verified OUTPUT_VARIABLE _physical_guard_output ERROR_VARIABLE _physical_guard_error)
  if(NOT _physical_verified STREQUAL "0")
    message(FATAL_ERROR "Physical-burst leaf vendor prerequisite failed: ${_physical_guard_output}${_physical_guard_error}")
  endif()
  # VerifyOnly proves known pre/post images; this target requires the APPLIED API.
  file(READ "${_physical_manifest}" _physical_contract)
  string(JSON _physical_count LENGTH "${_physical_contract}" files)
  math(EXPR _physical_last "${_physical_count} - 1")
  foreach(_physical_index RANGE 0 ${_physical_last})
    string(JSON _physical_path GET "${_physical_contract}" files ${_physical_index} path)
    string(JSON _physical_expected GET "${_physical_contract}" files ${_physical_index} patchedSha256)
    file(SHA256 "${JUCE_ROOT}/${_physical_path}" _physical_actual)
    if(NOT _physical_actual STREQUAL _physical_expected)
      message(FATAL_ERROR "Apply the reviewed physical-callback adapter before building its leaf")
    endif()
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_physical_manifest}" "${_physical_guard}"
    "${JUCE_ROOT}/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.h"
    "${JUCE_ROOT}/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/PhysicalBurstContract.h" OR
     NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/PhysicalBurstAcceptance.h")
    message(FATAL_ERROR "Physical-burst leaf requires its reviewed application contract and original fixture")
  endif()
  juce_add_console_app(VibeRaPhysicalBurstTests PRODUCT_NAME "VibeRa physical-burst source acceptance")
  target_sources(VibeRaPhysicalBurstTests PRIVATE tests/PhysicalBurstMain.cpp)
  target_include_directories(VibeRaPhysicalBurstTests PRIVATE src tests)
  # Same relevant JUCE backend/modal/CURL options as current VibeRaCore. The
  # intentionally smaller JUCE module-availability set is not binary Core reuse.
  target_compile_definitions(VibeRaPhysicalBurstTests PRIVATE
    VIBERA_PHYSICAL_BURST_V2_VERIFIED=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0 JUCE_ASIO=0 JUCE_WASAPI=1)
  target_link_libraries(VibeRaPhysicalBurstTests PRIVATE juce::juce_audio_devices juce::juce_cryptography)
  # Inherits root CMAKE_MSVC_RUNTIME_LIBRARY: /MT Release and /MTd Debug.
  if(MSVC)
    target_compile_options(VibeRaPhysicalBurstTests PRIVATE /W4 $<$<CONFIG:Release>:/Zi>)
    target_link_options(VibeRaPhysicalBurstTests PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>
      $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
  endif()
  add_test(NAME physical_burst_leaf COMMAND VibeRaPhysicalBurstTests --physical-burst-check
    "${CMAKE_CURRENT_BINARY_DIR}/physical_burst_leaf")
  set_tests_properties(physical_burst_leaf PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()

# The pinned JUCE rcfile command depends on the icon but omits Info.txt.
# Include metadata as an input so version changes regenerate Windows resources.
if(WIN32)
  foreach(appTarget VibeRa VibeRaTests VibeRaPluginWorker VibeRaScannerFixture CoProducerTests VibeRaPhysicalBurstTests)
    if(TARGET ${appTarget}_rc_lib)
      get_target_property(resourceInfo ${appTarget} JUCE_INFO_FILE)
      get_target_property(resourceDirectory ${appTarget} JUCE_GENERATED_SOURCES_DIRECTORY)
      add_custom_command(OUTPUT "${resourceDirectory}/${appTarget}_resources.rc"
        APPEND DEPENDS "${resourceInfo}")
    endif()
  endforeach()
endif()

add_test(NAME audio_playlist_live COMMAND VibeRa --audio-playlist-live-check "${CMAKE_CURRENT_BINARY_DIR}/audio_playlist_live")
set_tests_properties(audio_playlist_live PROPERTIES TIMEOUT 60)

add_test(NAME sampler_gestures_gui COMMAND VibeRa --sampler-gesture-check "${CMAKE_CURRENT_BINARY_DIR}/sampler_gestures_gui")
set_tests_properties(sampler_gestures_gui PROPERTIES TIMEOUT 60)
add_test(NAME controller_mapping_gui COMMAND VibeRa --controller-check "${CMAKE_CURRENT_BINARY_DIR}/controller_mapping_gui")
set_tests_properties(controller_mapping_gui PROPERTIES TIMEOUT 60)
add_test(NAME automation_clip_gui COMMAND VibeRa --automation-check "${CMAKE_CURRENT_BINARY_DIR}/automation_clip_gui")
set_tests_properties(automation_clip_gui PROPERTIES TIMEOUT 60)

# Signature native instrument: pure audio and actual processor/editor acceptance.
add_executable(VibeRaSynthEngineTests tests/SynthEngineTests.cpp src/synth/SynthEngine.cpp)
target_include_directories(VibeRaSynthEngineTests PRIVATE src)
add_test(NAME vibera_synth_engine COMMAND VibeRaSynthEngineTests)
set_tests_properties(vibera_synth_engine PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
juce_add_console_app(VibeRaSynthPluginTests PRODUCT_NAME "VibeRa Synthesizer acceptance")
set_target_properties(VibeRaSynthPluginTests PROPERTIES OUTPUT_NAME VibeRaSynthPluginTests)
target_sources(VibeRaSynthPluginTests PRIVATE tests/SynthPluginTests.cpp
  src/synth/SynthPlugin.cpp src/synth/SynthPresets.cpp src/synth/SynthEngine.cpp src/synth/SynthEditor.cpp
  src/effects/dsp/Dynamics.cpp src/effects/dsp/TimeEffects.cpp src/effects/dsp/PitchCorrect.cpp src/effects/dsp/Vocoder.cpp src/effects/dsp/StudioEffects.cpp)
target_include_directories(VibeRaSynthPluginTests PRIVATE src)
target_compile_definitions(VibeRaSynthPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(VibeRaSynthPluginTests PRIVATE juce::juce_audio_utils juce::juce_cryptography)
add_test(NAME vibera_synth_processor COMMAND VibeRaSynthPluginTests "${CMAKE_CURRENT_BINARY_DIR}/synthesizer-acceptance")
set_tests_properties(vibera_synth_processor PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

add_test(NAME vibera_synth_mcp COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-synthesizer-mcp.py" "$<TARGET_FILE:VibeRa>"
  "${CMAKE_CURRENT_BINARY_DIR}/synthesizer-mcp")
set_tests_properties(vibera_synth_mcp PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME vibera_synth_host COMMAND VibeRaTests --synth-host-check "${CMAKE_CURRENT_BINARY_DIR}/synthesizer-host")
set_tests_properties(vibera_synth_host PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)


# Content has real dependency edges: changing a sample/map/notice also updates an
# otherwise up-to-date executable folder, without relinking or fetching assets.
file(GLOB_RECURSE VIBERA_SYNTH_CONTENT CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer/samples/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer/sources/*")
list(APPEND VIBERA_SYNTH_CONTENT "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer/library.json")
# Track the actual copies, not just a stamp: retiring development runnables also
# retires their Content folders while preserving the valid compilation cache.
# A missing map/sample must therefore invalidate this rule without a clean build.
set(VIBERA_SYNTH_CONTENT_HOSTS VibeRaPluginWorker VibeRa VibeRaTests VibeRaSynthPluginTests)
set(VIBERA_SYNTH_CONTENT_OUTPUTS)
foreach(host IN LISTS VIBERA_SYNTH_CONTENT_HOSTS)
  get_target_property(content_destination ${host} RUNTIME_OUTPUT_DIRECTORY)
  if(APPLE)
    get_target_property(content_bundle ${host} MACOSX_BUNDLE)
    if(content_bundle)
      get_target_property(content_name ${host} JUCE_PRODUCT_NAME)
      string(APPEND content_destination "/${content_name}.app/Contents/MacOS")
    endif()
  endif()
  foreach(content IN LISTS VIBERA_SYNTH_CONTENT)
    file(RELATIVE_PATH content_relative "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer" "${content}")
    list(APPEND VIBERA_SYNTH_CONTENT_OUTPUTS
      "${content_destination}/Content/Synthesizer/${content_relative}")
  endforeach()
endforeach()
set(VIBERA_SYNTH_CONTENT_STAMP "${CMAKE_CURRENT_BINARY_DIR}/synthesizer-content-$<CONFIG>.stamp")
add_custom_command(OUTPUT "${VIBERA_SYNTH_CONTENT_STAMP}" ${VIBERA_SYNTH_CONTENT_OUTPUTS}
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer"
    "$<TARGET_FILE_DIR:VibeRaPluginWorker>/Content/Synthesizer"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer"
    "$<TARGET_FILE_DIR:VibeRa>/Content/Synthesizer"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer"
    "$<TARGET_FILE_DIR:VibeRaTests>/Content/Synthesizer"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer"
    "$<TARGET_FILE_DIR:VibeRaSynthPluginTests>/Content/Synthesizer"
  COMMAND ${CMAKE_COMMAND} -E touch "${VIBERA_SYNTH_CONTENT_STAMP}"
  COMMENT "Synchronizing native Synthesizer content: changed inputs or missing copies"
  DEPENDS ${VIBERA_SYNTH_CONTENT} "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
add_custom_target(VibeRaSynthContent DEPENDS "${VIBERA_SYNTH_CONTENT_STAMP}")
foreach(host IN LISTS VIBERA_SYNTH_CONTENT_HOSTS)
  add_dependencies(${host} VibeRaSynthContent)
endforeach()
add_test(NAME vibera_synth_content COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer/validate_library.py"
  --library "${CMAKE_CURRENT_SOURCE_DIR}/assets/synthesizer/library.json"
  --output "${CMAKE_CURRENT_BINARY_DIR}/synthesizer-content/verification.json")
set_tests_properties(vibera_synth_content PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

# VibeRa Tune shifts pitch with the Signalsmith Stretch (MIT) bundled with the pinned engine.
foreach(tune_target VibeRaPluginWorker VibeRaEffectsDspTests VibeRaNativeEffectsTests VibeRaSynthPluginTests)
  if(TARGET ${tune_target})
    target_include_directories(${tune_target} PRIVATE "${TE_ROOT}/modules/tracktion_engine/3rd_party/signalsmith-stretch")
  endif()
endforeach()
