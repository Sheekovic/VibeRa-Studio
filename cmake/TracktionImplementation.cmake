# Original Windows-only Tracktion implementation ownership experiment.
# Preserve the complete upstream module usage graph; gate implementation sources
# only. JUCE implementations and consumer startup/resource definitions are intact.
if(WIN32)
  function(vibera_own_tracktion_implementation module)
    get_target_property(originalSources ${module} INTERFACE_SOURCES)
    get_target_property(implementations ${module} INTERFACE_JUCE_MODULE_SOURCES)
    set(expectedNames ${ARGN})
    set(actualNames)
    foreach(source IN LISTS implementations)
      if(NOT IS_ABSOLUTE "${source}" OR source MATCHES "\\$<" OR NOT source MATCHES "\\.cpp$")
        message(FATAL_ERROR "Unexpected Tracktion implementation source: ${source}")
      endif()
      get_filename_component(name "${source}" NAME)
      list(APPEND actualNames "${name}")
    endforeach()
    list(SORT actualNames)
    list(SORT expectedNames)
    if(NOT "${actualNames}" STREQUAL "${expectedNames}")
      message(FATAL_ERROR "Pinned Tracktion implementation inventory changed: ${module}")
    endif()
    set(usageProperties INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
      INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_OPTIONS INTERFACE_COMPILE_FEATURES
      INTERFACE_LINK_LIBRARIES INTERFACE_LINK_DIRECTORIES INTERFACE_LINK_OPTIONS
      INTERFACE_JUCE_MODULE_SOURCES INTERFACE_JUCE_MODULE_HEADERS INTERFACE_JUCE_MODULE_PATH)
    foreach(property IN LISTS usageProperties)
      get_property(savedSet_${property} TARGET ${module} PROPERTY ${property} SET)
      get_property(saved_${property} TARGET ${module} PROPERTY ${property})
    endforeach()
    set(rewrittenSources)
    set(ownedCount 0)
    set(seenImplementations)
    foreach(source IN LISTS originalSources)
      if(source IN_LIST implementations)
        if(source IN_LIST seenImplementations)
          message(FATAL_ERROR "Duplicate Tracktion implementation source: ${source}")
        endif()
        list(APPEND seenImplementations "${source}")
        list(APPEND rewrittenSources "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,VibeRaCore>:${source}>")
        math(EXPR ownedCount "${ownedCount} + 1")
      else()
        # Headers may be included by JUCE_ENABLE_MODULE_SOURCE_GROUPS. Refuse
        # unexpected implementation expressions instead of suppressing them.
        if(source MATCHES "\\$<" OR source MATCHES "\\.(c|cc|cpp|cxx|m|mm|s|asm)$")
          message(FATAL_ERROR "Unclassified Tracktion interface source: ${source}")
        endif()
        list(APPEND rewrittenSources "${source}")
      endif()
    endforeach()
    list(LENGTH implementations expectedCount)
    if(NOT ownedCount EQUAL expectedCount)
      message(FATAL_ERROR "Tracktion implementation missing/duplicated in interface: ${module}")
    endif()
    set_property(TARGET ${module} PROPERTY INTERFACE_SOURCES "${rewrittenSources}")
    foreach(property IN LISTS usageProperties)
      get_property(afterSet TARGET ${module} PROPERTY ${property} SET)
      get_property(after TARGET ${module} PROPERTY ${property})
      if(NOT "${savedSet_${property}}" STREQUAL "${afterSet}" OR
          NOT "${saved_${property}}" STREQUAL "${after}")
        message(FATAL_ERROR "Tracktion header/link usage changed: ${module}/${property}")
      endif()
    endforeach()
  endfunction()
  vibera_own_tracktion_implementation(tracktion_engine
    tracktion_engine_airwindows_1.cpp
    tracktion_engine_airwindows_2.cpp
    tracktion_engine_airwindows_3.cpp
    tracktion_engine_audio_files.cpp
    tracktion_engine_model_1.cpp
    tracktion_engine_model_2.cpp
    tracktion_engine_playback.cpp
    tracktion_engine_plugins.cpp
    tracktion_engine_timestretch.cpp
    tracktion_engine_utils.cpp)
  vibera_own_tracktion_implementation(tracktion_graph
    tracktion_graph.cpp)
  vibera_own_tracktion_implementation(tracktion_core
    tracktion_core.cpp)
endif()
