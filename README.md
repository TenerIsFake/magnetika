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

| Phase | Work | State |
|---|---|---|
| 0 | Reference engine extracted from the browser player; 17 reference renders | done |
| 1 | Plain C++ engine + offline renderer, matched against the reference | engine done |
| 2 | JUCE VST3 + Standalone: CC map, presets, 16 voices | |
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
```

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
- AU is not planned. VST3 and Standalone cover the Windows hosts this is built for.

## License

MIT — see [LICENSE](LICENSE). The plugin shell (phase 2) will be built with JUCE, which is
licensed separately: building it yourself needs a JUCE licence (the free Starter tier covers
individuals) or the AGPLv3 option. The engine and reference tools do not use JUCE.
