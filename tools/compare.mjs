// Compares the C++ renders against the reference renders, scenario by scenario.
//
//   node tools/compare.mjs <ref-dir> <port-dir> [--json] [--max-error-db=-150]
//
// With --max-error-db, exits non-zero if any scenario is missing or its error is
// above the limit. -150 dB was chosen from data (2026-11-03): the port's worst
// scenario sits at -202 dB (GCC) / -191 dB (MSVC), last-bit libm differences,
// while a 2 % change to one minor coupling coefficient lands at -78 to -102 dB.
//
// Reports, per scenario: whether the files are bit-identical, the first sample
// where they differ, the largest absolute difference, and the error level: the
// RMS of the difference relative to the RMS of the reference, in dB.
//
// This file is a script, not a module: there is deliberately no "am I main?"
// guard. One based on import.meta.url vs argv[1] silently did nothing (exit 0)
// for paths containing a space or a symlink, which a test runner reads as PASS.

import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

function usage(msg) {
  console.error(`compare.mjs: ${msg}\nusage: node tools/compare.mjs <ref-dir> <port-dir> [--json] [--max-error-db=<dB>]`);
  process.exit(2);
}

function readWav(path) {
  const b = readFileSync(path);
  const ok = b.length >= 44 && b.toString('ascii', 0, 4) === 'RIFF' && b.toString('ascii', 8, 12) === 'WAVE' &&
             b.readUInt16LE(20) === 3 && b.readUInt16LE(22) === 2 && b.readUInt16LE(34) === 32 &&
             b.toString('ascii', 36, 40) === 'data';
  if (!ok) throw new Error(`${path}: not a 44-byte-header stereo 32-bit float WAV`);
  const n = b.readUInt32LE(40) / 8;
  if (44 + 8 * n > b.length) throw new Error(`${path}: truncated`);
  const L = new Float32Array(n), R = new Float32Array(n);
  for (let i = 0; i < n; i++) { L[i] = b.readFloatLE(44 + 8 * i); R[i] = b.readFloatLE(48 + 8 * i); }
  return { L, R, sr: b.readUInt32LE(24), bytes: b };
}

function compare(ref, port) {
  if (ref.sr !== port.sr) return { error: `sample rate ${port.sr} != ${ref.sr}` };
  if (ref.L.length !== port.L.length) return { error: `length ${port.L.length} != ${ref.L.length}` };
  const identical = ref.bytes.equals(port.bytes);
  let first = -1, maxDiff = 0, sd = 0, sr = 0;
  for (let i = 0; i < ref.L.length; i++) {
    for (const ch of ['L', 'R']) {
      const a = ref[ch][i], b = port[ch][i], d = Math.abs(a - b);
      if (!(d === 0) && first < 0) first = i;  // NaN counts as a difference
      if (!(d <= maxDiff)) maxDiff = d;          // NaN propagates into maxDiff
      sd += d * d; sr += a * a;
    }
  }
  const errDb = sd === 0 ? -Infinity : 10 * Math.log10(sd / (sr || 1e-30));
  return { identical, first, firstSec: first < 0 ? null : first / ref.sr, maxDiff, errDb };
}

// ---------------------------------------------------------------- arguments
const positional = [], flags = { json: false, limit: null };
for (const a of process.argv.slice(2)) {
  if (a === '--json') flags.json = true;
  else if (a.startsWith('--max-error-db=')) {
    const s = a.slice('--max-error-db='.length);
    const v = Number(s);
    if (s.trim() === '' || !Number.isFinite(v)) usage(`--max-error-db needs a finite number, got "${s}"`);
    flags.limit = v;
  } else if (a.startsWith('--')) usage(`unknown option ${a} (use --max-error-db=<dB>, with "=")`);
  else positional.push(a);
}
if (positional.length !== 2) usage('expected exactly two directories');
const [refDir, portDir] = positional;

// ---------------------------------------------------------------- run
const names = readdirSync(refDir).filter(f => f.endsWith('.wav')).map(f => f.slice(0, -4)).sort();
if (names.length === 0) { console.error(`no reference renders in ${refDir}`); process.exit(2); }
const out = {};
for (const name of names) {
  try {
    out[name] = compare(readWav(join(refDir, `${name}.wav`)), readWav(join(portDir, `${name}.wav`)));
  } catch (e) {
    out[name] = { error: e.message };
  }
}

if (flags.json) console.log(JSON.stringify(out, null, 2));
else for (const [name, r] of Object.entries(out)) {
  if (r.error) { console.log(`${name.padEnd(22)} ERROR ${r.error}`); continue; }
  const where = r.first < 0 ? '—' : `${r.firstSec.toFixed(4)} s (sample ${r.first})`;
  console.log(`${name.padEnd(22)} ${r.identical ? 'IDENTICAL' : 'differs  '}  first diff ${where.padEnd(28)}` +
              `  max |d| ${r.maxDiff.toExponential(2)}  error ${r.errDb === -Infinity ? '-inf' : r.errDb.toFixed(1)} dB`);
}

if (flags.limit !== null) {
  const bad = Object.entries(out).filter(([, r]) => r.error || !(r.errDb <= flags.limit));
  if (bad.length) {
    console.error(`FAIL: ${bad.length} scenario(s) above ${flags.limit} dB or unreadable: ${bad.map(([n]) => n).join(', ')}`);
    process.exit(1);
  }
  console.log(`PASS: all ${names.length} scenarios at or below ${flags.limit} dB`);
}
