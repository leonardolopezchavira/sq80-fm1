// SPDX-License-Identifier: MIT
// The Backup page's banks section (app/backup_extras.js) against a simulated SQ-80 in JS: the loader's frames as
// app/app.c answers them (38 the counts, 31 a program, 20 / 21 / 22 a bank written), with its checks and status 7.
// A backup of 8 banks (bank 1 never loaded), a restore onto an empty unit byte for byte, a restore onto an SMK-37
// (2 banks), a damaged frame answered with 7 (sent again), a damaged section refused.
//   node app/tests/backup_extras_test.mjs
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const here = path.dirname(fileURLToPath(import.meta.url));
const EXTRAS = new Function(fs.readFileSync(path.join(here, "../backup_extras.js"), "utf8") + "\nreturn EXTRAS;")();
const ex = EXTRAS[0];
let fails = 0;
const ok = (c, what) => { if (!c) { fails++; console.log("FAIL " + what); } else console.log("  ok " + what); };

function device(nbank, nprog = 40) {
  const d = { nbank, nprog, banks: Array.from({ length: nbank }, () => ({ count: 0, progs: [] })), q: [], damage: 0 };
  const sumOk = (f) => { const s = f.slice(4, -3).reduce((a, b) => a + b, 0); return (s & 127) === f[f.length - 3] && ((s >> 7) & 127) === f[f.length - 2]; };
  const ack = (cmd, st, f) => d.q.push([0xF0, 0x7D, 0x53, 0x51, 0x7F, cmd, st, f[f.length - 3], f[f.length - 2], 0xF7]);
  d.link = {
    lost: false,
    drain() { d.q.length = 0; },
    send(f) {
      const cmd = f[4];
      if (d.damage && cmd === 0x21) { d.damage--; ack(cmd, 7, f); return true; }
      if (!sumOk(f)) { ack(cmd, 7, f); return true; }
      const b = f[5];
      if (cmd === 0x38) d.q.push([0xF0, 0x7D, 0x53, 0x51, 0x7E, 0x38, d.nbank, d.nprog, ...d.banks.map((x) => x.count), 0xF7]);
      else if (cmd === 0x31) {
        const pi = f[6], p = d.banks[b].progs[pi];
        const nb = []; for (const x of p) nb.push(x & 15, x >> 4);
        d.q.push([0xF0, 0x7D, 0x53, 0x51, 0x7E, 0x31, b, pi, 0, ...nb, 0xF7]);
      } else if (cmd === 0x20) { if (b >= d.nbank) ack(cmd, 1, f); else { d.banks[b] = { count: 0, progs: [] }; ack(cmd, 0, f); } }
      else if (cmd === 0x21) {
        const pi = f[6], nib = f.slice(7, 7 + 204), p = [];
        for (let i = 0; i < 102; i++) p.push(nib[2 * i] | nib[2 * i + 1] << 4);
        d.banks[b].progs[pi] = p; ack(cmd, 0, f);
      } else if (cmd === 0x22) { d.banks[b].count = f[6]; ack(cmd, 0, f); }
      return true;
    },
    async read() { return d.q.shift() || null; },
  };
  return d;
}
const prog = (b, i) => Array.from({ length: 102 }, (_, k) => (b * 31 + i * 7 + k * 13) & 255);

// a unit with banks 2, 3 and 5 loaded (40, 12 and 1 programs), bank 1 never loaded (the factory bank)
const a = device(8);
for (const [b, n] of [[1, 40], [2, 12], [4, 1]]) { a.banks[b].count = n; for (let i = 0; i < n; i++) a.banks[b].progs[i] = prog(b, i); }
let last = 0;
const data = await ex.backup(a.link, (f) => { last = f; });
ok(data.length === 2 + 8 + 53 * 102 && last === 1, `backup: ${data.length} bytes, progress to 1`);
ok(ex.check(data).includes("53 programs in 3 of 8 banks; bank 1 the factory bank"), "check: " + ex.check(data));

// onto an empty FM-1, two frames damaged on the way (sent again)
const b = device(8);
b.banks[0] = { count: 40, progs: Array.from({ length: 40 }, (_, i) => prog(9, i)) };   // its bank 1: cleared
b.damage = 2;
await ex.restore(b.link, data, () => {});
ok(JSON.stringify(b.banks.map((x) => x.count)) === JSON.stringify(a.banks.map((x) => x.count)), "restore: the counts");
ok(b.banks.every((x, i) => x.progs.slice(0, x.count).every((p, k) => JSON.stringify(p) === JSON.stringify(a.banks[i].progs[k]))),
   "restore: every program byte for byte");
const again = await ex.backup(b.link, () => {});
ok(Buffer.compare(Buffer.from(again), Buffer.from(data)) === 0, "a backup of the restored unit is the same file");

// onto an SMK-37: banks 1 and 2 only
const s = device(2);
await ex.restore(s.link, data, () => {});
ok(s.banks[0].count === 0 && s.banks[1].count === 40, "SMK-37: banks 1 and 2 restored, the rest left out");

// a damaged section
let threw = false;
try { ex.check(data.subarray(0, data.length - 5)); } catch (_) { threw = true; }
ok(threw, "a cut section refused");
threw = false;
try { await ex.restore(device(8).link, Uint8Array.from([8, 40, 41, 0, 0, 0, 0, 0, 0, 0]), () => {}); } catch (_) { threw = true; }
ok(threw, "a count over 40 refused");

if (fails) { console.log(`${fails} FAILED`); process.exit(1); }
console.log("backup_extras: all ok");
