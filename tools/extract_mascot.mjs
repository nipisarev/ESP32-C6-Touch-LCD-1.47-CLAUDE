// Builds engine/mascot_data.c from the Claude mascot animations by Ayotomiwa Wale-Durojaye
// (https://ayotomcs.me/claude-mascot, article on Codrops).
//
// The site bundle is NEVER executed: geometry is read with a small literal/JSX-call parser,
// and the four GSAP timelines are ported by hand below (transcribed from the bundle's effects).
//
//   node tools/extract_mascot.mjs <chunk 5ce46cbd0cd22c82.js> <out mascot_data.c> [scale]
import fs from 'node:fs';

const [, , chunkPath, outPath, scaleArg] = process.argv;
if (!chunkPath || !outPath) {
  console.error('usage: extract_mascot.mjs <chunk.js> <out.c> [scale]');
  process.exit(2);
}
const SCALE = Number(scaleArg || 1.0); // 1.0 keeps the 5x5 pixel art crisp and fits 172 px height
const SCREEN_W = 320;
const SRC = fs.readFileSync(chunkPath, 'utf8');
// The screen background is black: dark details outside the body must be lifted to stay visible.
// Eyes are black rects inside an orange body rect and stay black.
const BODY = 0xdd775b;
const RECOLOR_OUTSIDE_BODY = { 0x000000: 0x707070, 0x1a4c81: 0x3d74b8 };

// ---------------- static parser (no evaluation) ----------------
class Parser {
  constructor(src, pos) { this.s = src; this.i = pos; }
  ws() { while (this.i < this.s.length && /\s/.test(this.s[this.i])) this.i++; }
  peek(t) { this.ws(); return this.s.startsWith(t, this.i); }
  eat(t) {
    this.ws();
    if (!this.s.startsWith(t, this.i)) throw new Error(`expected ${t} at ${this.i}: ${this.s.slice(this.i, this.i + 40)}`);
    this.i += t.length;
  }
  value() {
    this.ws();
    if (this.peek('(0,i.jsx)(') || this.peek('(0,i.jsxs)(')) return this.jsx();
    const ch = this.s[this.i];
    if (ch === '{') return this.object();
    if (ch === '[') return this.array();
    if (ch === '"' || ch === "'") return this.string();
    return this.raw();
  }
  string() {
    const q = this.s[this.i++];
    let out = '';
    while (this.s[this.i] !== q) {
      if (this.s[this.i] === '\\') { out += this.s[this.i + 1]; this.i += 2; } else out += this.s[this.i++];
    }
    this.i++;
    return out;
  }
  skipString() {
    const q = this.s[this.i++];
    while (this.s[this.i] !== q) this.i += this.s[this.i] === '\\' ? 2 : 1;
    this.i++;
  }
  raw() {
    const st = this.i;
    let depth = 0;
    for (;;) {
      const ch = this.s[this.i];
      if (ch === '"' || ch === "'" || ch === '`') { this.skipString(); continue; }
      if ('([{'.includes(ch)) depth++;
      else if (')]}'.includes(ch)) { if (depth === 0) break; depth--; }
      else if (ch === ',' && depth === 0) break;
      this.i++;
    }
    const text = this.s.slice(st, this.i).trim();
    if (/^-?\d*\.?\d+(e-?\d+)?$/.test(text)) return parseFloat(text);
    if (text === 'void 0') return undefined;
    return { raw: text };
  }
  object() {
    this.eat('{');
    const o = {};
    while (!this.peek('}')) {
      this.ws();
      let key;
      if (this.s[this.i] === '"' || this.s[this.i] === "'") key = this.string();
      else { const m = /^[\w$]+/.exec(this.s.slice(this.i)); key = m[0]; this.i += key.length; }
      this.eat(':');
      o[key] = this.value();
      if (this.peek(',')) this.eat(',');
    }
    this.eat('}');
    return o;
  }
  array() {
    this.eat('[');
    const a = [];
    while (!this.peek(']')) { a.push(this.value()); if (this.peek(',')) this.eat(','); }
    this.eat(']');
    return a;
  }
  jsx() {
    this.eat(this.peek('(0,i.jsxs)(') ? '(0,i.jsxs)(' : '(0,i.jsx)(');
    const type = this.value();
    this.eat(',');
    const props = this.value();
    let key;
    if (this.peek(',')) { this.eat(','); key = this.value(); }
    this.eat(')');
    return { jsx: true, type: typeof type === 'string' ? type : '#fragment', props, key };
  }
}
function moduleText(id) {
  const m = new RegExp(`[,\\[]${id},t=>\\{`).exec(SRC);
  if (!m) throw new Error(`module ${id} not found`);
  const p = new Parser(SRC, m.index + m[0].length - 1);
  let depth = 0;
  for (;;) { // find the brace closing the factory body
    const ch = SRC[p.i];
    if (ch === '"' || ch === "'" || ch === '`') { p.skipString(); continue; }
    if ('([{'.includes(ch)) depth++;
    else if (')]}'.includes(ch) && --depth === 0) break;
    p.i++;
  }
  return SRC.slice(m.index, p.i + 1);
}
const parseAt = (text, marker, offset = 0) => {
  const at = text.indexOf(marker);
  if (at < 0) throw new Error(`marker not found: ${marker}`);
  return new Parser(text, at + offset).value();
};

