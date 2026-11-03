# Magnetika

A physical-model instrument plugin: glass bowls and metal bars driven by electromagnetic coils.

Each glass bowl is modelled as its two degenerate (2,0) modes plus the (3,0) pair, played with a
wet finger (stick-slip friction) and controlled as a pair through virtual coils, the way a
hemispherical resonator gyroscope controls its resonator. That gives sustain after the finger
lifts, braking, vibrato and pitch bend through an electronic spring, electronic trimming of the
mode split, and rotation of the standing-wave pattern around the bowl, which is heard as a
swirl across the stereo field. Bars use the same coil laws with a mallet.

It is the software half of a hardware design (a rotating-mode glass armonica with a fired
magnetic band on each rim). The plugin does not need the hardware to exist.

## Status

**Phase 1: C++ engine.** The original engine is a JavaScript AudioWorklet in a browser player
(`reference/original/player.html`). It is extracted, seeded, and rendered into reference audio,
and the C++ engine in `engine/` reproduces those renders: 7 of 17 scenarios are bit-identical,
and the rest differ only by last-bit maths-library rounding (worst −202 dB). The test limit is
−150 dB. A 2 % change to one minor coupling coefficient already lands at −78 dB.
Checked with GCC 13 (Linux) and MSVC 2026 (Windows, worst scenario −190.6 dB): the limit holds
for both maths libraries.

| Phase | Work | State |
|---|---|---|
| 0 | Reference engine extracted from the browser player; 17 reference renders | done |
| 1 | Plain C++ engine + offline renderer, matched against the reference | done |
| 2 | JUCE VST3 + Standalone: CC map, parameters, 16 voices, material picker | plugin shell done |
| 3 | Bowl-view editor | |
| 4 | MPE, per-bowl tuning table, `.scl` microtuning | |

## Layout

```
reference/
  original/            the browser player and single-bowl simulation, unmodified
  engine.mjs           GENERATED from original/player.html, plus a seeded RNG
  player-defaults.json GENERATED: the player's slider defaults, which it pushes at start
  scenarios.mjs        one scenario per mechanism (sustain, brake, rotation, trim, ...)
  render.mjs           renders the scenarios through the player's own control flow
  manifest.json        sha256 + level of every reference render
engine/
  include/magnetika/   bowl, bar, synth, params; jsmath (V8-compatible hypot, xorshift32)
  src/
  tests/               behaviour the reference does not have (armed coils, voice stealing)
tools/
  extract-reference.mjs
  render-cli/          replays renders/trace.txt through the C++ engine
  compare.mjs          port vs reference, per scenario
plugin/                JUCE 9 VST3 + Standalone (Windows/macOS builds)
  Parameters.*         every player control, plus armed coils, damper, mass scaling
  PluginProcessor.*    sample-accurate MIDI, last-writer-wins between CC and parameter
  PluginEditor.*       plain controls + keyboard with the C3–C6 instrument range marked
  tests/               MIDI timing, block-size independence, CC mapping, state, mono bus
```

## Building the plugin

On Windows with Visual Studio 2022 or later (JUCE 9.0.5 is fetched by CMake at configure time):

```sh
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

This produces `build/plugin/Magnetika_artefacts/Release/VST3/Magnetika.vst3` and
`.../Standalone/Magnetika.exe`. Copy the `.vst3` folder to `C:\Program Files\Common Files\VST3`.
On Linux the plugin is off by default (`-DMAGNETIKA_PLUGIN=ON` needs JUCE's X11/ALSA dependencies);
the engine and its tests build everywhere.

Plugin defaults depart from the reference player: coils always armed, a 1.5 s glass damper and a
2.5 s bar damper, and 16 voices instead of 8. Validated with pluginval at strictness 10.

## Running

Node 18 or later, CMake 3.22 or later, a C++17 compiler. No other dependencies.

```sh
npm run reference          # extract the engine, render renders/*.wav and renders/trace.txt
npm run reference:check    # fail if the extraction or any render has changed
npm test                   # build, then run every test, including the port against the reference
```

`renders/` holds 32-bit float stereo WAVs at 48 kHz. They are not committed; `manifest.json` is.
The reference renderer also writes `renders/trace.txt`, every call it made into the engine,
which `render-cli` replays.

The engine must be compiled without floating-point contraction or fast-math (the CMake
build sets this); otherwise the comparison with the reference loses its meaning.

## MIDI

The plugin keeps the browser player's map, so the two are interchangeable on one controller.

| Control | Effect |
|---|---|
| Note on / off | finger on the bowl (velocity → finger load) / finger lifts. Bars: mallet strike |
| CC64 | sustain: coils hold the note after the finger lifts |
| CC67 / CC66 | brake: coils oppose the rim velocity |
| CC1 | vibrato depth, 0–30 ¢ |
| CC74 | drive-axis rotation, −1 … +1 rev/s |
| CC71 | electronic split trim |
| CC11 / aftertouch | finger load |
| Pitch bend | ±40 ¢, limited by the spring force cap |
| Program 0 / 1 | glass / bars |

## Notes on the port

- **"Coils always armed"** (default on): in the original, pitch bend and vibrato act only while
  sustain or brake is engaged, because the electronic spring is the coils. The plugin keeps the
  spring and trim live at zero sustain gain unless the switch is off.
- Voice stealing never takes a held note (the original can, with all eight voices held).
- Voices are preallocated; nothing allocates on the audio thread.
- **Damper** (`damperT60`, seconds): after release, a dry finger on the bowl (the traditional way
  to stop an armonica note) or the damper bar on a vibraphone. It brings the tail to the set
  60 dB time, whatever the note's mass, and stays off while the sustain coils hold the note.
  It exists because with constant Q a low note rings for a long time (about 17 s at C2), and the
  force-limited brake cannot stop it quickly.
- **Nyquist guard**: a mode that would sit above half the sample rate is muted in the output
  (it still runs, since it couples through the finger). This is decided once per note, so it never
  switches mid-note. At 48 kHz it affects bars from MIDI 114 and glass from MIDI 121, all above
  the real instrument's C3–C6 range.
- **Modal-mass scaling** (optional, off): mass follows the design tables (glass doubles per octave
  down, bars by √2), anchored at C5. It is off because the finger and coil forces do not scale
  with it. A C3 bowl then barely speaks in its first second, and how hard a player presses a big
  bowl is not modelled. The bench rig is what would calibrate it.
- Upward-only "always on" protections: adaptive substeps keep the top of the keyboard stable (the
  original diverges from MIDI 119), and a non-finite voice is freed rather than silencing the mix.

All of these are off, or cannot trigger, in the reference range, so the port-vs-reference test
still holds with them compiled in.
- AU is not planned. VST3 and Standalone cover the Windows hosts this is built for.

## License

MIT — see [LICENSE](LICENSE). The plugin shell (phase 2) will be built with JUCE, which is
licensed separately: building it yourself needs a JUCE licence (the free Starter tier covers
individuals) or the AGPLv3 option. The engine and reference tools do not use JUCE.
