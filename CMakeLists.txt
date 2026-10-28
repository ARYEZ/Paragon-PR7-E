cmake_minimum_required(VERSION 3.24)

# macOS: build one universal binary for Apple Silicon and Intel Macs.
# These must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(ParagonX1 VERSION 0.20.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PARAGON_BUILD_TESTS "Build the unit tests" ON)

include(cmake/Dependencies.cmake)

juce_add_plugin(ParagonX1
    PRODUCT_NAME             "Paragon PR7-X" # the utility processor of the PR7 family (PR7-E until 0.20.0): its own name, bundle ID and plugin code, so it installs beside the others
    COMPANY_NAME             "Paragon"
    BUNDLE_ID                "com.paragon.paragonpr7e"   # kept from PR7-E, with the plugin code: the same plugin to a DAW, so projects open it
    PLUGIN_MANUFACTURER_CODE Prgn     # four characters, at least one upper case
    PLUGIN_CODE              Pr7e     # kept from PR7-E (renamed PR7-X in 0.20.0): changing it would orphan every saved project; four characters, exactly one upper case; never PR7-G's Px1a, PR7-B's Pr7b, PR7-V's Pr7v or PR7-C's Pr7c
    FORMATS                  VST3
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE    # MIDI CC 11 for the expression pedal
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    VST3_CATEGORIES          Fx Tools   # a utility, so DAWs file it with the tools
    COPY_PLUGIN_AFTER_BUILD  FALSE)

# Inter (SIL Open Font License, see resources/fonts/OFL-LICENSE.txt), built
# into the plugin so it looks the same on every computer, as in the mockup.
juce_add_binary_data(ParagonX1Fonts
    HEADER_NAME ParagonFonts.h
    NAMESPACE   ParagonFonts
    SOURCES
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-SemiBold.ttf
        resources/fonts/InterDisplay-BlackItalic.ttf)

target_sources(ParagonX1
    PRIVATE
        source/Parameters.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/dsp/PitchDetector.cpp
        source/dsp/Tuner.cpp
        source/dsp/dominion/Dominion.cpp
        source/dsp/oracle/Oracle.cpp
        source/dsp/effects/Compressor.cpp
        source/dsp/effects/MultibandComp.cpp
        source/dsp/effects/Limiter.cpp
        source/dsp/effects/Loudness.cpp
        source/dsp/effects/MasterLimiter.cpp
        source/dsp/eq/EqDesign.cpp
        source/dsp/eq/ParametricEq.cpp
        source/dsp/eq/EqMatch.cpp
        source/dsp/eq/EqMatchSession.cpp
        source/rigs/Rig.cpp
        source/rigs/FactoryRigs.cpp
        source/rigs/DominionPresets.cpp
        source/rigs/CompPresets.cpp
        source/rigs/LimiterPresets.cpp
        source/rigs/RigLibrary.cpp
        source/ui/LevelMeter.cpp
        source/ui/Logo.cpp
        source/ui/Footswitch.cpp
        source/ui/EffectPanel.cpp
        source/ui/RigBrowser.cpp
        source/ui/Icons.cpp
        source/ui/PedalBody.cpp
        source/ui/LedDisplay.cpp
        source/ui/SlotWindow.cpp
        source/ui/SignalChain.cpp
        source/ui/DockPanels.cpp
        source/ui/EqDisplay.cpp
        source/ui/EqPage.cpp
        source/ui/EqMatchPanel.cpp
        source/ui/DominionPage.cpp
        source/ui/OraclePage.cpp
        source/ui/CompPage.cpp
        source/ui/LimiterPage.cpp
        source/ui/RackUnit.cpp
        source/ui/RackSpecs.cpp
        source/ui/PresetButton.cpp
        source/ui/PerformanceControls.cpp
        source/ui/MainView.cpp
        source/ui/ParagonLookAndFeel.cpp
        source/ui/Finish.cpp)

target_include_directories(ParagonX1 PUBLIC source)

target_compile_definitions(ParagonX1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1)   # EQ Match can load MP3s as references

target_link_libraries(ParagonX1
    PRIVATE
        ParagonX1Fonts
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_warning_flags   # strict warnings for our code only
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(PARAGON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
