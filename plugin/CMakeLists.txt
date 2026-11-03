juce_add_plugin(Magnetika
  COMPANY_NAME "Tener Jenkins"
  PRODUCT_NAME "Magnetika"
  PLUGIN_MANUFACTURER_CODE Tjnk
  PLUGIN_CODE Mgtk
  FORMATS VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth)

target_sources(Magnetika PRIVATE
  Parameters.cpp
  PluginProcessor.cpp
  PluginEditor.cpp)

target_compile_definitions(Magnetika PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

# The engine is its own static library with its own floating-point flags
# (no contraction, no fast-math); nothing here changes how it is compiled.
target_link_libraries(Magnetika
  PRIVATE
    magnetika_engine
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Plugin-layer tests link the plugin's shared code (the "Magnetika" target that
# juce_add_plugin creates, which every format wrapper links too).
juce_add_console_app(test-plugin PRODUCT_NAME "test-plugin")
target_sources(test-plugin PRIVATE tests/test_plugin.cpp)
target_compile_definitions(test-plugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(test-plugin PRIVATE Magnetika magnetika_engine juce::juce_audio_utils
  juce::juce_recommended_config_flags)
add_test(NAME plugin COMMAND test-plugin)
