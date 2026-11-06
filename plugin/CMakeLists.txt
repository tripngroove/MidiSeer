# MidiSeer: VST3 + CLAP. It loads as an instrument that outputs MIDI, which is how VST3 hosts
# accept note effects; in Bitwig (CLAP) it is a note effect. An AU MIDI effect for Logic can
# follow the way PatternArp does it.

juce_add_plugin(MidiSeer
    PRODUCT_NAME "MidiSeer"
    COMPANY_NAME "tripngroove"
    BUNDLE_ID "com.tripngroove.midiseer"
    PLUGIN_MANUFACTURER_CODE Trpg
    PLUGIN_CODE Mdsr
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Generator
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MidiSeer PRIVATE
    LearnedMap.cpp
    LearnedMap.h
    PluginEditor.cpp
    PluginEditor.h
    PluginProcessor.cpp
    PluginProcessor.h
    ui/LearnedView.cpp
    ui/LearnedView.h
    ui/RampView.cpp
    ui/RampView.h)

target_compile_definitions(MidiSeer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MidiSeer
    PRIVATE
        midiseer_engine
        tng::tng_ui
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET MidiSeer
    CLAP_ID "com.tripngroove.midiseer"
    CLAP_FEATURES note-effect utility)
