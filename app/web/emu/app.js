// SPDX-License-Identifier: MIT
// SQ-80's own part of the browser emulator's page (fm1-base web/emu: app.js, with the parts "midi" and "data"):
// "Your ROM pack…" and "Your banks…" in the page's bar load the player's own files into the emulated FM-1, as the
// loader page does into a real one (app/app.c "the loader": the same frames, through the emulator's USB MIDI). The
// emulator keeps its flash in this browser (the part "data"), so they are there next time; nothing is uploaded.
// Without them it plays the built-in waves (tools/gen_waves.py) and INIT programs.
import { toDevice, fromDevice } from "./parts/midi.js";

const HEAD = [0x7D, 0x53, 0x51];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const u7 = (v, n) => Array.from({ length: n }, (_, i) => Math.floor(v / 2 ** (7 * i)) & 0x7F);
function pack7(d) {
  const o = [];
  for (let i = 0; i < d.length; i += 7) {
    const c = d.subarray(i, i + 7);
    let hi = 0;
    c.forEach((b, k) => { hi |= ((b >> 7) & 1) << k; });
    o.push(hi);
    c.forEach((b) => o.push(b & 0x7F));
  }
  return o;
}
const CRC_T = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
function crc32(d) { let c = 0xFFFFFFFF; for (const b of d) c = CRC_T[(c ^ b) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
function programsFrom(d) {   // ESQ-1 / SQ-80 dumps: F0 0F 02 ch 01 (one) or 02 (40), each program 204 nibbles
  const progs = [];
  for (let i = 0; i + 5 <= d.length; i++) {
    if (d[i] !== 0xF0 || d[i + 1] !== 0x0F || d[i + 2] !== 0x02 || (d[i + 4] !== 0x01 && d[i + 4] !== 0x02)) continue;
    const n = d[i + 4] === 0x01 ? 1 : 40;
    let p = i + 5;
    for (let k = 0; k < n && p + 204 <= d.length; k++, p += 204) {
      const prog = new Uint8Array(102);
      for (let j = 0; j < 102; j++) prog[j] = (d[p + 2 * j] & 15) | ((d[p + 2 * j + 1] & 15) << 4);
      progs.push(prog);
    }
    i = p - 1;
  }
  return progs.slice(0, 40);
}
const isPack = (b) => b.length > 32 && String.fromCharCode(...b.subarray(0, 8)) === "SQ80PACK";

// one frame and its answer (F0 7D 53 51 7F CMD STATUS E0 E1 ..), sent again when lost or damaged
let waiting = null;
fromDevice((d) => {
  const w = waiting;
  if (!w || d.length < 9 || d[0] !== 0xF0 || d[1] !== 0x7D || d[2] !== 0x53 || d[3] !== 0x51 || d[4] !== 0x7F || d[5] !== w.cmd) return;
  waiting = null;
  clearTimeout(w.timer);
  w.resolve(Array.from(d));
});
function once(cmd, body, ms) {
  return new Promise((resolve, reject) => {
    let frame = [cmd, ...body];
    if ((cmd >= 0x10 && cmd <= 0x12) || (cmd >= 0x20 && cmd <= 0x22)) {
      const s = frame.reduce((a, b) => a + b, 0);
      frame = [...frame, s & 0x7F, (s >> 7) & 0x7F];
    }
    waiting = { cmd, resolve, timer: setTimeout(() => { waiting = null; reject(new Error("no answer")); }, ms) };
    toDevice([0xF0, ...HEAD, ...frame, 0xF7]);
  });
}
async function send(cmd, body = [], ms = 3000) {
  let last = null;
  for (let t = 0; t < 4; t++) {
    try {
      const a = await once(cmd, body, ms);
      if (a[6] === 7) { last = new Error("a damaged frame"); continue; }
      if (cmd >= 0x10 && a[6] !== 0) throw new Error("refused (" + a[6] + ")");
      return a;
    } catch (e) { last = e; await sleep(100); }
  }
  throw last;
}

let busy = false;
async function load(files, say) {
  if (busy) return;
  busy = true;
  try {
    await send(0x01, [], 2000).catch(() => { throw new Error("switch the emulator on first"); });
    const list = [...files].sort((a, b) => a.name.localeCompare(b.name));
    let bank = 0;
    for (const f of list) {
      const b = new Uint8Array(await f.arrayBuffer());
      if (isPack(b)) {
        say(`Loading the ROM pack ${f.name}…`);
        await send(0x10, u7(b.length, 4), 8000);
        for (let off = 0; off < b.length; off += 224) {
          await send(0x11, [...u7(off, 4), ...pack7(b.subarray(off, off + 224))]);
          if ((off / 224) % 64 === 0) say(`Loading the ROM pack… ${Math.round(100 * off / b.length)} %`);
        }
        await send(0x12, u7(crc32(b), 5), 15000);
        say(`ROM pack ${f.name} loaded: the emulator plays your waves.`);
        continue;
      }
      const progs = programsFrom(b);
      if (!progs.length || bank >= 8) continue;
      say(`Loading ${f.name} into bank ${bank + 1}…`);
      await send(0x20, [bank]);
      for (let i = 0; i < progs.length; i++) {
        const nib = [];
        progs[i].forEach((x) => nib.push(x & 15, x >> 4));
        await send(0x21, [bank, i, ...nib]);
      }
      await send(0x22, [bank, progs.length]);
      bank++;
    }
    say(bank ? `Loaded: banks 1–${bank}. SELECT picks the bank, PRESET the program.` : "Loaded.");
  } catch (e) {
    say("Not loaded: " + (e && e.message || e));
  } finally {
    busy = false;
  }
}

export function attach(ctx) {
  const bar = document.querySelector(".bar");
  if (!bar) return;
  const say = (s) => { const st = document.getElementById("status"); if (st) st.textContent = s; else if (ctx.status) ctx.status(s); };
  const input = document.createElement("input");
  input.type = "file";
  input.multiple = true;
  input.accept = ".rompack,.syx,.SYX";
  input.hidden = true;
  input.addEventListener("change", () => { if (input.files.length) load(input.files, say); input.value = ""; });
  const btn = document.createElement("button");
  btn.type = "button";
  btn.textContent = "Your ROM pack and banks…";
  btn.title = "Load your own SQ-80 ROM pack (.rompack) and ESQ-1 / SQ-80 banks (.syx): they stay in this browser";
  btn.addEventListener("click", () => input.click());
  bar.append(btn, input);
}
