// Reference scenarios: each exercises one mechanism of the engine, so a port
// mismatch points at the mechanism that diverged. Times in seconds; events are
// quantised to 128-sample blocks by the renderer, as in the browser player.
//
// Event types (the player's own controls):
//   on/off      note, vel 0..1
//   perf        sustain | brake | trim | n3ctl (0/1), vibDepth ¢, rot rev/s, trimAmt, bend ¢, pressure
//   inst        'glass' | 'bars'   (program 0/1)
//   param       slider on the current instrument page: any key in player-defaults.json
//
// Constraint: never hold more than 8 notes. The original's voice stealing can
// steal a held voice, and the port fixes that, so the two must not be compared there.

export const SAMPLE_RATE = 48000;
export const BLOCK = 128;

const on = (t, note, vel = 0.8) => ({ t, type: 'on', note, vel });
const off = (t, note) => ({ t, type: 'off', note });
const perf = (t, key, val) => ({ t, type: 'perf', key, val });
const param = (t, key, val) => ({ t, type: 'param', key, val });
const inst = (t, i) => ({ t, type: 'inst', inst: i });

export const SCENARIOS = [
  // Finger only, turning bowl (player default spin = 1 rev/s): stick-slip onset and free decay.
  { name: 'glass_rub_release', seconds: 3, events: [on(0, 72), off(1.5, 72)] },

  // Fixed bowl: the finger travels instead of the glass; split beats are audible.
  { name: 'glass_fixed_bowl', seconds: 3, events: [param(0, 'spin', 0), on(0, 72), off(1.5, 72)] },

  // Coils hold the note after the finger lifts (amplitude regulator to vset).
  { name: 'glass_sustain', seconds: 3, events: [perf(0, 'sustain', 1), on(0, 72), off(1.0, 72)] },

  // Sustained, then braked against the velocity vector.
  { name: 'glass_brake', seconds: 3,
    events: [perf(0, 'sustain', 1), on(0, 72), off(1.0, 72), perf(1.5, 'sustain', 0), perf(1.5, 'brake', 1)] },

  // Drive axis rotating at 0.5 rev/s: the pattern precesses, heard as the swirl.
  { name: 'glass_rotation', seconds: 3,
    events: [perf(0, 'sustain', 1), perf(0, 'rot', 0.5), on(0, 72), off(0.8, 72)] },

  // Bryan precession on a fast spindle with the finger lifted.
  { name: 'glass_bryan_spin', seconds: 3,
    events: [param(0, 'spin', 2), param(0, 'kappa', 0.6), on(0, 69), off(1.0, 69)] },

  // Fixed bowl, wide split, electronic trim cancelling it.
  { name: 'glass_trim', seconds: 3,
    events: [param(0, 'spin', 0), param(0, 'split', 2), perf(0, 'trim', 1), perf(0, 'sustain', 1),
             on(0, 72), off(1.0, 72)] },

  // Electronic-spring vibrato, 20 ¢.
  { name: 'glass_vibrato', seconds: 3,
    events: [perf(0, 'sustain', 1), perf(0, 'vibDepth', 20), on(0, 72), off(1.0, 72)] },

  // Static pitch bend through the spring, +30 ¢ arriving mid-note.
  { name: 'glass_bend', seconds: 3,
    events: [perf(0, 'sustain', 1), on(0, 72), off(1.0, 72), perf(1.5, 'bend', 30)] },

  // Coils on the (3,0) pair as well.
  { name: 'glass_n3', seconds: 3, events: [perf(0, 'sustain', 1), perf(0, 'n3ctl', 1), on(0, 76), off(1.0, 76)] },

  // Fused silica + band: very high Q, small split.
  { name: 'glass_silica', seconds: 4,
    events: [param(0, 'Q2', 40000), param(0, 'Q3', 28000), param(0, 'split', 0.1), on(0, 67), off(1.0, 67)] },

  // Finger load from aftertouch / CC11 while held.
  { name: 'glass_pressure', seconds: 3,
    events: [on(0, 72), perf(0.5, 'pressure', 1.8), perf(1.2, 'pressure', 0.4), off(2.0, 72)] },

  // Three-note chord, staggered releases.
  { name: 'glass_chord', seconds: 3,
    events: [on(0, 60, 0.7), on(0.05, 64, 0.7), on(0.1, 67, 0.7), off(1.2, 60), off(1.4, 64), off(1.6, 67)] },

  // Extremes of the playable range.
  { name: 'glass_low_high', seconds: 3, events: [on(0, 48), on(0, 96), off(1.5, 48), off(1.5, 96)] },

  // Bars: mallet strike and free ring.
  { name: 'bars_strike', seconds: 3, events: [inst(0, 'bars'), on(0, 69, 0.9)] },

  // Bars: soft mallet, then sustained, then braked.
  { name: 'bars_sustain_brake', seconds: 3,
    events: [inst(0, 'bars'), param(0, 'contact', 5), perf(0, 'sustain', 1), on(0, 69, 0.6),
             perf(1.5, 'sustain', 0), perf(1.5, 'brake', 1)] },

  // Bars: vibrato under sustain.
  { name: 'bars_vibrato', seconds: 3,
    events: [inst(0, 'bars'), perf(0, 'sustain', 1), perf(0, 'vibDepth', 15), on(0, 72, 0.8)] },
];
