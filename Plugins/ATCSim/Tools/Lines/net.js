// routing network over the painted centrelines, with realistic connectors where the paint stops
const L = require('./lib.js'); const PV = require('./pave.js'); const GR = require('./graph.js');

function hermite(x0, y0, tx0, ty0, x1, y1, tx1, ty1) {
  const d = Math.hypot(x1 - x0, y1 - y0), m = d * 1.05, n = Math.max(4, Math.ceil(d)), p = [];
  for (let i = 0; i <= n; i++) {
    const u = i / n, u2 = u * u, u3 = u2 * u, a = 2 * u3 - 3 * u2 + 1, b = u3 - 2 * u2 + u, c = -2 * u3 + 3 * u2, e = u3 - u2;
    p.push([a * x0 + b * m * tx0 + c * x1 + e * m * tx1, a * y0 + b * m * ty0 + c * y1 + e * m * ty1]);
  }
  return p;
}
function minRadius(p) {
  let r = 1e9;
  for (let i = 2; i < p.length - 2; i++) {
    const a = p[i - 2], b = p[i], c = p[i + 2];
    const cr = Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
    if (cr < 1e-6) continue;
    r = Math.min(r, Math.hypot(b[0] - a[0], b[1] - a[1]) * Math.hypot(c[0] - b[0], c[1] - b[1]) * Math.hypot(c[0] - a[0], c[1] - a[1]) / (2 * cr));
  }
  return r;
}
const plen = p => p.reduce((s, q, i) => i ? s + Math.hypot(q[0] - p[i - 1][0], q[1] - p[i - 1][1]) : 0, 0);
function turning(p) {
  let t = 0;
  for (let i = 2; i < p.length; i++) {
    const a = Math.atan2(p[i - 1][1] - p[i - 2][1], p[i - 1][0] - p[i - 2][0]), b = Math.atan2(p[i][1] - p[i - 1][1], p[i][0] - p[i - 1][0]);
    let d = b - a; while (d > Math.PI) d -= 2 * Math.PI; while (d < -Math.PI) d += 2 * Math.PI; t += Math.abs(d);
  }
  return t;
}
const pavedPath = p => { for (const q of p) if (!PV.paved(q[0], q[1])) return false; return true; };
// at least 6 m of pavement either side
function widePath(p, w = 6) {
  for (let i = 0; i < p.length; i++) {
    const a = p[Math.max(0, i - 1)], b = p[Math.min(p.length - 1, i + 1)], l = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    const nx = -(b[1] - a[1]) / l, ny = (b[0] - a[0]) / l;
    for (const q of [-w, 0, w]) if (!PV.paved(p[i][0] + nx * q, p[i][1] + ny * q)) return false;
  }
  return true;
}

