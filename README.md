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
| 2 | JUCE VST3 + Standalone: CC map, parameters, 16 voices, material picker | done |
| 3 | The instrument editor: play view (oblique 3-D, touch) and modify view (cut-away, faceplate) | done |
| 4 | MPE: per-note bend, pressure and slide; MPE configuration messages | done |
| 5 | The instrument as built: rev B bowl sizes, simulated build variation per bowl and bar | done |
| later | `.scl` microtuning | |

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
  PluginEditor.*       play page / modify page on a 1200 x 800 canvas, scaled to a resizable 3:2 window
  ui/GlassView.*       the armonica in oblique 3-D (37 bowls, rev B sizes); touch, glissando, rubbing
  ui/BarsView.*        the vibraphone in oblique 3-D; strike, roll, strike position
  ui/PlayPage.*        pedals, spindle, presets, rotation / vibrato strips, note strip, level and meters
  ui/ModifyPage.*      live cut-away, aluminium faceplate with knobs, ADVANCED drawer (every parameter)
  ui/PresetStore.h     user presets in Documents/Magnetika/Presets (*.magnetika)
  ui/Theme.*           walnut, brass, aluminium, amber; IBM Plex fonts (SIL OFL, ui/fonts/OFL.txt)
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
2.5 s bar damper, 16 voices instead of 8, and a build variation of 0.5. Validated with pluginval
at strictness 10.

### Onset and sustain

- **Coil start** (`Glass coil start`, default 0.1 N, 0 = off): from rest, the finger's stick-slip
  needs 0.7–1.4 s at 1 rev/s before a bowl is even audible (higher bowls slowest). The rev B
  design has coil-started notes, so at note-on the coils drive the bowl's (2,0) mode at its
  frequency for up to 120 ms (velocity-scaled, smooth, stopped once the mode is at half the
  sustain setpoint); the rub then takes over. Measured: audible in 0.075–0.09 s, −6 dB in about
  0.4 s (0.15 s on a fixed bowl), steady tone unchanged. Needs the coils (armed, the default).
  The burst rings like a soft strike and decays over the glass's long tail whatever the finger
  then does.
- **Sustain fade** (`Sustain fade`, default 6 s, 0 = hold): with the sustain pedal down, the
  coils hold a note after the finger lifts. Held forever, every key played stays in the sound;
  so the coils' setpoint for a released note decays to −60 dB over this time. Notes still held
  keep full sustain. Both instruments.

### The instrument as built

- **Build variation** (0–1, default 0.5) and **Instrument No.** (1–999): every bowl and bar
  differs a little, as real ones do: tuning (±3 ¢ at full variation), Q (±20 %), the A–B split
  (±40 %), the principal-axis angle (±45°), the Bryan factor (±15 %), the partial ratios (±1 %).
  This is *simulated*, not measured: rev B lists only each bowl's pitch, size and mass, and leaves
  these values to a calibration routine on the finished hardware. The pattern is fixed by the
  instrument number, so one instrument always sounds the same; changes apply from the next note.
  Projects and presets saved before this setting existed load with variation 0, so they sound
  as they did. Both are in the modify view's top bar (No. ‹ › and the VAR knob).
- **Rev B bowl sizes** (off by default): each bowl's rim at its rev B diameter (147 mm across at
  C4 to 73 mm at C6, √2 per octave beyond) instead of 90 mm for all, so a big bowl's rim runs
  faster under the finger. In this friction model the stick-slip stops holding past about
  0.35 m/s of rim speed, so at the default 1 rev/s the bowls below about G4 stop speaking. Play
  rev B sizes at about **0.5 rev/s**, where every bowl speaks and the registers are more even
  (C3 −21 dB to C6 −13 dB, against −28 to −10 dB at 90 mm). MPE slide limits scale with each
  bowl's radius. It is the REV B switch on the glass faceplate; both views show a reminder while
  the spindle is too fast for it.

On a machine with little free memory, build JUCE with `-- /m:1 /p:CL_MPCount=2` (one project at
a time, two compiler processes, about 3 GB).

### The editor

**Play view.** The instrument at an angle, drawn from the hardware design's tables in
millimetres. Sounding bowls glow and show their (2,0) vibration pattern turning at the angle the
engine reports; sounding bars light up.

