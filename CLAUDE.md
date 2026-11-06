# MidiSeer notes for Claude

- The owner is not a programmer: explain changes in plain language and keep CI green, since CI builds are how they get plugins to test.
- `engine/` must stay free of JUCE so it remains unit-testable. Put musical logic there with Catch2 tests in `tests/`.
- One plugin target, `MidiSeer` (VST3 + CLAP, an instrument with MIDI out; a note effect in Bitwig). An AU MIDI effect for Logic can follow PatternArp's pattern (`JucePlugin_IsMidiEffect`).
- It must work for software synths and hardware over 5-pin MIDI: keep the CC rate limit (`Seer::minGapMs`) in mind. Main DAW is Bitwig; Ableton and Logic must also work.
- Shared look (colours, spacing, fonts, LookAndFeel, generic widgets) lives in the `libs/plugin-ui-kit` submodule (JUCE module `tng_ui`, namespace `tng`), with its `STYLE_GUIDE.md`. Put anything another plugin could reuse in the kit; changes there go to the kit repo first, then bump the submodule here.
- Local Linux build deps are listed in `.github/workflows/build.yml`. The smoke test saves an editor picture with MIDISEER_SNAPSHOT=path.png (MIDISEER_STATE=state.xml to load a state first).
