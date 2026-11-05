cmake_minimum_required(VERSION 3.22)

# TB-303 circuit model as a VST3 / AU / standalone instrument. The sound engine is the web app's,
# ported line for line to Source/dsp/TB303Model.h.
project(TB303CircuitModel VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

# JUCE: a local checkout via -DJUCE_DIR=..., otherwise fetched from GitHub
set(JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (optional)")
if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

set(TB303_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND TB303_FORMATS AU)
endif()

juce_add_plugin(TB303CircuitModel
  COMPANY_NAME "Johnny Hawk"
  COMPANY_WEBSITE "https://tb303-circuit-model.pages.dev"
  BUNDLE_ID "dev.johnnyhawk.tb303circuitmodel"
  PLUGIN_MANUFACTURER_CODE Jhwk
  PLUGIN_CODE T3cm
  PRODUCT_NAME "TB303 Circuit Model"
  FORMATS ${TB303_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  NEEDS_WEB_BROWSER TRUE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TB303CircuitModel PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

# the window is the web app's own page: Resources/ui.html (written by `node build.js` from src/, with the factory
# presets and patterns inside), the VT323 pixel font it uses (SIL Open Font License, Resources/VT323-OFL.txt),
# and the Conv reverb's recorded impulse responses (../irs, credits in irs/CREDITS.md) with their list
file(GLOB TB303_IRS ${CMAKE_CURRENT_SOURCE_DIR}/../irs/*.flac)
juce_add_binary_data(TB303Data SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui.html ${CMAKE_CURRENT_SOURCE_DIR}/Resources/VT323-Regular.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/../irs/irs.json ${TB303_IRS})

target_compile_definitions(TB303CircuitModel PUBLIC
  JUCE_WEB_BROWSER=1
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

# match JavaScript's arithmetic: no fused multiply-adds (see tests/ and TB303Model.h)
target_compile_options(TB303CircuitModel PRIVATE -ffp-contract=off)

target_link_libraries(TB303CircuitModel
  PRIVATE TB303Data juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# parity test: the C++ engine against the web app's JS, sample for sample (run: ctest, needs node)
add_executable(tb303_render tests/render.cpp)
target_compile_options(tb303_render PRIVATE -ffp-contract=off)
target_compile_definitions(tb303_render PRIVATE SCENARIO="${CMAKE_CURRENT_SOURCE_DIR}/tests/scenario.json")
enable_testing()
find_program(NODE node)
if(NODE)
  add_test(NAME parity COMMAND ${CMAKE_COMMAND}
    -DNODE=${NODE} -DRENDER=$<TARGET_FILE:tb303_render> -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/tests -DOUT=${CMAKE_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity.cmake)
endif()

# host-style test of the processor: transport sync and MIDI (run: tb303_hosttest)
juce_add_console_app(tb303_hosttest PRODUCT_NAME "tb303_hosttest")
target_sources(tb303_hosttest PRIVATE tests/host_test.cpp Source/PluginProcessor.cpp)
target_compile_definitions(tb303_hosttest PRIVATE TB303_NO_EDITOR=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="TB303 Circuit Model" JUCE_STANDALONE_APPLICATION=1)
target_compile_options(tb303_hosttest PRIVATE -ffp-contract=off)
target_link_libraries(tb303_hosttest PRIVATE TB303Data juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
add_test(NAME host COMMAND tb303_hosttest)
