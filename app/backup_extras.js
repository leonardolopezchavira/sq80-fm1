/* SPDX-License-Identifier: MIT
 * SQ-80's own section of a backup (fm1-base web/backup/index.html EXTRAS; tools/make_backup_page.py puts it in):
 * the banks. fm1-base's backup holds SQ-80's store object (the settings, with the pattern); the banks live in flash
 * sectors of their own, so the page reads and writes them over the loader's frames (app/app.c):
 *   38                 -> 7E 38 NBANK NPROG COUNT x NBANK   the programs stored in each bank (0: never loaded)
 *   31 B P             -> 7E 31 B P E NIBBLES(204)          a program
 *   20 B               -> 7F 20 status                      a bank cleared (bank 1: the factory bank again)
 *   21 B P NIBBLES     -> 7F 21 status                      a program into a bank
 *   22 B COUNT         -> 7F 22 status                      the bank's count: it is loaded
 * each frame with its 14-bit check, status 7: damaged on the way, sent again.
 * The section's bytes: NBANK, NPROG, COUNT x NBANK, then the stored programs (102 bytes each) bank by bank. */
const EXTRAS = [(() => {
  const HEAD = [0xF0, 0x7D, 0x53, 0x51];
  const isOurs = (p, kind, cmd) => p && p.length > 6 && p[0] === 0xF0 && p[1] === 0x7D && p[2] === 0x53 &&
    p[3] === 0x51 && p[4] === kind && p[5] === cmd;
  const STATUS = { 1: "SQ-80 did not understand a frame", 2: "SQ-80's flash refused a write",
                   7: "frames kept arriving damaged" };
  function framed(cmd, body) {
    const f = [cmd, ...body], s = f.reduce((a, b) => a + b, 0);
    return { bytes: [...HEAD, ...f, s & 0x7F, (s >> 7) & 0x7F, 0xF7], echo: [s & 0x7F, (s >> 7) & 0x7F] };
  }
  // send a frame and wait for its answer: kind 0x7E (data) or 0x7F (a status); tries again if lost or damaged
  async function call(link, cmd, body, kind, ms = 4000) {
    let last = null;
    for (let tries = 0; tries < 6; tries++) {
      const { bytes, echo } = framed(cmd, body);
      link.drain();
      if (!link.send(bytes)) throw new Error("the USB connection was lost.");
      const end = Date.now() + (tries ? Math.max(ms, 1500) : ms);
      let got = null;
      while (Date.now() < end) {
        const p = await link.read(end - Date.now());
        if (!p) break;
        if (isOurs(p, 0x7F, cmd) && p[6] === 7) { got = "again"; break; }       // damaged on the way
        if (kind === 0x7F && isOurs(p, 0x7F, cmd) && p[7] === echo[0] && p[8] === echo[1]) { got = p; break; }
        if (kind === 0x7E && isOurs(p, 0x7E, cmd)) { got = p; break; }
        if (kind === 0x7E && isOurs(p, 0x7F, cmd) && p[6]) throw new Error(STATUS[p[6]] || `error ${p[6]}`);
      }
      if (got && got !== "again") {
        if (kind === 0x7F && got[6] !== 0) throw new Error(`${STATUS[got[6]] || "error " + got[6]}.`);
        return got;
      }
      last = got === "again" ? STATUS[7] : "SQ-80 stopped answering";
      if (link.lost) throw new Error("the USB connection was lost.");
      await new Promise((r) => setTimeout(r, 200));
    }
    throw new Error(`${last}.`);
  }
  const nib = (bytes) => { const o = []; for (const b of bytes) o.push(b & 15, b >> 4); return o; };
  async function counts(link) {
    const p = await call(link, 0x38, [], 0x7E);
    const nbank = p[6], nprog = p[7];
    return { nbank, nprog, counts: p.slice(8, 8 + nbank) };
  }
  function parse(data) {
    if (data.length < 2) throw new Error("its banks are cut short");
    const nbank = data[0], nprog = data[1];
    if (!nbank || nbank > 16 || !nprog || nprog > 64 || data.length < 2 + nbank) throw new Error("its banks are damaged");
    const c = Array.from(data.subarray(2, 2 + nbank));
    if (c.some((n) => n > nprog)) throw new Error("its banks are damaged");
    const total = c.reduce((a, b) => a + b, 0);
    if (data.length !== 2 + nbank + total * 102) throw new Error("its banks are cut short");
    return { nbank, nprog, counts: c, total };
  }
  return {
    tag: "BNKS",
    what: "the banks",
    async backup(link, onProgress) {
      const { nbank, nprog, counts: c } = await counts(link);
      const total = c.reduce((a, b) => a + b, 0), out = [nbank, nprog, ...c];
      let done = 0;
      onProgress(0);
      for (let b = 0; b < nbank; b++)
        for (let pi = 0; pi < c[b]; pi++) {
          const p = await call(link, 0x31, [b, pi], 0x7E);
          if (p.length < 9 + 204 + 1 || p[6] !== b || p[7] !== pi) throw new Error("a program came back garbled.");
          for (let i = 0; i < 102; i++) out.push((p[9 + 2 * i] & 15) | (p[10 + 2 * i] & 15) << 4);
          onProgress(++done / Math.max(total, 1));
        }
      onProgress(1);
      return Uint8Array.from(out);
    },
    check(data) {
      const { nbank, counts: c, total } = parse(data);
      const used = c.filter((n) => n).length;
      return `the banks (${total} programs in ${used} of ${nbank} banks${c[0] ? "" : "; bank 1 the factory bank"})`;
    },
    async restore(link, data, onProgress) {
      const f = parse(data), dev = await counts(link);
      if (f.nprog !== dev.nprog) throw new Error(`these banks hold ${f.nprog} programs each, SQ-80's ${dev.nprog}.`);
      const n = Math.min(f.nbank, dev.nbank), total = f.counts.slice(0, n).reduce((a, b) => a + b, 0) + n;
      let at = 2 + f.nbank, done = 0;
      onProgress(0);
      for (let b = 0; b < f.nbank; b++) {
        const cnt = f.counts[b];
        if (b >= n) break;                        // (an FM-1's banks 3..8 on an SMK-37: it has two)
        await call(link, 0x20, [b], 0x7F, 8000);  // cleared: never loaded (bank 1: the factory bank again)
        onProgress(++done / total);
        for (let pi = 0; pi < cnt; pi++) {
          await call(link, 0x21, [b, pi, ...nib(data.subarray(at, at + 102))], 0x7F);
          at += 102;
          onProgress(++done / total);
        }
        if (cnt) await call(link, 0x22, [b, cnt], 0x7F);
      }
      onProgress(1);                              // (an FM-1's banks 3..8 on an SMK-37: left out, as the page says)
    },
  };
})()];