- *Glass:* press a rim to sound it (pen or touch pressure sets the finger load), slide across rims
  for a glissando, several fingers at once. With the spindle stopped a bowl only sounds while the
  finger rubs along its rim.
- *Bars:* tap to strike; where along the bar sets the strike position; drag across bars for a
  roll; holding keeps that bar's damper off.
- Header: SUSTAIN, BRAKE (their lamps also follow MIDI pedals), SPINDLE and its speed (glass) or the
  DAMPER tail (bars), MODIFY. Second row: presets (materials or mallet setups, then user presets,
  with SAVE), the ROTATION strip (glass) and VIBRATO. A note strip C3–C6 latches notes and lights a
  lamp for notes outside the drawn range. Level knob, output meters, LIMIT and MIDI lamps, voices,
  and the MPE switch (lit while MPE is active, including when a controller turned it on).

**Modify view.** A live cut-away (a bowl seen along the spindle with its band, coils and sensors;
or a bar in side view with magnet, coil and sensor), a brushed-aluminium faceplate of lit switches
and twelve knobs per instrument, an ADVANCED drawer with every parameter, and pedals, keyboard and
meters. DONE returns to the play view.

`test-plugin --snapshot out.png [glass|bars|modify|modify-bars][-mpe]` renders the editor offscreen with a
chord playing, for looking at the UI without a desktop session.

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

### MPE

With the **MPE** switch on (play view, or ADVANCED › Performance), member channels carry
per-note expression. The master channel, and any channel outside the zone, keep the map above.
The switch also turns on, with the controller's zone layout, when an MPE Configuration Message
(RPN 6 on channel 1 or 16) arrives. Without one, MPE uses the usual lower zone: master channel 1,
members 2–16.

| Per-note control | Glass | Bars |
|---|---|---|
| Pitch bend | the coils bend that bowl; past their reach the finger slides to the next bowl | bends that bar (clamped) |
| Pressure (channel or poly aftertouch) | how firmly the finger presses: whether and how the glass speaks | — |
| Slide (CC74) | rub speed: the loudness | where the mallet lands (at note-on) |

The glass plays like a bowed string, and the mapping follows what the model does (measured on
C4–C6):

- **Pressure is finger load, like bow force.** It decides whether the glass speaks, not how
  loud it is. A resting finger (0) whispers; every note has caught by about 30 % of the travel;
  the rest goes from a clean tone to a pressed, choked one at about the same level. The strike's
  velocity sets the first contact, and pressure takes over once it reaches that load, so a hard
  strike is not silenced by a controller whose pressure starts at 0. From then on the catch point
  is the same however hard the note was struck.
- **Slide is rub speed, like bow speed, and so the loudness.** Rubbed glass gets louder with the
  finger's speed against the rim up to about 4–6 rad/s, then stops speaking. Spindle stopped:
  bottom is a still finger (silent), centre the finger-speed setting, top 4.5 rad/s. Spindle
  running: the finger moves with the glass (a slower rub, quieter) below centre, or against it
  above, never past 5 rad/s unless the spindle alone already is. Centre plays as without MPE.
- **Glide.** A real armonica cannot glide; a glissando is a finger crossing rims. Past half a
  semitone (plus 5 ¢, so it does not flicker) the finger hands off to the nearest bowl, whose
  coils supply the rest; the bowl left behind rings on. A semitone glide is continuous within
  each bowl and steps about 20 ¢ at each hand-off. `MPE glide across bowls` turns this off.

`MPE bend range` (default 48 semitones, the MPE standard, or RPN 0 from the controller) says what a
full member-channel bend means. The coils' spring is limited to `Pitch-bend range` (at most
±40 ¢). Expression sent before a note-on applies to that note. Two channels on the same note
play one bowl or bar, which sounds at the first and is released at the last.

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

MIT — see [LICENSE](LICENSE). The plugin is built with JUCE 9, which is licensed separately and
fetched at configure time, never vendored: building it yourself needs a JUCE licence (the free
Starter tier covers individuals) or the AGPLv3 option. The engine and reference tools do not use
JUCE. The embedded IBM Plex fonts are under the SIL Open Font License 1.1
([plugin/ui/fonts/OFL.txt](plugin/ui/fonts/OFL.txt)).
