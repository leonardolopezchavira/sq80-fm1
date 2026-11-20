// SPDX-License-Identifier: MIT
/* SQ-80 for the FM-1: the tablet (index.html). The panel's programs and the sequencer's tracks are one and the same:
 * part N of the panel is track N of the sequence on screen, and its sound is the program that track keeps. With the
 * FM-1 in MULTI (frame 34), track N plays on MIDI channel N, the FM-1's part N; with status TABLET it plays on this
 * tablet's own copy of the engine (sq80_editor.wasm, the editor page's). The sequencer is seqcore.js. */
"use strict";
const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const S = window.SeqCore;
const NOTE = (k) => ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][((k % 12) + 12) % 12] + (Math.floor(k / 12) - 1);
const nib = (prog) => { const o = []; for (const x of prog) o.push(x & 15, x >> 4); return o; };
const unnib = (d, p) => { const r = new Uint8Array(102); for (let j = 0; j < 102; j++) r[j] = (d[p + 2 * j] & 15) | ((d[p + 2 * j + 1] & 15) << 4); return r; };
const nameOf = (prog) => String.fromCharCode(...prog.slice(0, 6)).replace(/[^\x20-\x7e]/g, " ");
const VERSION = "1.1.0";

/* ================================================================== USB MIDI on the Fire tablet (android/) */
/* The tablet's app has no Web MIDI in its web view: it gives SQ80Android (android/.../MidiBridge.java) and calls
 * __sq80midi / __sq80ports. This makes navigator.requestMIDIAccess out of it, with what the page uses. */
(function bridge() {
  const A = window.SQ80Android;
  if (!A) return;                              // (it wins over a web view's own, which may only refuse)
  const access = { inputs: new Map(), outputs: new Map(), onstatechange: null, sysexEnabled: true };
  const hex = (d) => Array.from(d, (b) => (b & 255).toString(16).padStart(2, "0")).join("");
  function sync() {
    let list;
    try { list = JSON.parse(A.ports()); } catch (_) { return; }
    const seen = new Set();
    for (const p of list.inputs) {
      seen.add("i" + p.id);
      if (!access.inputs.has(p.id)) access.inputs.set(p.id, { id: p.id, name: p.name, type: "input", state: "connected", onmidimessage: null,
        open() { A.openInput(p.id); return Promise.resolve(this); }, close() { return Promise.resolve(this); } });
    }
    for (const p of list.outputs) {
      seen.add("o" + p.id);
      if (!access.outputs.has(p.id)) access.outputs.set(p.id, { id: p.id, name: p.name, type: "output", state: "connected",
        open() { A.openOutput(p.id); return Promise.resolve(this); }, close() { return Promise.resolve(this); },
        send(data, ts) { A.send(p.id, hex(data), ts ? Math.max(0, ts - performance.now()) : 0); } });
    }
    for (const [id, p] of access.inputs) if (!seen.has("i" + id)) { p.state = "disconnected"; access.inputs.delete(id); }
    for (const [id, p] of access.outputs) if (!seen.has("o" + id)) { p.state = "disconnected"; access.outputs.delete(id); }
    if (access.onstatechange) access.onstatechange({});
  }
  window.__sq80ports = sync;
  window.__sq80midi = (id, h) => {
    const p = access.inputs.get(id);
    if (!p || !p.onmidimessage) return;
    const d = new Uint8Array(h.length / 2);
    for (let i = 0; i < d.length; i++) d[i] = parseInt(h.substr(2 * i, 2), 16);
    p.onmidimessage({ data: d, timeStamp: performance.now() });
  };
  navigator.requestMIDIAccess = () => { sync(); return Promise.resolve(access); };
})();

/* ================================================================== storage: IndexedDB, else localStorage */
const Store = {
  db: null,
  async open() {
    try {
      this.db = await new Promise((res, rej) => { const r = indexedDB.open("sq80-tablet", 1); r.onupgradeneeded = () => r.result.createObjectStore("kv");
        r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
    } catch (_) { this.db = null; }
  },
  async get(k) {
    if (!this.db) { try { return JSON.parse(localStorage.getItem("sq80t-" + k)); } catch (_) { return null; } }
    return new Promise((res) => { const q = this.db.transaction("kv").objectStore("kv").get(k); q.onsuccess = () => res(q.result ?? null); q.onerror = () => res(null); });
  },
  async set(k, v) {
    if (!this.db) { try { localStorage.setItem("sq80t-" + k, JSON.stringify(v)); } catch (_) { /* full */ } return; }
    return new Promise((res) => { const t = this.db.transaction("kv", "readwrite"); t.objectStore("kv").put(v, k); t.oncomplete = res; t.onerror = res; });
  },
};

/* ================================================================== the engine (sq80_editor.wasm) */
let wasmMod = null, wasmBytes = null, E = null, userPack = null;
const PARAM = {};                             // parameter ids (src/sq80/sq80_params.h)
const ENV = (n, p) => n * 11 + p, LFO = (n, p) => 44 + n * 8 + p, OSC = (n, p) => 68 + n * 14 + p, PRG = (p) => 110 + p;
const EF = { L1: 0, L2: 1, L3: 2, LV: 3, T1: 4, T2: 5, T3: 6, T4: 7, T1V: 8, TK: 9, R2: 10 };
const LF = { FREQ: 0, WAVE: 1, L1: 2, L2: 3, DELAY: 4, MOD: 5, HUMAN: 6, RESET: 7 };
const OF = { OCT: 0, SEMI: 1, FINE: 2, WAVE: 3, MOD1: 4, DEPTH1: 5, MOD2: 6, DEPTH2: 7, LEVEL: 8, OUTPUT: 9, DMOD1: 10, DDEPTH1: 11, DMOD2: 12, DDEPTH2: 13 };
const PF = { FREQ: 0, RES: 1, KEYBD: 2, FMOD1: 3, FDEPTH1: 4, FMOD2: 5, FDEPTH2: 6, FINAL: 7, PAN: 8, PANMOD: 9, PANDEPTH: 10, SYNC: 11, AM: 12,
             MONO: 13, GLIDE: 14, VC: 15, ENVRS: 16, OSCRS: 17, CYC: 18, NAME0: 19, LAYER: 25, SPLIT: 26, SPLAYR: 27, SPKEY: 28 };
const K = { NUM: 0, SIGNED: 1, ONOFF: 2, SOURCE: 3, WAVE: 4, LFOWAVE: 5, CHAR: 6, PROG: 7, KEY: 8 };
async function instance() {
  const imports = {};
  for (const im of WebAssembly.Module.imports(wasmMod)) (imports[im.module] ||= {})[im.name] = () => 0;
  return (await WebAssembly.instantiate(wasmMod, imports)).exports;
}
async function loadEngine() {
  const r = await fetch("../edit/sq80_editor.wasm");
  if (!r.ok) throw new Error("sq80_editor.wasm: " + r.status);
  wasmBytes = await r.arrayBuffer();
  wasmMod = await WebAssembly.compile(wasmBytes);
  E = await instance();
  E.init(48000);
}
const str = (X, p) => { const b = new Uint8Array(X.memory.buffer); let s = ""; while (b[p]) s += String.fromCharCode(b[p++]); return s; };
const eprog = () => new Uint8Array(E.memory.buffer, E.prog(), 102);
function usePackIn(X, bytes) {
  if (bytes.length > X.pack_max()) return false;
  new Uint8Array(X.memory.buffer, X.pack_buf(), bytes.length).set(bytes);
  return !!X.use_pack(bytes.length);
}

/* ------------------------------------------------------------------ the tablet's sound: one engine per track */
/* On the audio thread (tablet-worklet.js) where the browser has audio worklets: the page's drawing cannot hold the
 * sound up there. Else (an old web view) the main-thread way, below, which crackles while the screen is busy. */
const Tab = {
  ac: null, node: null, worklet: false, ready: null, has: [], vol: 0.7,
  start() {
    if (this.ac) { if (this.ac.state !== "running") this.ac.resume(); return this.ready; }
    this.ac = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: "interactive" });
    this.ready = (async () => {
      if (this.ac.audioWorklet && window.AudioWorkletNode) {
        try {
          await this.ac.audioWorklet.addModule("tablet-worklet.js");
          this.node = new AudioWorkletNode(this.ac, "sq80-tablet", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
                                                                      processorOptions: { wasm: wasmBytes } });
          this.node.connect(this.ac.destination);
          this.worklet = true;
          this.post({ k: "vol", v: this.vol });
          if (userPack) this.post({ k: "pack", bytes: userPack.bytes });
          return;
        } catch (e) { console.warn("audio worklet: " + e.message); }
      }
      Old.start(this.ac);
    })();
    return this.ready;
  },
  post(m) { if (this.worklet) this.node.port.postMessage(m); },
  /* performance time (ms) -> the context's time (s) at which it is heard */
  ctxTime(t) {
    if (!t) return 0;
    const ts = this.ac.getOutputTimestamp ? this.ac.getOutputTimestamp() : null;
    if (ts && ts.performanceTime) return ts.contextTime + (t - ts.performanceTime) / 1000;
    return this.ac.currentTime + (this.ac.outputLatency || this.ac.baseLatency || 0) + (t - performance.now()) / 1000;
  },
  program(tr, data) { this.has[tr] = true; this.start().then(() => this.worklet ? this.post({ k: "prog", tr, data: Array.from(data) }) : Old.program(tr, data)); },
  edit(tr, data) { if (!this.has[tr]) return; this.worklet ? this.post({ k: "edit", tr, data: Array.from(data) }) : Old.edit(tr, data); },
  at(t, tr, kind, a, b) {
    if (!this.ac) return;
    if (this.worklet) this.post({ k: "ev", t: this.ctxTime(t), tr, kind, a, b });
    else if (Old.node) Old.at(t, tr, kind, a, b);   // (neither yet: the worklet still loading)
  },
  now(tr, kind, a, b) { this.at(0, tr, kind, a, b); },
  allOff() { if (this.worklet) this.post({ k: "alloff" }); else Old.allOff(); },
  setVol(v) { this.vol = v; this.post({ k: "vol", v }); Old.vol = v; },
  setPack(bytes) { this.post({ k: "pack", bytes }); for (const X of Old.eng) if (X) usePackIn(X, bytes); },
  get eng() { return this.has; },
  click(t, accent) {                          // the metronome, a short blip at performance time t
    if (!this.ac) return;
    const at = Math.max(this.ac.currentTime, this.ctxTime(t) - (this.ac.outputLatency || this.ac.baseLatency || 0));
    const o = this.ac.createOscillator(), g = this.ac.createGain();
    o.frequency.value = accent ? 1600 : 1000;
    g.gain.setValueAtTime(0.0001, at); g.gain.exponentialRampToValueAtTime(0.35, at + 0.002); g.gain.exponentialRampToValueAtTime(0.0001, at + 0.04);
    o.connect(g).connect(this.ac.destination); o.start(at); o.stop(at + 0.05);
  },
};
/* the main-thread way (no audio worklet) */
const Old = {
  ac: null, node: null, eng: [], q: [], vol: 0.7,
  start(ac) {
    this.ac = ac;
    this.node = ac.createScriptProcessor(1024, 0, 2);
    this.node.onaudioprocess = (ev) => this.render(ev);
    this.node.connect(ac.destination);
  },
  async engine(tr) {
    if (this.eng[tr]) return this.eng[tr];
    const X = await instance();
    X.init(this.ac.sampleRate);
    if (userPack) usePackIn(X, userPack.bytes);
    this.eng[tr] = X;
    return X;
  },
  program(tr, data) { this.engine(tr).then((X) => { new Uint8Array(X.memory.buffer, X.prog(), 102).set(data); X.apply(1); }); },
  edit(tr, data) { const X = this.eng[tr]; if (X) { new Uint8Array(X.memory.buffer, X.prog(), 102).set(data); X.apply(0); } },
  at(t, tr, kind, a, b) { if (!this.eng[tr]) { this.engine(tr); if (kind !== "on") return; } this.q.push({ t, tr, kind, a, b }); },
  allOff() { for (const X of this.eng) if (X) X.all_off(); this.q = []; },
  render(ev) {
    const n = ev.outputBuffer.length, ol = ev.outputBuffer.getChannelData(0), or = ev.outputBuffer.getChannelData(1);
    ol.fill(0); or.fill(0);
    const sr = this.ac.sampleRate, lat = ((this.ac.outputLatency || this.ac.baseLatency || 0) * 1000) + n / sr * 1000;
    const t0 = performance.now() + lat, msPer = 1000 / sr;
    this.q.sort((a, b) => a.t - b.t);
    let done = 0, pos = 0;
    const apply = (e) => {
      const X = this.eng[e.tr]; if (!X) return;
      if (e.kind === "on") X.note_on(e.a, e.b); else if (e.kind === "off") X.note_off(e.a);
      else if (e.kind === "cc") X.cc(e.a, e.b); else if (e.kind === "bend") X.bend(e.a);
    };
    while (pos < n) {
      while (done < this.q.length && this.q[done].t <= t0 + pos * msPer) apply(this.q[done++]);
      const next = done < this.q.length ? Math.ceil((this.q[done].t - t0) / msPer) : n, m = clamp(next - pos, 1, n - pos);
      for (const X of this.eng) {
        if (!X || !X.voices()) continue;
        X.render(m);
        const L = new Float32Array(X.memory.buffer, X.left(), m), R = new Float32Array(X.memory.buffer, X.right(), m);
        for (let i = 0; i < m; i++) { ol[pos + i] += L[i]; or[pos + i] += R[i]; }
      }
      pos += m;
    }
    this.q.splice(0, done);
    const g = this.vol * 0.45;
    for (let i = 0; i < n; i++) { ol[i] = Math.tanh(ol[i] * g); or[i] = Math.tanh(or[i] * g); }
  },
};

