// Renders the reference scenarios with the original JS engine.
//
// The player's UI is part of the behaviour: it pushes slider defaults at start,
// routes G/Gb through `perf`, and re-sends the performance state to both
// instruments on every change. `Host` below mirrors that flow (pushAll,
// pushPerf, setPerfUI, setInst, sendParam in the player) so a render here is
// what the browser player plays, not what the engine's constructor defaults are.
//
//   node reference/render.mjs              write renders/*.wav and reference/manifest.json
//   node reference/render.mjs --check      re-render and fail on any hash change

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { Synth } from './engine.mjs';
import { SCENARIOS, SAMPLE_RATE, BLOCK } from './scenarios.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const defaults = JSON.parse(readFileSync(join(root, 'reference/player-defaults.json'), 'utf8'));

class Host {
  constructor(sr) {
    this.s = new Synth(sr);
    this.inst = 'glass';
    this.P = { glass: { ...defaults.glass }, bars: { ...defaults.bars } };
    this.perf = { ...defaults.perf, sustain: 0, brake: 0, trim: 0, n3ctl: 0 };
    this.pushAll();
  }
  pushAll() {
    for (const w of ['glass', 'bars']) {
      const o = {};
      for (const k in this.P[w]) {
        if (k === 'G' || k === 'Gb') continue;
        o[k] = k === 'listen' ? this.P[w][k] * Math.PI / 180 : this.P[w][k];
      }
      this.s.setParams(w, o);
    }
    this.s.setInst(this.inst);
    this.pushPerf();
  }
  pushPerf() {
    const p = this.perf, P = this.P[this.inst];
    this.s.setPerf({ sustain: p.sustain, brake: p.brake, Gs: P.G, Gb: P.Gb });
    for (const w of ['glass', 'bars'])
      this.s.setParams(w, { vibDepth: p.vibDepth, rot: p.rot, trim: p.trim ? p.trimAmt : 0,
                            bend: p.bend, pressure: p.pressure, n3ctl: p.n3ctl });
  }
  setPerf(key, val) { this.perf[key] = val; this.pushPerf(); }
  setInst(i) { this.inst = i; this.s.setInst(i); this.pushPerf(); }
  // Slider on the instrument page (sendParam): G and Gb go through perf.
  setParam(key, val) {
    this.P[this.inst][key] = val;
    if (key === 'Gb') { this.s.setPerf({ Gb: val }); return; }
    if (key === 'G') { this.s.setPerf({ Gs: val }); return; }
    this.s.setParams(this.inst, { [key]: key === 'listen' ? val * Math.PI / 180 : val });
  }
  apply(ev) {
    switch (ev.type) {
      case 'on': this.s.noteOn(ev.note, ev.vel); break;
      case 'off': this.s.noteOff(ev.note); break;
      case 'perf': this.setPerf(ev.key, ev.val); break;
      case 'inst': this.setInst(ev.inst); break;
      case 'param': this.setParam(ev.key, ev.val); break;
      default: throw new Error(`unknown event type ${ev.type}`);
    }
  }
}

function render(sc) {
  const host = new Host(SAMPLE_RATE);
  const n = Math.round(sc.seconds * SAMPLE_RATE / BLOCK) * BLOCK;
  const L = new Float32Array(n), R = new Float32Array(n);
  const bl = new Float32Array(BLOCK), br = new Float32Array(BLOCK);
  // Events land on block boundaries, as AudioWorklet messages do.
  const events = sc.events.map(e => ({ ...e, at: Math.round(e.t * SAMPLE_RATE / BLOCK) * BLOCK }))
                          .sort((a, b) => a.at - b.at);
  let k = 0;
  for (let i = 0; i < n; i += BLOCK) {
    while (k < events.length && events[k].at <= i) host.apply(events[k++]);
    host.s.render(bl, br, BLOCK);
    L.set(bl, i); R.set(br, i);
  }
  return { L, R };
}

function wavFloat32(L, R, sr) {
  const n = L.length, data = 8 * n, buf = Buffer.alloc(44 + data);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + data, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(3, 20); buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(sr, 24); buf.writeUInt32LE(sr * 8, 28); buf.writeUInt16LE(8, 32); buf.writeUInt16LE(32, 34);
  buf.write('data', 36); buf.writeUInt32LE(data, 40);
  for (let i = 0; i < n; i++) { buf.writeFloatLE(L[i], 44 + 8 * i); buf.writeFloatLE(R[i], 48 + 8 * i); }
  return buf;
}

function stats(L, R) {
  let peak = 0, ss = 0;
  for (let i = 0; i < L.length; i++) {
    peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
    ss += L[i] * L[i] + R[i] * R[i];
  }
  return { peak: +peak.toFixed(6), rms: +Math.sqrt(ss / (2 * L.length)).toFixed(6) };
}

const check = process.argv.includes('--check');
const manifestPath = join(root, 'reference/manifest.json');
const manifest = { sample_rate: SAMPLE_RATE, block: BLOCK, engine_source: defaults.source_sha256, scenarios: {} };
if (!check) mkdirSync(join(root, 'renders'), { recursive: true });

for (const sc of SCENARIOS) {
  const { L, R } = render(sc);
  const wav = wavFloat32(L, R, SAMPLE_RATE);
  const sha = createHash('sha256').update(wav).digest('hex');
  const st = stats(L, R);
  if (st.peak === 0 && !sc.silentOk) throw new Error(`${sc.name}: rendered silence`);
  manifest.scenarios[sc.name] = { sha256: sha, frames: L.length, ...st };
  if (!check) writeFileSync(join(root, 'renders', `${sc.name}.wav`), wav);
  console.log(`${sc.name.padEnd(24)} peak ${st.peak.toFixed(3)}  rms ${st.rms.toFixed(4)}  ${sha.slice(0, 12)}`);
}

if (check) {
  const old = JSON.parse(readFileSync(manifestPath, 'utf8'));
  const a = JSON.stringify(old), b = JSON.stringify(manifest);
  if (a !== b) { console.error('reference renders changed — see the diff of reference/manifest.json'); process.exit(1); }
  console.log('reference renders unchanged');
} else {
  writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n');
}
