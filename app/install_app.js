/* SPDX-License-Identifier: MIT
 * SQ-80's own steps in fm1-base's installer (fm1.toml install_app; web/installer.html APP): the player's ROM pack
 * and program banks, loaded in the same go as the firmware. before() asks for the files (or offers the ones this
 * browser kept from last time: kept in this browser only, never sent anywhere but the FM-1); after() waits for SQ-80
 * to start, then loads the ROM pack (unless the FM-1 already holds the same one) and the banks.
 * The protocol is app/app.c's ("the loader"), as app/loader/index.html and tools/fm1_loader.py speak it. */
(() => {
  const HEAD = [0x7D, 0x53, 0x51];
  const KEEP_DB = "sq80-setup", KEEP_STORE = "files", KEEP_KEY = "last";
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;"}[c]));

  /* ---------------------------------------------------------------- kept files (IndexedDB, this browser only) */
  function db() {
    return new Promise((resolve, reject) => {
      const r = indexedDB.open(KEEP_DB, 1);
      r.onupgradeneeded = () => r.result.createObjectStore(KEEP_STORE);
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
  }
  async function kept() {
    try {
      const d = await db();
      return await new Promise((resolve) => {
        const q = d.transaction(KEEP_STORE).objectStore(KEEP_STORE).get(KEEP_KEY);
        q.onsuccess = () => resolve(q.result || null);
        q.onerror = () => resolve(null);
      });
    } catch (_) { return null; }
  }
  async function keep(v) {
    try {
      const d = await db();
      await new Promise((resolve) => {
        const tx = d.transaction(KEEP_STORE, "readwrite");
        tx.objectStore(KEEP_STORE).put(v, KEEP_KEY);
        tx.oncomplete = tx.onerror = () => resolve();
      });
    } catch (_) { /* not kept: asked again next time */ }
  }

  /* ---------------------------------------------------------------- the files */
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
  async function readFiles(list) {   // -> {pack: {name, bytes}, banks: [{name, bytes}]}, by name order
    const files = [...list].sort((a, b) => a.name.localeCompare(b.name));
    let pack = null;
    const banks = [];
    for (const f of files) {
      const b = new Uint8Array(await f.arrayBuffer());
      if (isPack(b)) pack = {name: f.name, bytes: b};
      else if (programsFrom(b).length && banks.length < 8) banks.push({name: f.name, bytes: b});
    }
    return {pack, banks};
  }
  const describe = (s) => [
    s.pack ? `ROM pack: <b>${esc(s.pack.name)}</b>` : "no ROM pack (the FM-1 keeps the one it has)",
    ...s.banks.map((b, i) => `bank ${i + 1}: <b>${esc(b.name)}</b> (${programsFrom(b.bytes).length} programs)`),
  ].join("<br>");

  const PICK = `<p>Optional: SQ-80 has its own waves. If you have one, choose your ROM pack (<code>.rompack</code>,
    for the original waves) and up to eight ESQ-1 / SQ-80 SysEx banks
    (<code>.syx</code>) together: hold ⌘ (Mac) or Ctrl to pick several. Banks go into banks 1–8 in name order.</p>
    <p><input type="file" id="sq80files" multiple accept=".rompack,.syx,.SYX"></p>
    <p class="small">They stay on this computer, in this browser, so the next install can load them again with one
    click. Nothing is uploaded anywhere but to your FM-1.</p>`;

  async function before(midi, ui) {
    let saved = await kept();
    for (;;) {
      if (saved && (saved.pack || saved.banks.length)) {
        const c = await ui.ask("Your SQ-80 waves and programs",
          `<p>Load these into the FM-1 after the firmware, as last time?</p><p>${describe(saved)}</p>`,
          [["saved", "Load them too"], ["change", "Other files"], ["skip", "Firmware only"]]);
        if (c === "saved") return saved;
        if (c === "skip") return null;
        saved = null;
        continue;
      }
      const c = await ui.ask("Your SQ-80 waves and programs", PICK, [["files", "Use these"], ["skip", "Firmware only"]]);
      if (c !== "files") return null;
      const el = document.getElementById("sq80files");
      const got = await readFiles(el && el.files ? el.files : []);
      if (!got.pack && !got.banks.length) {
        ui.say("Those were not a ROM pack or ESQ-1 / SQ-80 banks. Choose again.");
        continue;
      }
      await keep(got);
      return got;
    }
  }

  /* ---------------------------------------------------------------- the protocol (app/app.c "the loader") */
  function u7(v, n) { const a = []; for (let i = 0; i < n; i++) a.push(Math.floor(v / 2 ** (7 * i)) & 0x7F); return a; }
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
  const CRC_T = (() => { const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
    return t; })();
  function crc32(d) { let c = 0xFFFFFFFF; for (const b of d) c = CRC_T[(c ^ b) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
  const STATUS = {1: "the FM-1 did not understand a frame", 2: "the FM-1's flash refused a write",
                  3: "the check after loading failed", 4: "the FM-1 lost the load",
                  5: "a byte read back from the flash differs", 6: "the FM-1 did not accept the ROM pack",
                  7: "frames kept arriving damaged"};
  const isOurs = (d) => d.length >= 9 && d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x53 && d[3] === 0x51;

  function outputFor(m, input) {
    const outs = [...m.outputs.values()].filter((o) => o.state !== "disconnected");
    const bare = (s) => (s || "").replace(/\s+\d+$/, "");
    const same = outs.filter((o) => bare(o.name) === bare(input.name));
    return outs.find((o) => o.name === input.name) || (same.length === 1 ? same[0] : null) ||
      (outs.length === 1 ? outs[0] : null);
  }

  class Link {
    constructor(input, output) {
      this.i = input; this.o = output; this.waiting = null;
      this.i.onmidimessage = (e) => this.take(e.data);
    }
    close() { this.i.onmidimessage = null; }
    take(d) {
      const w = this.waiting;
      if (!w || !isOurs(d) || d[4] !== 0x7F || d[5] !== w.cmd) return;
      if (w.echo && d[6] !== 7 && (d[7] !== w.echo[0] || d[8] !== w.echo[1])) return;   // a late answer to another
      this.waiting = null; clearTimeout(w.timer); w.resolve(Array.from(d));
    }
    once(cmd, body, ms) {
      return new Promise((resolve, reject) => {
        let frame = [cmd, ...body], echo = null;
        if ((cmd >= 0x10 && cmd <= 0x12) || (cmd >= 0x20 && cmd <= 0x22)) {
          const s = frame.reduce((a, b) => a + b, 0);
          echo = [s & 0x7F, (s >> 7) & 0x7F];
          frame = [...frame, ...echo];
        }
        this.waiting = {cmd, echo, resolve, timer: setTimeout(() => { this.waiting = null; reject(new Error("no answer from the FM-1")); }, ms)};
        this.o.send([0xF0, ...HEAD, ...frame, 0xF7]);
      });
    }
    async send(cmd, body = [], ms = 4000) {   // a frame lost or damaged goes again; a refusal is an error
      let a = null, last = null;
      for (let tries = 0; tries < 6 && !a; tries++) {
        try {
          a = await this.once(cmd, body, tries ? Math.max(ms, 1500) : ms);
          if (a[6] === 7) { last = new Error(STATUS[7]); a = null; }
        } catch (err) { last = err; await sleep(200); }
      }
      if (!a) throw last;
      if (cmd >= 0x10 && a[6] !== 0) throw new Error(STATUS[a[6]] || "error " + a[6]);
      return a;
    }
  }

  async function findSq80(midi, ms) {   // every port pair asked for this app's hello until one answers
    const end = Date.now() + ms;
    while (Date.now() < end) {
      for (const i of [...midi.inputs.values()].filter((p) => p.state !== "disconnected")) {
        const o = outputFor(midi, i);
        if (!o || /bluetooth/i.test(i.name || "")) continue;
        try { await i.open(); await o.open(); } catch (_) { continue; }
        const link = new Link(i, o);
        try { await link.once(0x01, [], 700); return link; } catch (_) { link.close(); }
      }
      await sleep(1000);
    }
    return null;
  }

  async function after(midi, ui, state) {
    if (!state || (!state.pack && !state.banks.length)) return;
    ui.say("Waiting for SQ-80 to start…");
    const link = await findSq80(midi, 60000);
    if (!link) throw new Error("SQ-80 did not answer after the install: load your files with the loader page (../load/)");
    try {
      if (state.pack) {
        const pack = state.pack.bytes, crc = crc32(pack);
        const a = await link.send(0x02, [], 5000);
        const num = (o, n) => a.slice(o, o + n).reduce((v, b, k) => v + b * 2 ** (7 * k), 0);
        if (a.length >= 19 && num(9, 4) === pack.length && num(13, 5) === crc) {
          ui.say("The FM-1 already holds this ROM pack.");
        } else {
          ui.say("Loading the ROM pack… (the FM-1 is silent meanwhile)");
          await link.send(0x10, u7(pack.length, 4), 8000);
          for (let off = 0; off < pack.length; off += 224) {
            await link.send(0x11, [...u7(off, 4), ...pack7(pack.subarray(off, off + 224))]);
            ui.progress(100 * off / pack.length);
          }
          await link.send(0x12, u7(crc, 5), 15000);
          ui.progress(100);
        }
      }
      for (let b = 0; b < state.banks.length; b++) {
        const progs = programsFrom(state.banks[b].bytes);
        ui.say(`Loading bank ${b + 1}: ${state.banks[b].name}…`);
        await link.send(0x20, [b]);
        for (let i = 0; i < progs.length; i++) {
          const nib = [];
          progs[i].forEach((x) => nib.push(x & 15, x >> 4));
          await link.send(0x21, [b, i, ...nib]);
          ui.progress(100 * (i + 1) / progs.length);
        }
        await link.send(0x22, [b, progs.length]);
      }
      ui.say("SQ-80 is installed with your waves and programs. SELECT picks the bank, PRESET the program.");
    } finally { link.close(); }
  }

  return {before, after};
})()
