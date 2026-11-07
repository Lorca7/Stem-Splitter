cmake_minimum_required(VERSION 3.22)

# macOS: build for Big Sur and newer (runs on Sonoma 14.6). Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(StemSplitter VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(STEMSPLITTER_BUILD_PLUGIN   "Build the plug-ins and the standalone app" ON)
option(STEMSPLITTER_BUILD_CLI      "Build the stemsplit command-line tool" ON)
option(STEMSPLITTER_BUILD_TESTS    "Build the automated tests" ON)
option(STEMSPLITTER_NATIVE_ARCH    "Optimise the inference engine for this machine's CPU only (not portable)" OFF)
option(STEMSPLITTER_USE_ACCELERATE "Use Apple Accelerate BLAS for matrix products on macOS" ON)
set(STEMSPLITTER_MODEL_DIR "" CACHE PATH
    "Folder with Demucs ggml model files to bundle inside the standalone app (optional)")
set(STEMSPLITTER_MODEL_BASE_URL "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main"
    CACHE STRING "Base URL the app downloads model files from")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------------------------
# JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------------------------
# Demucs v4 inference engine (C++17 + Eigen)
include(cmake/Demucs.cmake)


# ---------------------------------------------------------------------------------------------
# Shared engine sources (compiled into both the plug-in and the CLI)
set(STEMSPLITTER_CORE_SOURCES
    Source/Core/AudioIO.cpp
    Source/Core/DemucsSeparator.cpp
    Source/Core/FrequencyCut.cpp
    Source/Core/MixRenderer.cpp
    Source/Core/ModelManager.cpp
    Source/Core/SeparationJob.cpp
    Source/Core/SpectralSeparator.cpp
    Source/Core/WaveformPeaks.cpp)

set(STEMSPLITTER_UI_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/DropZone.cpp
    Source/UI/ExportPanel.cpp
    Source/UI/FrequencyCutView.cpp
    Source/UI/HeaderBar.cpp
    Source/UI/Icons.cpp
    Source/UI/ModelOverlay.cpp
    Source/UI/StemLane.cpp
    Source/UI/Theme.cpp
    Source/UI/TransportBar.cpp)

set(STEMSPLITTER_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    STEMSPLITTER_VERSION="${PROJECT_VERSION}"
    STEMSPLITTER_MODEL_BASE_URL="${STEMSPLITTER_MODEL_BASE_URL}")

# ---------------------------------------------------------------------------------------------
# Plug-in + Standalone app
if(STEMSPLITTER_BUILD_PLUGIN)
juce_add_plugin(StemSplitter
    PRODUCT_NAME                "Stem Splitter"
    COMPANY_NAME                "Stem Splitter"
    COMPANY_WEBSITE             "https://github.com/lorca7/stem-splitter"
    BUNDLE_ID                   "com.stemsplitter.StemSplitter"
    VERSION                     ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE    Stsp
    PLUGIN_CODE                 StSp
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Tools Fx
    AU_MAIN_TYPE                kAudioUnitType_Effect
    HARDENED_RUNTIME_ENABLED    TRUE
    DOCUMENT_EXTENSIONS         wav aif aiff flac mp3 m4a
    MICROPHONE_PERMISSION_ENABLED FALSE
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    NEEDS_CURL                  TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(StemSplitter PRIVATE
    ${STEMSPLITTER_CORE_SOURCES}
    ${STEMSPLITTER_UI_SOURCES}
    Source/StandaloneApp.cpp)

target_include_directories(StemSplitter PRIVATE Source)

target_compile_definitions(StemSplitter PUBLIC
    ${STEMSPLITTER_COMMON_DEFINITIONS}
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(StemSplitter
    PRIVATE
        stemsplitter_demucs
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optionally ship the neural-network models inside the standalone app bundle, so it works offline
# out of the box. The plug-ins find them there too (/Applications/Stem Splitter.app).
if(APPLE AND STEMSPLITTER_MODEL_DIR)
    file(GLOB STEMSPLITTER_BUNDLED_MODELS "${STEMSPLITTER_MODEL_DIR}/ggml-model-*.bin")
    if(STEMSPLITTER_BUNDLED_MODELS)
        add_custom_command(TARGET StemSplitter_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_BUNDLE_CONTENT_DIR:StemSplitter_Standalone>/Resources/Models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STEMSPLITTER_BUNDLED_MODELS}
                    "$<TARGET_BUNDLE_CONTENT_DIR:StemSplitter_Standalone>/Resources/Models/"
            COMMENT "Bundling Demucs models into Stem Splitter.app")
        message(STATUS "Stem Splitter: bundling models: ${STEMSPLITTER_BUNDLED_MODELS}")
    else()
        message(WARNING "STEMSPLITTER_MODEL_DIR is set but contains no ggml-model-*.bin files")
    endif()
endif()
endif() # STEMSPLITTER_BUILD_PLUGIN

# ---------------------------------------------------------------------------------------------
# Command-line tool (batch separation / testing)
if(STEMSPLITTER_BUILD_CLI)
    juce_add_console_app(stemsplit PRODUCT_NAME "stemsplit" NEEDS_CURL TRUE)

    target_sources(stemsplit PRIVATE ${STEMSPLITTER_CORE_SOURCES} Tools/stemsplit.cpp)
    target_include_directories(stemsplit PRIVATE Source)
    target_compile_definitions(stemsplit PRIVATE ${STEMSPLITTER_COMMON_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=0)

    target_link_libraries(stemsplit
        PRIVATE
            stemsplitter_demucs
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_events
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------------------------
# Tests:  ctest --test-dir build
if(STEMSPLITTER_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(StemSplitterTests PRODUCT_NAME "StemSplitterTests" NEEDS_CURL TRUE)
    target_sources(StemSplitterTests PRIVATE
        ${STEMSPLITTER_CORE_SOURCES} ${STEMSPLITTER_UI_SOURCES} Tests/ProcessorTests.cpp)
    target_include_directories(StemSplitterTests PRIVATE Source)
    target_compile_definitions(StemSplitterTests PRIVATE
        ${STEMSPLITTER_COMMON_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Stem Splitter"
        JucePlugin_Build_Standalone=0)
    target_link_libraries(StemSplitterTests PRIVATE
        stemsplitter_demucs
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test(NAME processor COMMAND StemSplitterTests)

    if(STEMSPLITTER_BUILD_CLI)
        add_test(NAME engine COMMAND stemsplit --selftest)
    endif()
endif()