/* ================================================================== MIDI: the ports, the FM-1 */
let midi = null, out = null, inp = null, link = null;
const HEAD = [0x7D, 0x53, 0x51];
const CHECKED = (c) => (c >= 0x10 && c <= 0x12) || (c >= 0x20 && c <= 0x22) || (c >= 0x30 && c <= 0x34);
class Link {                                  // the loader's frames, one at a time, each answered
  constructor(i, o) { this.i = i; this.o = o; this.waiting = null; this.chain = Promise.resolve(); this.multi = false; }
  take(d) {
    const w = this.waiting;
    if (!w || d.length < 6 || d[0] !== 0xF0 || d[1] !== 0x7D || d[2] !== 0x53 || d[3] !== 0x51) return false;
    if (!((d[4] === 0x7F || d[4] === 0x7E) && d[5] === w.cmd)) return false;
    this.waiting = null; clearTimeout(w.timer); w.resolve(Array.from(d));
    return true;
  }
  once(cmd, body, ms) {
    return new Promise((resolve, reject) => {
      let f = [cmd, ...body];
      if (CHECKED(cmd)) { const s = f.reduce((a, b) => a + b, 0); f = [...f, s & 0x7F, (s >> 7) & 0x7F]; }
      this.waiting = { cmd, resolve, timer: setTimeout(() => { this.waiting = null; reject(new Error("no answer from the FM-1")); }, ms) };
      const frame = [0xF0, ...HEAD, ...f, 0xF7];
      Mon.add("out", frame);
      this.o.send(frame);
    });
  }
  async tries(cmd, body, ms) {
    let last;
    for (let t = 0; t < 3; t++) {
      try {
        const a = await this.once(cmd, body, ms);
        if (a[4] === 0x7F && a[6] === 7) { last = new Error("a damaged frame"); continue; }
        if (a[4] === 0x7F && a[6] !== 0) throw Object.assign(new Error("the FM-1 refused it (" + a[6] + ")"), { refused: a[6] });
        return a;
      } catch (e) { if (e.refused) throw e; last = e; await sleep(60); }
    }
    throw last;
  }
  send(cmd, body = [], ms = 1200) {           // queued: one frame on the way at a time
    const p = this.chain.then(() => this.tries(cmd, body, ms));
    this.chain = p.catch(() => {});
    return p;
  }
}
async function getMidi() {
  if (midi) return midi;
  if (!navigator.requestMIDIAccess) throw new Error("no Web MIDI here: on the Fire tablet use the SQ-80 Tablet app; on a computer, Chrome or Edge");
  midi = await navigator.requestMIDIAccess({ sysex: true });
  midi.onstatechange = () => { fillPorts(); if (link && link.o.state === "disconnected") disconnect("The FM-1 went away."); };
  fillPorts();
  return midi;
}
const live = (m) => [...m.values()].filter((p) => p.state !== "disconnected");
function fillPorts() {
  if (!midi) return;
  const fillSel = (sel, ports, cur) => {
    sel.replaceChildren(new Option("none", ""));
    for (const p of ports) sel.append(new Option(p.name, p.id));
    sel.value = cur && ports.some((p) => p.id === cur) ? cur : "";
  };
  fillSel($("outSel"), live(midi.outputs), out && out.id);
  fillSel($("inSel"), live(midi.inputs), inp && inp.id);
}
function outputFor(i) {
  const outs = live(midi.outputs), bare = (s) => (s || "").replace(/\s+\d+$/, "").replace(/\s*(in|input)$/i, "");
  const same = outs.filter((o) => bare(o.name) === bare(i.name));
  return outs.find((o) => o.name === i.name) || (same.length === 1 ? same[0] : null) || (outs.length === 1 ? outs[0] : null);
}
function listen(i) {                          // one handler: the FM-1's answers, and the notes for the sequencer
  if (inp && inp !== i) inp.onmidimessage = null;
  inp = i;
  if (i) i.onmidimessage = (e) => onMidiIn(e.data, e.timeStamp && e.timeStamp > 1 ? e.timeStamp : performance.now());
  fillPorts();
}
async function connect(quiet) {
  try {
    await getMidi();
    say("connStatus", "Looking for the FM-1…");
    for (const i of live(midi.inputs).filter((p) => !/bluetooth/i.test(p.name || ""))) {
      const o = outputFor(i);
      if (!o) continue;
      try { await i.open(); await o.open(); } catch (_) { continue; }
      const prev = inp, l = new Link(i, o);
      link = l;                               // (its answers come through onMidiIn)
      listen(i);
      try { await l.once(0x01, [], 700); break; } catch (_) { link = null; listen(prev); }
    }
  } catch (e) { say("connStatus", "Not connected: " + (e.message || e), "bad"); return; }
  if (!link) { if (!quiet) say("connStatus", "No FM-1 answered. Is SQ-80 on it, and the USB cable in?", "bad"); setConn(); return; }
  out = link.o;
  fillPorts();
  try { await link.send(0x34, [st.multi ? 1 : 0, keysPart()], 800); link.multi = true; }
  catch (_) { link.multi = false; }
  setConn();
  say("connStatus", `Connected: ${link.o.name}` + (link.multi ? (st.multi ? ", MULTI on" : "") : " (no MULTI: SQ-80 is older than 0.15.0)"), "ok");
  sent.fill(null);
  await adoptPlaying();
  sendAllParts();
}
/* track 1 without a program takes the one the FM-1 plays (and sends nothing: the FM-1 has it) */
async function adoptPlaying() {
  const tr = curSeq().tracks[0];
  if (tr.prog || tr.ch !== 0) return;
  try {
    const r = await readProgram(0x7F, 0);
    setTrackProg(0, r.prog, { b: r.b, p: r.p }, true);
    if (st.track === 0) loadPart();
    refreshKnobs(); buildParts(); buildTracks();
  } catch (e) { toast("Could not read the FM-1's program: " + e.message + " (Setup's MIDI monitor shows what came back)", true); }
}
function disconnect(why) {
  link = null;
  setConn();
  say("connStatus", why || "Not connected", why ? "bad" : "");
}
function setConn() {
  $("led").className = "led" + (link ? link.multi ? " on" : " warn" : "");
  $("connBtn").textContent = link ? "Disconnect" : "Connect";
  $("connBtn").classList.toggle("pri", !link);
  document.querySelectorAll("button.fm").forEach((b) => { b.disabled = !link; });
}
/* the part the FM-1's own keys play: the selected track's (MULTI), if it plays there */
function keysPart() {
  const tr = curSeq() && curSeq().tracks[st.track];
  return st.keysFollow && st.multi && tr && tr.status !== "TABLET" && tr.ch < 8 ? tr.ch : 0;
}
let lastKeysPart = -1;
function followKeys() {
  if (!link || !link.multi) return;
  const k = keysPart();
  if (k === lastKeysPart) return;
  lastKeysPart = k;
  link.send(0x34, [st.multi ? 1 : 0, k], 800).catch(() => {});
}
/* the MIDI monitor (Setup): what went out and came in, the last 80 */
const Mon = {
  log: [], counts: { in: 0, out: 0, sxIn: 0, sxOut: 0 },
  add(dir, d) {
    const st = d[0];
    if (st >= 0xF8) return;                     // (the clock: too many to show)
    const sx = st === 0xF0, n = d.length, show = Array.from(d.slice(0, 12), (b) => b.toString(16).padStart(2, "0").toUpperCase()).join(" ");
    const end = sx ? (d[n - 1] === 0xF7 ? " … F7, whole" : " …, NO F7: cut short") : "";
    this.counts[dir]++; if (sx) this.counts[dir === "in" ? "sxIn" : "sxOut"]++;
    this.log.push(`${(performance.now() / 1000).toFixed(2).padStart(8)} ${dir === "in" ? "IN " : "OUT"} ${String(n).padStart(4)} B  ${show}${n > 12 ? end || " …" : ""}`);
    if (this.log.length > 80) this.log.shift();
    this.dirty = true;
  },
};
function monShow() {
  if (st.view !== "Setup") return;
  const A = window.SQ80Android;
  let head = A ? "" : "Web MIDI (this browser's own)";
  if (A && A.debug) {
    try {
      const d = JSON.parse(A.debug());
      head = `${d.path}\n${d.devices.map((x) => "  " + x).join("\n") || "  (no USB devices)"}\nfrom Android: ${d.bytes} bytes in ${d.pieces} pieces, ${d.messages} whole messages\n` +
             (d.raw.length ? "last pieces as Android gave them:\n" + d.raw.slice(-12).map((x) => "  " + x).join("\n") : "");
    } catch (e) { head = "the app's MIDI bridge: " + e.message; }
  }
  const c = Mon.counts;
  $("monHead").textContent = head;
  $("monLog").textContent = `in ${c.in} (SysEx ${c.sxIn}), out ${c.out} (SysEx ${c.sxOut})\n` + Mon.log.slice().reverse().join("\n");
}
setInterval(() => { if (Mon.dirty || st.view === "Setup") { Mon.dirty = false; monShow(); } }, 700);
/* send raw MIDI to the out port, at performance time t (0: now) */
function midiOut(bytes, t) {
  if (!out) return;
  Mon.add("out", bytes);
  try { out.send(bytes, t && t > performance.now() ? t : undefined); } catch (_) { /* (gone) */ }
}

/* ================================================================== the state */
const st = {
  view: "Panel", seq: 0, song: 0, track: 0, page: "OSC 1", knob: null, bank: 0, prog: 0, grid: 4, mode: "REPLACE",
  countoff: 1, click: false, punch: false, multi: true, keysFollow: true, live: true, clock: false, thru: false,
  look: 120, vol: 70, oct: 48, keys: true, rollGrid: 4, rollVel: 100, rollZoom: 1, stepSize: 4, stepGate: 90,
};
let project = S.newProject();
const libs = [];                              // banks: {name, progs: [Uint8Array] x 40, fm: bank index or -1}
let libSel = 0, libProg = 0;
const undo = [];
const curSeq = () => project.seqs[st.seq];
const curTrack = () => curSeq() && curSeq().tracks[st.track];

let saveTimer = 0;
function dirty() {
  clearTimeout(saveTimer);
  $("saveStatus").textContent = "…";
  saveTimer = setTimeout(async () => {
    await Store.set("project", project);
    const ui = {}; for (const k of ["seq", "song", "track", "page", "bank", "prog", "grid", "mode", "countoff", "click", "multi", "keysFollow",
      "live", "clock", "thru", "look", "vol", "oct", "keys", "rollGrid", "rollVel", "stepSize", "stepGate"]) ui[k] = st[k];
    ui.out = out && out.name; ui.inp = inp && inp.name;
    await Store.set("ui", ui);
    $("saveStatus").textContent = "kept " + new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  }, 700);
}
function pushUndo(what) {
  undo.push({ s: st.seq, data: S.clone(curSeq()), what });
  if (undo.length > 40) undo.shift();
}
function ensureSeq(i) {
  if (!project.seqs[i]) {
    project.seqs[i] = S.newSequence("SEQ" + String(i + 1).padStart(2, "0"));
    dirty();
  }
  return project.seqs[i];
}

/* ------------------------------------------------------------------ the parts: the tracks' programs */
const sent = new Array(8).fill(null);         // what each FM-1 part was last sent (its bytes, joined)
function partData(t) {                        // the track's program (INIT when it has none)
  const tr = curSeq().tracks[t];
  if (tr.prog) return Uint8Array.from(tr.prog.data);
  E.init_program();
  return new Uint8Array(eprog());
}
function setTrackProg(t, data, from, quiet) { // a program for track t: kept in it, sent where it plays (quiet: the
  const tr = curSeq().tracks[t];               // FM-1 plays it already)
  tr.prog = { name: nameOf(data).trim(), data: Array.from(data) };
  if (from) { tr.prog.b = from.b; tr.prog.p = from.p; }
  if (quiet) { if (tr.ch < 8) sent[tr.ch] = tr.prog.data.join(); if (tr.status !== "MIDI") Tab.program(t, tr.prog.data); }
  else sendPart(t, true);
  dirty();
}
let livePend = new Set(), liveBusy = false;
function sendPart(t, fresh) {                 // the track's program to the FM-1's part and/or the tablet's engine
  const seq = curSeq(), tr = seq && seq.tracks[t];
  if (!tr || !tr.prog) return;
  if (tr.status !== "MIDI") (fresh ? Tab.program(t, tr.prog.data) : Tab.edit(t, tr.prog.data));
  if (tr.status !== "TABLET" && tr.ch < 8) { livePend.add(t); pumpParts(); }
}
async function pumpParts() {
  if (liveBusy || !link || !st.live) return;
  liveBusy = true;
  while (livePend.size) {
    const t = livePend.values().next().value;
    livePend.delete(t);
    const seq = curSeq(), tr = seq.tracks[t];
    if (!tr.prog) continue;
    const ch = tr.ch, key = tr.prog.data.join();
    if (sent[ch] === key) continue;
    if (ch > 0 && !link.multi) continue;      // (an older SQ-80: one program, the panel's)
    try {
      await link.send(link.multi ? 0x33 : 0x30, link.multi ? [ch, ...nib(tr.prog.data)] : nib(tr.prog.data), 900);
      sent[ch] = key;
    } catch (e) { toast("The FM-1 did not take track " + (t + 1) + "'s program: " + e.message, true); break; }
  }
  liveBusy = false;
}
function sendAllParts() { const seq = curSeq(); if (!seq) return; seq.tracks.forEach((tr, t) => { if (tr.prog) sendPart(t, true); }); followKeys(); }