// ---------------- element tree ----------------
function build(el, parent, ctx) {
  if (el == null || typeof el !== 'object' || el.raw !== undefined) return;
  if (Array.isArray(el)) return el.forEach((c) => build(c, parent, ctx));
  if (!el.jsx) return;
  if (el.type === '#fragment') return build(el.props.children, parent, ctx);
  const node = { type: el.type, props: el.props, parent, children: [] };
  ctx.nodes.push(node);
  if (parent) parent.children.push(node);
  const ref = el.props.ref;
  if (ref && ref.raw) {
    const arr = /^\w+=>(\w+)\.current\[(\d+)\]=\w+$/.exec(ref.raw);
    if (arr) ((ctx.refArrays[arr[1]] ||= [])[+arr[2]] = node);
    else if (/^\w+$/.test(ref.raw)) ctx.refs[ref.raw] = node;
    else throw new Error(`unsupported ref ${ref.raw}`);
  }
  build(el.props.children, node, ctx);
}

const num = (v, d = 0) => (v === undefined ? d : parseFloat(v));
const IDENT = [1, 0, 0, 1, 0, 0];
const mul = (m, n) => [m[0] * n[0] + m[2] * n[1], m[1] * n[0] + m[3] * n[1], m[0] * n[2] + m[2] * n[3], m[1] * n[2] + m[3] * n[3],
  m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5]];
function parseTransform(s) {
  let m = IDENT;
  if (!s) return m;
  const rest = s.replace(/(translate|scale|rotate|matrix)\(([^)]*)\)/g, (_, fn, args) => {
    const a = args.trim().split(/[\s,]+/).map(parseFloat);
    let t;
    if (fn === 'translate') t = [1, 0, 0, 1, a[0], a[1] || 0];
    else if (fn === 'scale') t = [a[0], 0, 0, a.length > 1 ? a[1] : a[0], 0, 0];
    else if (fn === 'matrix') t = a;
    else {
      const r = (a[0] * Math.PI) / 180, c = Math.cos(r), sn = Math.sin(r), cx = a[1] || 0, cy = a[2] || 0;
      t = mul(mul([1, 0, 0, 1, cx, cy], [c, sn, -sn, c, 0, 0]), [1, 0, 0, 1, -cx, -cy]);
    }
    m = mul(m, t);
    return '';
  });
  if (rest.trim()) throw new Error(`unsupported transform ${s}`);
  return m.map((v) => (Math.abs(v) < 1e-9 ? 0 : v));
}
const parseColor = (c) => {
  if (c === 'black') return 0x000000;
  if (c === 'white') return 0xffffff;
  const m = /^#([0-9a-fA-F]{6})$/.exec(c || '');
  if (!m) throw new Error(`unsupported fill ${c}`);
  return parseInt(m[1], 16);
};

