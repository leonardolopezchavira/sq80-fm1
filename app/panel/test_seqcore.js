// SPDX-License-Identifier: MIT
// The tablet sequencer's model and clock (seqcore.js), without a page:  node app/panel/test_seqcore.js
"use strict";
const S = require("./seqcore.js");
let fails = 0;
const check = (ok, msg) => { if (!ok) { fails++; console.log("FAIL " + msg); } else console.log("  ok " + msg); };

// a project: sequence 0, 1 bar of 4/4 at 120: track 0 quarter notes C D E F, track 1 one long note, muted track 2
const p = S.newProject();
const q = p.seqs[0] = S.newSequence("TEST");
q.bars = 1; q.tempo = 120;
[60, 62, 64, 65].forEach((k, i) => q.tracks[0].ev.push({ t: i * 96, k, v: 100, d: 48 }));
q.tracks[1].ev.push({ t: 0, k: 48, v: 80, d: 384 }); q.tracks[1].ch = 1; q.tracks[1].mix = 50; q.tracks[1].status = "TABLET";
q.tracks[2].ev.push({ t: 0, k: 72, v: 100, d: 10 }); q.tracks[2].mute = true;
q.tracks[0].ev.push({ t: 200, c: 1, x: 64 });
S.sortEvents(q.tracks[0].ev);

// play it once (loop off) with the clock on: collect what the player hands out
function run(project, mode, index, opts, ms, input) {
  const out = [];
  const pl = new S.Player(project, (e) => out.push(e));
  Object.assign(pl, opts || {});
  if (opts && opts.arm) pl.armRecord(opts.arm);
  pl.start(mode, index, 0, opts && opts.bar);
  for (let t = 0; t <= ms; t += 25) {
    if (input) for (const [at, msg] of input) if (at >= t - 25 && at < t) pl.input(msg, at);
    pl.pump(t);
  }
  return { out, pl };
}
q.loop = false;
let { out } = run(p, "seq", 0, { clockOn: true }, 3000);
const ons = out.filter((e) => e.type === "on");
check(ons.length === 5, `5 notes (muted track skipped): ${ons.length}`);
const t0 = ons.find((e) => e.k === 60).time, t1 = ons.find((e) => e.k === 62).time;
check(Math.abs(t1 - t0 - 500) < 0.01, `a quarter at 120 BPM is 500 ms: ${(t1 - t0).toFixed(2)}`);
const off60 = out.find((e) => e.type === "off" && e.k === 60);
check(Math.abs(off60.time - t0 - 250) < 1, `an eighth's note lasts 250 ms: ${(off60.time - t0).toFixed(1)}`);
const long = ons.find((e) => e.k === 48);
check(long.vm === Math.round(80 * 50 / 99) && long.status === "TABLET" && long.ch === 1, "MIX scales velocity; status and channel");
check(out.filter((e) => e.type === "clock").length === 96, `96 clocks in a 4/4 bar: ${out.filter((e) => e.type === "clock").length}`);
check(out.some((e) => e.type === "end"), "ends (loop off)");
check(out.filter((e) => e.type === "cc").length === 1, "the controller");
check(out.findIndex((e) => e.type === "seq") === 0 || out.find((e) => e.type === "start"), "a segment start");
let last = -Infinity, ordered = true;
for (const e of out.filter((x) => x.type === "on")) { ordered = ordered && e.time >= last; last = e.time; }
check(ordered, "notes in time order");

// looping: two passes
q.loop = true;
({ out } = run(p, "seq", 0, {}, 4100));
check(out.filter((e) => e.type === "on" && e.k === 60).length === 3, `looped: C at 0, 2, 4 s: ${out.filter((e) => e.type === "on" && e.k === 60).length}`);

// a song: seq 0 twice, then seq 1 transposed +12 once
const s1 = p.seqs[1] = S.newSequence("TWO"); s1.bars = 1; s1.tempo = 60;
s1.tracks[0].ev.push({ t: 0, k: 51, v: 100, d: 96 });
p.songs[0] = S.newSong("SONG"); p.songs[0].steps = [{ s: 0, rep: 2, tr: 0 }, { s: 1, rep: 1, tr: 12 }];
({ out } = run(p, "song", 0, {}, 9000));
const segs = out.filter((e) => e.type === "seq");
check(segs.length === 3 && segs[2].s === 1 && segs[2].tr === 12, `song segments ${segs.map((e) => e.s + "/" + e.tr).join(" ")}`);
const n50 = out.find((e) => e.type === "on" && e.k === 62 + 0 && e.tr === 0);
const k62 = out.filter((e) => e.type === "on" && e.k === 62).length, k50 = out.find((e) => e.type === "on" && e.k === 51 + 12);
check(k62 === 2 && k50 && Math.abs(k50.time - 4030) < 1, `song: seq 0 twice then seq 1 +12 at 4 s (${k50 && k50.time.toFixed(1)})`);
check(out.some((e) => e.type === "end"), "song ends");
void n50;

// recording: replace into track 3 with 1/16 auto-correct, count-off 1 bar; the notes go in at the grid
q.loop = true;
q.tracks[3].ev.push({ t: 0, k: 30, v: 1, d: 1 });   // (replaced)
const beat = 500, start = 30 + 4 * beat;            // the take starts after the count-off bar
const input = [[start + 3, [0x90, 70, 90]], [start + 240, [0x80, 70, 0]], [start + 760, [0x90, 72, 90]], [start + 1100, [0x80, 72, 0]],
               [start + 900, [0xB0, 1, 99]], [100, [0x90, 99, 99]], [200, [0x80, 99, 0]]];