/* ================================================================== the sequencer */
let player = null, playMode = "seq";
const sounding = new Map();                   // "tr:k" -> {ch, status} (to let go of at a stop)
function emit(e) {
  switch (e.type) {
  case "on": case "off": case "cc": case "bend": case "at": {
    const toMidi = e.status !== "TABLET", toTab = e.status !== "MIDI";
    if (toMidi) {
      const c = e.ch & 15;
      if (e.type === "on") midiOut([0x90 | c, e.k, e.vm], e.time);
      else if (e.type === "off") midiOut([0x80 | c, e.k, 0], e.time);
      else if (e.type === "cc") midiOut([0xB0 | c, e.c, e.x], e.time);
      else if (e.type === "bend") midiOut([0xE0 | c, e.b & 127, e.b >> 7], e.time);
      else midiOut([0xD0 | c, e.a], e.time);
    }
    if (toTab) {
      if (e.type === "on") Tab.at(e.time, e.tr, "on", e.k, e.vm);
      else if (e.type === "off") Tab.at(e.time, e.tr, "off", e.k);
      else if (e.type === "cc") Tab.at(e.time, e.tr, "cc", e.c, e.x);
      else if (e.type === "bend") Tab.at(e.time, e.tr, "bend", e.b);
    }
    if (e.type === "on") sounding.set(e.tr + ":" + e.k, e); else if (e.type === "off") sounding.delete(e.tr + ":" + e.k);
    if (e.type === "on") flashTrack(e.tr, e.time);
    break;
  }
  case "seq": {                               // a new sequence in a song: its tracks' sounds
    if (playMode === "song" && e.s !== st.seq) { st.seq = e.s; sent.fill(null); refreshSeqViews(); }
    sendAllParts();
    songNow = e.step;
    break;
  }
  case "click": Tab.click(e.time, e.accent); break;
  case "clock": midiOut([0xF8], e.time); break;
  case "start": midiOut([0xFA], e.time); break;
  case "stop": midiOut([0xFC], e.time); break;
  case "end": setTimeout(() => stop(), Math.max(0, e.time - performance.now() + 50)); break;
  default: break;
  }
}
function allNotesOff() {
  for (const e of sounding.values()) {
    if (e.status !== "TABLET") midiOut([0x80 | (e.ch & 15), e.k, 0]);
  }
  sounding.clear();
  for (let c = 0; c < 8; c++) midiOut([0xB0 | c, 123, 0]);
  Tab.allOff();
}
let timer = 0, songNow = -1;
function tick() {
  if (!player || !player.playing) { timer = 0; return; }
  player.lookahead = st.look;
  player.pump(performance.now());
  timer = setTimeout(tick, 20);
}
function play() {
  if (player && player.playing) return;
  Tab.start();
  if (playMode === "seq") ensureSeq(st.seq);
  player = new S.Player(project, emit);
  player.clickOn = st.click || !!recArmed;
  player.clockOn = st.clock;
  player.countoff = st.countoff;
  if (recArmed && playMode === "seq") {
    pushUndo("recording");
    const seq = curSeq(), punch = st.punch ? punchRange() : null;
    player.armRecord({ s: st.seq, track: st.track, mode: st.mode, grid: S.GRIDS[st.grid][1], punchIn: punch ? punch[0] : -1, punchOut: punch ? punch[1] : -1 });
    void seq;
  }
  if (!player.start(playMode, playMode === "song" ? st.song : st.seq, performance.now(), playMode === "seq" ? locateBar : 0)) {
    toast(playMode === "song" ? "The song has no steps with sequences in them." : "Nothing to play."); player = null; return;
  }
  sent.fill(null);
  tick();
  transportUi();
}
function stop() {
  if (!player) return;
  const was = player.playing, rec = player.rec;
  player.stop(performance.now());
  if (rec) { player.disarm(); recArmed = false; toast(rec.took ? `Recorded ${rec.took} note${rec.took === 1 ? "" : "s"} on track ${rec.track + 1}.` : "Nothing recorded."); dirty(); }
  clearTimeout(timer); timer = 0;
  setTimeout(allNotesOff, 30);
  if (!was) { locateBar = 0; }
  transportUi();
  refreshSeqViews();
}
let recArmed = false, locateBar = 0;
function punchRange() { const s = curSeq(); return [Math.min(st.punchIn || 0, s.bars - 1), Math.min(st.punchOut || s.bars, s.bars)]; }
function transportUi() {
  const playing = player && player.playing;
  $("playBtn").classList.toggle("on", !!playing);
  $("recBtn").classList.toggle("on", !!(playing && player.rec));
  $("recBtn").classList.toggle("armed", !!(recArmed && !playing));
}
$("playBtn").onclick = () => { if (player && player.playing) return; playMode = st.view === "Song" ? "song" : "seq"; play(); };
$("stopBtn").onclick = () => { if (player && player.playing) stop(); else { locateBar = 0; allNotesOff(); lcd(); } };
$("recBtn").onclick = () => {
  if (player && player.playing) {             // during play: punch in (ADD) on the selected track, or out
    if (player.rec) { const r = player.disarm(); toast(`Recorded ${r.took} on track ${r.track + 1}.`); dirty(); }
    else if (playMode === "seq") { pushUndo("recording"); player.armRecord({ s: st.seq, track: st.track, mode: "ADD", grid: S.GRIDS[st.grid][1] }); }
  } else recArmed = !recArmed;
  transportUi();
};

/* the LCD: where the play is */
let flashAt = [];
function flashTrack(tr, t) { flashAt[tr] = t; }
function lcd() {
  const seq = curSeq();
  let a, b;
  const pos = player && player.playing ? player.positionAt(performance.now()) : null;
  if (playMode === "song" && player && player.playing) {
    const g = project.songs[st.song];
    a = `SONG ${String(st.song + 1).padStart(2, "0")} ${(g ? g.name : "").padEnd(6)}`;
    b = `STEP ${String((pos ? pos.step : 0) + 1).padStart(2, "0")}`;
  } else {
    a = `SEQ ${String(st.seq + 1).padStart(2, "0")} ${(seq ? seq.name : "------").padEnd(6)}`;
    b = "";
  }
  const bar = pos ? (pos.pre > 0 ? "-" + String(Math.ceil(pos.pre / (S.PPQ * 4 / seq.den))).padStart(2) + " " : String(pos.bar + 1).padStart(3, "0") + "." + (pos.beat + 1))
                  : String(locateBar + 1).padStart(3, "0") + ".1";
  const tempo = seq ? String(seq.tempo).padStart(3) : "---";
  $("lcd").textContent = `${a} ${b ? b + " " : ""}BAR ${bar}  ${tempo}`.replace(/ /g, "!");   /* (DSEG14's full-width blank) */
}

/* ================================================================== MIDI in: recording, step entry, the keys' echo */
let midiBlink = 0;
function onMidiIn(d, t) {
  Mon.add("in", d);
  if (d[0] === 0xF0) { if (link) link.take(d); return; }
  if (d[0] >= 0xF8) return;
  $("midiLed").classList.add("midi"); clearTimeout(midiBlink); midiBlink = setTimeout(() => $("midiLed").classList.remove("midi"), 80);
  const s = d[0] & 0xF0, fromFm1 = link && inp === link.i;
  if (player && player.step) { if (player.input(d, t)) { stepUi(); return; } }
  if (player && player.playing && player.rec) player.input(d, t);
  if (st.thru && !fromFm1) {                  // another keyboard: heard on the selected track
    const tr = curTrack();
    if (tr && (s === 0x90 || s === 0x80 || s === 0xB0 || s === 0xE0)) {
      if (tr.status !== "TABLET") midiOut([s | (tr.ch & 15), d[1], d[2]]);
      if (tr.status !== "MIDI") Tab.now(st.track, s === 0x90 && d[2] ? "on" : s === 0xB0 ? "cc" : s === 0xE0 ? "bend" : "off", s === 0xE0 ? d[1] | d[2] << 7 : d[1], d[2]);
    }
  }
  if (s === 0x90 && d[2]) markKey(d[1], true); else if (s === 0x80 || s === 0x90) markKey(d[1], false);
}

/* ================================================================== the on-screen keys */
function keysTarget() { return st.view === "Panel" ? st.track : st.track; }
function noteOn(k, v) {
  Tab.start();
  const t = keysTarget(), tr = curSeq().tracks[t];
  if (player && player.step) { player.input([0x90, k, v], performance.now()); stepUi(); }
  else if (player && player.playing && player.rec) player.input([0x90, k, v], performance.now());
  if (tr.status !== "TABLET") midiOut([0x90 | (tr.ch & 15), k, v]);
  if (tr.status !== "MIDI") { if (tr.prog && !Tab.has[t]) Tab.program(t, tr.prog.data); Tab.now(t, "on", k, v); }
  markKey(k, true);
}
function noteOff(k) {
  const t = keysTarget(), tr = curSeq().tracks[t];
  if (player && player.step) { player.input([0x80, k, 0], performance.now()); stepUi(); }
  else if (player && player.playing && player.rec) player.input([0x80, k, 0], performance.now());
  if (tr.status !== "TABLET") midiOut([0x80 | (tr.ch & 15), k, 0]);
  if (tr.status !== "MIDI") Tab.now(t, "off", k);
  markKey(k, false);
}
const keyEls = {};
function buildKbd() {
  const kb = $("kbd");
  kb.replaceChildren();
  const W = 100 / 22;
  let wi = 0;
  for (let k = st.oct; k <= st.oct + 36; k++) {
    const black = [1, 3, 6, 8, 10].includes(k % 12), d = document.createElement("div");
    if (black) { d.className = "b"; d.style.left = `${wi * W - W * 0.32}%`; d.style.width = `${W * 0.64}%`; }
    else { d.className = "w"; d.style.left = `${wi * W}%`; d.style.width = `${W}%`; wi++; }
    d.onpointerdown = (e) => { e.preventDefault(); d.setPointerCapture(e.pointerId); const r = d.getBoundingClientRect();
      noteOn(k, clamp(Math.round(40 + 87 * (e.clientY - r.top) / r.height), 1, 127)); };
    d.onpointerup = d.onpointercancel = () => noteOff(k);
    keyEls[k] = d;
    kb.append(d);
  }
  $("octShow").textContent = NOTE(st.oct);
}
function markKey(k, on) { if (keyEls[k]) keyEls[k].classList.toggle("down", on); }
$("octDn").onclick = () => { if (st.oct > 12) { st.oct -= 12; buildKbd(); dirty(); } };
$("octUp").onclick = () => { if (st.oct < 84) { st.oct += 12; buildKbd(); dirty(); } };
$("keysHide").onclick = $("keysBtn").onclick = () => { st.keys = !st.keys; keysUi(); dirty(); };
function keysUi() {
  $("keys").hidden = !st.keys;
  $("keysBtn").setAttribute("aria-pressed", String(st.keys));
  const tr = curTrack();
  $("keysTo").textContent = tr ? `plays track ${st.track + 1}` + (tr.status === "TABLET" ? " on the tablet" : tr.ch < 8 ? ` (the FM-1's part ${tr.ch + 1})` : ` (MIDI channel ${tr.ch + 1})`) : "";
}
$("panicBtn").onclick = () => { allNotesOff(); for (const k in keyEls) markKey(+k, false); };

/* ================================================================== dialogs */
let toastT = 0;
function toast(msg, bad) {
  let t = document.querySelector(".toast");
  if (!t) { t = document.createElement("div"); t.className = "toast"; document.body.append(t); }
  t.textContent = msg; t.classList.toggle("bad", !!bad); t.hidden = false;
  clearTimeout(toastT); toastT = setTimeout(() => { t.hidden = true; }, bad ? 4500 : 2500);
}
function say(id, text, cls) { const s = $(id); if (!s) return; s.textContent = text; s.className = "status" + (cls ? " " + cls : ""); }
function overlay(title, build) {
  return new Promise((resolve) => {
    const ov = document.createElement("div"), dlg = document.createElement("div");
    ov.className = "ov"; dlg.className = "dlg";
    const h = document.createElement("h3"); h.textContent = title;
    dlg.append(h);
    const close = (v) => { ov.remove(); resolve(v); };
    build(dlg, close);
    ov.append(dlg);
    ov.onpointerdown = (e) => { if (e.target === ov) close(undefined); };
    document.body.append(ov);
  });
}
function choose(title, items, cur) {          // items: [label, value]; a value, or undefined
  return overlay(title, (dlg, close) => {
    const g = document.createElement("div"); g.className = "choices";
    for (const [label, v] of items) {
      const b = document.createElement("button"); b.textContent = label;
      if (v === cur) b.classList.add("on");
      b.onclick = () => close(v);
      g.append(b);
    }
    dlg.append(g);
  });
}
function askNumber(title, v, lo, hi, fmt = String) {
  return overlay(title, (dlg, close) => {
    const big = document.createElement("div"); big.className = "big";
    const r = document.createElement("input"); r.type = "range"; r.min = lo; r.max = hi; r.value = v;
    const show = () => { big.textContent = fmt(+r.value); };
    r.oninput = show; show();
    const row = document.createElement("div"); row.className = "row"; row.style.marginTop = "10px";
    const btn = (t, f) => { const b = document.createElement("button"); b.textContent = t; b.onclick = f; return b; };
    const by = (d) => () => { r.value = clamp(+r.value + d, lo, hi); show(); };
    row.append(btn("−10", by(-10)), btn("−1", by(-1)), btn("+1", by(1)), btn("+10", by(10)));
    const sp = document.createElement("span"); sp.className = "grow";
    const ok = btn("OK", () => close(+r.value)); ok.className = "pri";
    row.append(sp, btn("Cancel", () => close(undefined)), ok);
    dlg.append(big, r, row);
  });
}
function confirmBox(title, text, yes = "OK") {
  return overlay(title, (dlg, close) => {
    const p = document.createElement("p"); p.textContent = text; p.style.margin = "0 0 12px";
    const row = document.createElement("div"); row.className = "row";
    const sp = document.createElement("span"); sp.className = "grow";
    const c = document.createElement("button"); c.textContent = "Cancel"; c.onclick = () => close(false);
    const o = document.createElement("button"); o.textContent = yes; o.className = "pri"; o.onclick = () => close(true);
    row.append(sp, c, o);
    dlg.append(p, row);
  });
}
function pickFile(input) {
  return new Promise((res) => { input.value = ""; input.onchange = async () => { const f = input.files[0]; res(f ? { name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) } : null); }; input.click(); });
}
function download(bytes, name, type = "application/octet-stream") {
  if (window.SQ80Android && window.SQ80Android.save) {          // (the tablet's app: Android's own save dialog)
    let b = ""; for (let i = 0; i < bytes.length; i++) b += String.fromCharCode(bytes[i]);
    window.SQ80Android.save(name, btoa(b)); return;   // (Android asks where; it says when it is saved)
  }
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([bytes], { type })); a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 3000);
}