// ---------------- minimal GSAP timeline (our code; mirrors GSAP 3 position rules) ----------------
class TL {
  constructor(opts = {}) {
    this.repeat = opts.repeat ?? 0; this.delay = opts.delay ?? 0;
    this.items = []; this.labels = {}; this.last = null; this.offset = 0; this.parent = null;
  }
  get end() { return this.items.reduce((m, it) => Math.max(m, it.start + (it.dur || 0)), 0); }
  resolve(pos) {
    if (pos === undefined) return this.end;
    if (typeof pos === 'number') return pos;
    if (pos === '<') return this.last ? this.last.start : 0;
    let m = /^\+=(\d*\.?\d+)$/.exec(pos);
    if (m) return this.end + parseFloat(m[1]);
    m = /^(\w+)(?:\+=(\d*\.?\d+))?$/.exec(pos);
    if (m && m[1] in this.labels) return this.labels[m[1]] + (m[2] ? parseFloat(m[2]) : 0);
    throw new Error(`unsupported position ${pos}`);
  }
  _add(item, pos, delay = 0) { item.start = this.resolve(pos) + delay; this.items.push(item); this.last = item; return this; }
  to(targets, vars, pos) {
    const { duration = 0.5, ease = 'power1.out', delay = 0, ...props } = vars;
    return this._add({ targets: [].concat(targets), props, dur: duration, ease }, pos, delay);
  }
  set(targets, vars, pos) { return this._add({ targets: [].concat(targets), props: vars, dur: 0, ease: 'none' }, pos); }
  addLabel(name) { this.labels[name] = this.end; return this; }
  add(child, pos) { child.parent = this; child.offset = this.resolve(pos); return this._add({ targets: [], props: {}, dur: 0 }, child.offset); }
}

// ---------------- the four animations ----------------
function walking(ctx) {
  const R = ctx.refs; // t eyes, l body, r/x/c/s legs 1-4, d Group 2, a left hand, g right hand
  const legs = [R.r, R.x, R.c, R.s];
  const n = 107 * SCALE, w = (107 / n) * (SCREEN_W - n), j = 0.55 * w, y = w - j;
  const origin = (yy) => legs.forEach((leg, k) => ctx.initialOrSet(leg, { svgOrigin: `${[16.5, 37.5, 69.5, 90.5][k]} ${yy}` }));
  origin(86);
  const m = ctx.timeline({ repeat: -1 });
  const P2O = 'power2.out';
  m.to(R.t, { x: -3, duration: 0.4, ease: P2O })
    .to(R.l, { rotation: -3, x: -3, y: -5, svgOrigin: '53 65', duration: 0.4, ease: P2O }, '<')
    .to(legs, { rotation: (k) => [-7, -8, -8, -9][k], scaleY: (k) => [1.35, 1.3, 1.2, 1.15][k], duration: 0.4, ease: P2O }, '<')
    .to(R.t, { x: 4, y: 12, duration: 0.4, ease: P2O, delay: 1.5 })
    .to(R.l, { rotation: 3, x: 3, y: -5, svgOrigin: '53 65', duration: 0.4, ease: P2O }, '<')
    .to(legs, { rotation: (k) => [9, 8, 8, 7][k], scaleY: (k) => [1.15, 1.2, 1.3, 1.35][k], duration: 0.4, ease: P2O }, '<')
    .to(R.t, { x: 0, y: 23, duration: 0.2, ease: P2O, delay: 1.2 })
    .to(R.l, { rotation: 0, x: 0, y: 0, duration: 0.2, ease: P2O }, '<')
    .to(legs, { rotation: 0, scaleY: 1, duration: 0.2, ease: P2O }, '<')
    .to(R.d, { y: -18, duration: 0.18, ease: P2O })
    .to(R.d, { y: 0, duration: 0.15, ease: 'power3.in' });
  legs.forEach((leg, k) => m.set(leg, { svgOrigin: `${[16.5, 37.5, 69.5, 90.5][k]} 60` })); // .call(): pivot at hip
  m.addLabel('walk')
    .to(R.d, { x: j, duration: 2.2, ease: 'linear' }, 'walk')
    .to(R.t, { x: 4, y: 0, duration: 0.2, ease: P2O }, 'walk');
  for (let k = 0; k < 20; k++) {
    const pair = k % 2 === 0 ? [R.r, R.c] : [R.x, R.s];
    m.to(pair, { scaleY: 0.45, duration: 0.1, ease: P2O }, `walk+=${(k * 0.1).toFixed(1)}`)
      .to(pair, { scaleY: 1, duration: 0.1, ease: 'power2.in' }, `walk+=${((k + 1) * 0.1).toFixed(1)}`);
  }
  m.to(legs, { scaleY: 1, duration: 0.08, ease: 'power2.in' }, 'walk+=2.1');
  legs.forEach((leg, k) => m.set(leg, { svgOrigin: `${[16.5, 37.5, 69.5, 90.5][k]} 86` })); // .call(): pivot at foot
  m.to(R.t, { y: 12, duration: 0.4, ease: P2O, delay: 0.3 })
    .to(R.l, { rotation: 3, x: 3, y: -5, svgOrigin: '53 65', duration: 0.4, ease: P2O }, '<')
    .to(legs, { rotation: (k) => [9, 8, 8, 7][k], scaleY: (k) => [1.15, 1.2, 1.3, 1.35][k], duration: 0.4, ease: P2O }, '<')
    .to(R.t, { y: 23, duration: 0.3, ease: P2O, delay: 0.6 })
    .to(R.t, { x: 4, y: 0, duration: 0.4, ease: P2O, delay: 1 })
    .to(R.l, { rotation: 0, x: 0, y: 0, duration: 0.4, ease: P2O }, '<')
    .to(legs, { rotation: 0, scaleY: 1, duration: 0.4, ease: P2O }, '<')
    .to(R.l, { y: 8, duration: 0.1, ease: 'power3.in', delay: 0.2 })
    .to([R.a, R.g], { y: 10, duration: 0.1, ease: 'power3.in' }, '<')
    .addLabel('jump')
    .to(R.d, { x: `+=${y}`, duration: 0.85, ease: 'power1.inOut' }, 'jump')
    .to(R.d, { y: -90, duration: 0.42, ease: 'sine.out' }, 'jump')
    .to(R.l, { y: 0, duration: 0.42, ease: 'sine.out' }, 'jump')
    .to([R.a, R.g], { y: -12, duration: 0.42, ease: 'sine.out' }, 'jump')
    .to(R.d, { y: 0, duration: 0.2, ease: 'power3.in' }, 'jump+=0.6')
    .to([R.a, R.g], { y: 0, duration: 0.2, ease: 'power3.in' }, 'jump+=0.6')
    .to([R.a, R.g], { y: 6, duration: 0.05, ease: 'power2.in' })
    .to([R.a, R.g], { y: 0, duration: 0.08, ease: P2O });
  m.set(R.d, { x: 0 }, '+=1').set(R.t, { x: 0, y: 0 }, '<'); // .call(..., "+=1"): teleport back, loop
}

