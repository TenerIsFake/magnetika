// Compares the C++ renders against the reference renders, scenario by scenario.
//
//   node tools/compare.mjs <ref-dir> <port-dir> [--json] [--max-error-db=-150]
//
// With --max-error-db, exits non-zero if any scenario is missing or its error is
// above the limit. -150 dB was chosen from data (2026-11-03): the port's worst
// scenario sits at -202 dB (last-bit libm differences), while a 2 % change to one
// minor coupling coefficient lands at -78 to -102 dB. The limit sits between.
//
// Reports, per scenario: whether the files are bit-identical, the first sample
// where they differ, the largest absolute difference, and the error level: the
// RMS of the difference relative to the RMS of the reference, in dB.

import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

function readWav(path) {
  const b = readFileSync(path);
  if (b.toString('ascii', 0, 4) !== 'RIFF' || b.readUInt16LE(20) !== 3 || b.readUInt16LE(22) !== 2)
    throw new Error(`${path}: not a stereo float WAV`);
  const n = b.readUInt32LE(40) / 8;
  const L = new Float32Array(n), R = new Float32Array(n);
  for (let i = 0; i < n; i++) { L[i] = b.readFloatLE(44 + 8 * i); R[i] = b.readFloatLE(48 + 8 * i); }
  return { L, R, sr: b.readUInt32LE(24), bytes: b };
}

export function compare(ref, port) {
  if (ref.L.length !== port.L.length) return { error: `length ${port.L.length} != ${ref.L.length}` };
  const identical = ref.bytes.equals(port.bytes);
  let first = -1, maxDiff = 0, sd = 0, sr = 0;
  for (let i = 0; i < ref.L.length; i++) {
    for (const ch of ['L', 'R']) {
      const a = ref[ch][i], b = port[ch][i], d = Math.abs(a - b);
      if (d > 0 && first < 0) first = i;
      if (d > maxDiff) maxDiff = d;
      sd += d * d; sr += a * a;
    }
  }
  const errDb = sd === 0 ? -Infinity : 10 * Math.log10(sd / (sr || 1e-30));
  return { identical, first, firstSec: first < 0 ? null : first / ref.sr, maxDiff, errDb };
}

const isMain = import.meta.url === `file://${process.argv[1]}`;
if (isMain) {
  const [refDir, portDir] = process.argv.slice(2);
  const json = process.argv.includes('--json');
  const names = readdirSync(refDir).filter(f => f.endsWith('.wav')).map(f => f.slice(0, -4)).sort();
  const out = {};
  for (const name of names) {
    let port;
    try { port = readWav(join(portDir, `${name}.wav`)); } catch (e) { out[name] = { error: e.message }; continue; }
    out[name] = compare(readWav(join(refDir, `${name}.wav`)), port);
  }
  if (names.length === 0) { console.error(`no reference renders in ${refDir}`); process.exit(2); }
  if (json) console.log(JSON.stringify(out, null, 2));
  else for (const [name, r] of Object.entries(out)) {
    if (r.error) { console.log(`${name.padEnd(22)} ERROR ${r.error}`); continue; }
    const where = r.first < 0 ? '—' : `${r.firstSec.toFixed(4)} s (sample ${r.first})`;
    console.log(`${name.padEnd(22)} ${r.identical ? 'IDENTICAL' : 'differs  '}  first diff ${where.padEnd(28)}` +
                `  max |d| ${r.maxDiff.toExponential(2)}  error ${r.errDb === -Infinity ? '-inf' : r.errDb.toFixed(1)} dB`);
  }
  const limitArg = process.argv.find(a => a.startsWith('--max-error-db='));
  if (limitArg) {
    const limit = Number(limitArg.split('=')[1]);
    const bad = Object.entries(out).filter(([, r]) => r.error || !(r.errDb <= limit));
    if (bad.length) {
      console.error(`FAIL: ${bad.length} scenario(s) above ${limit} dB: ${bad.map(([n]) => n).join(', ')}`);
      process.exit(1);
    }
    console.log(`PASS: all ${names.length} scenarios at or below ${limit} dB`);
  }
}