let r = run(p, "seq", 0, { countoff: 1, clickOn: true, arm: { s: 0, track: 3, mode: "REPLACE", grid: 24 } }, start + 1400, input);
r.pl.stop(start + 1400);
const ev3 = q.tracks[3].ev.filter(S.isNote);
check(ev3.length === 2 && ev3[0].k === 70 && ev3[0].t === 0 && ev3[1].k === 72 && ev3[1].t === 144,
      `recorded at the grid: ${JSON.stringify(ev3.map((e) => [e.k, e.t, e.d]))}`);
check(Math.abs(ev3[0].d - 237 / 500 * 96) < 1.5, `length kept: ${ev3[0].d}`);
check(q.tracks[3].ev.some((e) => e.c === 1 && e.x === 99), "a controller recorded");
check(r.out.filter((e) => e.type === "click" && e.pre).length === 4, "count-off: 4 clicks");
check(!q.tracks[3].ev.some((e) => e.k === 99) && !q.tracks[3].ev.some((e) => e.k === 30), "count-off notes ignored; the old take replaced");

// a held note at stop is closed
q.tracks[4].ev = [];
r = run(p, "seq", 0, { arm: { s: 0, track: 4, mode: "ADD", grid: 1 } }, 600, [[100, [0x90, 40, 100]]]);
r.pl.stop(600);
check(q.tracks[4].ev.length === 1 && q.tracks[4].ev[0].d > 40, `held note closed at stop: ${JSON.stringify(q.tracks[4].ev)}`);

// step entry: a chord, a rest, a note, a tie, back
const st = p.seqs[2] = S.newSequence("STEP");
const pl = new S.Player(p, () => {});
pl.stepStart(2, 0, 24, 50);
pl.input([0x90, 60, 100], 0); pl.input([0x90, 64, 100], 0); pl.input([0x80, 60, 0], 0); pl.input([0x80, 64, 0], 0);
pl.stepRest();
pl.input([0x90, 67, 90], 0); pl.input([0x80, 67, 0], 0);
pl.stepTie();
pl.input([0x90, 69, 90], 0); pl.input([0x80, 69, 0], 0);
pl.stepBack();
const se = st.tracks[0].ev;
check(se.length === 3 && se[0].t === 0 && se[1].t === 0 && se[0].d === 12 && se[2].k === 67 && se[2].t === 48 && se[2].d === 36 &&
      pl.step.cursor === 96, `step entry ${JSON.stringify(se.map((e) => [e.k, e.t, e.d]))} cursor ${pl.step.cursor}`);

// edit functions
const e = S.newSequence("ED"); e.bars = 4;
e.tracks[0].ev = [{ t: 5, k: 60, v: 100, d: 10 }, { t: 100, k: 62, v: 100, d: 10 }, { t: 400, c: 7, x: 1 }, { t: 700, k: 64, v: 100, d: 200 }];
S.Edit.quantize(e, e.tracks[0], 24, 100);
check(e.tracks[0].ev.filter(S.isNote).map((x) => x.t).join() === "0,96,696", "quantize");
S.Edit.transpose(e.tracks[0], 12);
check(e.tracks[0].ev[0].k === 72, "transpose");
S.Edit.insertBars(e, 1, 2);
check(e.bars === 6 && e.tracks[0].ev.find((x) => x.k === 76).t === 696 + 768, "insert bars");
S.Edit.deleteBars(e, 1, 2);
check(e.bars === 4 && e.tracks[0].ev.find((x) => x.k === 76).t === 696, "delete bars");
S.Edit.setLength(e, 2);
check(e.tracks[0].ev.find((x) => x.k === 76).d === 72, "shorter: a note cut at the end");
S.Edit.removeControllers(e.tracks[0]);
check(e.tracks[0].ev.every(S.isNote), "remove controllers");
S.Edit.copy(e.tracks[0], e.tracks[1]); S.Edit.merge(e.tracks[0], e.tracks[1]);
check(e.tracks[1].ev.length === 6, "copy, merge");
const a2 = S.newSequence("A"); a2.bars = 1; a2.tracks[0].ev = [{ t: 0, k: 1, v: 1, d: 1 }];
S.Edit.append(a2, e);
check(a2.bars === 3 && a2.tracks[0].ev.length === 4 && a2.tracks[0].ev[1].t === 384, "append");

// MIDI file out and back in
const smf = S.toSMF(p, "seq", 0);
check(String.fromCharCode(...smf.slice(0, 4)) === "MThd" && smf[9] === 1, "SMF type 1");
const back = S.fromSMF(smf, "BACK");
const t0n = back.tracks.find((t) => t.ev.some((x) => x.k === 60));
check(t0n && t0n.ev.filter(S.isNote).length === 4 && back.tempo === 120 && back.bars === 1, "SMF read back");

// JSON round trip
const json = JSON.parse(JSON.stringify(p)), again = S.loadProject(json);
check(again.seqs[0].tracks[1].status === "TABLET" && again.songs[0].steps.length === 2 && again.seqs[0].tracks[0].ev.length === 5, "project JSON round trip");
check(S.loadProject({ seqs: [{ bars: -4, tracks: [{ ch: 99, ev: [{ t: -1 }, null] }] }] }).seqs[0].bars === 1, "a damaged project made whole");

console.log(fails ? `${fails} FAILED` : "all tests passed");
process.exit(fails ? 1 : 0);
