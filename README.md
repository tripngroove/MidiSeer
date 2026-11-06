# MidiSeer

A MIDI plugin that sends a CC ramp with every note: the CC starts at one value when the note starts and arrives at another exactly as the note ends, however long the note is. It sits on an instrument track before a software synth or a hardware synth and passes the notes on untouched.

**Status:** first version, CLAP in Bitwig on macOS first (VST3 builds too).

## How it knows how long a note is

No DAW tells a plugin how long a note will be when it starts. MidiSeer learns it instead:

1. **First time through**, a note MidiSeer hasn't heard ramps over the length of the last note that ended (one beat before any has). If the note ends sooner, the CC glides to the end value in 10 ms; if it lasts longer, the CC waits at the end value.
2. **Every note off** teaches MidiSeer that note's length, remembered by where it starts in the song and its pitch.
3. **From the second time through**, every ramp lands exactly as its note ends. Edit a note and MidiSeer relearns it the next time it plays.

To skip the first pass, drop a MIDI file of the clip on the plugin (or click **Load MIDI file**). Its notes count from the song start, or from the loop start when Loop is on. In Bitwig, drag a clip to the Finder to get its MIDI file. **Forget all** clears what has been learned. What has been learned is saved with the project.

Notes played with the song stopped are not learned (there is no song position), but still set the guess for the next note.

## Settings

| Setting | What it does |
| --- | --- |
| CC | Which CC number the ramps send (default 1, the mod wheel; 74 with MPE on). |
| Start | The CC value sent with each note on. |
| End | The value each ramp reaches as its note ends. It stays there after the note. |
| Curve | The ramp's shape: 0% is a straight line, + starts slowly and speeds up, - starts fast and settles. |
| Loop | Off: positions follow the song (the arranger). For a clip in the launcher, set the clip's length in bars, so every pass of the loop finds the same notes. Relaunching the clip at a different point of the bar may make MidiSeer relearn. |
| MPE | Off: one ramp for every note. A new note restarts it, and notes that start together (a chord) follow the longest one. On: every note gets its own ramp on its own MIDI channel. Notes on channel 1 (a normal clip) are spread over channels 2 to 16; notes already on their own channels keep them. Switch MPE on in the synth too. |

All of them can be automated. Click a value to pick from a list, or drag it up and down.

## Hardware and software synths

MidiSeer sends a new CC value only when it changes, and at most once every 4 ms on each channel. That is smooth for software synths and uses about a quarter of a 5-pin MIDI cable's capacity for one ramp, leaving room for the notes. The start value goes out just before its note, so the synth starts from it.

## Getting a test build

Every push builds the plugin for macOS and Linux on GitHub.

1. Open the [Actions tab](../../actions), pick the latest green **Build** run, and download `MidiSeer-macOS`.
2. Unzip it and copy `MidiSeer.clap` to `~/Library/Audio/Plug-Ins/CLAP/` (and `MidiSeer.vst3` to `~/Library/Audio/Plug-Ins/VST3/`).
3. Test builds are not signed yet, so clear the download quarantine once per build:
   ```sh
   xattr -cr ~/Library/Audio/Plug-Ins/CLAP/MidiSeer.clap ~/Library/Audio/Plug-Ins/VST3/MidiSeer.vst3
   ```
4. Rescan plugins in your DAW. In Bitwig, quit and reopen to load a new build.

## Building from source

Needs CMake 3.28+, a C++20 compiler and Ninja (optional). Dependencies (JUCE 9, clap-juce-extensions, Catch2) are downloaded automatically. The shared UI kit is a git submodule, so clone with `--recursive`, or run `git submodule update --init` once in an existing checkout.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Plugins land in `build/plugin/MidiSeer_artefacts/Release/`.

## Layout

| Folder | What it holds |
| --- | --- |
| `engine/` | The ramp engine and the learned-length map: plain C++ with no plugin code, so it can be tested on its own. |
| `plugin/` | The JUCE plugin wrapper (MIDI in/out, host timing, parameters, saving, UI in `plugin/ui/`). |
| `libs/plugin-ui-kit/` | The look shared by tripngroove plugins and its [style guide](libs/plugin-ui-kit/STYLE_GUIDE.md). A git submodule of [tripngroove/plugin-ui-kit](https://github.com/tripngroove/plugin-ui-kit). |
| `tests/` | Catch2 tests for the engine, plus a plugin smoke test. |
| `.github/workflows/` | CI: builds, runs tests, validates with pluginval and clap-validator, and uploads the plugins. |

## Licence note

MidiSeer uses [JUCE](https://juce.com), which is free under AGPLv3 or its Starter licence for small projects. Check JUCE's current terms before distributing builds.
