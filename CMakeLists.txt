cmake_minimum_required(VERSION 3.22)

# Version comes from the topmost "## [x.y.z]" heading in CHANGELOG.md.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" TD_CHANGELOG_HEADINGS REGEX "^## \\[[0-9]+\\.[0-9]+\\.[0-9]+\\]")
list(LENGTH TD_CHANGELOG_HEADINGS TD_HEADING_COUNT)
if(TD_HEADING_COUNT EQUAL 0)
    message(FATAL_ERROR "CHANGELOG.md has no '## [x.y.z]' heading; cannot determine the plug-in version.")
endif()
list(GET TD_CHANGELOG_HEADINGS 0 TD_TOP_HEADING)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" TD_VERSION "${TD_TOP_HEADING}")
message(STATUS "TetraDrive version ${TD_VERSION} (from CHANGELOG.md)")

project(TetraDrive VERSION ${TD_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

add_subdirectory(external/JUCE)

# Re-run configure when the changelog changes so the version stays current.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md")

# Build info: short git hash and configure date.
find_package(Git QUIET)
set(TD_GIT_HASH "")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE TD_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
string(TIMESTAMP TD_BUILD_DATE "%Y-%m-%d")

set(TD_FORMATS VST3)
if(APPLE)
    list(APPEND TD_FORMATS AU)
endif()

juce_add_plugin(TetraDrive
    VERSION                  ${TD_VERSION}
    COMPANY_NAME             "Daddasounds"
    COMPANY_WEBSITE          "https://daddasounds.com"
    BUNDLE_ID                com.daddasounds.tetradrive
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Ddsd
    PLUGIN_CODE              Tdrv
    FORMATS                  ${TD_FORMATS}
    VST3_CATEGORIES          Fx Distortion
    AU_MAIN_TYPE             kAudioUnitType_Effect
    PRODUCT_NAME             "TetraDrive")

# JUCE's .rc rule does not depend on the version info; drop it so a version bump regenerates it.
get_target_property(TD_GENERATED_DIR TetraDrive JUCE_GENERATED_SOURCES_DIRECTORY)
file(REMOVE "${TD_GENERATED_DIR}/TetraDrive_resources.rc")

juce_add_binary_data(TetraDriveData
    NAMESPACE TetraDriveBinary
    SOURCES
        resources/About.json
        CHANGELOG.md
        resources/fonts/InterVariable.ttf
        resources/icons/power.svg
        resources/icons/prev.svg
        resources/icons/next.svg
        resources/icons/save.svg
        resources/icons/info.svg
        resources/icons/help.svg
        resources/icons/close.svg
        resources/icons/trash.svg)

set(TD_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/SpectrumAnalyzer.cpp
    Source/gui/TetraLookAndFeel.cpp
    Source/gui/SpectrumBandView.cpp
    Source/gui/BandStrip.cpp
    Source/gui/GlobalPanel.cpp
    Source/gui/PresetBar.cpp
    Source/gui/HelpBar.cpp
    Source/gui/AboutPanel.cpp
    Source/gui/Logo.cpp)

target_sources(TetraDrive PRIVATE ${TD_PLUGIN_SOURCES})
target_include_directories(TetraDrive PRIVATE src Source)

set(TD_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TD_GIT_HASH="${TD_GIT_HASH}"
    TD_BUILD_DATE="${TD_BUILD_DATE}")

target_compile_definitions(TetraDrive PUBLIC ${TD_COMMON_DEFINITIONS})

target_link_libraries(TetraDrive
    PRIVATE
        TetraDriveData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless engine tests (no JUCE): cmake -DTD_BUILD_TESTS=ON, then ctest or run td_engine_test.
option(TD_BUILD_TESTS "Build the headless engine tests" OFF)
if(TD_BUILD_TESTS)
    add_executable(td_engine_test tests/EngineTest.cpp)
    target_include_directories(td_engine_test PRIVATE src)
    if(MSVC)
        target_compile_options(td_engine_test PRIVATE /W4)
    else()
        target_compile_options(td_engine_test PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion)
    endif()
    enable_testing()
    add_test(NAME td_engine_test COMMAND td_engine_test)
endif()