/* ================================================================== PANEL: the SQ-80's pages */
const PAGES = {
  "OSC 1": () => osc(0), "OSC 2": () => osc(1), "OSC 3": () => osc(2),
  "DCA 1": () => dca(0), "DCA 2": () => dca(1), "DCA 3": () => dca(2),
  "FILTER": () => [[PRG(PF.FREQ), "FREQ"], [PRG(PF.RES), "Q"], [PRG(PF.KEYBD), "KEYBD"], [PRG(PF.FMOD1), "MOD#1"], [PRG(PF.FDEPTH1), "DEPTH"],
                   [PRG(PF.FMOD2), "MOD#2"], [PRG(PF.FDEPTH2), "DEPTH"]],
  "DCA 4": () => [[PRG(PF.FINAL), "FINAL VOL"], [PRG(PF.PAN), "PAN"], [PRG(PF.PANMOD), "PAN MOD"], [PRG(PF.PANDEPTH), "DEPTH"]],
  "MODES": () => [[PRG(PF.SYNC), "SYNC"], [PRG(PF.AM), "AM"], [PRG(PF.MONO), "MONO"], [PRG(PF.GLIDE), "GLIDE"], [PRG(PF.VC), "VC"],
                  [PRG(PF.ENVRS), "ENV RS"], [PRG(PF.OSCRS), "OSC RS"], [PRG(PF.CYC), "CYC"]],
  "LFO 1": () => lfo(0), "LFO 2": () => lfo(1), "LFO 3": () => lfo(2),
  "ENV 1": () => env(0), "ENV 2": () => env(1), "ENV 3": () => env(2), "ENV 4": () => env(3),
  "SPLIT": () => [[PRG(PF.LAYER), "LAYER"], [PRG(PF.SPLIT), "SPLIT"], [PRG(PF.SPLAYR), "SPL LYR"], [PRG(PF.SPKEY), "SPL KEY"]],
};
function osc(n) { return [[OSC(n, OF.OCT), "OCT"], [OSC(n, OF.SEMI), "SEMI"], [OSC(n, OF.FINE), "FINE"], [OSC(n, OF.WAVE), "WAVE"],
  [OSC(n, OF.MOD1), "MOD#1"], [OSC(n, OF.DEPTH1), "DEPTH"], [OSC(n, OF.MOD2), "MOD#2"], [OSC(n, OF.DEPTH2), "DEPTH"]]; }
function dca(n) { return [[OSC(n, OF.LEVEL), "LEVEL"], [OSC(n, OF.OUTPUT), "OUTPUT"], [OSC(n, OF.DMOD1), "MOD#1"], [OSC(n, OF.DDEPTH1), "DEPTH"],
  [OSC(n, OF.DMOD2), "MOD#2"], [OSC(n, OF.DDEPTH2), "DEPTH"]]; }
function lfo(n) { return [[LFO(n, LF.FREQ), "FREQ"], [LFO(n, LF.RESET), "RESET"], [LFO(n, LF.HUMAN), "HUMAN"], [LFO(n, LF.WAVE), "WAV"],
  [LFO(n, LF.L1), "L1"], [LFO(n, LF.DELAY), "DELAY"], [LFO(n, LF.L2), "L2"], [LFO(n, LF.MOD), "MOD"]]; }
function env(n) { return ["L1", "L2", "L3", "LV", "T1V", "T1", "T2", "T3", "T4", "TK", "R2"].map((f) => [ENV(n, EF[f]), f]); }

let ed = null;                                // the program being edited: the selected track's, in E's buffer
function loadPart() {
  ed = partData(st.track);
  eprog().set(ed);
}
function pget(id) { return E.param_get(id); }
function pset(id, v) {
  const tr = curTrack();
  if (!tr.prog) return;                       // (a track without a program: nothing to change, nothing sent)
  E.param_set(id, v);
  tr.prog.data = Array.from(eprog());
  tr.prog.name = nameOf(tr.prog.data).trim();
  tr.prog.edited = true;
  sendPart(st.track, false);
  dirty();
}
const kindOf = (id) => E.param_kind(id), minOf = (id) => E.param_min(id), maxOf = (id) => E.param_max(id);
function valueText(id, v) {
  const k = kindOf(id);
  if (k === K.ONOFF) return v ? "ON" : "OFF";
  if (k === K.SOURCE) return str(E, E.source_name(v)).trim();
  if (k === K.WAVE) return str(E, E.wave_name(v)).trim();
  if (k === K.LFOWAVE) return str(E, E.lfo_wave_name(v)).trim();
  if (k === K.PROG) return v ? String(v) : "OFF";
  if (k === K.KEY) return NOTE(v);
  if (k === K.SIGNED && v > 0) return "+" + v;
  return String(v);
}
const knobs = [];
function buildParts() {
  const g = $("parts");
  g.replaceChildren();
  const seq = curSeq();
  for (let t = 0; t < 8; t++) {
    const b = document.createElement("button"), tr = seq.tracks[t];
    b.innerHTML = `TRK ${t + 1}<small></small>`;
    b.querySelector("small").textContent = tr.prog ? tr.prog.name || "------" : "·";
    if (t === st.track) b.classList.add("on");
    b.onclick = () => selectTrack(t);
    g.append(b);
  }
}
function buildPages() {
  const g = $("pages");
  g.replaceChildren();
  for (const p of Object.keys(PAGES)) {
    const b = document.createElement("button");
    b.textContent = p;
    if (p === st.page) b.classList.add("on");
    b.onclick = () => { st.page = p; st.knob = null; buildPages(); buildKnobs(); dirty(); };
    g.append(b);
  }
}
function buildKnobs() {
  const g = $("knobs");
  g.replaceChildren();
  knobs.length = 0;
  for (const [id, label] of PAGES[st.page]()) {
    const k = document.createElement("div"), c = document.createElement("canvas"), l = document.createElement("div"), v = document.createElement("div");
    k.className = "knob" + ([K.ONOFF, K.SOURCE, K.WAVE, K.LFOWAVE].includes(kindOf(id)) ? " sw" : "");
    c.width = 144; c.height = 144; l.className = "kl"; l.textContent = label; v.className = "kv";
    k.append(c, l, v);
    const kn = { id, el: k, c, v };
    knobs.push(kn);
    knobInput(kn);
    g.append(k);
  }
  if (!st.knob || !knobs.some((k) => k.id === st.knob)) st.knob = knobs[0] && knobs[0].id;
  refreshKnobs();
}
function knobInput(kn) {
  let y0 = 0, v0 = 0, moved = false, id = kn.id;
  kn.el.onpointerdown = (e) => {
    e.preventDefault(); kn.el.setPointerCapture(e.pointerId);
    y0 = e.clientY; v0 = pget(id); moved = false; st.knob = id; refreshKnobs();
  };
  kn.el.onpointermove = (e) => {
    if (!kn.el.hasPointerCapture(e.pointerId)) return;
    const span = maxOf(id) - minOf(id), dy = y0 - e.clientY;
    if (!moved && Math.abs(dy) < 6) return;
    moved = true;
    const v = clamp(Math.round(v0 + dy * Math.max(span, 8) / 220), minOf(id), maxOf(id));
    if (v !== pget(id)) { pset(id, v); refreshKnobs(); }
  };
  kn.el.onpointerup = async () => {
    if (moved) return;
    const k = kindOf(id), cur = pget(id);
    if (k === K.ONOFF) { pset(id, cur ? 0 : 1); refreshKnobs(); return; }
    let v;
    if (k === K.SOURCE) v = await choose("source", Array.from({ length: 16 }, (_, i) => [str(E, E.source_name(i)).trim(), i]), cur);
    else if (k === K.WAVE) v = await choose("wave", Array.from({ length: 75 }, (_, i) => [`${String(i).padStart(2, "0")} ${str(E, E.wave_name(i)).trim()}`, i]), cur);
    else if (k === K.LFOWAVE) v = await choose("LFO wave", Array.from({ length: 4 }, (_, i) => [str(E, E.lfo_wave_name(i)).trim(), i]), cur);
    else if (k === K.PROG) v = await choose("program of the bank (by place)", Array.from({ length: 41 }, (_, i) => [i ? String(i) : "OFF", i]), cur);
    else v = await askNumber(kn.el.querySelector(".kl").textContent, cur, minOf(id), maxOf(id), (x) => valueText(id, x));
    if (v !== undefined && v !== cur) { pset(id, v); refreshKnobs(); }
  };
}
function drawKnob(kn) {
  const x = kn.c.getContext("2d"), id = kn.id, v = pget(id), lo = minOf(id), hi = maxOf(id), k = kindOf(id);
  const W = kn.c.width, r = W * 0.38, cx = W / 2, cy = W / 2, a0 = Math.PI * 0.75, a1 = Math.PI * 2.25;
  x.clearRect(0, 0, W, W);
  x.lineCap = "round"; x.lineWidth = 10;
  x.strokeStyle = "#262626"; x.beginPath(); x.arc(cx, cy, r, a0, a1); x.stroke();
  const f = hi > lo ? (v - lo) / (hi - lo) : 0, z = lo < 0 ? (0 - lo) / (hi - lo) : 0;
  const ang = (q) => a0 + (a1 - a0) * q;
  x.strokeStyle = id === st.knob ? "#f2ead6" : "#40dce6";
  x.beginPath();
  if (k === K.ONOFF) { if (v) x.arc(cx, cy, r, a0, a1); }
  else x.arc(cx, cy, r, ang(Math.min(f, z)), ang(Math.max(f, z)) + 0.0001);
  x.stroke();
  x.fillStyle = "#f2ead6";
  const a = ang(f);
  x.beginPath(); x.arc(cx + Math.cos(a) * (r - 22), cy + Math.sin(a) * (r - 22), 6, 0, Math.PI * 2); x.fill();
  kn.v.textContent = valueText(id, v);
}
function refreshKnobs() {
  const none = !curTrack().prog;
  $("noProg").hidden = !none;
  $("knobs").classList.toggle("off", none); $("dataRow").classList.toggle("off", none); $("pname").disabled = none;
  $("noProgTrk").textContent = st.track + 1;
  for (const kn of knobs) { kn.el.classList.toggle("sel", kn.id === st.knob); drawKnob(kn); }
  const id = st.knob;
  if (id !== null && id !== undefined) {
    const r = $("dataSlider");
    r.min = minOf(id); r.max = maxOf(id); r.value = pget(id);
  }
  drawGraph();
  $("pname").value = nameOf(eprog()).trimEnd();
}
$("dataSlider").oninput = () => { if (st.knob === null) return; pset(st.knob, +$("dataSlider").value); refreshKnobs(); };
$("dataDn").onclick = () => { if (st.knob === null) return; const v = pget(st.knob); if (v > minOf(st.knob)) { pset(st.knob, v - 1); refreshKnobs(); } };
$("dataUp").onclick = () => { if (st.knob === null) return; const v = pget(st.knob); if (v < maxOf(st.knob)) { pset(st.knob, v + 1); refreshKnobs(); } };
$("pname").oninput = () => {
  const s = $("pname").value.toUpperCase().padEnd(6, " ").slice(0, 6);
  partUndo();
  for (let i = 0; i < 6; i++) pset(PRG(PF.NAME0 + i), clamp(s.charCodeAt(i), 32, 126));
  buildParts();
};
const partUndos = [];
function partUndo() { const tr = curTrack(); partUndos.push({ t: st.track, s: st.seq, d: tr.prog ? Array.from(eprog()) : null }); if (partUndos.length > 60) partUndos.shift(); }
$("undoBtn").onclick = () => {
  for (let i = partUndos.length - 1; i >= 0; i--) {
    if (partUndos[i].t !== st.track || partUndos[i].s !== st.seq) continue;
    const u = partUndos.splice(i, 1)[0];
    if (u.d) setTrackProg(st.track, Uint8Array.from(u.d)); else { curTrack().prog = null; dirty(); }
    loadPart(); refreshKnobs(); buildParts(); buildTracks(); return;
  }
  toast("Nothing to undo on this track.");
};
$("initBtn").onclick = () => { partUndo(); E.init_program(); setTrackProg(st.track, eprog()); loadPart(); refreshKnobs(); buildParts(); };
$("noProgGet").onclick = () => $("getPlaying").click();
$("noProgLib").onclick = () => showView("Lib");
$("noProgInit").onclick = () => $("initBtn").click();
$("clearProg").onclick = async () => {
  const tr = curTrack();
  if (!tr.prog) return;
  if (!await confirmBox("No program", `Take the program off track ${st.track + 1}? The track then plays whatever its part of the FM-1 has; nothing is sent to it.`, "Take it off")) return;
  partUndo();
  tr.prog = null; tr.ev = tr.ev;               // (its notes stay)
  dirty(); loadPart(); refreshKnobs(); buildParts(); buildTracks();
};
$("rndBtn").onclick = () => { partUndo(); E.randomize(40, 0, (Math.random() * 4294967295) >>> 0, 1 + (Math.random() * 998 | 0));
  setTrackProg(st.track, eprog()); loadPart(); refreshKnobs(); buildParts(); };