function buildNet(routes, opt = {}) {
  const G = GR.build(); GR.classify(G, routes); const { chains, N, near } = G;
  for (const c of chains) {
    c.centre = c.used > 0.02 || (c.osm > 0.5 && c.edge < 0.4);
    if (c.len < 4 && c.used === 0) c.centre = false;
    if (c.virtual) c.centre = true;
  }
  const st = (v, s) => v * 2 + s, extra = new Map();
  const tan = (v, s) => s ? [-N[v].tx, -N[v].ty] : [N[v].tx, N[v].ty];
  function link(a, sa, b, sb, cost, pts, kind) {
    const add = (x, y, p) => { let l = extra.get(x); if (!l) extra.set(x, l = []); l.push({ to: y, cost, pts: p, kind }); };
    add(st(a, sa), st(b, sb), pts); add(st(b, 1 - sb), st(a, 1 - sa), pts ? pts.slice().reverse() : pts);
  }
  const edgeAt = (x, y) => { let hit = false; near(x, y, 0.75, w => { const c = chains[N[w].c]; if (!c.centre && c.len > 12) hit = true; }); return hit; };
  const freeOfEdges = p => { for (const q of p) if (edgeAt(q[0], q[1])) return false; return true; };
  // ends of lines: joined to a line they run into, else a dead end
  const ends = [], joined = []; let joins = 0;
  for (const c of chains) {
    if (!c.centre) continue;
    for (const e of [0, 1]) {
      const v = e ? c.first + c.p.length - 1 : c.first, s = e ? 0 : 1, t = tan(v, s);
      let best = -1, bd = 1e9, bs = 0;
      near(N[v].x, N[v].y, 1.6, (w, d) => {
        if (N[w].c === c.id) return;
        const o = chains[N[w].c]; if (!o.centre) return;
        const dot = N[w].tx * t[0] + N[w].ty * t[1]; if (Math.abs(dot) < 0.94) return;
        const k = N[w].k, ss = dot > 0 ? 0 : 1, ahead = ss === 0 ? o.p.length - 1 - k : k, score = d + (ahead < 2 ? 1.5 : 0);
        if (score < bd) { bd = score; best = w; bs = ss; }
      });
      if (best >= 0) {
        link(v, s, best, bs, Math.max(0.1, Math.hypot(N[v].x - N[best].x, N[v].y - N[best].y)), [], 'join'); joins++;
        const o = chains[N[best].c], ahead = bs === 0 ? o.p.length - 1 - N[best].k : N[best].k;
        if (ahead >= 2) { joined.push({ v, s, c: c.id, t }); continue; }
      }
      ends.push({ v, s, c: c.id, t });
    }
  }
  // two lines that touch or run together in the same direction: from one to the other
  let touches = 0; const tseen = new Set();
  for (let v = 0; v < N.length; v++) {
    const c = chains[N[v].c]; if (!c.centre) continue;
    near(N[v].x, N[v].y, 0.6, (w) => {
      if (w <= v || N[w].c === N[v].c) return; const o = chains[N[w].c]; if (!o.centre) return;
      const dot = N[v].tx * N[w].tx + N[v].ty * N[w].ty; if (Math.abs(dot) < 0.98) return;
      const key = N[v].c + '_' + N[w].c + '_' + Math.round(N[v].k / 4); if (tseen.has(key)) return; tseen.add(key);
      const s = dot > 0 ? 0 : 1; link(v, 0, w, s, 0.5, [], 'join'); link(v, 1, w, 1 - s, 0.5, [], 'join'); touches++;
    });
  }
  // dead ends: straight on, or a smooth curve to a line ahead
  let gaps = 0;
  for (const E of ends) {
    const n = N[E.v], cand = new Map();
    near(n.x, n.y, 75, (w, d) => {
      if (d < 0.8 || (N[w].c === E.c && Math.abs(N[w].k - N[E.v].k) < 60)) return;
      const o = chains[N[w].c]; if (!o.centre) return;
      const cx = (N[w].x - n.x) / d, cy = (N[w].y - n.y) / d, a0 = cx * E.t[0] + cy * E.t[1]; if (a0 < 0.6) return;
      for (const s of [0, 1]) {
        const q = tan(w, s), a1 = cx * q[0] + cy * q[1]; if (a1 < 0.55) continue;
        if (E.t[0] * q[0] + E.t[1] * q[1] < -0.1) continue;
        const ahead = s === 0 ? o.p.length - 1 - N[w].k : N[w].k; if (ahead < 3) continue;
        const bend = Math.acos(Math.min(1, a0)) + Math.acos(Math.min(1, a1)), cost = d * 1.3 + bend * 25, key = N[w].c * 2 + s;
        const l = cand.get(key) || []; l.push([cost, w, s, d]); cand.set(key, l);
      }
    });
    for (let a = 76; a < 260; a += 2) {   // straight far ahead
      const x = n.x + E.t[0] * a, y = n.y + E.t[1] * a; let hit = null;
      near(x, y, Math.min(8, 2 + a * 0.04), (w, d) => {
        const o = chains[N[w].c]; if (!o.centre || N[w].c === E.c) return;
        const dot = N[w].tx * E.t[0] + N[w].ty * E.t[1]; if (Math.abs(dot) < 0.97) return;
        if (!hit || d < hit[1]) hit = [w, d, dot > 0 ? 0 : 1];
      });
      if (hit) { const w = hit[0], s = hit[2], d = Math.hypot(N[w].x - n.x, N[w].y - n.y); cand.set('f' + N[w].c + s, [[d * 1.3, w, s, d]]); break; }
    }
    for (const [, l] of cand) {
      l.sort((p, q) => p[0] - q[0]);
      let best = null;   // short, but a wide bend is worth some metres more
      for (let i = 0; i < Math.min(l.length, 60); i++) {
        const [, w, s, d] = l[i], q = tan(w, s), p = hermite(n.x, n.y, E.t[0], E.t[1], N[w].x, N[w].y, q[0], q[1]), r = minRadius(p);
        if (d > 5 && r < 21) continue;
        const len = plen(p); if (len > 1.25 * d) continue;
        const cost = len * 2.5 + turning(p) * 10 + 2, score = cost + (d > 5 ? Math.max(0, 50 - r) * 2 : 0);
        if (best && score >= best.score) continue;
        if (!pavedPath(p) || !freeOfEdges(p)) continue;
        best = { score, cost, w, s, p };
      }
      if (best) { link(E.v, E.s, best.w, best.s, best.cost, best.p.slice(1, -1), 'gap'); gaps++; }
    }
  }
  // turns without paint: where two lines cross (or a line's straight extension meets another line)
  function walk(v, s, dist) {   // the node dist back along the line against direction s; null past its end
    let left = Math.round(dist), x = v, d = 1 - s;   // d: the direction walked
    for (let hop = 0; hop < 6; hop++) {
      const c = chains[N[x].c], room = d === 0 ? c.p.length - 1 - N[x].k : N[x].k;
      if (left <= room) return [c.first + N[x].k + (d === 0 ? left : -left), 1 - d];
      left -= room; const e = c.first + (d === 0 ? c.p.length - 1 : 0); let nx = null;
      for (let back = 0; back < 3 && !nx; back++) { const q = e + (d === 0 ? -back : back); if (q < c.first || q >= c.first + c.p.length) break; const ex = extra.get(q * 2 + d); if (ex) for (const j of ex) if (j.kind === 'join' && N[j.to >> 1].c !== c.id) { nx = j.to; break; } }
      if (nx === null) return null; x = nx >> 1; d = nx & 1;
    }
    return null;
  }
  const cross = [], seen = new Set(), usedNear = opt.usedNear;
  for (let v = 0; v < N.length; v++) {
    const c = chains[N[v].c]; if (!c.centre || (usedNear && !usedNear(N[v].x, N[v].y))) continue;
    near(N[v].x, N[v].y, 0.8, (w) => {
      if (w <= v || N[w].c === N[v].c) return;
      const o = chains[N[w].c]; if (!o.centre) return;
      if (Math.abs(N[v].tx * N[w].ty - N[v].ty * N[w].tx) < 0.26) return;
      const key = N[v].c + '_' + N[w].c + '_' + Math.round(N[v].x / 6) + '_' + Math.round(N[v].y / 6);
      if (seen.has(key)) return; seen.add(key);
      cross.push({ x: (N[v].x + N[w].x) / 2, y: (N[v].y + N[w].y) / 2, a: v, ea: 0, b: w });
    });
  }
  for (const E of ends.concat(joined)) {
    const n = N[E.v]; if (usedNear && !usedNear(n.x, n.y)) continue; const far = E.joined ? 0 : joined.includes(E) ? 14 : 70;
    const hits = new Set();
    for (let a = 0.5; a < far && hits.size < 3; a += 0.5) {
      const x = n.x + E.t[0] * a, y = n.y + E.t[1] * a, found = new Map();
      near(x, y, 0.7, (w, d) => {
        const o = chains[N[w].c]; if (!o.centre || N[w].c === E.c || hits.has(N[w].c)) return;
        if (Math.abs(E.t[0] * N[w].ty - E.t[1] * N[w].tx) < 0.26) return;
        const f = found.get(N[w].c); if (!f || d < f[1]) found.set(N[w].c, [w, d]);
      });
      for (const [c, f] of found) { hits.add(c); cross.push({ x, y, a: E.v, ea: a, es: E.s, b: f[0] }); }
    }
  }
  for (let i = 0; i < ends.length; i++) for (let j = i + 1; j < ends.length; j++) {
    const E = ends[i], F = ends[j], e = N[E.v], f = N[F.v]; if (E.c === F.c || Math.hypot(e.x - f.x, e.y - f.y) > 60) continue;
    if (usedNear && !usedNear(e.x, e.y)) continue;
    const cr = E.t[0] * F.t[1] - E.t[1] * F.t[0]; if (Math.abs(cr) < 0.26) continue;
    const dx = f.x - e.x, dy = f.y - e.y, a = (dx * F.t[1] - dy * F.t[0]) / cr, b = (dx * E.t[1] - dy * E.t[0]) / cr;
    if (a < 0.5 || a > 50 || b < 0.5 || b > 50) continue;
    cross.push({ x: e.x + E.t[0] * a, y: e.y + E.t[1] * a, a: E.v, ea: a, es: E.s, b: F.v, eb: b, bs: F.s });
  }
  let turns = 0;
  for (const X of cross) {
    for (const sa of (X.ea ? [X.es] : [0, 1])) for (const sb of [0, 1]) {
      const ta = tan(X.a, sa), tb = tan(X.b, sb), th = Math.acos(Math.max(-1, Math.min(1, ta[0] * tb[0] + ta[1] * tb[1])));
      if (th < 0.2 || th > 2.6) continue;
      if (X.eb && sb === X.bs) continue;   // off the end of b
      // the point da before the crossing on a, db after it on b: on the line or on its straight extension
      const before = da => { if (X.ea && da <= X.ea) return { v: X.a, s: sa, x: X.x - ta[0] * da, y: X.y - ta[1] * da, tx: ta[0], ty: ta[1], ext: X.ea - da }; const q = walk(X.a, sa, da - (X.ea || 0)); if (!q) return null; const t = tan(q[0], q[1]); return { v: q[0], s: q[1], x: N[q[0]].x, y: N[q[0]].y, tx: t[0], ty: t[1], ext: 0 }; };
      const after = db => { if (X.eb && db <= X.eb) return { v: X.b, s: sb, x: X.x + tb[0] * db, y: X.y + tb[1] * db, tx: tb[0], ty: tb[1], ext: X.eb - db }; const q = walk(X.b, 1 - sb, db - (X.eb || 0)); if (!q) return null; const s2 = 1 - q[1], t = tan(q[0], s2); return { v: q[0], s: s2, x: N[q[0]].x, y: N[q[0]].y, tx: t[0], ty: t[1], ext: 0 }; };
      let best = null;
      const As = [], Bs = []; for (let d = 6; d <= 75; d += 3) { As.push(before(d)); Bs.push(after(d)); }
      for (let i = 0; i < As.length; i++) for (let j = Math.max(0, i - 5); j <= Math.min(Bs.length - 1, i + 5); j++) {
        const A = As[i], B = Bs[j]; if (!A || !B) continue;
        const span = (i + j) * 3 + 12; if (best && span * 2.2 > best.score) continue;
        const cx = B.x - A.x, cy = B.y - A.y, ch = Math.hypot(cx, cy); if (ch < 4) continue;
        const dot = A.tx * B.tx + A.ty * B.ty, turn = Math.acos(Math.max(-1, Math.min(1, dot))); if (turn > 2.7) continue;
        // handles of a circle through that turn
        const R = ch / (2 * Math.sin(turn / 2) || 1e-6), h0 = turn < 0.05 ? ch / 3 : Math.min(ch, 4 / 3 * Math.tan(turn / 4) * R);
        for (const k of [1, 0.85, 1.15]) {
          const h = h0 * k, n = Math.max(8, Math.ceil(ch * 1.2)), p = [];
          const P1 = [A.x + A.tx * h, A.y + A.ty * h], P2 = [B.x - B.tx * h, B.y - B.ty * h];
          for (let q = 0; q <= n; q++) { const u = q / n, w = 1 - u; p.push([w * w * w * A.x + 3 * w * w * u * P1[0] + 3 * w * u * u * P2[0] + u * u * u * B.x, w * w * w * A.y + 3 * w * w * u * P1[1] + 3 * w * u * u * P2[1] + u * u * u * B.y]); }
          const r = minRadius(p); if (r < 20) continue;
          const score = span * 2.2 + Math.max(0, 30 - r) * 2 + Math.abs(i - j) * 3;
          if (best && score >= best.score) continue;
          if (!widePath(p) || !freeOfEdges(p)) continue;
          best = { score, A, B, p, r };
        }
      }
      if (!best) continue;
      const { A, B, p } = best, full = p.slice();
      if (A.ext > 0.5) { const pre = []; for (let q = 1; q < A.ext; q++) pre.push([N[A.v].x + ta[0] * q, N[A.v].y + ta[1] * q]); full.unshift(...pre); }
      if (B.ext > 0.5) { const t = tb; for (let q = 1; q < B.ext; q++) full.push([B.x + t[0] * q, B.y + t[1] * q]); }
      link(A.v, A.s, B.v, B.s, plen(full) * 2.5 + th * 10 + 8, full.slice(A.ext > 0.5 ? 0 : 1, B.ext > 0.5 ? undefined : -1), 'turn'); turns++;
    }
  }
  console.log('centre chains', chains.filter(c => c.centre).length, 'joins', joins, 'touches', touches, 'dead ends', ends.length, 'gap links', gaps, 'crossings', cross.length, 'turn links', turns);
  return { G, chains, N, near, extra, tan, ends, freeOfEdges };
}
module.exports = { buildNet, hermite, minRadius, plen, turning, pavedPath, widePath };
