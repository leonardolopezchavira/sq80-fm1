// SPDX-License-Identifier: MIT
/* SQ-80 for the FM-1: the tablet's own sound (tracks with status TABLET), on the audio thread. One engine
 * (sq80_editor.wasm, compiled here from the bytes the page sends) per track; events come with the context time they
 * are due at (0: at once) and are applied at their sample within the 128-frame block, so the page's drawing never
 * holds the sound up. */
class Sq80Tablet extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    this.mod = new WebAssembly.Module(opts.processorOptions.wasm);
    this.eng = [];
    this.q = [];
    this.vol = 0.7;
    this.pack = null;
    this.port.onmessage = (e) => this.msg(e.data);
  }
  engine(tr) {
    if (this.eng[tr]) return this.eng[tr];
    const imports = {};
    for (const im of WebAssembly.Module.imports(this.mod)) (imports[im.module] ||= {})[im.name] = () => 0;
    const X = new WebAssembly.Instance(this.mod, imports).exports;
    X.init(sampleRate);
    if (this.pack) this.usePack(X);
    this.eng[tr] = X;
    return X;
  }
  usePack(X) {
    const b = this.pack;
    if (b.length > X.pack_max()) return;
    new Uint8Array(X.memory.buffer, X.pack_buf(), b.length).set(b);
    X.use_pack(b.length);
  }
  msg(m) {
    switch (m.k) {
    case "prog": case "edit": {
      const X = this.engine(m.tr);
      new Uint8Array(X.memory.buffer, X.prog(), 102).set(m.data);
      X.apply(m.k === "prog" ? 1 : 0);
      break;
    }
    case "ev": this.engine(m.tr); this.q.push(m); break;
    case "alloff": this.q = []; for (const X of this.eng) if (X) X.all_off(); break;
    case "vol": this.vol = m.v; break;
    case "pack": this.pack = m.bytes; for (const X of this.eng) if (X) this.usePack(X); break;
    default: break;
    }
  }
  apply(e) {
    const X = this.eng[e.tr];
    if (!X) return;
    if (e.kind === "on") X.note_on(e.a, e.b);
    else if (e.kind === "off") X.note_off(e.a);
    else if (e.kind === "cc") X.cc(e.a, e.b);
    else if (e.kind === "bend") X.bend(e.a);
  }
  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;
    if (this.q.length > 1) this.q.sort((a, b) => a.t - b.t);
    let done = 0, pos = 0;
    const at = (e) => (e.t ? Math.round((e.t - currentTime) * sampleRate) : 0);
    while (pos < n) {
      while (done < this.q.length && at(this.q[done]) <= pos) this.apply(this.q[done++]);
      const next = done < this.q.length ? Math.min(n, at(this.q[done])) : n, m = Math.max(1, next - pos);
      for (const X of this.eng) {
        if (!X || !X.voices()) continue;
        X.render(m);
        const xl = new Float32Array(X.memory.buffer, X.left(), m), xr = new Float32Array(X.memory.buffer, X.right(), m);
        for (let i = 0; i < m; i++) { L[pos + i] += xl[i]; R[pos + i] += xr[i]; }
      }
      pos += m;
    }
    if (done) this.q.splice(0, done);
    const g = this.vol * 0.45;
    for (let i = 0; i < n; i++) { L[i] = Math.tanh(L[i] * g); if (R !== L) R[i] = Math.tanh(R[i] * g); }
    return true;
  }
}
registerProcessor("sq80-tablet", Sq80Tablet);
