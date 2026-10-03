cmake_minimum_required(VERSION 3.22)

project(Looper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOOPER_BUILD_PLUGIN "Fetch JUCE and build the VST3/AU plugin target" ON)
option(LOOPER_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# AutoMapper library (no JUCE — tests & tools can link this on any host)
# ---------------------------------------------------------------------------
add_library(LooperAutoMapper STATIC
    Source/AutoMapper/FilenameTokens.cpp
    Source/AutoMapper/AutoMapper.cpp
    Source/AutoMapper/PitchDetector.cpp
)
target_include_directories(LooperAutoMapper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_compile_features(LooperAutoMapper PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# DSP library (no JUCE — Hermite, AmpEnv, SamplePool, VoiceEngine)
# ---------------------------------------------------------------------------
add_library(LooperDsp STATIC
    Source/VoiceEngine/VoiceEngine.cpp
)
target_include_directories(LooperDsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_compile_features(LooperDsp PUBLIC cxx_std_20)
set_target_properties(LooperDsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(LooperAutoMapper PROPERTIES POSITION_INDEPENDENT_CODE ON)
# VoiceEngine uses <cmath>; link libm on platforms that need it
find_library(LOOPER_LIBM m)
if(LOOPER_LIBM)
    target_link_libraries(LooperDsp PUBLIC ${LOOPER_LIBM})
endif()

# ---------------------------------------------------------------------------
# Patch JSON library (no JUCE — InstrumentMap + SampleRef serialize)
# ---------------------------------------------------------------------------
add_library(LooperPatch STATIC
    Source/PatchStore/PatchStore.cpp
    Source/Prefs/SessionPrefs.cpp
    Source/PatchStore/SampleRelocator.cpp
)
target_include_directories(LooperPatch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_compile_features(LooperPatch PUBLIC cxx_std_20)
set_target_properties(LooperPatch PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Zone editor model (no JUCE — clamp / overlap / auto values / undoable edits)
# ---------------------------------------------------------------------------
add_library(LooperZoneEdit STATIC
    Source/ZoneEdit/ZoneEditor.cpp
    Source/ZoneEdit/ZoneSelection.cpp
    Source/ZoneEdit/ZoneStrip.cpp
)
target_include_directories(LooperZoneEdit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_compile_features(LooperZoneEdit PUBLIC cxx_std_20)
# Strip tooltips name notes with the AutoMapper's C4/C3 = 60 helpers
target_link_libraries(LooperZoneEdit PUBLIC LooperAutoMapper)
set_target_properties(LooperZoneEdit PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Unit tests (no plugin host required)
# ---------------------------------------------------------------------------
if(LOOPER_BUILD_TESTS)
    enable_testing()

    add_executable(AutoMapperTests Tests/AutoMapperTests.cpp)
    target_link_libraries(AutoMapperTests PRIVATE LooperAutoMapper LooperZoneEdit)
    add_test(NAME AutoMapperTests COMMAND AutoMapperTests)

    add_executable(PitchDetectorTests Tests/PitchDetectorTests.cpp)
    target_link_libraries(PitchDetectorTests PRIVATE LooperAutoMapper)
    add_test(NAME PitchDetectorTests COMMAND PitchDetectorTests)

    add_executable(DspVoiceTests Tests/DspVoiceTests.cpp)
    target_link_libraries(DspVoiceTests PRIVATE LooperDsp)
    add_test(NAME DspVoiceTests COMMAND DspVoiceTests)

    add_executable(PatchStoreTests Tests/PatchStoreTests.cpp)
    target_link_libraries(PatchStoreTests PRIVATE LooperPatch)
    add_test(NAME PatchStoreTests COMMAND PatchStoreTests)

    add_executable(SessionPrefsTests Tests/SessionPrefsTests.cpp)
    target_link_libraries(SessionPrefsTests PRIVATE LooperPatch)
    add_test(NAME SessionPrefsTests COMMAND SessionPrefsTests)

    add_executable(RelocatorTests Tests/RelocatorTests.cpp)
    target_link_libraries(RelocatorTests PRIVATE LooperPatch)
    add_test(NAME RelocatorTests COMMAND RelocatorTests)

    # Round-robin Cycle / Random selection, RT safety, patch field round trip
    add_executable(RoundRobinTests Tests/RoundRobinTests.cpp)
    target_link_libraries(RoundRobinTests PRIVATE LooperDsp LooperPatch)
    add_test(NAME RoundRobinTests COMMAND RoundRobinTests)

    # Unpitched drum-key fallback, filename/audio mismatch, C3=60 naming, pitch metadata in patches
    add_executable(PitchMappingTests Tests/PitchMappingTests.cpp)
    target_link_libraries(PitchMappingTests PRIVATE LooperAutoMapper LooperPatch)
    add_test(NAME PitchMappingTests COMMAND PitchMappingTests)

    # Mojibake guard: non-ASCII UI text must go through Source/UI/Glyphs.h
    add_executable(SourceEncodingTests Tests/SourceEncodingTests.cpp)
    target_compile_definitions(SourceEncodingTests PRIVATE
        LOOPER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Source")
    add_test(NAME SourceEncodingTests COMMAND SourceEncodingTests)
endif()

# ---------------------------------------------------------------------------
# JUCE 8 plugin (instrument: MIDI in, stereo out)
# ---------------------------------------------------------------------------
if(LOOPER_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    # AU is Apple-only; VST3 + Standalone elsewhere
    if(APPLE)
        set(LOOPER_FORMATS VST3 AU Standalone)
    else()
        set(LOOPER_FORMATS VST3 Standalone)
    endif()

    juce_add_plugin(Looper
        COMPANY_NAME "Loop Audio Lab"
        BUNDLE_ID com.loopaudiolab.Looper
        COMPANY_WEBSITE "https://github.com/TheLoopGolf/Looper"
        COMPANY_CODE Loop
        PLUGIN_MANUFACTURER_CODE Loop
        PLUGIN_CODE LoAp
        FORMATS ${LOOPER_FORMATS}
        PRODUCT_NAME "Looper"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )

    target_sources(Looper
        PRIVATE
            Source/Plugin/PluginProcessor.cpp
            Source/Plugin/PluginProcessor.h
            Source/Plugin/PluginEditor.cpp
            Source/Plugin/PluginEditor.h
            Source/Import/ImportController.cpp
            Source/Import/ImportController.h
            Source/Import/MapCommit.h
            Source/UI/MainView.cpp
            Source/UI/MainView.h
            Source/UI/ReviewMapView.cpp
            Source/UI/ReviewMapView.h
            Source/UI/SettingsView.cpp
            Source/UI/SettingsView.h
            Source/UI/RelocateView.cpp
            Source/UI/RelocateView.h
            Source/UI/LooperLookAndFeel.cpp
            Source/UI/LooperLookAndFeel.h
            Source/UI/ZoneKeyboardComponent.cpp
            Source/UI/ZoneKeyboardComponent.h
            Source/UI/LooperControls.cpp
            Source/UI/LooperControls.h
            Source/UI/Glyphs.h
            Source/Prefs/SessionPrefs.h
            Source/SamplePool/SamplePool.h
            Source/InstrumentMap/InstrumentMap.h
            Source/VoiceEngine/VoiceEngine.h
            Source/VoiceEngine/Hermite.h
            Source/VoiceEngine/FastRng.h
            Source/AmpEnv/AmpEnv.h
            Source/Filter/SvfFilter.h
            Source/MidiRouter/MidiRouter.h
            Source/PatchStore/PatchStore.h
            Source/PatchStore/SampleRelocator.h
            Source/ZoneEdit/ZoneEditor.h
            Source/ZoneEdit/ZoneEditAction.h
            Source/ZoneEdit/EditHistory.h
            Source/ZoneEdit/ZoneSelection.h
            Source/ZoneEdit/ZoneStrip.h
            Source/VoiceEngine/GainRamp.h
            Source/UI/ZoneEditorPanel.cpp
            Source/UI/ZoneEditorPanel.h
    )

    target_include_directories(Looper
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )

    target_compile_definitions(Looper
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(Looper
        PRIVATE
            LooperAutoMapper
            LooperDsp
            LooperPatch
            LooperZoneEdit
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_gui_basics
            juce::juce_gui_extra
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endif()

# ---------------------------------------------------------------------------
# Zone editor tests: edits, clamping, overlaps, auto/detected actions, persistence,
# live voice updates. With JUCE available (LOOPER_BUILD_PLUGIN) the same suite also
# drives the real juce::UndoManager + ZoneEditAction undo/redo path.
# ---------------------------------------------------------------------------
if(LOOPER_BUILD_TESTS)
    add_executable(ZoneEditorTests Tests/ZoneEditorTests.cpp)
    target_link_libraries(ZoneEditorTests PRIVATE LooperZoneEdit LooperDsp LooperPatch)
    if(LOOPER_BUILD_PLUGIN)
        target_link_libraries(ZoneEditorTests PRIVATE
            juce::juce_data_structures
            juce::juce_recommended_config_flags)
        target_compile_definitions(ZoneEditorTests PRIVATE
            LOOPER_ZONE_EDIT_WITH_JUCE=1
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
            JUCE_STANDALONE_APPLICATION=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
    endif()
    add_test(NAME ZoneEditorTests COMMAND ZoneEditorTests)
endif()

# ---------------------------------------------------------------------------
# Zone strip / multi-select tests: strip drag maths, key snapping, clamping, multi-selection
# relative/absolute edits, saved-state ("unsaved") tracking through undo, gain ramp on
# sounding notes, exact-zone audition. With JUCE the undo grouping runs on juce::UndoManager.
# ---------------------------------------------------------------------------
if(LOOPER_BUILD_TESTS)
    add_executable(ZoneStripTests Tests/ZoneStripTests.cpp)
    target_link_libraries(ZoneStripTests PRIVATE LooperZoneEdit LooperDsp LooperPatch)
    if(LOOPER_BUILD_PLUGIN)
        target_link_libraries(ZoneStripTests PRIVATE
            juce::juce_data_structures
            juce::juce_recommended_config_flags)
        target_compile_definitions(ZoneStripTests PRIVATE
            LOOPER_ZONE_EDIT_WITH_JUCE=1
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
            JUCE_STANDALONE_APPLICATION=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
    endif()
    add_test(NAME ZoneStripTests COMMAND ZoneStripTests)
endif()
