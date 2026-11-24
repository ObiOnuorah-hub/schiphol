// lays every route again over the painted lines, close to where it ran
const A = require('./audit.js'); const L = A.L; const NET = require('./net.js');
const routes = A.R; const cos25 = Math.cos(25 * Math.PI / 180);
const off = new Map(); const cell = 10;
for (const r of routes) {
  const n = r.pts.length; r.off = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const a = r.pts[Math.max(0, i - 1)], b = r.pts[Math.min(n - 1, i + 1)], l = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1, p = r.pts[i];
    r.off[i] = A.nearest(p[0], p[1], (b[0] - a[0]) / l, (b[1] - a[1]) / l, 12, cos25)[0];
    if (r.off[i] > 0.6) off.set(Math.floor(p[0] / cell) * 100003 + Math.floor(p[1] / cell), 1);
  }
}
const usedNear = (x, y) => {
  const i = Math.floor(x / cell), j = Math.floor(y / cell);
  for (let a = -6; a <= 6; a++) for (let b = -6; b <= 6; b++) if (off.has((i + a) * 100003 + j + b)) return true;
  return false;
};
const net = NET.buildNet(routes, { usedNear });
const { N, chains, near, extra } = net;
class Heap {
  constructor() { this.a = []; }
  push(k, v) { const a = this.a; a.push([k, v]); let i = a.length - 1; while (i) { const p = (i - 1) >> 1; if (a[p][0] <= a[i][0]) break; [a[p], a[i]] = [a[i], a[p]]; i = p; } }
  pop() { const a = this.a, r = a[0], l = a.pop(); if (a.length) { a[0] = l; let i = 0; for (;;) { let c = 2 * i + 1; if (c >= a.length) break; if (c + 1 < a.length && a[c + 1][0] < a[c][0]) c++; if (a[i][0] <= a[c][0]) break; [a[i], a[c]] = [a[c], a[i]]; i = c; } } return r; }
  get n() { return this.a.length; }
}
function solve(r, starts, goals, opt = {}) {
  const g = new Map();
  for (let i = 0; i < r.pts.length; i += 2) { const p = r.pts[i], k = Math.floor(p[0] / 20) * 100003 + Math.floor(p[1] / 20); let a = g.get(k); if (!a) g.set(k, a = []); a.push(p); }
  const memo = new Map();
  const dold = v => {
    let d = memo.get(v); if (d !== undefined) return d; d = 999; const n = N[v], I = Math.floor(n.x / 20), J = Math.floor(n.y / 20);
    for (let a = -5; a <= 5; a++) for (let b = -5; b <= 5; b++) { const l = g.get((I + a) * 100003 + J + b); if (l) for (const p of l) { const q = Math.hypot(p[0] - n.x, p[1] - n.y); if (q < d) d = q; } }
    memo.set(v, d); return d;
  };
  const maxD = opt.maxD || 90, w = opt.w || +(process.env.W || 0.04);
  const mult = v => { const d = dold(v); if (d > maxD) return Infinity; return (chains[N[v].c].virtual ? 2 : chains[N[v].c].centre ? 1 : 6) * (1 + w * Math.max(0, d - 3)); };
  const goalSet = new Set(goals.map(g => g[0] * 2 + g[1])), startSet = new Set(starts.map(g => g[0] * 2 + g[1])), dist = new Map(), from = new Map(), H = new Heap(), done = new Set(); let goal = -1;
  for (const x of startSet) { dist.set(x, 0); H.push(0, x); }
  while (H.n) {
    const [k, x] = H.pop(); if (done.has(x)) continue; done.add(x); if (goalSet.has(x)) { goal = x; break; }
    const v = x >> 1, s = x & 1, c = chains[N[v].c];
    const relax = (y, cost, e) => { const nd = k + cost; if (nd < (dist.has(y) ? dist.get(y) : Infinity)) { dist.set(y, nd); from.set(y, [x, e]); H.push(nd, y); } };
    const kk = N[v].k + (s === 0 ? 1 : -1);
    if (kk >= 0 && kk < c.p.length) { const y = c.first + kk, m = mult(y); if (m < Infinity) relax(y * 2 + s, Math.hypot(N[y].x - N[v].x, N[y].y - N[v].y) * m, null); }
    const ex = extra.get(x);
    if (ex) for (const e of ex) { const m = mult(e.to >> 1); if (m < Infinity) relax(e.to, e.cost * Math.min(m, 3), e); }
  }
  if (goal < 0) { let far = 0; for (const x of done) { const n = N[x >> 1]; for (let i = far + 1; i < r.pts.length; i += 3) if (Math.hypot(r.pts[i][0] - n.x, r.pts[i][1] - n.y) < 1.5) far = i; } solve.far = far; return null; }
  const steps = []; let x = goal; while (from.has(x)) { const [p, e] = from.get(x); steps.push([x, e]); x = p; } steps.push([x, null]); steps.reverse();
  const pts = [], kinds = [];
  for (const [y, e] of steps) { if (e && e.pts) for (const p of e.pts) { pts.push([p[0], p[1]]); kinds.push(e.kind); } const n = N[y >> 1]; pts.push([n.x, n.y]); kinds.push(e && e.kind !== 'join' ? e.kind + '!' : 'line'); }
  return { pts, kinds, first: steps[0][0], cost: dist.get(goal), links: steps.filter(s => s[1] && s[1].kind !== 'join').map(s => s[1].kind) };
}
// the node of the painted line at sample i of a route, with the direction it goes
function anchor(r, i, max = 0.95) {
  const n = r.pts.length, p = r.pts[i], a = r.pts[Math.max(0, i - 1)], b = r.pts[Math.min(n - 1, i + 1)], l = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
  const dx = (b[0] - a[0]) / l, dy = (b[1] - a[1]) / l; const res = [];
  near(p[0], p[1], max, (v, d) => { if (!chains[N[v].c].centre) return; const dot = N[v].tx * dx + N[v].ty * dy; if (Math.abs(dot) < 0.95) return; res.push([v, dot > 0 ? 0 : 1, d]); });
  return res.length ? res : null;
}
module.exports = { A, L, routes, net, solve, anchor, usedNear };
if (require.main === module) {
  const out = []; let fail = 0; const kindsum = {}; const t0 = Date.now();
  for (const r of routes) {
    if (/Pushback/.test(r.tag)) continue;
    const n = r.pts.length; let i0 = -1, i1 = -1, a0, a1;
    const run = (i, d) => { for (let k = 0; k < 8; k++) { const j = i + d * k; if (j >= 0 && j < n && !(r.off[j] < 0.8)) return false; } return true; };
    for (let i = 0; i < n; i++) { if (!run(i, 1)) continue; const a = anchor(r, i); if (a) { i0 = i; a0 = a; break; } }
    for (let i = n - 1; i >= 0; i--) { if (!run(i, -1)) continue; const a = anchor(r, i); if (a) { i1 = i; a1 = a; break; } }
    if (i0 < 0 || i1 <= i0) { console.log('NO ANCHOR', r.tag); fail++; continue; }
    const q = solve(r, a0, a1);
    if (!q) { console.log('NO PATH', r.tag, i0, i1, 'reached', solve.far, r.pts[solve.far].map(v => Math.round(v)).slice(0, 2).join(',')); fail++; continue; }
    q.links.forEach(k => kindsum[k] = (kindsum[k] || 0) + 1);
    out.push({ tag: r.tag, i0, i1, n, head: i0, tail: n - 1 - i1, old: i1 - i0, nw: q.pts.length, links: q.links.join(','), q, r });
  }
  console.log('solved', out.length, 'fail', fail, kindsum, (Date.now() - t0) / 1000 + 's');
  out.sort((a, b) => Math.abs(b.nw - b.old) - Math.abs(a.nw - a.old));
  for (const o of out.slice(0, 25)) console.log(o.tag, 'old', o.old, 'new', o.nw, 'head', o.head, 'tail', o.tail, o.links);
  const hd = {}; out.forEach(o => { const k = 'head ' + (o.head < 5 ? '<5' : o.head < 50 ? '<50' : o.head < 450 ? '<450' : '>450') + ' tail ' + (o.tail < 5 ? '<5' : o.tail < 30 ? '<30' : o.tail < 100 ? '<100' : '>100'); hd[k] = (hd[k] || 0) + 1; }); console.log(hd);
  L.fs.writeFileSync(L.dir + 'new_routes.txt', out.map(o => 'R ' + o.tag + ' ' + (o.q.pts.length * 100) + ' 0 x\n' + o.r.pts.slice(0, o.i0).concat(o.q.pts, o.r.pts.slice(o.i1 + 1)).map(p => (p[0] * 100).toFixed(1) + ' ' + (p[1] * 100).toFixed(1) + ' 0').join('\n')).join('\n') + '\n');
}
