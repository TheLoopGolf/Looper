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
)
target_include_directories(LooperPatch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_compile_features(LooperPatch PUBLIC cxx_std_20)
set_target_properties(LooperPatch PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Unit tests (no plugin host required)
# ---------------------------------------------------------------------------
if(LOOPER_BUILD_TESTS)
    enable_testing()

    add_executable(AutoMapperTests Tests/AutoMapperTests.cpp)
    target_link_libraries(AutoMapperTests PRIVATE LooperAutoMapper)
    add_test(NAME AutoMapperTests COMMAND AutoMapperTests)

    add_executable(DspVoiceTests Tests/DspVoiceTests.cpp)
    target_link_libraries(DspVoiceTests PRIVATE LooperDsp)
    add_test(NAME DspVoiceTests COMMAND DspVoiceTests)

    add_executable(PatchStoreTests Tests/PatchStoreTests.cpp)
    target_link_libraries(PatchStoreTests PRIVATE LooperPatch)
    add_test(NAME PatchStoreTests COMMAND PatchStoreTests)

    add_executable(SessionPrefsTests Tests/SessionPrefsTests.cpp)
    target_link_libraries(SessionPrefsTests PRIVATE LooperPatch)
    add_test(NAME SessionPrefsTests COMMAND SessionPrefsTests)
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
            Source/UI/LooperLookAndFeel.cpp
            Source/UI/LooperLookAndFeel.h
            Source/UI/ZoneKeyboardComponent.cpp
            Source/UI/ZoneKeyboardComponent.h
            Source/Prefs/SessionPrefs.h
            Source/SamplePool/SamplePool.h
            Source/InstrumentMap/InstrumentMap.h
            Source/VoiceEngine/VoiceEngine.h
            Source/VoiceEngine/Hermite.h
            Source/AmpEnv/AmpEnv.h
            Source/Filter/SvfFilter.h
            Source/MidiRouter/MidiRouter.h
            Source/PatchStore/PatchStore.h
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