/* pointer-down on a knob remembers the program for Undo */
$("knobs").addEventListener("pointerdown", () => { if (curTrack().prog) partUndo(); }, true);

/* the page's picture: the wave, the envelope, the LFO, the filter */
function drawGraph() {
  const c = $("graph"), x = c.getContext("2d"), W = c.width, H = c.height, p = st.page;
  x.clearRect(0, 0, W, H);
  x.fillStyle = "#1a1a1a"; x.fillRect(0, H / 2, W, 1);
  x.strokeStyle = "#40dce6"; x.lineWidth = 3;
  const n = +p.slice(-1) - 1;
  if (p.startsWith("OSC") || p.startsWith("DCA") && n < 3) {
    const key = clamp(60 + 12 * pget(OSC(n, OF.OCT)) + pget(OSC(n, OF.SEMI)), 0, 127);
    const len = E.wave_samples(pget(OSC(n, OF.WAVE)), key);
    if (!len) return;
    const d = new Float32Array(E.memory.buffer, E.left(), len), lvl = p.startsWith("DCA") ? pget(OSC(n, OF.LEVEL)) / 63 : 1;
    x.lineWidth = 1.5; x.beginPath();
    for (let i = 0; i < W; i++) {
      const a = Math.floor(i * len / W), b = Math.max(a + 1, Math.floor((i + 1) * len / W));
      let lo = 1, hi = -1;
      for (let k = a; k < b && k < len; k++) { lo = Math.min(lo, d[k]); hi = Math.max(hi, d[k]); }
      x.moveTo(i, H / 2 - hi * lvl * (H / 2 - 6)); x.lineTo(i, H / 2 - lo * lvl * (H / 2 - 6) + 1);
    }
    x.stroke();
  } else if (p.startsWith("ENV")) {
    const g = (f) => pget(ENV(n, EF[f])), lv = [0, g("L1"), g("L2"), g("L3"), 0], tk = ["T1", "T2", "T3", "T4"].map((t) => E.env_ticks(g(t)));
    const neg = lv.some((v) => v < 0), zero = neg ? H / 2 : H - 6, sc = (neg ? H / 2 - 6 : H - 12) / 63;
    const hold = W * 0.15, tot = tk.reduce((a, b) => a + b, 0) || 1, avail = W - hold - 24;
    let px = 6; x.beginPath(); x.moveTo(px, zero);
    for (let k = 0; k < 4; k++) { px += 6 + tk[k] * avail / tot; x.lineTo(px, zero - lv[k + 1] * sc); if (k === 2) { px += hold; x.lineTo(px, zero - lv[3] * sc); } }
    x.stroke();
    x.fillStyle = "#8a857a"; x.font = "26px system-ui"; x.fillText((tot / 83.59).toFixed(2) + " s", W - 120, 34);
  } else if (p.startsWith("LFO")) {
    const g = (f) => pget(LFO(n, LF[f])), w = g("WAVE"), l1 = g("L1"), l2 = g("L2"), dl = g("DELAY"), hz = 0.1 + g("FREQ") * 0.4;
    let seed = 7, held = 0; x.beginPath();
    for (let i = 0; i < W; i++) {
      const t = i / W * 2, ph = (t * hz) % 1, ramp = dl ? Math.min(1, t / (dl / 20)) : 1, depth = (l1 + (l2 - l1) * ramp) / 63;
      let v = w === 0 ? 1 - 4 * Math.abs(ph - 0.5) : w === 1 ? 2 * ph - 1 : w === 2 ? (ph < 0.5 ? 1 : -1) : 0;
      if (w === 3) { if (Math.floor(t * hz * 4) !== held) { held = Math.floor(t * hz * 4); seed = (seed * 1103515245 + 12345) & 0x7fffffff; } v = seed / 0x3fffffff - 1; }
      const y = H / 2 - v * depth * (H / 2 - 6);
      if (i) x.lineTo(i, y); else x.moveTo(i, y);
    }
    x.stroke();
  } else if (p === "FILTER") {
    const fc = E.filter_cutoff(60), top = 30, span = 72, yOf = (db) => clamp((top - db) * (H - 1) / span, 0, H - 1);
    x.beginPath();
    for (let i = 0; i <= W; i += 3) { const y = yOf(E.filter_db(20 * Math.pow(1000, i / W), fc)); if (i) x.lineTo(i, y); else x.moveTo(i, y); }
    x.stroke();
    x.fillStyle = "#8a857a"; x.font = "26px system-ui"; x.textAlign = "right";
    x.fillText(fc >= 1000 ? (fc / 1000).toFixed(2) + " kHz" : Math.round(fc) + " Hz", W - 10, 34); x.textAlign = "left";
  } else {
    x.fillStyle = "#555"; x.font = "28px system-ui";
    const tr = curTrack();
    x.fillText(`Track ${st.track + 1} · ${tr.status === "TABLET" ? "the tablet's engine" : tr.ch < 8 ? "the FM-1's part " + (tr.ch + 1) : "MIDI channel " + (tr.ch + 1)}`, 16, H / 2 + 10);
  }
}

/* the FM-1's banks: get a program onto this track, save this track's sound there */
document.querySelectorAll("[data-s]").forEach((b) => { b.onclick = () => {
  if (b.dataset.s === "bank") st.bank = (st.bank + +b.dataset.d + 8) % 8; else st.prog = (st.prog + +b.dataset.d + 40) % 40;
  $("fmBank").textContent = st.bank + 1; $("fmProg").textContent = String(st.prog + 1).padStart(2, "0"); dirty();
}; });
async function readProgram(b, p) { const a = await link.send(0x31, [b, p], 1500); return { b: a[6], p: a[7], edited: a[8], prog: unnib(a, 9) }; }
$("getBtn").onclick = async () => {
  try { const r = await readProgram(st.bank, st.prog); partUndo(); setTrackProg(st.track, r.prog, { b: r.b, p: r.p }); loadPart(); refreshKnobs(); buildParts();
        say("partStatus", `Bank ${r.b + 1} #${r.p + 1} on track ${st.track + 1}.`, "ok"); }
  catch (e) { say("partStatus", "Not read: " + e.message, "bad"); }
};
$("getPlaying").onclick = async () => {
  try { const r = await readProgram(0x7F, 0); partUndo(); setTrackProg(st.track, r.prog, { b: r.b, p: r.p }, curTrack().ch === 0); loadPart(); refreshKnobs(); buildParts(); buildTracks();
        say("partStatus", `The FM-1's program (bank ${r.b + 1} #${r.p + 1}${r.edited ? ", edited" : ""}) on track ${st.track + 1}.`, "ok"); }
  catch (e) { say("partStatus", "Not read: " + e.message, "bad"); }
};
$("saveBtn").onclick = async () => {
  const name = nameOf(eprog()).trim();
  if (!await confirmBox("Save to the FM-1", `Store "${name}" into the FM-1's bank ${st.bank + 1}, program ${st.prog + 1}? It replaces what is there.`, "Save")) return;
  try {
    await link.send(0x32, [st.bank, st.prog, ...nib(eprog())], 4000);
    sent[0] = null;                            // (the FM-1's panel now plays the saved one: track 1's sound goes back)
    if (curSeq().tracks.some((t) => t.prog && t.ch === 0)) sendAllParts();
    const tr = curTrack(); tr.prog.b = st.bank; tr.prog.p = st.prog; tr.prog.edited = false;
    const L = libs.find((l) => l.fm === st.bank); if (L) L.progs[st.prog] = new Uint8Array(eprog());
    dirty();
    say("partStatus", `Saved into bank ${st.bank + 1}, program ${st.prog + 1}.`, "ok");
  } catch (e) { say("partStatus", "Not saved: " + e.message, "bad"); }
};

/* ================================================================== SEQ */
function selectTrack(t) {
  st.track = t;
  loadPart();
  buildParts(); refreshKnobs(); buildTracks(); keysUi(); followKeys();
  $("partLbl").textContent = `track ${t + 1}`;
  $("edTrk").textContent = t + 1; $("libTrkN").textContent = t + 1;
  say("partStatus", "");
  dirty();
}
function selectSeq(i) {
  if (player && player.playing && playMode === "seq") stop();
  st.seq = i; ensureSeq(i);
  locateBar = 0;
  sent.fill(null);
  refreshSeqViews();
  sendAllParts();
  dirty();
}
function refreshSeqViews() {
  loadPart();
  buildSeqList(); buildSeqCtl(); buildTracks(); buildParts(); refreshKnobs(); keysUi(); buildRollTracks(); drawRoll(); lcd();
}
function buildSeqList() {
  const L = $("seqList");
  L.replaceChildren();
  for (let i = 0; i < S.NSEQ; i++) {
    const q = project.seqs[i], b = document.createElement("button");
    b.innerHTML = `<span class="n">${String(i + 1).padStart(2, "0")}</span><span></span>`;
    b.lastChild.textContent = q ? q.name : "empty";
    if (!q) b.classList.add("empty");
    if (i === st.seq) b.classList.add("on");
    b.onclick = () => selectSeq(i);
    L.append(b);
  }
}
const SIGS = [[4, 4], [3, 4], [2, 4], [5, 4], [6, 8], [7, 8], [9, 8], [12, 8], [2, 2], [3, 8], [5, 8]];
function buildSeqCtl() {
  const q = curSeq();
  $("seqName").value = q.name;
  $("qTempo").textContent = q.tempo; $("qBars").textContent = q.bars;
  $("qSig").textContent = q.num + "/" + q.den;
  $("qLoop").textContent = q.loop ? "On" : "Off"; $("qLoop").classList.toggle("on", q.loop);
  $("qClick").textContent = st.click ? "On" : "Off"; $("qClick").classList.toggle("on", st.click);
  $("qCount").textContent = st.countoff ? st.countoff + " bar" + (st.countoff > 1 ? "s" : "") : "Off";
  $("qGrid").textContent = S.GRIDS[st.grid][0];
  $("qMode").textContent = st.mode === "REPLACE" ? "Replace" : "Add";
  $("qPunch").textContent = st.punch ? `${(st.punchIn || 0) + 1}–${st.punchOut || q.bars}` : "Off";
  $("qPunch").classList.toggle("on", !!st.punch);
}
$("seqName").oninput = () => { curSeq().name = $("seqName").value.toUpperCase().slice(0, 6); buildSeqList(); lcd(); dirty(); };
let holdT = 0;
document.querySelectorAll("[data-q]").forEach((b) => {
  const step = () => {
    const q = curSeq(), d = +b.dataset.d;
    if (b.dataset.q === "tempo") q.tempo = clamp(q.tempo + d, 20, 300);
    else { pushUndo("length"); S.Edit.setLength(q, q.bars + d); }
    buildSeqCtl(); buildTracks(); lcd(); dirty();
  };
  b.onpointerdown = () => { step(); let n = 0; clearInterval(holdT); holdT = setInterval(() => { if (++n > 4) step(); }, 90); };
  b.onpointerup = b.onpointerleave = b.onpointercancel = () => clearInterval(holdT);
});
$("qSig").onclick = async () => {
  const q = curSeq(), v = await choose("time signature", SIGS.map(([n, d]) => [n + "/" + d, n + "/" + d]), q.num + "/" + q.den);
  if (!v) return;
  pushUndo("time"); const [n, d] = v.split("/").map(Number); S.Edit.timeSignature(q, n, d); buildSeqCtl(); buildTracks(); dirty();
};
$("qLoop").onclick = () => { curSeq().loop = !curSeq().loop; buildSeqCtl(); dirty(); };
$("qClick").onclick = () => { st.click = !st.click; if (player) player.clickOn = st.click; buildSeqCtl(); dirty(); };
$("qCount").onclick = () => { st.countoff = (st.countoff + 1) % 3; buildSeqCtl(); dirty(); };
$("qGrid").onclick = async () => { const v = await choose("auto-correct", S.GRIDS.map((g, i) => [g[0], i]), st.grid); if (v !== undefined) { st.grid = v; buildSeqCtl(); dirty(); } };
$("qMode").onclick = () => { st.mode = st.mode === "REPLACE" ? "ADD" : "REPLACE"; buildSeqCtl(); dirty(); };
$("qPunch").onclick = async () => {
  if (st.punch) { st.punch = false; buildSeqCtl(); return; }
  const q = curSeq(), a = await askNumber("punch in at bar", (st.punchIn || 0) + 1, 1, q.bars);
  if (a === undefined) return;
  const b = await askNumber("punch out after bar", Math.max(a, st.punchOut || q.bars), a, q.bars);
  if (b === undefined) return;
  st.punch = true; st.punchIn = a - 1; st.punchOut = b; buildSeqCtl();
};