function gym(ctx, text) {
  const seq = parseAt(text, 'let l=[', 6);
  const frames = ctx.refArrays.s;
  const e = ctx.timeline({ repeat: -1 });
  let r = 0;
  seq.forEach((x, h) => {
    frames.forEach((fr, i) => e.set(fr, { display: i === x ? 'inline' : 'none' }, r));
    r += h === seq.length - 1 ? 1.5 : x === 6 || x === 7 ? 0.27 : x === 15 || x === 21 ? 0.4 : 0.085;
  });
  e.period = r; // keep the documented 1.5 s hold on the last frame (GSAP itself drops it)
}

function flag(ctx) {
  const swayX = [0, 0, -5, -5, 0, 4, 4, 4, 0, 0, -5, -5];
  const handExtraX = [0, -6, -12, -14, -8, -2, 0, 0, -4, -10, -16, -18];
  const leftHandY = [0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 4, 4];
  const frames = ctx.refArrays.g;
  const R = ctx.refs; // t body group, d right hand + flags, a left hand
  const w = (tl, h, at) => {
    tl.set(R.t, { x: swayX[h] }, at).set(R.d, { x: handExtraX[h] }, at).set(R.a, { attr: { y: 90 + leftHandY[h] } }, at);
    frames.forEach((fr, i) => tl.set(fr, { display: i === h ? 'inline' : 'none' }, at));
  };
  const intro = ctx.timeline();
  for (let i = 0; i < 12; i++) w(intro, i, 0.07 * i);
  const loop = ctx.timeline({ repeat: -1 });
  for (let i = 3; i < 12; i++) w(loop, i, 0.07 * (i - 3));
  loop.set([], {}, 0.07 * 9);
  intro.add(loop, 0.07 * 12);
}

