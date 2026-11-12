cmake_minimum_required(VERSION 3.22)
project(MERGEX VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

# ---- Pure C++ engine tests + demo renderer (no JUCE)
add_executable(mx_engine_tests tests/test_engine.cpp)
target_include_directories(mx_engine_tests PRIVATE src/engine tests)
add_test(NAME engine_textbook_math COMMAND mx_engine_tests)
add_executable(mx_render_demos tests/render_demos.cpp)
target_include_directories(mx_render_demos PRIVATE src/engine tests)

option(MX_ENGINE_ONLY "Build dependency-free engine tests only" OFF)
if(MX_ENGINE_ONLY)
  return()
endif()

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
  add_subdirectory(JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(MergeX
  VERSION 1.0.0
  COMPANY_NAME koobacorp
  BUNDLE_ID com.koobacorp.mergex
  PLUGIN_MANUFACTURER_CODE Koob
  PLUGIN_CODE MrgX
  FORMATS VST3
  PRODUCT_NAME "MergeX"
  DESCRIPTION "MergeX real-time sound morph"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Modulation)
target_sources(MergeX PRIVATE src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp)
target_include_directories(MergeX PUBLIC src/engine src/plugin)
target_compile_definitions(MergeX PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0 JUCE_USE_CAMERA=0 JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
target_link_libraries(MergeX PRIVATE juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_basics
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
target_compile_options(MergeX PUBLIC $<$<CONFIG:Release>:-O3>)
set_target_properties(MergeX PROPERTIES VISIBILITY_INLINES_HIDDEN ON CXX_VISIBILITY_PRESET hidden)
if(APPLE)
  target_link_options(MergeX PUBLIC -Wl,-dead_strip)
endif()

option(MX_BUILD_TOOLS "Build JUCE plugin tests and UI screenshots" ON)
if(MX_BUILD_TOOLS)
  juce_add_console_app(mx_plugin_tests PRODUCT_NAME mx_plugin_tests)
  target_sources(mx_plugin_tests PRIVATE tests/plugin_tests.cpp)
  target_include_directories(mx_plugin_tests PRIVATE src/engine src/plugin tests $<TARGET_PROPERTY:MergeX,INCLUDE_DIRECTORIES>)
  target_compile_definitions(mx_plugin_tests PRIVATE $<TARGET_PROPERTY:MergeX,COMPILE_DEFINITIONS>)
  target_link_libraries(mx_plugin_tests PRIVATE MergeX juce::juce_recommended_config_flags)
  add_test(NAME plugin_params_buses_latency_state_presets_editor COMMAND mx_plugin_tests)
endif()