const lanes = [];
function buildTracks() {
  const box = $("tracks"), q = curSeq();
  box.replaceChildren();
  lanes.length = 0;
  q.tracks.forEach((tr, t) => {
    const r = document.createElement("div");
    r.className = "trk" + (t === st.track ? " sel" : "") + (player && player.rec && player.rec.track === t ? " rec" : "");
    const btn = (cls, text, f) => { const b = document.createElement("button"); b.className = cls; b.textContent = text; b.onclick = f; return b; };
    r.append(btn("tn", String(t + 1), () => selectTrack(t)));
    r.append(btn("tp", tr.prog ? tr.prog.name || "------" : "no program", async () => { selectTrack(t); showView("Lib"); }));
    r.append(btn("", tr.status, () => { const i = S.STATUS.indexOf(tr.status); tr.status = S.STATUS[(i + 1) % 3]; sent[tr.ch] = null; sendPart(t, true);
      if (tr.status !== "MIDI" && tr.prog) Tab.program(t, tr.prog.data); buildTracks(); keysUi(); followKeys(); dirty(); }));
    r.append(btn("", "CH " + (tr.ch + 1), async () => {
      const v = await choose(`track ${t + 1}: MIDI channel`, Array.from({ length: 16 }, (_, i) => [`${i + 1}${i < 8 ? "  FM-1 part " + (i + 1) : ""}`, i]), tr.ch);
      if (v !== undefined) { tr.ch = v; sent.fill(null); sendPart(t, true); buildTracks(); keysUi(); followKeys(); dirty(); }
    }));
    const mix = document.createElement("div"); mix.className = "mix";
    mix.innerHTML = `<span>MIX <b></b></span><input type="range" min="0" max="99">`;
    const mr = mix.querySelector("input"), mb = mix.querySelector("b");
    mr.value = tr.mix; mb.textContent = tr.mix;
    mr.oninput = () => { tr.mix = +mr.value; mb.textContent = tr.mix; dirty(); };
    r.append(mix);
    const mute = btn("mute" + (tr.mute ? " on" : ""), "M", () => { tr.mute = !tr.mute; mute.classList.toggle("on", tr.mute); dirty(); });
    r.append(mute);
    r.append(btn("", "✎", () => { selectTrack(t); showView("Roll"); }));
    const cv = document.createElement("canvas"); cv.width = 800; cv.height = 92;
    cv.onclick = () => selectTrack(t);
    r.append(cv);
    lanes[t] = cv;
    box.append(r);
    drawLane(t);
  });
  $("edTrk").textContent = st.track + 1;
}
function drawLane(t, pos) {
  const cv = lanes[t]; if (!cv) return;
  const q = curSeq(), tr = q.tracks[t], x = cv.getContext("2d"), W = cv.width, H = cv.height, len = S.seqLength(q), bt = S.barTicks(q);
  x.clearRect(0, 0, W, H);
  x.fillStyle = "#1c1c1c";
  for (let b = 0; b <= q.bars; b++) x.fillRect(Math.round(b * bt / len * (W - 1)), 0, 2, H);
  const notes = tr.ev.filter(S.isNote);
  if (notes.length) {
    let lo = 127, hi = 0; for (const e of notes) { lo = Math.min(lo, e.k); hi = Math.max(hi, e.k); }
    const span = Math.max(12, hi - lo + 1);
    x.fillStyle = tr.mute ? "#3a3a3a" : t === st.track ? "#40dce6" : "#2f8e94";
    for (const e of notes) {
      const y = H - 6 - (e.k - lo) / span * (H - 12);
      x.fillRect(e.t / len * W, y - 3, Math.max(3, e.d / len * W), 6);
    }
  }
  if (pos !== undefined) { x.fillStyle = "#f2ead6"; x.fillRect(pos / len * W, 0, 2, H); }
}
function edStatus(t, cls) { say("seqStatus", t, cls); }
document.querySelectorAll("[data-ed]").forEach((b) => { b.onclick = () => seqEdit(b.dataset.ed); });
async function seqEdit(what) {
  const q = curSeq(), tr = curTrack(), T = st.track;
  const others = (title) => choose(title, q.tracks.map((x, i) => [`Track ${i + 1}${x.prog ? " " + x.prog.name : ""}`, i]).filter(([, i]) => i !== T));
  const seqs = (title, any) => choose(title, project.seqs.map((s, i) => [`${String(i + 1).padStart(2, "0")} ${s ? s.name : "(empty)"}`, i]).filter(([, i]) => i !== st.seq && (any || project.seqs[i])));
  switch (what) {
  case "undo": {
    for (let i = undo.length - 1; i >= 0; i--) if (undo[i].s === st.seq) {
      const u = undo.splice(i, 1)[0]; project.seqs[st.seq] = S.loadProject({ seqs: [u.data] }).seqs[0];
      refreshSeqViews(); edStatus("Undone: " + u.what, "ok"); dirty(); return;
    }
    edStatus("Nothing to undo.");
    return;
  }
  case "step": {
    if (player && player.playing) stop();
    player = player || new S.Player(project, emit);
    pushUndo("step entry");
    player.stepStart(st.seq, T, S.GRIDS[Math.max(1, st.stepSize)][1], st.stepGate);
    $("stepBox").hidden = false; stepUi();
    return;
  }
  case "transpose": { const v = await askNumber(`transpose track ${T + 1}`, 0, -24, 24, (x) => (x > 0 ? "+" : "") + x); if (!v) return;
    pushUndo("transpose"); S.Edit.transpose(tr, v); break; }
  case "quantize": { const v = await choose(`auto-correct track ${T + 1}`, S.GRIDS.slice(1).map((g, i) => [g[0], i + 1]), st.grid); if (v === undefined) return;
    const s = await askNumber("strength", 100, 10, 100, (x) => x + "%"); if (s === undefined) return;
    pushUndo("auto-correct"); S.Edit.quantize(q, tr, S.GRIDS[v][1], s); break; }
  case "velocity": { const v = await askNumber(`velocity of track ${T + 1}: scale`, 100, 10, 200, (x) => x + "%"); if (v === undefined) return;
    pushUndo("velocity"); S.Edit.velocity(tr, v, 0); break; }
  case "remctl": pushUndo("remove controllers"); S.Edit.removeControllers(tr); break;
  case "erase": if (!await confirmBox("Erase track", `Erase track ${T + 1}'s notes?`, "Erase")) return; pushUndo("erase"); S.Edit.erase(tr); break;
  case "copy": { const d = await others(`copy track ${T + 1} to`); if (d === undefined) return; pushUndo("copy"); S.Edit.copy(tr, q.tracks[d]); break; }
  case "merge": { const d = await others(`merge track ${T + 1} into`); if (d === undefined) return; pushUndo("merge"); S.Edit.merge(tr, q.tracks[d]); break; }
  case "addbars": { const at = await askNumber("insert before bar", q.bars + 1, 1, q.bars + 1); if (at === undefined) return;
    const n = await askNumber("how many bars", 1, 1, 64); if (n === undefined) return; pushUndo("add bars"); S.Edit.insertBars(q, at - 1, n); break; }
  case "delbars": { const at = await askNumber("delete from bar", 1, 1, q.bars); if (at === undefined) return;
    const n = await askNumber("how many bars", 1, 1, q.bars - at + 1); if (n === undefined) return; pushUndo("delete bars");
    if (!S.Edit.deleteBars(q, at - 1, n)) { edStatus("A sequence keeps at least one bar.", "bad"); return; } break; }
  case "append": { const d = await seqs("append which sequence after this one"); if (d === undefined) return; pushUndo("append"); S.Edit.append(q, project.seqs[d]); break; }
  case "copyseq": { const d = await seqs("copy this sequence to", true); if (d === undefined) return;
    if (project.seqs[d] && !await confirmBox("Copy sequence", `Replace sequence ${d + 1} (${project.seqs[d].name})?`, "Replace")) return;
    project.seqs[d] = S.clone(q); dirty(); buildSeqList(); edStatus(`Copied to ${d + 1}.`, "ok"); return; }
  case "eraseseq": if (!await confirmBox("Erase sequence", `Erase sequence ${st.seq + 1} (${q.name}) and everything in it?`, "Erase")) return;
    pushUndo("erase sequence"); project.seqs[st.seq] = null; ensureSeq(st.seq); refreshSeqViews(); dirty(); return;
  case "exportmid": download(S.toSMF(project, "seq", st.seq), (q.name.trim() || "SEQ") + ".mid", "audio/midi"); return;
  case "importmid": {
    const f = await pickFile($("midFile")); if (!f) return;
    try { pushUndo("import"); const n = S.fromSMF(f.bytes, f.name.replace(/\.midi?$/i, "")); project.seqs[st.seq] = n; refreshSeqViews(); dirty(); edStatus("Imported " + f.name, "ok"); }
    catch (e) { edStatus("Not imported: " + e.message, "bad"); }
    return;
  }
  default: return;
  }
  buildTracks(); buildSeqCtl(); drawRoll(); dirty();
  edStatus("Done. Undo takes it back.", "ok");
}
/* step entry */
function stepUi() {
  const s = player && player.step;
  if (!s) { $("stepBox").hidden = true; return; }
  const q = curSeq(), bt = S.barTicks(q), beatT = S.PPQ * 4 / q.den, bar = Math.floor(s.cursor / bt), beat = Math.floor((s.cursor % bt) / beatT);
  $("stepPos").textContent = `${String(bar + 1).padStart(3, "0")}.${beat + 1}.${String(s.cursor % beatT).padStart(2, "0")}`;
  $("stepSize").textContent = S.GRIDS[Math.max(1, st.stepSize)][0];
  $("stepGate").textContent = `gate ${st.stepGate}%`;
  drawLane(st.track, s.cursor); drawRoll(); buildSeqCtl();
}
$("stepSize").onclick = async () => { const v = await choose("step size", S.GRIDS.slice(1).map((g, i) => [g[0], i + 1]), st.stepSize);
  if (v !== undefined) { st.stepSize = v; if (player && player.step) player.step.size = S.GRIDS[v][1]; stepUi(); dirty(); } };
$("stepGate").onclick = () => { st.stepGate = st.stepGate >= 100 ? 25 : st.stepGate + 25; if (st.stepGate === 125) st.stepGate = 100;
  if (player && player.step) player.step.gate = st.stepGate; stepUi(); };
$("stepRest").onclick = () => { if (player && player.step) { player.stepRest(); stepUi(); } };
$("stepTie").onclick = () => { if (player && player.step) { player.stepTie(); stepUi(); } };
$("stepBack").onclick = () => { if (player && player.step) { player.stepBack(); stepUi(); } };
$("stepDone").onclick = () => { if (player) player.stepStop(); $("stepBox").hidden = true; buildTracks(); dirty(); };