function confetti(ctx) {
  const ys = [-65, -72, -76, -70, -58, -42, -22, 0];
  const R = ctx.refs, A = ctx.refArrays; // t char frames, r/x particle frames, c/s particle groups
  const f = ctx.timeline({ repeat: -1 });
  for (let t = 0; t < 8; t++) A.t.forEach((fr, h) => f.set(fr, { display: h === t ? 'inline' : 'none' }, 0.125 * t));
  const burst = (delay, group, frames, tf) => {
    const tl = ctx.timeline({ repeat: -1, delay });
    tl.set(group, { display: 'inline' }, 0);
    for (let t = 0; t < 8; t++) {
      tl.set(group, { attr: { transform: tf(ys[t]) } }, 0.125 * t);
      frames.forEach((fr, h) => tl.set(fr, { display: h === t ? 'inline' : 'none' }, 0.125 * t));
    }
    return tl;
  };
  burst(0.125, R.c, A.r, (y) => `translate(90, ${y})`);
  burst(0.75, R.s, A.x, (y) => `translate(40, ${y}) scale(-1, 1)`);
  for (const tl of ctx.timelines) tl.period = 1.0; // 8 x 125 ms; GSAP drops the last step
}

// ---------------- extraction ----------------
const PROPS = ['display', 'x', 'y', 'rot', 'sy', 'ox', 'oy', 'attr_y', 'ma', 'mb', 'mc', 'md', 'me', 'mf'];
const PID = Object.fromEntries(PROPS.map((p, i) => [p, i]));
const EASES = ['none', 'power1.in', 'power1.out', 'power1.inOut', 'power2.in', 'power2.out', 'power2.inOut',
  'power3.in', 'power3.out', 'power3.inOut', 'sine.in', 'sine.out', 'sine.inOut'];
const easeId = (e) => {
  const i = EASES.indexOf(e === 'linear' ? 'none' : e);
  if (i < 0) throw new Error(`unsupported ease ${e}`);
  return i;
};
function expandProps(props, k) {
  const out = [];
  for (const [key, raw] of Object.entries(props)) {
    const v = typeof raw === 'function' ? raw(k) : raw;
    if (key === 'x' || key === 'y') out.push({ prop: key, value: v });
    else if (key === 'rotation') out.push({ prop: 'rot', value: v });
    else if (key === 'scaleY') out.push({ prop: 'sy', value: v });
    else if (key === 'svgOrigin') {
      const [ox, oy] = String(v).trim().split(/[\s,]+/).map(parseFloat);
      out.push({ prop: 'ox', value: ox, instant: true }, { prop: 'oy', value: oy, instant: true });
    } else if (key === 'display') out.push({ prop: 'display', value: v === 'none' ? 0 : 1, instant: true });
    else if (key === 'attr') {
      for (const [ak, av] of Object.entries(v)) {
        if (ak === 'y') out.push({ prop: 'attr_y', value: parseFloat(av), instant: true });
        else if (ak === 'transform') parseTransform(av).forEach((mv, mi) => out.push({ prop: PROPS[PID.ma + mi], value: mv, instant: true }));
        else throw new Error(`unsupported attr ${ak}`);
      }
    } else throw new Error(`unsupported prop ${key}`);
  }
  return out;
}

const SOURCES = {
  walking: { id: 630665, tree: (t) => parseAt(t, '(0,i.jsxs)("svg",{ref:f'), anim: walking },
  gym: {
    id: 282808,
    tree: (t) => {
      const frames = parseAt(t, 'r=[{transform:', 2);
      const vb = /c="(0 0 \d+ \d+)"/.exec(t)[1];
      return { jsx: true, type: 'svg', props: { viewBox: vb, children: frames.map((fr, e) => ({
        jsx: true, type: 'g', props: { transform: fr.transform, ref: { raw: `t=>s.current[${e}]=t` },
          style: e === 0 ? undefined : { display: 'none' }, children: fr.content } })) } };
    },
    anim: gym,
  },
  flag: { id: 4030, tree: (t) => parseAt(t, '(0,i.jsxs)("svg",{className:"w-24'), anim: flag },
  confetti: { id: 926067, tree: (t) => parseAt(t, '(0,i.jsxs)("svg",{viewBox:"0 0 129 113"'), anim: confetti },
};

