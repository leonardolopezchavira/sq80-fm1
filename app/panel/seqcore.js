// SPDX-License-Identifier: MIT
/* SQ-80 for the FM-1: the tablet's sequencer, after the SQ-80's own (Musician's Manual, "The Sequencer"):
 * sequences of 8 tracks (a program, a MIDI channel, a MIX level and a status each), chained into songs of up to 99
 * steps (a sequence, its repeats, a transposition). This file is the model and the clock only, no page: the page
 * (index.html) draws it and sends what it plays to the FM-1, to other MIDI gear or to the tablet's own engine.
 *
 * Time: 96 ticks to the quarter note. A note is {t, k, v, d} (start tick, key, velocity, length in ticks), a
 * controller {t, c, x}, a bend {t, b} (0..16383), channel pressure {t, a}. A track's events are kept sorted by t.
 * The player is driven by the page's timer (pump, every ~25 ms): it hands out the events of the next stretch with
 * their times (performance.now() milliseconds), early enough for MIDI's timestamped sends. */
(function (root) {
  "use strict";
  const PPQ = 96;
  const NSEQ = 60, NSONG = 20, NTRACK = 8, MAXSTEP = 99;
  /* AUTO-CORRECT (quantize): the SQ-80's values, as ticks */
  const GRIDS = [["OFF", 1], ["1/4", 96], ["1/8", 48], ["1/8T", 32], ["1/16", 24], ["1/16T", 16], ["1/32", 12],
                 ["1/32T", 8], ["1/64", 6]];
  const STATUS = ["MIDI", "TABLET", "BOTH"];   /* the SQ-80's MIDI / LOCAL / BOTH: here LOCAL is the tablet */

  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const isNote = (e) => e.k !== undefined;

  /* ----------------------------------------------------------------------------------------- the model --- */
  function newTrack(i) {
    return { prog: null, ch: i, mix: 99, status: "MIDI", mute: false, ev: [] };   /* prog: progFrom's, or null */
  }
  function newSequence(name) {
    const tracks = [];
    for (let i = 0; i < NTRACK; i++) tracks.push(newTrack(i));
    return { name: (name || "SEQ").toUpperCase().slice(0, 6), bars: 4, num: 4, den: 4, tempo: 120, loop: true, tracks };
  }
  function newSong(name) { return { name: (name || "SONG").toUpperCase().slice(0, 6), steps: [], loop: false }; }
  function newProject() {
    return { v: 1, seqs: new Array(NSEQ).fill(null), songs: new Array(NSONG).fill(null) };
  }
  const barTicks = (s) => s.num * PPQ * 4 / s.den;
  const seqLength = (s) => s.bars * barTicks(s);

  function sortEvents(ev) { ev.sort((a, b) => a.t - b.t || (isNote(a) ? 1 : 0) - (isNote(b) ? 1 : 0)); return ev; }
  function insertEvent(ev, e) {             /* in order (after any at the same tick) */
    let lo = 0, hi = ev.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (ev[m].t <= e.t) lo = m + 1; else hi = m; }
    ev.splice(lo, 0, e);
  }
  function firstAt(ev, t) {                  /* the index of the first event at or after tick t */
    let lo = 0, hi = ev.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (ev[m].t < t) lo = m + 1; else hi = m; }
    return lo;
  }
  const clone = (x) => JSON.parse(JSON.stringify(x));

  /* a track's program: {name, data (its 102 bytes, as on the SQ-80: the track keeps the sound itself), b, p (where
   * it came from: bank 0..7, place 0..39; sent as a program change to gear that is not an SQ-80)} */
  function progFrom(o) {
    if (!o || !Array.isArray(o.data) || o.data.length !== 102) return null;
    const r = { name: String(o.name || "").slice(0, 6), data: o.data.map((x) => x & 255) };
    if (Number.isInteger(o.b) && Number.isInteger(o.p)) { r.b = clamp(o.b, 0, 127); r.p = clamp(o.p, 0, 127); }
    return r;
  }
  /* a project from JSON (an older or damaged one made whole) */
  function loadProject(o) {
    const p = newProject();
    if (!o || typeof o !== "object") return p;
    (o.seqs || []).slice(0, NSEQ).forEach((s, i) => {
      if (!s) return;
      const q = newSequence(s.name);
      q.bars = clamp(s.bars | 0 || 4, 1, 999); q.num = clamp(s.num | 0 || 4, 1, 16);
      q.den = [2, 4, 8, 16].includes(s.den) ? s.den : 4; q.tempo = clamp(+s.tempo || 120, 20, 300);
      q.loop = s.loop !== false;
      (s.tracks || []).slice(0, NTRACK).forEach((t, j) => {
        const r = q.tracks[j];
        r.prog = progFrom(t.prog);
        r.ch = clamp(t.ch | 0, 0, 15); r.mix = clamp(t.mix === undefined ? 99 : t.mix | 0, 0, 99);
        r.status = STATUS.includes(t.status) ? t.status : "MIDI"; r.mute = !!t.mute;
        r.ev = sortEvents((t.ev || []).filter((e) => e && Number.isFinite(e.t) && e.t >= 0).map((e) => ({ ...e })));
      });
      p.seqs[i] = q;
    });
    (o.songs || []).slice(0, NSONG).forEach((s, i) => {
      if (!s) return;
      const g = newSong(s.name);
      g.loop = !!s.loop;
      g.steps = (s.steps || []).slice(0, MAXSTEP).map((st) => ({ s: clamp(st.s | 0, 0, NSEQ - 1), rep: clamp(st.rep | 0 || 1, 1, 99),
                                                               tr: clamp(st.tr | 0, -24, 24) }));
      p.songs[i] = g;
    });
    return p;
  }

  /* ----------------------------------------------------------------------------------- edit functions --- */
  /* (the SQ-80's EDIT page: TRANSPOSE, REM CTRLS, AUTO-CORRECT, ERASE, MERGE, COPY; and for sequences APPEND, ADD
   * BARS, DELETE BARS, COPY) */
  const Edit = {
    transpose(tr, semis) { for (const e of tr.ev) if (isNote(e)) e.k = clamp(e.k + semis, 0, 127); },
    quantize(seq, tr, grid, strength) {     /* the starts moved toward the grid (strength 0..100); lengths kept */
      const len = seqLength(seq), s = (strength === undefined ? 100 : strength) / 100;
      if (grid <= 1) return;
      for (const e of tr.ev) {
        if (!isNote(e)) continue;
        const q = Math.round(e.t / grid) * grid;
        e.t = Math.round(e.t + (q - e.t) * s) % len;
      }
      sortEvents(tr.ev);
    },
    removeControllers(tr) { tr.ev = tr.ev.filter(isNote); },
    erase(tr) { tr.ev = []; },
    copy(src, dst) {                         /* the events and the track's settings */
      dst.ev = clone(src.ev); dst.prog = src.prog ? clone(src.prog) : null; dst.mix = src.mix; dst.ch = src.ch;
      dst.status = src.status;
    },
    merge(src, dst) { for (const e of src.ev) insertEvent(dst.ev, { ...e }); },   /* src into dst, src kept */
    setLength(seq, bars) {                   /* shorter: what lies past the end goes; notes cut at it */
      seq.bars = clamp(bars | 0, 1, 999);
      const len = seqLength(seq);
      for (const tr of seq.tracks) {
        tr.ev = tr.ev.filter((e) => e.t < len);
        for (const e of tr.ev) if (isNote(e) && e.t + e.d > len) e.d = Math.max(1, len - e.t);
      }
    },
    insertBars(seq, at, n) {                 /* n empty bars before bar `at` (0-based) */
      const bt = barTicks(seq), from = at * bt, by = n * bt;
      seq.bars = clamp(seq.bars + n, 1, 999);
      for (const tr of seq.tracks) for (const e of tr.ev) if (e.t >= from) e.t += by;
    },
    deleteBars(seq, at, n) {
      const bt = barTicks(seq), from = at * bt;
      n = Math.min(n, seq.bars - at);
      if (n <= 0 || seq.bars - n < 1) return false;
      const to = from + n * bt;
      seq.bars -= n;
      for (const tr of seq.tracks) {
        tr.ev = tr.ev.filter((e) => e.t < from || e.t >= to);
        for (const e of tr.ev) if (e.t >= to) e.t -= n * bt;
      }
      return true;
    },
    append(seq, other) {                     /* other's bars after seq's (track by track) */
      const off = seqLength(seq);
      seq.bars = clamp(seq.bars + Math.round(seqLength(other) / barTicks(seq)), 1, 999);
      other.tracks.forEach((tr, i) => { for (const e of tr.ev) insertEvent(seq.tracks[i].ev, { ...e, t: off + e.t }); });
    },
    timeSignature(seq, num, den) {           /* the length kept in ticks as near as whole bars allow */
      const len = seqLength(seq);
      seq.num = clamp(num | 0, 1, 16); seq.den = [2, 4, 8, 16].includes(den) ? den : 4;
      Edit.setLength(seq, Math.max(1, Math.round(len / barTicks(seq))));
    },
    velocity(tr, scale, add) { for (const e of tr.ev) if (isNote(e)) e.v = clamp(Math.round(e.v * scale / 100 + add), 1, 127); },
  };

  /* ---------------------------------------------------------------------------------- the play plan --- */
  /* What plays, in order: a sequence alone (looping, or once), or a song's steps, each `rep` times. A segment is
   * one pass of one sequence, with its transposition. */
  function plan(project, mode, index) {
    const segs = [];
    if (mode === "song") {
      const g = project.songs[index];
      if (!g) return { segs, loop: false };
      for (const st of g.steps) {
        if (!project.seqs[st.s]) continue;
        for (let r = 0; r < st.rep; r++) segs.push({ s: st.s, tr: st.tr, step: g.steps.indexOf(st), rep: r });
      }
      return { segs, loop: g.loop };
    }
    if (project.seqs[index]) segs.push({ s: index, tr: 0, step: -1, rep: 0 });
    return { segs, loop: !!(project.seqs[index] && project.seqs[index].loop) };
  }

  /* ----------------------------------------------------------------------------------------- the player --- */
  /* emit(ev): {time, type, ...}
   *   on:   tr, ch, k, v (as recorded), vm (scaled by MIX), status        off: tr, ch, k, status
   *   cc:   tr, ch, c, x, status      bend: tr, ch, b, status      at: tr, ch, a, status
   *   seq:  s (index), step, rep (a new segment begins: the page sends the tracks' programs and MIX)
   *   clock / start / stop (MIDI real time), click: accent, end (nothing more to play)
   * Muted tracks and status are the page's to route: the player skips muted tracks itself. */
  class Player {
    constructor(project, emit) {
      this.project = project; this.emit = emit; this.playing = false; this.rec = null; this.step = null;
      this.clickOn = false; this.clockOn = false; this.countoff = 0; this.lookahead = 120;
      this.offs = [];                        /* note offs not yet handed out: {time, tr, ch, k, status} */
      this.held = new Map();                 /* recording: key -> {t, v, seg} */
    }
    get seq() { const sg = this.segs && this.segs[this.si]; return sg ? this.project.seqs[sg.s] : null; }
    msPerTick(seq) { return 60000 / ((seq ? seq.tempo : 120) * PPQ); }

    /* start at bar `bar` (0-based) of the first segment (a song: of the whole), at time `now` */
    start(mode, index, now, bar) {
      this.stop(now);
      const pl = plan(this.project, mode, index);
      if (!pl.segs.length) return false;
      this.mode = mode; this.index = index; this.segs = pl.segs; this.loopAll = pl.loop;
      this.si = 0; this.tick = 0; this.gtick = 0; this.time = now + 30;   /* (a little later: the first sends in time) */
      let b = bar | 0;                        /* (a song: walk the segments to the bar) */
      while (b > 0 && this.si < this.segs.length) {
        const s = this.project.seqs[this.segs[this.si].s], n = s.bars;
        if (b < n) { this.tick = b * barTicks(s); break; }
        b -= n; this.si++;
      }
      if (this.si >= this.segs.length) { this.si = 0; this.tick = 0; }
      const s0 = this.seq;
      this.pre = this.rec && this.countoff ? this.countoff * barTicks(s0) : 0;   /* count-off: clicks only */
      this.playStart = this.time + this.pre * this.msPerTick(s0); this.startTick = this.tick;
      this.playing = true; this.newSeg = true; this.nextClock = 0;
      if (this.clockOn) this.emit({ time: this.time, type: "start" });
      return true;
    }
    stop(now) {
      if (!this.playing) return;
      this.playing = false;
      if (this.rec) this.recCloseAll(this.positionAt(now));
      for (const o of this.offs) this.emit({ ...o, time: now, type: "off" });
      this.offs = [];
      if (this.clockOn) this.emit({ time: now, type: "stop" });
      this.emit({ time: now, type: "stopped" });
    }
    /* where the play is at time t: {si, s, tick, bar, beat, pre} (pre: count-off ticks still to go) */
    positionAt(t) {
      if (!this.playing) return null;
      const seq = this.seq, mspt = this.msPerTick(seq), sg = this.segs[this.si];
      if (t < this.playStart) return { si: this.si, s: sg.s, tick: this.startTick, bar: 0, beat: 0, pre: (this.playStart - t) / mspt, step: sg.step };
      let tick = this.tick + (t - this.time) / mspt;
      const len = seqLength(seq);
      while (tick < 0) tick += len;
      if (tick >= len) tick -= len;           /* (just past the end: the next pass's start, as near as known) */
      const bt = barTicks(seq), bar = Math.floor(tick / bt), beat = Math.floor((tick - bar * bt) / (PPQ * 4 / seq.den));
      return { si: this.si, s: sg.s, tick, bar, beat, pre: 0, step: sg.step };
    }

    /* hand out everything due before now + lookahead */
    pump(now) {
      if (!this.playing) return;
      const horizon = now + this.lookahead;
      if (this.time < now - 250) this.time = now;   /* (the page slept: skip ahead rather than rush) */
      let guard = 0;
      while (this.playing && this.time < horizon && guard++ < 64) {
        const seq = this.seq, mspt = this.msPerTick(seq), len = seqLength(seq), bt = barTicks(seq), beatT = PPQ * 4 / seq.den;
        if (this.pre > 0) {                   /* count-off: the click alone */
          const take = Math.min(this.pre, (horizon - this.time) / mspt), total = this.countoff * bt, done = total - this.pre;
          for (let b = Math.ceil(done / beatT) * beatT; b < done + take - 1e-9; b += beatT)
            this.emit({ time: this.time + (b - done) * mspt, type: "click", accent: b % bt === 0, pre: true });
          this.time += take * mspt; this.pre -= take;
          if (this.pre > 1e-9) continue;
          this.pre = 0;
        }
        if (this.newSeg) {
          this.newSeg = false;
          const sg = this.segs[this.si];
          this.emit({ time: this.time, type: "seq", s: sg.s, step: sg.step, rep: sg.rep, tr: sg.tr });
        }
        const span = Math.min(len - this.tick, (horizon - this.time) / mspt), from = this.tick, to = from + span;
        const tr0 = this.segs[this.si].tr;
        seq.tracks.forEach((tr, ti) => {
          if (tr.mute) return;
          for (let i = firstAt(tr.ev, Math.ceil(from - 1e-9)); i < tr.ev.length && tr.ev[i].t < to; i++) {
            const e = tr.ev[i], time = this.time + (e.t - from) * mspt, base = { time, tr: ti, ch: tr.ch, status: tr.status };
            if (isNote(e)) {
              const k = clamp(e.k + tr0, 0, 127), vm = clamp(Math.round(e.v * tr.mix / 99), 1, 127);
              this.emit({ ...base, type: "on", k, v: e.v, vm });
              this.offs.push({ time: time + Math.max(1, e.d) * mspt - 0.5, tr: ti, ch: tr.ch, k, status: tr.status });
            } else if (e.c !== undefined) this.emit({ ...base, type: "cc", c: e.c, x: e.x });
            else if (e.b !== undefined) this.emit({ ...base, type: "bend", b: e.b });
            else if (e.a !== undefined) this.emit({ ...base, type: "at", a: e.a });
          }
        });
        if (this.clickOn)
          for (let b = Math.ceil(from / beatT - 1e-9) * beatT; b < to - 1e-9; b += beatT)
            this.emit({ time: this.time + (b - from) * mspt, type: "click", accent: b % bt === 0 });
        if (this.clockOn) {                    /* 24 to the quarter: every 4 ticks */
          for (; this.nextClock < this.gtick + span - 1e-9; this.nextClock += PPQ / 24)
            this.emit({ time: this.time + (this.nextClock - this.gtick) * mspt, type: "clock" });
        }
        this.time += span * mspt; this.tick = to; this.gtick += span;
        if (this.tick >= len - 1e-9) this.nextSegment();
      }
      this.flushOffs(horizon);
    }
    flushOffs(horizon) {
      if (!this.offs.length) return;
      this.offs.sort((a, b) => a.time - b.time);
      let n = 0;
      while (n < this.offs.length && this.offs[n].time < horizon) n++;
      for (const o of this.offs.splice(0, n)) this.emit({ ...o, type: "off" });
    }
    nextSegment() {
      const loopRec = this.rec && this.mode === "seq";
      this.tick = 0;
      if (this.si + 1 < this.segs.length) { this.si++; this.newSeg = true; }
      else if (this.loopAll || loopRec) { this.si = 0; this.newSeg = this.segs.length > 1; if (this.rec) this.recPass(); }
      else { const t = this.time; this.flushOffs(Infinity); this.playing = false; if (this.clockOn) this.emit({ time: t, type: "stop" });
             this.emit({ time: t, type: "end" }); }
    }

    /* ------------------------------------------------------------------------------------- recording --- */
    /* rec: {track, mode: "REPLACE" | "ADD", grid (ticks), punchIn, punchOut (bars, 0-based, out exclusive; -1 none),
     * ctrls (record controllers)} — set before start; REPLACE clears the punch range when the take begins. */
    armRecord(opts) {
      const seq = this.project.seqs[opts.s];
      if (!seq) return false;
      this.rec = { s: opts.s, track: opts.track, mode: opts.mode || "REPLACE", grid: opts.grid || 1, ctrls: opts.ctrls !== false,
                   punchIn: opts.punchIn === undefined ? -1 : opts.punchIn, punchOut: opts.punchOut === undefined ? -1 : opts.punchOut,
                   undo: clone(seq.tracks[opts.track].ev), took: 0 };
      if (this.rec.mode === "REPLACE") this.clearRange(seq);
      return true;
    }
    disarm() { const r = this.rec; this.rec = null; this.held.clear(); return r; }
    recRange(seq) {
      const bt = barTicks(seq), len = seqLength(seq), r = this.rec;
      return [r.punchIn >= 0 ? r.punchIn * bt : 0, r.punchOut > 0 ? Math.min(len, r.punchOut * bt) : len];
    }
    clearRange(seq) {
      const [a, b] = this.recRange(seq), tr = seq.tracks[this.rec.track];
      tr.ev = tr.ev.filter((e) => e.t < a || e.t >= b);
      this.rec.passes = 0;
    }
    recPass() { if (this.rec) this.rec.passes = (this.rec.passes || 0) + 1; }
    /* an incoming message at time t (ms): [status, d1, d2]; true when it went into the take */
    input(msg, t) {
      if (this.step) return this.stepInput(msg);
      const r = this.rec;
      if (!r || !this.playing || this.mode !== "seq") return false;
      const pos = this.positionAt(t);
      if (!pos || pos.pre > 0) return false;
      const seq = this.project.seqs[r.s], tr = seq.tracks[r.track], len = seqLength(seq), [a, b] = this.recRange(seq);
      const st = msg[0] & 0xF0, on = st === 0x90 && msg[2] > 0, off = st === 0x80 || (st === 0x90 && msg[2] === 0);
      const inRange = pos.tick >= a && pos.tick < b;
      if (on) {
        if (!inRange) return false;
        this.held.set(msg[1], { t: pos.tick, v: msg[2], g: this.gtickAt(t) });
        return true;
      }
      if (off) {
        const h = this.held.get(msg[1]);
        if (!h) return false;
        this.held.delete(msg[1]);
        this.recNote(seq, tr, msg[1], h, this.gtickAt(t) - h.g);
        return true;
      }
      if (!r.ctrls || !inRange) return false;
      const q = Math.floor(pos.tick);
      if (st === 0xB0) insertEvent(tr.ev, { t: q, c: msg[1], x: msg[2] });
      else if (st === 0xE0) insertEvent(tr.ev, { t: q, b: msg[1] | msg[2] << 7 });
      else if (st === 0xD0) insertEvent(tr.ev, { t: q, a: msg[1] });
      else return false;
      return true;
    }
    gtickAt(t) { return this.gtick + (t - this.time) / this.msPerTick(this.seq); }
    recNote(seq, tr, k, h, dur) {
      const len = seqLength(seq), g = this.rec.grid;
      let t = g > 1 ? Math.round(h.t / g) * g : Math.round(h.t);
      t %= len;
      insertEvent(tr.ev, { t, k, v: h.v, d: clamp(Math.round(dur), 1, len * 4) });
      this.rec.took++;
    }
    recCloseAll(pos) {
      if (!this.rec) return;
      const seq = this.project.seqs[this.rec.s], tr = seq.tracks[this.rec.track], g = this.gtick;
      for (const [k, h] of this.held) this.recNote(seq, tr, k, h, Math.max(1, g - h.g));
      this.held.clear();
    }

    /* ---------------------------------------------------------------------------------- step entry --- */
    /* the SQ-80's STEP mode: each chord played goes in at the cursor, `step` ticks long (`gate` % sounding);
     * REST moves on, BACK takes the last step out, TIE lengthens it */
    stepStart(s, track, stepTicks, gate) {
      const seq = this.project.seqs[s];
      if (!seq) return false;
      this.step = { s, track, cursor: 0, size: stepTicks || 24, gate: gate || 90, chord: new Map(), down: 0, hist: [],
                    undo: clone(seq.tracks[track].ev) };
      return true;
    }
    stepStop() { const st = this.step; this.step = null; return st; }
    stepInput(msg) {
      const st = this.step, seq = this.project.seqs[st.s], tr = seq.tracks[st.track];
      const s = msg[0] & 0xF0, on = s === 0x90 && msg[2] > 0, off = s === 0x80 || (s === 0x90 && msg[2] === 0);
      if (on) { st.chord.set(msg[1], msg[2]); st.down++; return true; }
      if (off && st.down > 0) {
        st.down--;
        if (st.down === 0 && st.chord.size) {
          const len = seqLength(seq), d = Math.max(1, Math.round(st.size * st.gate / 100)), added = [];
          if (st.cursor >= len) Edit.setLength(seq, seq.bars + 1);
          for (const [k, v] of st.chord) { const e = { t: st.cursor, k, v, d }; insertEvent(tr.ev, e); added.push(e); }
          st.hist.push({ at: st.cursor, added });
          st.chord.clear();
          st.cursor += st.size;
        }
        return true;
      }
      return false;
    }
    stepRest() { const st = this.step; st.hist.push({ at: st.cursor, added: [] }); st.cursor += st.size; }
    stepBack() {
      const st = this.step, h = st.hist.pop();
      if (!h) return;
      const tr = this.project.seqs[st.s].tracks[st.track];
      tr.ev = tr.ev.filter((e) => !h.added.includes(e));
      st.cursor = h.at;
    }
    stepTie() {                              /* the last chord lasts one more step */
      const st = this.step, h = st.hist[st.hist.length - 1];
      if (!h || !h.added.length) { this.stepRest(); return; }
      for (const e of h.added) e.d += st.size;
      st.hist.push({ at: st.cursor, added: [], tie: h.added });
      st.cursor += st.size;
    }
  }

  /* ------------------------------------------------------------------------------- Standard MIDI File --- */
  function vlq(n) { const o = [n & 0x7F]; while ((n >>= 7)) o.unshift((n & 0x7F) | 0x80); return o; }
  function chunk(id, data) { const n = data.length; return [...id].map((c) => c.charCodeAt(0)).concat([n >>> 24, (n >> 16) & 255, (n >> 8) & 255, n & 255], data); }
  function trackChunk(evs) {                /* evs: [{t, bytes}] in any order */
    evs.sort((a, b) => a.t - b.t || a.o - b.o);
    const out = []; let last = 0;
    for (const e of evs) { out.push(...vlq(Math.max(0, e.t - last)), ...e.bytes); last = e.t; }
    out.push(0, 0xFF, 0x2F, 0);
    return chunk("MTrk", out);
  }
  /* a sequence (once) or a song (its steps in order) as a type 1 file, one MIDI track per SQ-80 track used */
  function toSMF(project, mode, index) {
    const segs = plan(project, mode, index).segs;
    const meta = [], tracks = Array.from({ length: NTRACK }, () => []);
    let at = 0, lastTempo = -1, lastSig = "";
    for (const sg of segs) {
      const seq = project.seqs[sg.s], len = seqLength(seq);
      if (seq.tempo !== lastTempo) {
        const us = Math.round(60000000 / seq.tempo);
        meta.push({ t: at, o: 0, bytes: [0xFF, 0x51, 3, us >> 16, (us >> 8) & 255, us & 255] }); lastTempo = seq.tempo;
      }
      const sig = seq.num + "/" + seq.den;
      if (sig !== lastSig) { meta.push({ t: at, o: 0, bytes: [0xFF, 0x58, 4, seq.num, Math.log2(seq.den), 24, 8] }); lastSig = sig; }
      seq.tracks.forEach((tr, ti) => {
        if (tr.mute || !tr.ev.length) return;
        const ch = tr.ch & 15, T = tracks[ti];
        if (tr.prog && tr.prog.p !== undefined) T.push({ t: at, o: 0, bytes: [0xC0 | ch, tr.prog.p & 127] });
        for (const e of tr.ev) {
          const t = at + e.t;
          if (isNote(e)) {
            const k = clamp(e.k + sg.tr, 0, 127);
            T.push({ t, o: 2, bytes: [0x90 | ch, k, clamp(Math.round(e.v * tr.mix / 99), 1, 127)] });
            T.push({ t: t + Math.max(1, e.d), o: 1, bytes: [0x80 | ch, k, 0] });
          } else if (e.c !== undefined) T.push({ t, o: 1, bytes: [0xB0 | ch, e.c & 127, e.x & 127] });
          else if (e.b !== undefined) T.push({ t, o: 1, bytes: [0xE0 | ch, e.b & 127, (e.b >> 7) & 127] });
          else if (e.a !== undefined) T.push({ t, o: 1, bytes: [0xD0 | ch, e.a & 127] });
        }
      });
      at += len;
    }
    const used = tracks.map((T, i) => [T, i]).filter(([T]) => T.length);
    const body = [trackChunk(meta)];
    for (const [T, i] of used) {
      const name = [..."TRACK " + (i + 1)].map((c) => c.charCodeAt(0));
      body.push(trackChunk([{ t: 0, o: -1, bytes: [0xFF, 0x03, name.length, ...name] }, ...T]));
    }
    const head = chunk("MThd", [0, 1, 0, body.length, 0, PPQ]);
    return new Uint8Array(head.concat(...body));
  }

  /* a Standard MIDI File into a sequence: its first 8 tracks (or channels, for a type 0 file) */
  function fromSMF(bytes, name) {
    const d = bytes, u32 = (i) => (d[i] << 24 | d[i + 1] << 16 | d[i + 2] << 8 | d[i + 3]) >>> 0, u16 = (i) => d[i] << 8 | d[i + 1];
    if (String.fromCharCode(d[0], d[1], d[2], d[3]) !== "MThd") throw new Error("not a MIDI file");
    const div = u16(12);
    if (div & 0x8000) throw new Error("SMPTE time is not supported");
    const scale = PPQ / div, seq = newSequence(name);
    let p = 8 + u32(4), tempo = 0, sig = null, maxT = 0;
    const lanes = [];                        /* per source track: {ch -> notes} */
    while (p + 8 <= d.length) {
      const id = String.fromCharCode(d[p], d[p + 1], d[p + 2], d[p + 3]), n = u32(p + 4);
      if (id === "MTrk") {
        let i = p + 8, t = 0, run = 0;
        const end = p + 8 + n, open = new Map(), byCh = new Map();
        const lane = (ch) => { if (!byCh.has(ch)) byCh.set(ch, []); return byCh.get(ch); };
        const rd = () => { let v = 0, b; do { b = d[i++]; v = v << 7 | (b & 127); } while (b & 128 && i < end); return v; };
        while (i < end) {
          t += rd();
          let st = d[i];
          if (st & 0x80) i++; else st = run;
          if (st === 0xFF) { const ty = d[i++], ln = rd();
            if (ty === 0x51 && !tempo) tempo = 60000000 / (d[i] << 16 | d[i + 1] << 8 | d[i + 2]);
            if (ty === 0x58 && !sig) sig = [d[i], 2 ** d[i + 1]];
            i += ln; continue; }
          if (st === 0xF0 || st === 0xF7) { i += rd(); continue; }
          run = st;
          const ty = st & 0xF0, ch = st & 15, a = d[i++], b = ty === 0xC0 || ty === 0xD0 ? 0 : d[i++], T = Math.round(t * scale);
          if (ty === 0x90 && b) open.set(ch << 8 | a, { t: T, v: b });
          else if (ty === 0x80 || ty === 0x90) { const o = open.get(ch << 8 | a); if (o) { open.delete(ch << 8 | a); lane(ch).push({ t: o.t, k: a, v: o.v, d: Math.max(1, T - o.t) }); maxT = Math.max(maxT, T); } }
          else if (ty === 0xB0) lane(ch).push({ t: T, c: a, x: b });
          else if (ty === 0xE0) lane(ch).push({ t: T, b: a | b << 7 });
          else if (ty === 0xD0) lane(ch).push({ t: T, a });
        }
        for (const [ch, ev] of byCh) if (ev.some(isNote)) lanes.push({ ch, ev });
      }
      p += 8 + n;
    }
    lanes.slice(0, NTRACK).forEach((l, i) => { seq.tracks[i].ch = l.ch; seq.tracks[i].ev = sortEvents(l.ev); });
    if (tempo) seq.tempo = clamp(Math.round(tempo), 20, 300);
    if (sig) { seq.num = clamp(sig[0], 1, 16); seq.den = [2, 4, 8, 16].includes(sig[1]) ? sig[1] : 4; }
    seq.bars = clamp(Math.ceil(maxT / barTicks(seq)) || 1, 1, 999);
    return seq;
  }

  const SeqCore = { PPQ, NSEQ, NSONG, NTRACK, MAXSTEP, GRIDS, STATUS, newTrack, newSequence, newSong, newProject,
                    loadProject, barTicks, seqLength, sortEvents, insertEvent, firstAt, isNote, Edit, plan, Player,
                    toSMF, fromSMF, clone, progFrom };
  if (typeof module === "object" && module.exports) module.exports = SeqCore; else root.SeqCore = SeqCore;
})(typeof self !== "undefined" ? self : this);