/* ================================================================== SONG */
function buildSongList() {
  const L = $("songList");
  L.replaceChildren();
  for (let i = 0; i < S.NSONG; i++) {
    const g = project.songs[i], b = document.createElement("button");
    b.innerHTML = `<span class="n">${String(i + 1).padStart(2, "0")}</span><span></span>`;
    b.lastChild.textContent = g ? g.name : "empty";
    if (!g) b.classList.add("empty");
    if (i === st.song) b.classList.add("on");
    b.onclick = () => { st.song = i; if (!project.songs[i]) project.songs[i] = S.newSong("SONG" + String(i + 1).padStart(2, "0")); buildSong(); dirty(); };
    L.append(b);
  }
}
function buildSong() {
  buildSongList();
  const g = project.songs[st.song] || (project.songs[st.song] = S.newSong("SONG" + String(st.song + 1).padStart(2, "0")));
  $("songName").value = g.name;
  $("songLoop").textContent = g.loop ? "Loop on" : "Loop off"; $("songLoop").classList.toggle("on", g.loop);
  const box = $("steps");
  box.replaceChildren();
  const seqOpts = () => project.seqs.map((s, i) => s ? [`${String(i + 1).padStart(2, "0")} ${s.name}`, i] : null).filter(Boolean);
  g.steps.forEach((stp, i) => {
    const r = document.createElement("div"); r.className = "stp" + (i === songNow && player && player.playing && playMode === "song" ? " now" : "");
    const num = document.createElement("div"); num.className = "num"; num.textContent = String(i + 1).padStart(2, "0");
    const sel = document.createElement("select");
    for (const [l, v] of seqOpts()) sel.append(new Option(l, v));
    if (!project.seqs[stp.s]) sel.append(new Option(`${String(stp.s + 1).padStart(2, "0")} (empty)`, stp.s));
    sel.value = stp.s;
    sel.onchange = () => { stp.s = +sel.value; buildTimeline(); dirty(); };
    const stepper = (label, get, set, lo, hi, fmt) => {
      const w = document.createElement("div"); w.className = "stepper"; w.style.gridTemplateColumns = "40px 1fr 40px";
      const v = document.createElement("div"); v.className = "vfd"; const show = () => { v.textContent = label + " " + fmt(get()); };
      const m = document.createElement("button"); m.textContent = "−"; m.onclick = () => { set(clamp(get() - 1, lo, hi)); show(); buildTimeline(); dirty(); };
      const p = document.createElement("button"); p.textContent = "+"; p.onclick = () => { set(clamp(get() + 1, lo, hi)); show(); buildTimeline(); dirty(); };
      show(); w.append(m, v, p); return w;
    };
    const rep = stepper("×", () => stp.rep, (v) => { stp.rep = v; }, 1, 99, (x) => String(x).padStart(2, "0"));
    const trn = stepper("TR", () => stp.tr, (v) => { stp.tr = v; }, -24, 24, (x) => (x > 0 ? "+" : x < 0 ? "−" : " ") + String(Math.abs(x)).padStart(2, "0"));
    const btn = (t, f) => { const b = document.createElement("button"); b.className = "sm"; b.textContent = t; b.onclick = f; return b; };
    r.append(num, sel, rep, trn,
      btn("▲", () => { if (i) { [g.steps[i - 1], g.steps[i]] = [g.steps[i], g.steps[i - 1]]; buildSong(); dirty(); } }),
      btn("▼", () => { if (i < g.steps.length - 1) { [g.steps[i + 1], g.steps[i]] = [g.steps[i], g.steps[i + 1]]; buildSong(); dirty(); } }),
      btn("✕", () => { g.steps.splice(i, 1); buildSong(); dirty(); }));
    box.append(r);
  });
  buildTimeline();
}
function buildTimeline() {
  const g = project.songs[st.song], tl = $("timeline");
  tl.replaceChildren();
  if (!g) return;
  let total = 0;
  const parts = g.steps.map((s) => { const q = project.seqs[s.s], w = q ? S.seqLength(q) * s.rep : 0; total += w; return { s, q, w }; });
  let at = 0;
  for (const p of parts) {
    if (!p.w) continue;
    const d = document.createElement("div");
    d.style.left = (at / total * 100) + "%"; d.style.width = `calc(${p.w / total * 100}% - 2px)`;
    d.textContent = `${p.q.name}${p.s.rep > 1 ? " ×" + p.s.rep : ""}${p.s.tr ? (p.s.tr > 0 ? " +" : " ") + p.s.tr : ""}`;
    tl.append(d); at += p.w;
  }
  const ph = document.createElement("div"); ph.className = "ph"; ph.id = "songPh"; ph.hidden = true; tl.append(ph);
  const secs = parts.reduce((a, p) => a + (p.q ? p.w * 60 / (p.q.tempo * S.PPQ) : 0), 0);
  say("songStatus", g.steps.length ? `${g.steps.length} step${g.steps.length > 1 ? "s" : ""}, ${Math.floor(secs / 60)}:${String(Math.round(secs % 60)).padStart(2, "0")}` : "No steps yet.");
}
$("songName").oninput = () => { project.songs[st.song].name = $("songName").value.toUpperCase().slice(0, 6); buildSongList(); dirty(); };
$("songLoop").onclick = () => { const g = project.songs[st.song]; g.loop = !g.loop; buildSong(); dirty(); };
$("addStep").onclick = () => {
  const g = project.songs[st.song];
  if (g.steps.length >= S.MAXSTEP) return;
  const last = g.steps[g.steps.length - 1];
  g.steps.push({ s: last ? last.s : st.seq, rep: 1, tr: 0 });
  ensureSeq(g.steps[g.steps.length - 1].s);
  buildSong(); dirty();
};
$("songPlay").onclick = () => { if (player && player.playing) stop(); playMode = "song"; play(); };
$("songErase").onclick = async () => { if (!await confirmBox("Erase song", "Erase this song's steps? (Its sequences stay.)", "Erase")) return;
  project.songs[st.song] = null; buildSong(); dirty(); };

/* ================================================================== ROLL: the track's notes, by touch */
const roll = { x0: 0, top: 84, sel: null, drag: null, rowH: 22, keyW: 54 };
function buildRollTracks() {
  const box = $("rollTracks"); box.replaceChildren();
  for (let t = 0; t < 8; t++) {
    const b = document.createElement("button"); b.className = "sm" + (t === st.track ? " on" : ""); b.textContent = t + 1;
    b.onclick = () => { roll.sel = null; selectTrack(t); buildRollTracks(); drawRoll(); };
    box.append(b);
  }
  $("rollGrid").textContent = "grid " + S.GRIDS[st.rollGrid][0];
  const q = curSeq();
  $("rollBars").textContent = q.bars; $("rollSig").textContent = q.num + "/" + q.den;
  $("rollVel").textContent = "vel " + (roll.sel ? roll.sel.v : st.rollVel);
}
function rollGeom() {
  const c = $("roll"), q = curSeq(), len = S.seqLength(q);
  const W = c.width, H = c.height, ppt = (W - roll.keyW) / (len / st.rollZoom * 1.06);   /* (a little room after the end) */
  return { c, q, len, W, H, ppt, rows: Math.ceil(H / roll.rowH) };
}
function drawRoll(pos) {
  const c = $("roll");
  if (!c || $("vRoll").hidden) return;
  const r = c.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  if (c.width !== Math.round(r.width * dpr) || c.height !== Math.round(r.height * dpr)) { c.width = Math.round(r.width * dpr); c.height = Math.round(r.height * dpr); }
  roll.rowH = Math.round(20 * dpr); roll.keyW = Math.round(50 * dpr);
  const g = rollGeom(), x = c.getContext("2d"), { q, len, W, H, ppt } = g, bt = S.barTicks(q), beatT = S.PPQ * 4 / q.den, grid = S.GRIDS[st.rollGrid][1];
  x.clearRect(0, 0, W, H);
  const yOf = (k) => (roll.top - k) * roll.rowH, xOf = (t) => roll.keyW + (t - roll.x0) * ppt;
  for (let k = roll.top; k > roll.top - g.rows - 1; k--) {
    const y = yOf(k), black = [1, 3, 6, 8, 10].includes(((k % 12) + 12) % 12);
    x.fillStyle = black ? "#0a0a0a" : "#121212"; x.fillRect(roll.keyW, y, W, roll.rowH - 1);
    x.fillStyle = black ? "#1d1d1d" : "#d9d1bc"; x.fillRect(0, y, roll.keyW - 2, roll.rowH - 1);
    if (k % 12 === 0) { x.fillStyle = "#555"; x.font = `${Math.round(11 * dpr)}px system-ui`; x.fillText(NOTE(k), 4 * dpr, y + roll.rowH - 6 * dpr); }
  }
  for (let t = Math.floor(roll.x0 / grid) * grid; t <= len; t += grid) {
    const px = xOf(t); if (px < roll.keyW) continue; if (px > W) break;
    x.fillStyle = t % bt === 0 ? "#3a3a3a" : t % beatT === 0 ? "#222" : "#171717"; x.fillRect(px, 0, t % bt === 0 ? 2 : 1, H);
  }
  /* the loop: its end a line, what lies past it dim; the bars numbered along the top */
  x.fillStyle = "rgba(0,0,0,.6)"; x.fillRect(xOf(len), 0, W, H);
  x.fillStyle = "#f2ead6"; x.fillRect(xOf(len) - dpr, 0, 3 * dpr, H);
  x.font = `600 ${Math.round(11 * dpr)}px system-ui`;
  x.fillText("END · " + q.bars + " bar" + (q.bars > 1 ? "s" : ""), xOf(len) + 6 * dpr, 14 * dpr);
  x.fillStyle = "#8a857a";
  for (let b = 0; b < q.bars; b++) { const px = xOf(b * bt); if (px >= roll.keyW && px < W - 20 * dpr) x.fillText(String(b + 1), px + 4 * dpr, 14 * dpr); }
  q.tracks.forEach((tr, ti) => {                // the other tracks, faint
    if (ti === st.track) return;
    x.fillStyle = "#1e3a3c";
    for (const e of tr.ev) if (S.isNote(e)) x.fillRect(xOf(e.t), yOf(e.k) + 4, Math.max(2, e.d * ppt), roll.rowH - 9);
  });
  for (const e of curTrack().ev) {
    if (!S.isNote(e)) continue;
    const px = xOf(e.t), w = Math.max(4, e.d * ppt), y = yOf(e.k);
    if (px + w < roll.keyW || px > W) continue;
    x.fillStyle = e === roll.sel ? "#f2ead6" : `rgba(64,220,230,${0.45 + 0.55 * e.v / 127})`;
    x.fillRect(px, y + 1, w, roll.rowH - 3);
    x.fillStyle = "#001215"; x.fillRect(px + w - 3, y + 1, 3, roll.rowH - 3);
  }
  if (pos !== undefined) { x.fillStyle = "#f2ead6"; x.fillRect(xOf(pos), 0, 2 * dpr, H); }
}
(function rollInput() {
  const c = $("roll");
  const at = (e) => { const r = c.getBoundingClientRect(), dpr = c.width / r.width; return { px: (e.clientX - r.left) * dpr, py: (e.clientY - r.top) * dpr }; };
  const hit = (px, py) => {
    const g = rollGeom(), k = roll.top - Math.floor(py / roll.rowH), t = roll.x0 + (px - roll.keyW) / g.ppt;
    const n = curTrack().ev.filter((e) => S.isNote(e) && e.k === k && t >= e.t && t <= e.t + Math.max(e.d, 4 / g.ppt)).pop();
    return { k, t, n, edge: n && (e2px(n.t + n.d, g) - px) < 14 * (window.devicePixelRatio || 1) };
  };
  const e2px = (t, g) => roll.keyW + (t - roll.x0) * g.ppt;
  let lastTap = 0;
  c.onpointerdown = (e) => {
    e.preventDefault(); c.setPointerCapture(e.pointerId);
    const p = at(e), h = hit(p.px, p.py), g = rollGeom();
    if (p.px < roll.keyW) { noteOn(h.k, st.rollVel); setTimeout(() => noteOff(h.k), 250); return; }
    roll.drag = { p, h, x0: roll.x0, top: roll.top, moved: false, mode: h.n ? (h.edge ? "len" : "move") : "pan", t0: h.n ? h.n.t : 0, k0: h.n ? h.n.k : 0, d0: h.n ? h.n.d : 0, g };
    if (h.n) { pushUndo("note edit"); roll.sel = h.n; buildRollTracks(); drawRoll(); }
  };
  c.onpointermove = (e) => {
    const d = roll.drag; if (!d) return;
    const p = at(e), dx = p.px - d.p.px, dy = p.py - d.p.py, grid = S.GRIDS[st.rollGrid][1], len = S.seqLength(curSeq());
    if (!d.moved && Math.hypot(dx, dy) < 8) return;
    d.moved = true;
    if (d.mode === "pan") {
      roll.x0 = clamp(d.x0 - dx / d.g.ppt, 0, Math.max(0, len - len / st.rollZoom));
      roll.top = clamp(Math.round(d.top + dy / roll.rowH), 24, 127);
    } else if (d.mode === "move") {
      const n = roll.sel, t = clamp(Math.round((d.t0 + dx / d.g.ppt) / grid) * grid, 0, len - 1), k = clamp(d.k0 + Math.round(-dy / roll.rowH), 0, 127);
      if (k !== n.k) { noteOn(k, n.v); setTimeout(() => noteOff(k), 120); }
      n.t = t; n.k = k;
    } else {
      roll.sel.d = Math.max(grid > 1 ? grid / 2 : 6, Math.round((d.d0 + dx / d.g.ppt) / grid) * grid || grid);
    }
    drawRoll();
  };
  c.onpointerup = (e) => {
    const d = roll.drag; roll.drag = null; if (!d) return;
    if (d.mode !== "pan" && d.moved) { S.sortEvents(curTrack().ev); dirty(); drawLane(st.track); return; }
    if (d.moved) return;
    const now = performance.now();
    if (d.h.n) {                              // a tap on a note: select; a double tap: delete
      if (now - lastTap < 350 && roll.sel === d.h.n) { curTrack().ev.splice(curTrack().ev.indexOf(d.h.n), 1); roll.sel = null; dirty(); }
      else { noteOn(d.h.n.k, d.h.n.v); setTimeout(() => noteOff(d.h.n.k), 200); }
      lastTap = now; buildRollTracks(); drawRoll(); return;
    }
    const grid = S.GRIDS[st.rollGrid][1], len = S.seqLength(curSeq()), t = Math.floor(d.h.t / grid) * grid;
    if (t < 0 || t >= len) return;
    pushUndo("add note");
    const n = { t, k: d.h.k, v: st.rollVel, d: grid > 1 ? grid : 24 };
    S.insertEvent(curTrack().ev, n); roll.sel = n;
    noteOn(n.k, n.v); setTimeout(() => noteOff(n.k), 200);
    void e; dirty(); buildRollTracks(); drawRoll(); drawLane(st.track);
  };
  c.onpointercancel = () => { roll.drag = null; };
})();
$("rollGrid").onclick = async () => { const v = await choose("grid", S.GRIDS.slice(1).map((g, i) => [g[0], i + 1]), st.rollGrid); if (v) { st.rollGrid = v; buildRollTracks(); drawRoll(); dirty(); } };
$("rollVel").onclick = async () => {
  const v = await askNumber(roll.sel ? "this note's velocity" : "new notes' velocity", roll.sel ? roll.sel.v : st.rollVel, 1, 127);
  if (v === undefined) return;
  if (roll.sel) { pushUndo("velocity"); roll.sel.v = v; } else st.rollVel = v;
  buildRollTracks(); drawRoll(); dirty();
};
$("rollDel").onclick = () => { if (!roll.sel) return; pushUndo("delete note"); const ev = curTrack().ev; ev.splice(ev.indexOf(roll.sel), 1); roll.sel = null; drawRoll(); drawLane(st.track); dirty(); };
function rollLen(d) {                        // the loop's length from the Roll page (the Seq page's BARS)
  const q = curSeq();
  if (q.bars + d < 1) return;
  pushUndo("length"); S.Edit.setLength(q, q.bars + d);
  roll.x0 = Math.min(roll.x0, Math.max(0, S.seqLength(q) * (1 - 1 / st.rollZoom)));
  buildRollTracks(); drawRoll(); buildSeqCtl(); lcd(); dirty();
}
$("rollBarsDn").onclick = () => rollLen(-1);
$("rollBarsUp").onclick = () => rollLen(1);
$("rollSig").onclick = async () => { await $("qSig").onclick(); buildRollTracks(); drawRoll(); };
$("rollZoomIn").onclick = () => { st.rollZoom = Math.min(16, st.rollZoom * 2); drawRoll(); };
$("rollZoomOut").onclick = () => { st.rollZoom = Math.max(1, st.rollZoom / 2); roll.x0 = Math.min(roll.x0, S.seqLength(curSeq()) * (1 - 1 / st.rollZoom)); drawRoll(); };
addEventListener("resize", () => drawRoll());