function extract(name) {
  const src = SOURCES[name];
  const text = moduleText(src.id);
  const ctx = { nodes: [], refs: {}, refArrays: {}, timelines: [], initial: [] };
  build(src.tree(text), null, ctx);
  ctx.timeline = (opts) => { const tl = new TL(opts); ctx.timelines.push(tl); return tl; };
  ctx.initialOrSet = (target, vars) => ctx.initial.push({ targets: [target], props: vars });
  src.anim(ctx, text);

  const clips = {};
  for (const n of ctx.nodes) if (n.type === 'clipPath') {
    const r = n.children.find((c) => c.type === 'rect');
    clips[n.props.id] = { x: num(r.props.x), y: num(r.props.y), w: num(r.props.width), h: num(r.props.height) };
  }
  const drawn = ctx.nodes.filter((n) => {
    for (let p = n; p; p = p.parent) if (p.type === 'defs') return false;
    return ['svg', 'g', 'rect'].includes(n.type);
  });
  drawn.forEach((n, i) => { n.idx = i; });
  const base = new Map();
  for (const n of drawn) {
    const disp = n.props.display === 'none' || (n.props.style && n.props.style.display === 'none') ? 0 : 1;
    base.set(n, [disp, 0, 0, 0, 1, 0, 0, num(n.props.y), ...parseTransform(n.props.transform)]);
  }
  for (const s of ctx.initial) s.targets.forEach((tg, k) => {
    for (const e of expandProps(s.props, k)) base.get(tg)[PID[e.prop]] = e.value;
  });

  const tls = ctx.timelines.map((tl) => {
    let delay = tl.delay;
    for (let p = tl; p.parent; p = p.parent) delay += p.offset + p.parent.delay;
    return { delay, repeat: tl.repeat, period: tl.period ?? tl.end };
  });
  const tracks = [];
  ctx.timelines.forEach((tl, ti) => {
    const items = tl.items.map((it, o) => ({ it, o })).sort((a, b) => a.it.start - b.it.start || a.o - b.o).map((x) => x.it);
    const byKey = new Map();
    for (const a of items) a.targets.forEach((tg, k) => {
      if (!base.has(tg)) throw new Error(`${name}: animated target is not a drawn node`);
      for (const e of expandProps(a.props, k)) {
        const key = `${tg.idx}:${e.prop}`;
        if (!byKey.has(key)) byKey.set(key, { node: tg.idx, prop: PID[e.prop], tl: ti, segs: [] });
        const tr = byKey.get(key);
        const prev = tr.segs.length ? tr.segs[tr.segs.length - 1].v1 : base.get(tg)[PID[e.prop]];
        let to = e.value;
        if (typeof to === 'string') to = to.startsWith('+=') ? prev + parseFloat(to.slice(2)) : parseFloat(to);
        const dur = e.instant ? 0 : a.dur;
        tr.segs.push({ t0: a.start, t1: a.start + dur, v0: prev, v1: to, ease: dur ? easeId(a.ease) : 0 });
      }
    });
    tracks.push(...byKey.values());
  });
  tracks.sort((a, b) => tls[a.tl].delay - tls[b.tl].delay);

  const vb = drawn[0].props.viewBox.split(/\s+/).map(parseFloat);
  // Static bounding boxes (base state) in root space, to tell eyes from dark props.
  const world = new Map();
  for (const n of drawn) {
    const b = base.get(n);
    const local = mul([1, 0, 0, 1, b[PID.x], b[PID.y]], b.slice(PID.ma, PID.ma + 6));
    world.set(n, n.parent ? mul(world.get(n.parent), local) : local);
  }
  const bbox = (n) => {
    const m = world.get(n), x0 = num(n.props.x), y0 = num(n.props.y), x1 = x0 + num(n.props.width), y1 = y0 + num(n.props.height);
    const pts = [[x0, y0], [x1, y0], [x0, y1], [x1, y1]].map(([x, y]) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]);
    return [Math.min(...pts.map((p) => p[0])), Math.min(...pts.map((p) => p[1])), Math.max(...pts.map((p) => p[0])), Math.max(...pts.map((p) => p[1]))];
  };
  const frameOf = (n) => { let p = n; while (p.parent && p.parent.parent) p = p.parent; return p; };
  const bodies = drawn.filter((n) => n.type === 'rect' && parseColor(n.props.fill) === BODY);
  const insideBody = (n) => { // every corner of n lies in some body rect of the same frame
    const [ax0, ay0, ax1, ay1] = bbox(n);
    const boxes = bodies.filter((b) => frameOf(b) === frameOf(n)).map(bbox);
    const covered = (x, y) => boxes.some(([bx0, by0, bx1, by1]) => x >= bx0 - 1e-6 && y >= by0 - 1e-6 && x <= bx1 + 1e-6 && y <= by1 + 1e-6);
    return covered(ax0, ay0) && covered(ax1, ay0) && covered(ax0, ay1) && covered(ax1, ay1);
  };
  const nodes = drawn.map((n) => {
    const clipRef = /url\(#([^)]+)\)/.exec(n.props.clipPath || '');
    let color = n.type === 'rect' ? parseColor(n.props.fill) : 0;
    if (n.type === 'rect' && color in RECOLOR_OUTSIDE_BODY && !insideBody(n)) color = RECOLOR_OUTSIDE_BODY[color];
    return {
      parent: n.parent ? n.parent.idx : -1, rect: n.type === 'rect',
      x: num(n.props.x), y: num(n.props.y), w: num(n.props.width), h: num(n.props.height),
      color, clip: clipRef ? clips[clipRef[1]] : null, base: base.get(n), id: n.props.id || '',
    };
  });
  return { name, vb, nodes, tracks, tls };
}

