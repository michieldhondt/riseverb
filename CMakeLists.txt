cmake_minimum_required(VERSION 3.22)
project(RiseVerb VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(RV_FORMATS AU VST3 Standalone)
else()
    set(RV_FORMATS VST3 Standalone)
endif()

# JUCE is fetched by the build script / CI into ./JUCE
add_subdirectory(JUCE)

juce_add_plugin(RiseVerb
    COMPANY_NAME "M1lobeats"
    BUNDLE_ID "com.m1lobeats.riseverb"
    PLUGIN_MANUFACTURER_CODE Mchl
    PLUGIN_CODE Rsvb
    FORMATS ${RV_FORMATS}
    PRODUCT_NAME "RiseVerb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(RiseVerb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(RiseVerb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(RiseVerb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(RV_BUILD_TEST "Build headless test app" OFF)
if(RV_BUILD_TEST)
    juce_add_gui_app(RiseVerbTest PRODUCT_NAME "RiseVerbTest")
    target_sources(RiseVerbTest PRIVATE tests/main.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(RiseVerbTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(RiseVerbTest PRIVATE juce::juce_audio_utils juce::juce_audio_formats)
endif()