/* ================================================================== LIBRARY */
function buildLib() {
  const L = $("bankList"); L.replaceChildren();
  libs.forEach((b, i) => {
    const e = document.createElement("button");
    e.innerHTML = `<span class="n">${b.fm >= 0 ? "FM" : "SYX"}</span><span></span>`; e.lastChild.textContent = b.name;
    if (i === libSel) e.classList.add("on");
    e.onclick = () => { libSel = i; libProg = 0; buildLib(); };
    L.append(e);
  });
  const g = $("progs"); g.replaceChildren();
  const b = libs[libSel];
  $("libTitle").textContent = b ? b.name : "Read a bank from the FM-1, or open a .syx file.";
  if (!b) return;
  b.progs.forEach((p, i) => {
    const e = document.createElement("button");
    e.innerHTML = `<span class="n">${String(i + 1).padStart(2, "0")}</span><span></span>`; e.lastChild.textContent = p ? nameOf(p) : "------";
    if (i === libProg) e.classList.add("on");
    e.onclick = () => { libProg = i; buildLib(); };
    e.ondblclick = () => useLib();
    g.append(e);
  });
}
function useLib() {
  const b = libs[libSel]; if (!b || !b.progs[libProg]) return;
  partUndo();
  setTrackProg(st.track, b.progs[libProg], b.fm >= 0 ? { b: b.fm, p: libProg } : null);
  loadPart(); refreshKnobs(); buildParts(); buildTracks();
  say("libStatus", `${nameOf(b.progs[libProg]).trim()} on track ${st.track + 1}.`, "ok");
}
$("libUse").onclick = useLib;
$("libTrkDn").onclick = () => selectTrack((st.track + 7) % 8);
$("libTrkUp").onclick = () => selectTrack((st.track + 1) % 8);
async function saveLibs() { await Store.set("libs", libs.map((b) => ({ name: b.name, fm: b.fm, progs: b.progs.map((p) => p ? Array.from(p) : null) }))); }
$("readBank").onclick = async () => {
  const b = st.bank, got = [];
  try {
    for (let p = 0; p < 40; p++) { say("libStatus", `Reading the FM-1's bank ${b + 1}: ${p + 1} / 40…`); got.push((await readProgram(b, p)).prog); }
  } catch (e) { say("libStatus", "Not read: " + e.message, "bad"); return; }
  const i = libs.findIndex((l) => l.fm === b), entry = { name: `FM-1 bank ${b + 1}`, fm: b, progs: got };
  if (i >= 0) libs[i] = entry; else libs.unshift(entry);
  libSel = libs.indexOf(entry); libProg = 0;
  buildLib(); saveLibs(); say("libStatus", `Bank ${b + 1} read (change the bank on the Panel page).`, "ok");
};
$("openSyx").onclick = async () => {
  const f = await pickFile($("syxFile")); if (!f) return;
  const progs = [], d = f.bytes;
  for (let i = 0; i + 5 <= d.length; i++) {
    if (d[i] !== 0xF0 || d[i + 1] !== 0x0F || d[i + 2] !== 0x02 || (d[i + 4] !== 0x01 && d[i + 4] !== 0x02)) continue;
    const n = d[i + 4] === 0x01 ? 1 : 40;
    let p = i + 5;
    for (let k = 0; k < n && p + 204 <= d.length; k++, p += 204) progs.push(unnib(d, p));
    i = p - 1;
  }
  if (!progs.length) { say("libStatus", "No ESQ-1 / SQ-80 programs in that file.", "bad"); return; }
  for (let s = 0; s < progs.length; s += 40) libs.push({ name: f.name + (progs.length > 40 ? ` (${s / 40 + 1})` : ""), fm: -1, progs: progs.slice(s, s + 40) });
  libSel = libs.length - Math.ceil(progs.length / 40); libProg = 0;
  buildLib(); saveLibs(); say("libStatus", `${progs.length} programs from ${f.name}.`, "ok");
};
$("exportSyx").onclick = () => {
  const b = libs[libSel]; if (!b) return;
  const all = [];
  for (let i = 0; i < 40; i++) { let p = b.progs[i]; if (!p) { E.init_program(); p = new Uint8Array(eprog()); } all.push(...nib(p)); }
  loadPart();
  download(new Uint8Array([0xF0, 0x0F, 0x02, 0x00, 0x02, ...all, 0xF7]), b.name.replace(/[^A-Za-z0-9_+-]+/g, "_") + ".syx");
};

/* ================================================================== SETUP */
function toggle(id, key, after) {
  const b = $(id);
  const show = () => { b.setAttribute("aria-pressed", String(!!st[key])); b.textContent = st[key] ? "On" : "Off"; };
  b.onclick = () => { st[key] = !st[key]; show(); dirty(); if (after) after(); };
  return show;
}
const setupShows = [
  toggle("multiBtn", "multi", () => { if (link && link.multi) link.send(0x34, [st.multi ? 1 : 0, keysPart()], 800).then(() => { sent.fill(null); sendAllParts(); }).catch(() => {}); }),
  toggle("keysFollow", "keysFollow", () => { lastKeysPart = -1; followKeys(); }),
  toggle("liveBtn", "live", () => pumpParts()),
  toggle("clockBtn", "clock"),
  toggle("thruBtn", "thru"),
];
$("connBtn").onclick = () => { if (link) disconnect(); else connect(); };
$("monClear").onclick = () => { Mon.log = []; Mon.counts = { in: 0, out: 0, sxIn: 0, sxOut: 0 }; if (window.SQ80Android && SQ80Android.debugClear) SQ80Android.debugClear(); monShow(); };
$("monTest").onclick = async () => {
  if (!link) { toast("Connect the FM-1 first.", true); return; }
  Mon.log.push("— test: read the FM-1's program (frame 31) —");
  try { const r = await readProgram(0x7F, 0); toast(`Read "${nameOf(r.prog).trim()}" (bank ${r.b + 1} #${r.p + 1}): reading works.`); }
  catch (e) { toast("No answer: " + e.message + ". The monitor shows what came in.", true); }
  monShow();
};
$("outSel").onchange = () => { out = midi && midi.outputs.get($("outSel").value) || null; if (out) out.open(); dirty(); };
$("inSel").onchange = () => { const i = midi && midi.inputs.get($("inSel").value); if (i) { i.open(); listen(i); } else listen(null); dirty(); };
$("lookSel").onchange = () => { st.look = +$("lookSel").value; dirty(); };
$("tabVol").oninput = () => { st.vol = +$("tabVol").value; Tab.setVol(st.vol / 100); dirty(); };
$("packBtn").onclick = async () => {
  const f = await pickFile($("packFile")); if (!f) return;
  if (!usePackIn(E, f.bytes)) { toast("That is not a ROM pack.", true); return; }
  userPack = f; Tab.setPack(f.bytes);
  await Store.set("pack", { name: f.name, bytes: Array.from(f.bytes) });
  $("wavesStatus").textContent = f.name; refreshKnobs();
};
$("expProj").onclick = () => download(new TextEncoder().encode(JSON.stringify({ sq80tablet: 1, project })), "sq80-tablet-" + new Date().toISOString().slice(0, 10) + ".json", "application/json");
$("impProj").onclick = async () => {
  const f = await pickFile($("projFile")); if (!f) return;
  try {
    const o = JSON.parse(new TextDecoder().decode(f.bytes));
    if (!await confirmBox("Import", "Replace this tablet's sequences and songs with the file's?", "Replace")) return;
    project = S.loadProject(o.project || o); st.seq = 0; st.song = 0; ensureSeq(0); refreshSeqViews(); buildSong(); dirty(); toast("Imported.");
  } catch (e) { toast("Not a project file: " + e.message, true); }
};
$("newProj").onclick = async () => {
  if (!await confirmBox("New project", "Erase every sequence and song on this tablet? (Export first to keep them.)", "Erase all")) return;
  project = S.newProject(); st.seq = 0; st.song = 0; ensureSeq(0); refreshSeqViews(); buildSong(); dirty();
};

/* ================================================================== views, the loop */
function showView(v) {
  st.view = v;
  document.querySelectorAll("#nav button").forEach((b) => b.setAttribute("aria-selected", String(b.dataset.v === v)));
  for (const n of ["Panel", "Seq", "Song", "Roll", "Lib", "Setup"]) $("v" + n).hidden = n !== v;
  if (v === "Roll") {                         // (opened on the track's notes)
    const ks = curTrack().ev.filter(S.isNote).map((e) => e.k);
    if (ks.length && (Math.max(...ks) > roll.top || Math.min(...ks) < roll.top - 24)) roll.top = clamp(Math.max(...ks) + 6, 24, 127);
    buildRollTracks(); requestAnimationFrame(() => drawRoll());
  }
  if (v === "Song") buildSong();
  if (v === "Lib") buildLib();
  if (v === "Seq") buildTracks();
}
document.querySelectorAll("#nav button").forEach((b) => { b.onclick = () => showView(b.dataset.v); });
let lastFrameSong = -2;
function frame() {
  const playing = player && player.playing;
  if (playing) {
    lcd();
    const pos = player.positionAt(performance.now());
    if (pos && pos.pre <= 0) {
      if (st.view === "Seq") for (let t = 0; t < 8; t++) drawLane(t, pos.tick);
      if (st.view === "Roll") drawRoll(pos.tick);
      if (st.view === "Song") {
        const ph = $("songPh"), g = project.songs[st.song];
        if (ph && g && playMode === "song") {
          let total = 0, at = 0;
          g.steps.forEach((s, i) => { const q = project.seqs[s.s]; if (!q) return; const w = S.seqLength(q) * s.rep; if (i < pos.step) at += w; total += w; });
          const q = project.seqs[pos.s], rep = player.segs[player.si].rep;
          at += rep * S.seqLength(q) + pos.tick;
          ph.hidden = false; ph.style.left = (at / total * 100) + "%";
        }
        if (songNow !== lastFrameSong) { lastFrameSong = songNow; buildSong(); }
      }
    }
  }
  requestAnimationFrame(frame);
}
addEventListener("keydown", (e) => {
  if (/INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName) && document.activeElement.type !== "range") return;
  if (e.code === "Space") { e.preventDefault(); if (player && player.playing) stop(); else $("playBtn").click(); }
});
document.addEventListener("visibilitychange", () => { if (document.hidden) dirty(); });
window.__sq80back = () => {                   // (the tablet app's back key: a dialog closed, else the Panel)
  const ov = document.querySelector(".ov");
  if (ov) ov.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
  else showView("Panel");
};

/* ================================================================== start */
(async () => {
  await Store.open();
  try { await loadEngine(); } catch (e) { $("lcd").textContent = "NO ENGINE"; toast("The engine did not load: " + e.message, true); return; }
  const saved = await Store.get("project");
  if (saved) project = S.loadProject(saved);
  const ui = await Store.get("ui");
  if (ui) for (const k of Object.keys(ui)) if (k in st) st[k] = ui[k];
  st.seq = clamp(st.seq | 0, 0, S.NSEQ - 1); st.track = clamp(st.track | 0, 0, 7); if (!PAGES[st.page]) st.page = "OSC 1";
  const kept = await Store.get("libs");
  if (kept) for (const b of kept) libs.push({ name: b.name, fm: b.fm, progs: b.progs.map((p) => p ? Uint8Array.from(p) : null) });
  const pack = await Store.get("pack");
  if (pack && usePackIn(E, Uint8Array.from(pack.bytes))) { userPack = { name: pack.name, bytes: Uint8Array.from(pack.bytes) }; $("wavesStatus").textContent = pack.name; }
  Tab.setVol(st.vol / 100); $("tabVol").value = st.vol; $("lookSel").value = String(st.look);
  setupShows.forEach((f) => f());
  $("fmBank").textContent = st.bank + 1; $("fmProg").textContent = String(st.prog + 1).padStart(2, "0");
  $("about").textContent = `SQ-80 Tablet ${VERSION}${window.SQ80Android ? " (app)" : ""}. Sequences and songs stay on this tablet; Export makes a file of them.`;
  ensureSeq(st.seq);
  buildPages(); buildKnobs(); buildKbd(); keysUi();
  refreshSeqViews(); selectTrack(st.track);
  showView("Panel");
  setConn();
  requestAnimationFrame(frame);
  /* MIDI: straight away where it asks nobody (the app); otherwise on the first touch, as browsers want a gesture */
  if (window.SQ80Android) connect(true);
  else if (navigator.permissions) {
    try { const p = await navigator.permissions.query({ name: "midi", sysex: true }); if (p.state === "granted") connect(true); } catch (_) { /* (unknown) */ }
  }
  document.addEventListener("pointerdown", () => { Tab.start(); if (!link && !midi) connect(true); }, { once: true });
  if (window.SQ80Android) window.__sq80attached = () => { if (!link) connect(true); };
})();