const anims = Object.keys(SOURCES).map(extract);
for (const a of anims) {
  const colors = {};
  a.nodes.filter((n) => n.rect).forEach((n) => { colors[n.color.toString(16).padStart(6, '0')] = (colors[n.color.toString(16).padStart(6, '0')] || 0) + 1; });
  console.log(`${a.name}: viewBox ${a.vb.join(' ')}, nodes ${a.nodes.length}, tracks ${a.tracks.length}, segs ${a.tracks.reduce((s, t) => s + t.segs.length, 0)}`);
  console.log(`   timelines: ${a.tls.map((t) => `delay ${t.delay.toFixed(3)} period ${t.period.toFixed(3)} repeat ${t.repeat}`).join(' | ')}`);
  console.log(`   colors: ${JSON.stringify(colors)}`);
}

// ---------------- emit C ----------------
const f = (v) => { const r = +v.toFixed(4) || 0; return Number.isInteger(r) ? `${r}.0f` : `${r}f`; };
const nodesC = [], tracksC = [], segsC = [], tlsC = [], animsC = [];
let nodeOff = 0, trackOff = 0, segOff = 0, tlOff = 0;
for (const a of anims) {
  for (const n of a.nodes) {
    const clip = n.clip ? `1, {${f(n.clip.x)}, ${f(n.clip.y)}, ${f(n.clip.w)}, ${f(n.clip.h)}}` : '0, {0, 0, 0, 0}';
    nodesC.push(`    {${n.parent}, ${n.rect ? 1 : 0}, ${clip}, {${f(n.x)}, ${f(n.y)}, ${f(n.w)}, ${f(n.h)}}, 0x${n.color.toString(16).padStart(6, '0')}, {${n.base.map(f).join(', ')}}},${n.id ? ` /* ${n.id} */` : ''}`);
  }
  for (const t of a.tracks) {
    tracksC.push(`    {${t.node}, ${t.prop}, ${t.tl}, ${segOff - 0}, ${t.segs.length}},`);
    for (const s of t.segs) segsC.push(`    {${f(s.t0)}, ${f(s.t1)}, ${f(s.v0)}, ${f(s.v1)}, ${s.ease}},`);
    segOff += t.segs.length;
  }
  for (const t of a.tls) tlsC.push(`    {${f(t.delay)}, ${f(t.period)}, ${t.repeat}},`);
  animsC.push(`    {"${a.name}", ${f(a.vb[2])}, ${f(a.vb[3])}, ${nodeOff}, ${a.nodes.length}, ${trackOff}, ${a.tracks.length}, ${tlOff}, ${a.tls.length}},`);
  nodeOff += a.nodes.length; trackOff += a.tracks.length; tlOff += a.tls.length;
}
const c = `/* GENERATED by tools/extract_mascot.mjs (scale ${SCALE}) - do not edit.
 * Animations by Ayotomiwa Wale-Durojaye (https://ayotomcs.me/claude-mascot). No license: personal use only. */
#include "mascot_anim.h"

const manim_node_t mascot_nodes[] = {
${nodesC.join('\n')}
};

const manim_track_t mascot_tracks[] = {
${tracksC.join('\n')}
};

const manim_seg_t mascot_segs[] = {
${segsC.join('\n')}
};

const manim_tl_t mascot_tls[] = {
${tlsC.join('\n')}
};

const manim_anim_t mascot_anims[MANIM_COUNT] = {
${animsC.join('\n')}
};

const float mascot_data_scale = ${f(SCALE)};
`;
fs.writeFileSync(outPath, c);
console.log(`wrote ${outPath}: ${nodeOff} nodes, ${trackOff} tracks, ${segOff} segments, ${(fs.statSync(outPath).size / 1024).toFixed(0)} KB source`);
