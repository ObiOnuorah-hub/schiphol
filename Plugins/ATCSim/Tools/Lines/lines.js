// stands on their lead-in line, pushbacks, departures and arrivals over the painted lines
const S = require('./solve.js'); const { L, routes, net, solve, anchor } = S; const { N, chains, near, extra, tan, ends } = net;
const NET = require('./net.js'); const PV = require('./pave.js');
const byTag = new Map(routes.map(r => [r.tag, r]));
const D2R = Math.PI / 180;

// ---------------------------------------------------------------- stands
const stands = {};
for (const l of L.fs.readFileSync(L.dir + 'stands.txt', 'utf8').split('\n')) {
  const m = l.match(/^Stand\.EHAM\.(\S+) (\S+) (\S+) (\S+)/); if (!m) continue;
  stands[m[1]] = { ref: m[1], ox: m[2] / 100, oy: m[3] / 100, oyaw: +m[4] };
}
// by hand: the models of these gates stood beside their painted stand line, or the stand was the wrong way round
const HAND = { P1: { x: -455.7, y: 658.0, yaw: -32 }, P9: { x: -562.3, y: 679.2, yaw: 148 }, P5: { x: -534.8, y: 865.0, yaw: -32 }, D3: { x: 257.6, y: -197, yaw: 94 } };
for (const s of Object.values(stands)) {
  const h = HAND[s.ref] || { x: s.ox, y: s.oy, yaw: s.oyaw }, dx = Math.cos(h.yaw * D2R), dy = Math.sin(h.yaw * D2R);
  let best = null;
  near(h.x, h.y, 5, v => {
    const c = chains[N[v].c]; if (!c.centre || c.len < 15) return; const dot = N[v].tx * dx + N[v].ty * dy; if (Math.abs(dot) < 0.97) return;
    const lat = (N[v].x - h.x) * -dy + (N[v].y - h.y) * dx, al = (N[v].x - h.x) * dx + (N[v].y - h.y) * dy; if (Math.abs(al) > 0.8 || Math.abs(lat) > 4) return;
    if (!best || Math.abs(lat) + (c.len < 30 ? 1 : 0) < Math.abs(best.lat) + (chains[N[best.v].c].len < 30 ? 1 : 0)) best = { v, lat, al, s: dot > 0 ? 0 : 1 };
  });
  if (!best) { console.log('stand without line', s.ref); continue; }
  const t = tan(best.v, best.s);
  // where the stand's point projects onto the line
  const al = (h.x - N[best.v].x) * t[0] + (h.y - N[best.v].y) * t[1];
  s.v = best.v; s.sIn = best.s; s.x = N[best.v].x + t[0] * al; s.y = N[best.v].y + t[1] * al; s.yaw = Math.atan2(t[1], t[0]) / D2R; s.moved = Math.hypot(s.x - s.ox, s.y - s.oy);
  let dyaw = s.yaw - s.oyaw; while (dyaw > 180) dyaw -= 360; while (dyaw < -180) dyaw += 360; s.turned = dyaw;
}
console.log('stands moved onto their line (m, deg):', Object.values(stands).map(s => s.ref + ' ' + (s.moved === undefined ? '?' : s.moved.toFixed(1) + '/' + s.turned.toFixed(1))).join('  '));

// ---------------------------------------------------------------- pushback: out along the lead-in, round onto the taxilane, 10 m straight
const isLeadIn = c => chains[c].stand > 0.5;
function localPaths(v0, s0, maxLen) {   // states reachable from the stand, outward
  const H = [[0, v0 * 2 + s0]], dist = new Map([[v0 * 2 + s0, 0]]), from = new Map(), done = new Set(), found = [];
  while (H.length) {
    H.sort((a, b) => b[0] - a[0]); const [k, x] = H.pop(); if (done.has(x)) continue; done.add(x);
    const v = x >> 1, s = x & 1, c = chains[N[v].c]; if (k > maxLen) continue;
    const relax = (y, cost, e) => { if (k + cost < (dist.has(y) ? dist.get(y) : 1e9)) { dist.set(y, k + cost); from.set(y, [x, e]); H.push([k + cost, y]); } };
    const kk = N[v].k + (s === 0 ? 1 : -1); if (kk >= 0 && kk < c.p.length) relax((c.first + kk) * 2 + s, 1, null);
    const ex = extra.get(x); if (ex) for (const e of ex) { const to = e.to >> 1; if (e.kind === 'join' && N[to].c === N[v0].c) continue; const own = isLeadIn(N[to].c) || N[to].c === N[v0].c; if (e.kind !== 'join' && !own) { found.push([x, e]); continue; } if (e.kind !== 'join' && e.pts.length > 30) continue; if (!own) continue; relax(e.to, e.kind === 'join' ? 0.5 : NET.plen([[N[v].x, N[v].y]].concat(e.pts, [[N[to].x, N[to].y]])) * (e.kind === 'turn' ? 1.1 : 1), e); }
  }
  return { dist, from, found };
}
function trace(from, x) { const st = []; while (from.has(x)) { const [p, e] = from.get(x); st.push([x, e]); x = p; } st.push([x, null]); st.reverse(); return st; }
function geometry(steps) { const p = []; for (const [y, e] of steps) { if (e && e.pts) for (const q of e.pts) p.push([q[0], q[1]]); const n = N[y >> 1]; p.push([n.x, n.y]); } return p; }
function onward(v, s, len) {   // along the line from a state, across joins; null when the line ends
  let left = len, x = v * 2 + s; const st = []; const seen = new Set([x]);
  for (let i = 0; i < 400 && left > 0; i++) {
    const a = x >> 1, sa = x & 1, c = chains[N[a].c], kk = N[a].k + (sa === 0 ? 1 : -1); let nx = null;
    if (kk >= 0 && kk < c.p.length) nx = (c.first + kk) * 2 + sa;
    else { const ex = extra.get(x); if (ex) for (const e of ex) if (e.kind === 'join' && !seen.has(e.to)) { nx = e.to; break; } }
    if (nx === null) return { st, left, end: x }; seen.add(nx); left -= Math.hypot(N[nx >> 1].x - N[a].x, N[nx >> 1].y - N[a].y); st.push([nx, null]); x = nx;
  }
  return { st, left: Math.min(0, left), end: x };
}
function bez(A, tA, B, tB) {
  const ch = Math.hypot(B[0] - A[0], B[1] - A[1]), turn = Math.acos(Math.max(-1, Math.min(1, tA[0] * tB[0] + tA[1] * tB[1]))), R = ch / (2 * Math.sin(turn / 2) || 1e-6), h = turn < 0.05 ? ch / 3 : Math.min(ch, 4 / 3 * Math.tan(turn / 4) * R);
  const n = Math.max(8, Math.ceil(ch * 1.2)), p = [], P1 = [A[0] + tA[0] * h, A[1] + tA[1] * h], P2 = [B[0] - tB[0] * h, B[1] - tB[1] * h];
  for (let q = 0; q <= n; q++) { const u = q / n, w = 1 - u; p.push([w * w * w * A[0] + 3 * w * w * u * P1[0] + 3 * w * u * u * P2[0] + u * u * u * B[0], w * w * w * A[1] + 3 * w * w * u * P1[1] + 3 * w * u * u * P2[1] + u * u * u * B[1]]); }
  return p;
}
// ways to push back: { pts (from the stand), end [x,y], starts: states the taxi begins from, pre: points before the first state }
function pushOptions(st) {
  const opts = [], sOut = 1 - st.sIn, { dist, from, found } = localPaths(st.v, sOut, 260);
  for (const [x, e] of found) {
    const steps = trace(from, x).concat([[e.to, e]]), o = onward(e.to >> 1, e.to & 1, 10); if (o.left > 0.01) continue;
    const pts = geometry(steps.concat(o.st)), end = o.end; if (NET.plen(pts) > 260) continue;
    opts.push({ pts, starts: [[end >> 1, 1 - (end & 1)]], pre: [], len: NET.plen(pts), kind: e.kind });
  }
  // onto the straight extension of a taxilane whose paint ends near here
  for (const LE of ends) {
    const leadState = LE.v * 2 + LE.s; if (!dist.has(leadState) || !(isLeadIn(LE.c) || LE.c === N[st.v].c)) continue;
    const leadEnd = LE.v, lead = chains[LE.c], sO = LE.s, tl = LE.t, a = N[leadEnd];
    for (const E of ends) {
      if (E.c === lead.id || isLeadIn(E.c) || chains[E.c].len < 20 || chains[E.c].virtual) continue; const b = N[E.v]; if (Math.hypot(b.x - a.x, b.y - a.y) > 70) continue;
      const cr = tl[0] * E.t[1] - tl[1] * E.t[0]; if (Math.abs(cr) < 0.2) continue;
      const dx = b.x - a.x, dy = b.y - a.y, la = (dx * E.t[1] - dy * E.t[0]) / cr, lb = (dx * tl[1] - dy * tl[0]) / cr; if (la < -20 || la > 60 || lb < -20 || lb > 60) continue;
      const X = [a.x + tl[0] * la, a.y + tl[1] * la];
      // not across another taxilane on the way there
      let crosses = false; for (let q = 1; q < la - 2 && !crosses; q++) near(a.x + tl[0] * q, a.y + tl[1] * q, 0.7, w => { const o = chains[N[w].c]; if (o.centre && !o.virtual && N[w].c !== E.c && !isLeadIn(N[w].c) && o.len > 30 && Math.abs(N[w].tx * tl[1] - N[w].ty * tl[0]) > 0.26) crosses = true; });
      if (crosses) continue;
      for (let d = 14; d <= 60; d += 2) {
        let A, tA, pre = 0, q0 = leadEnd;
        if (d <= la) { A = [X[0] - tl[0] * d, X[1] - tl[1] * d]; tA = tl; pre = la - d; } else { const k = N[leadEnd].k + (sO === 0 ? -1 : 1) * Math.round(d - la); if (k < 0 || k >= lead.p.length) continue; q0 = lead.first + k; if (!dist.has(q0 * 2 + sO)) continue; A = [N[q0].x, N[q0].y]; tA = tan(q0, sO); }
        const B = [X[0] + E.t[0] * d, X[1] + E.t[1] * d], p = bez(A, tA, B, E.t); if (NET.minRadius(p) < 15) continue;
        const endP = [B[0] + E.t[0] * 10, B[1] + E.t[1] * 10]; if (!NET.widePath(p.concat([endP])) || !net.freeOfEdges(p)) continue;
        const pts = geometry(trace(from, q0 * 2 + sO)); if (pre > 1) for (let q = 1; q < pre; q++) pts.push([a.x + tl[0] * q, a.y + tl[1] * q]);
        const all = pts.concat(p.slice(1), [endP]), ext = [], D = Math.hypot(endP[0] - b.x, endP[1] - b.y); for (let q = 0; q < D; q++) ext.push([endP[0] - E.t[0] * q, endP[1] - E.t[1] * q]);
        opts.push({ pts: all, starts: [[E.v, 1 - E.s]], pre: ext, len: NET.plen(all), kind: 'extension' }); break;
      }
    }
  }
  return opts;
}

// ---------------------------------------------------------------- routes
const goalOf = r => { const n = r.pts.length; const run = i => { for (let k = 0; k < 8; k++) { const j = i - k; if (j >= 0 && !(r.off[j] < 0.8)) return false; } return true; }; for (let i = n - 1; i >= 0; i--) { if (!run(i)) continue; const a = anchor(r, i); if (a) return { i, a }; } return null; };
const startOf = r => { const n = r.pts.length; const run = i => { for (let k = 0; k < 8; k++) { const j = i + k; if (j < n && !(r.off[j] < 0.8)) return false; } return true; }; for (let i = 0; i < n; i++) { if (!run(i)) continue; const a = anchor(r, i); if (a) return { i, a }; } return null; };
function lineup(r, g) {
  const n = r.pts.length, e = r.pts[n - 1], b = r.pts[n - 61], l = Math.hypot(e[0] - b[0], e[1] - b[1]), rd = [(e[0] - b[0]) / l, (e[1] - b[1]) / l];
  const old = r.pts.slice(g.i + 1).map(p => [p[0], p[1]]), [v, s] = g.a[0], o = onward(v, s, 120);
  const st = [[v * 2 + s, null]].concat(o.st); let k = st.length - 1;
  // as far along the paint as it stays off the runway centre line
  const side = x => (N[x >> 1].x - e[0]) * -rd[1] + (N[x >> 1].y - e[1]) * rd[0];
  const s0 = side(st[0][0]); for (let i = 0; i < st.length; i++) if (Math.abs(side(st[i][0])) < 4 || side(st[i][0]) * s0 < 0) {
    // the paint itself comes onto the runway centre line: along it, the last metres onto the centre line
    const j = Math.max(0, i - 1), x = st[j][0], q = N[x >> 1], T = tan(x >> 1, x & 1), al = (q.x - e[0]) * rd[0] + (q.y - e[1]) * rd[1];
    for (const f of [20, 25, 30, 40]) {
      const B = [e[0] + rd[0] * (al + f), e[1] + rd[1] * (al + f)], p = bez([q.x, q.y], T, B, rd); if (NET.minRadius(p) < 30 || -(al + f) < 62) continue;
      const pts = geometry(st.slice(1, j + 1)).concat(p.slice(1)); for (let d = 1; d < -(al + f); d++) pts.push([B[0] + rd[0] * d, B[1] + rd[1] * d]); pts.push([e[0], e[1]]); return pts;
    }
    return old;
  }
  for (; k >= 0; k -= 1) {
    const x = st[k][0], q = N[x >> 1], T = tan(x >> 1, x & 1), cr = T[0] * rd[1] - T[1] * rd[0]; if (Math.abs(cr) < 0.3) continue;
    const dx = e[0] - q.x, dy = e[1] - q.y, a = (dx * rd[1] - dy * rd[0]) / cr; if (a < 10 || a > 90) continue;
    const X = [q.x + T[0] * a, q.y + T[1] * a], B = [X[0] + rd[0] * a, X[1] + rd[1] * a], p = bez([q.x, q.y], T, B, rd); if (NET.minRadius(p) < 25) continue;
    const toEnd = (e[0] - B[0]) * rd[0] + (e[1] - B[1]) * rd[1]; if (toEnd < 62) continue;
    const pts = geometry(st.slice(1, k + 1)).concat(p.slice(1)); for (let d = 1; d < toEnd; d++) pts.push([B[0] + rd[0] * d, B[1] + rd[1] * d]); pts.push([e[0], e[1]]);
    return pts;
  }
  return old;
}
// off the runway: straight down its centre line, and from there over 80 m without a kink onto the painted lead-off line beside it
function leadOff(r, s, q) {
  const b = r.pts[0], e = r.pts[Math.min(60, r.pts.length - 1)], l = Math.hypot(e[0] - b[0], e[1] - b[1]), rd = [(e[0] - b[0]) / l, (e[1] - b[1]) / l], nr = [-rd[1], rd[0]];
  const at = p => [(p[0] - b[0]) * rd[0] + (p[1] - b[1]) * rd[1], (p[0] - b[0]) * nr[0] + (p[1] - b[1]) * nr[1]];
  let x = q.first, back = [];   // the paint before the first point, as long as it runs beside the centre line
  for (const st of onward(x >> 1, 1 - (x & 1), 150).st) { const n = N[st[0] >> 1], T = tan(st[0] >> 1, 1 - (st[0] & 1)); if (Math.abs(at([n.x, n.y])[1]) > 2.5 || T[0] * rd[0] + T[1] * rd[1] < 0.99) break; back.push([n.x, n.y]); }
  const line = back.reverse().concat(q.pts), s0 = at(line[0])[0]; if (s0 < 5 || Math.abs(at(line[0])[1]) > 3) return null;
  const out = []; for (let d = 0; d < s0; d++) out.push([b[0] + rd[0] * d, b[1] + rd[1] * d]);
  let past = false;
  for (const p of line) { const [a, y] = at(p); past = past || a > s0 + 80; if (past) { out.push(p); continue; } const u = Math.max(0, Math.min(1, (a - s0) / 80)), k = u * u * u * (10 - 15 * u + 6 * u * u); out.push(u < 1 ? [b[0] + rd[0] * a + nr[0] * y * k, b[1] + rd[1] * a + nr[1] * y * k] : p); }
  return out;
}
const result = new Map(); const problems = [];
const gates = Object.keys(stands);
for (const ref of gates) {
  const st = stands[ref]; if (st.v === undefined) continue;
  const mine = routes.filter(r => r.tag.startsWith('Route.EHAM.' + ref + '.') && !/Arr22$/.test(r.tag));   // none on runway 22
  // departure: the pushback that gives the best taxi
  const deps = mine.filter(r => /\.Rwy/.test(r.tag)), main = deps.find(r => !/\.Alt\./.test(r.tag)), push = mine.find(r => /Pushback/.test(r.tag));
  if (main && push) {
    const g = goalOf(main), opts = pushOptions(st); let best = null;
    const oe = push.pts[push.pts.length - 1];
    for (const o of opts) {
      if (process.env.DBG && process.env.DBG.split(',').includes(ref)) console.log(ref, o.kind, o.len.toFixed(0), 'end', o.pts[o.pts.length - 1].map(v => v.toFixed(0)).join(','), 'starts', o.starts.map(q => N[q[0]].x.toFixed(0) + ',' + N[q[0]].y.toFixed(0)).join(' '));
      const q = solve(main, o.starts, g.a, { maxD: 120 }); if (!q) continue;
      const e = o.pts[o.pts.length - 1], cost = q.cost + o.len * 1.6 + (o.kind === 'extension' ? 40 : 0) + (o.kind === 'gap' ? 30 : 0) + (o.kind !== 'extension' && chains[N[o.starts[0][0]].c].virtual ? 80 : 0);
      o.cost = cost; o.away = Math.hypot(e[0] - oe[0], e[1] - oe[1]);
      if (process.env.DBG && process.env.DBG.split(',').includes(ref)) console.log('    cost', cost.toFixed(0), 'taxi', q.cost.toFixed(0), 'away', o.away.toFixed(0)); if (!best || cost < best.cost) best = { o, q, cost };
    }
    if (!best) problems.push('no pushback for ' + ref + ' (' + opts.length + ' options)');
    else {
      result.set(push.tag, { pts: best.o.pts, r: push, push: true });
      const add = (r, q, gg) => result.set(r.tag, { pts: best.o.pre.concat(q.pts, lineup(r, gg)), r, tailFrom: best.o.pre.length + q.pts.length, links: q.links, headTo: 0 });
      add(main, best.q, g);
      for (const r of deps) if (r !== main) { const gg = goalOf(r), q = solve(r, best.o.starts, gg.a, { maxD: 120 }); if (q) add(r, q, gg); else problems.push('no path ' + r.tag); }
      st.pushInfo = `${best.o.kind} ${best.o.len.toFixed(0)} m (was ${push.len.toFixed(0)}), end ${best.o.away.toFixed(0)} m from the old end, options ${opts.length}`;
      const p = result.get(push.tag).pts, a = p[p.length - 1], b = p[Math.max(0, p.length - 6)]; const face = Math.atan2(b[1] - a[1], b[0] - a[0]) / D2R;   // the nose points back the way it came
      const of = push.pts, oa = of[of.length - 1], ob = of[Math.max(0, of.length - 4)]; let df = face - Math.atan2(ob[1] - oa[1], ob[0] - oa[0]) / D2R; while (df > 180) df -= 360; while (df < -180) df += 360;
      st.face = face; st.faceChange = df;
    }
  }
  // arrivals: nose-in onto the stand
  for (const r of mine.filter(r => /\.Arr/.test(r.tag))) {
    const s = startOf(r); if (!s) { problems.push('no start ' + r.tag); continue; }
    const goals = []; near(st.x, st.y, 1.0, v => { const c = chains[N[v].c]; if (!c.centre) return; const t = tan(st.v, st.sIn), dot = N[v].tx * t[0] + N[v].ty * t[1]; if (Math.abs(dot) < 0.97) return; goals.push([v, dot > 0 ? 0 : 1]); });
    let q = solve(r, s.a, goals.concat([[st.v, st.sIn]]), { maxD: 120 }); if (!q) q = solve(r, s.a, goals.concat([[st.v, st.sIn]]), { maxD: 320 });
    if (!q) { problems.push('no path ' + r.tag); continue; }
    const off = leadOff(r, s, q) || r.pts.slice(0, s.i).map(p => [p[0], p[1]]).concat(q.pts);
    result.set(r.tag, { pts: off.concat([[st.x, st.y]]), r, links: q.links });
  }
}
console.log('routes', result.size, 'of', routes.length, 'problems', problems.length); problems.forEach(p => console.log('  ', p));
for (const ref of gates) if (stands[ref].pushInfo) console.log(ref.padEnd(4), 'push', stands[ref].pushInfo, 'face change', stands[ref].faceChange.toFixed(0));

// ---------------------------------------------------------------- smooth
function resample(p, step) { return require('./graph.js').resample(p, step); }
function smooth(points, minR, keepStart, keepEnd) {
  const K = 3, n0 = resample(points, 1.0), n = n0.length; if (n < 10) return { P: n0, worst: 1e9 };
  // where the path goes over from one painted line to another they can lie a few decimetres apart: a quadratic fit over 11 m
  // takes that out and leaves straights and curves where they are
  let P = n0;
  for (let pass = 0; pass < 3; pass++) { const Q = P.map(p => [p[0], p[1]]); for (let i = 5; i < n - 5; i++) { let x = 0, y = 0; for (let k = -5; k <= 5; k++) { const w = 3 * (89 - 5 * k * k) / 1287; x += w * P[i + k][0]; y += w * P[i + k][1]; } Q[i] = [x, y]; } P = Q; }
  const radius = i => { const a = P[i - K], b = P[i], c = P[i + K], cr = Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])); return cr < 1e-5 ? 1e9 : Math.hypot(b[0] - a[0], b[1] - a[1]) * Math.hypot(c[0] - b[0], c[1] - b[1]) * Math.hypot(c[0] - a[0], c[1] - a[1]) / (2 * cr); };
  const lo0 = Math.max(2, keepStart), hi0 = Math.min(n - 3, n - 1 - keepEnd);
  for (let round = 0; round < 20; round++) {
    const reg = []; for (let i = K; i < n - K; i++) if (radius(i) < minR) { if (reg.length && i - reg[reg.length - 1][1] <= 12) reg[reg.length - 1][1] = i; else reg.push([i, i]); }
    if (!reg.length) break;
    for (const [a, b] of reg) {
      let lo = Math.max(K, a - 6), hi = Math.min(n - K - 1, b + 6);
      for (let pass = 0; pass < 20000 && lo <= hi; pass++) {
        let nl = n, nh = -1;
        for (let i = lo; i <= hi; i++) { if (radius(i) >= minR) continue; for (let j = Math.max(lo0, i - K); j <= Math.min(hi0, i + K); j++) { P[j] = [(P[j][0] + (P[j - 1][0] + P[j + 1][0]) / 2) / 2, (P[j][1] + (P[j - 1][1] + P[j + 1][1]) / 2) / 2]; } nl = Math.min(nl, i); nh = Math.max(nh, i); }
        lo = Math.max(K, nl - 6); hi = Math.min(n - K - 1, nh + 6);
      }
    }
  }
  let worst = 1e9; for (let i = K; i < n - K; i++) worst = Math.min(worst, radius(i));
  return { P, worst };
}
let tight = 0; const out = [];
if (process.env.RAW) for (const [tag, o] of result) if (tag.includes(process.env.RAW)) L.fs.writeFileSync(L.dir + "raw.txt", o.pts.map(p => p.join(" ")).join(String.fromCharCode(10)));
for (const [tag, o] of result) {
  const push = !!o.push, s = smooth(o.pts, push ? 14 : 21, 0, /\.Rwy/.test(tag) ? 60 : 0);
  o.P = s.P; o.worst = s.worst; if (s.worst < (push ? 12 : 18)) { tight++; console.log('tight', tag, s.worst.toFixed(1)); }
  out.push('R ' + tag + ' ' + (NET.plen(s.P) * 100).toFixed(0) + ' 0 x\n' + s.P.map(p => (p[0] * 100).toFixed(1) + ' ' + (p[1] * 100).toFixed(1) + ' 0').join('\n'));
}
L.fs.writeFileSync(L.dir + 'new_routes.txt', out.join('\n') + '\n');
function hermite(p, i) {   // the spline between points i and i + 1, as SetRoutePoints makes it
  const tangent = k => { const In = k > 0 ? [p[k][0] - p[k - 1][0], p[k][1] - p[k - 1][1]] : null, Out = k < p.length - 1 ? [p[k + 1][0] - p[k][0], p[k + 1][1] - p[k][1]] : null, li = In ? Math.hypot(...In) : 0, lo = Out ? Math.hypot(...Out) : 0; let dx = (In ? In[0] / li : 0) + (Out ? Out[0] / lo : 0), dy = (In ? In[1] / li : 0) + (Out ? Out[1] / lo : 0); const l = Math.hypot(dx, dy) || 1, m = Math.min(In ? li : lo, Out ? lo : li, 50); return [dx / l * m, dy / l * m]; };
  const A = p[i], B = p[i + 1], T0 = tangent(i), T1 = tangent(i + 1), n = Math.max(4, Math.ceil(Math.hypot(B[0] - A[0], B[1] - A[1]))), out = [];
  for (let q = 0; q < n; q++) { const u = q / n, u2 = u * u, u3 = u2 * u, h0 = 2 * u3 - 3 * u2 + 1, h1 = u3 - 2 * u2 + u, h2 = -2 * u3 + 3 * u2, h3 = u3 - u2; out.push([h0 * A[0] + h1 * T0[0] + h2 * B[0] + h3 * T1[0], h0 * A[1] + h1 * T0[1] + h2 * B[1] + h3 * T1[1]]); }
  return out;
}
let fitBad = 0;
function fit(P, minR) {
  const first = fitOnce(P, minR, false); if (first.worst >= minR) return first.p;
  const even = fitOnce(P, minR, true); const best = even.worst > first.worst ? even : first;
  if (best.worst < minR) { fitBad++; console.log('spline tight', best.worst.toFixed(1), P[0].map(v => v.toFixed(0)).join(','), P[P.length - 1].map(v => v.toFixed(0)).join(',')); }
  return best.p;
}
function fitOnce(P, minR, even) {
  // indices of P that are kept: first by the line itself, then halved where the spline is not good enough
  const kept = new Set(); { const key = new Map(P.map((q, i) => [q, i])); for (const q of dp(P, even ? 0.25 : 0.08)) kept.add(key.get(q)); }
  if (even) {
    const bend = new Set(); for (let i = 5; i < P.length - 5; i += 5) { const a = P[i - 5], b = P[i], c = P[i + 5], cr = Math.abs((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])) / 25; if (cr > 0.012) for (const j of [i - 5, i, i + 5]) if (j > 0 && j < P.length - 1) bend.add(j); }
    for (const j of [...kept]) if (j > 0 && j < P.length - 1 && [-4, -3, -2, -1, 1, 2, 3, 4].some(d => bend.has(j + d))) kept.delete(j);
    for (const j of bend) kept.add(j);
  }
  let bad = [];
  for (let round = 0; round < (even ? 1 : 8); round++) {
    const idx = [...kept].sort((x, y) => x - y), p = idx.map(i => P[i]), add = []; bad = [];
    const line = [], seg = []; for (let i = 0; i < p.length - 1; i++) for (const q of hermite(p, i)) { line.push(q); seg.push(i); }
    const s = resample(line.concat([p[p.length - 1]]), 1.0);
    let k = 0; const segOf = q => { let best = 1e9, bi = 0; for (let j = Math.max(0, k - 40); j < Math.min(line.length, k + 60); j++) { const d = Math.hypot(line[j][0] - q[0], line[j][1] - q[1]); if (d < best) { best = d; bi = j; } } k = bi; return seg[bi]; };
    for (let i = 3; i < s.length - 3; i++) { const sg = segOf(s[i]), a = s[i - 3], b = s[i], c = s[i + 3], cr = Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])); const r = cr < 1e-5 ? 1e9 : Math.hypot(b[0] - a[0], b[1] - a[1]) * Math.hypot(c[0] - b[0], c[1] - b[1]) * Math.hypot(c[0] - a[0], c[1] - a[1]) / (2 * cr); if (r < minR && i > 10 && i < s.length - 10) { bad.push(r); for (let j = Math.max(0, sg - 1); j <= Math.min(p.length - 2, sg + 1); j++) add.push(j); } }
    if (!add.length) break;
    // even points every 4 m round a bend that comes out too tight
    for (const j of new Set(add)) { const lo = idx[j], hi = idx[j + 1], n = Math.round((hi - lo) / 4); if (n > 1) for (let q = 1; q < n; q++) kept.add(Math.round(lo + (hi - lo) * q / n)); else if (round > 2 && hi - lo < 4 && j > 0 && j < p.length - 2) kept.delete(idx[j]); }
  }
  return { p: [...kept].sort((x, y) => x - y).map(i => P[i]), worst: bad.length ? Math.min(...bad) : 1e9 };
}
function dp(p, tol) {
  const keep = new Uint8Array(p.length); keep[0] = keep[p.length - 1] = 1; const st = [[0, p.length - 1]];
  while (st.length) {
    const [a, b] = st.pop(); let w = 0, wi = -1; const dx = p[b][0] - p[a][0], dy = p[b][1] - p[a][1], l = dx * dx + dy * dy || 1e-9;
    for (let i = a + 1; i < b; i++) { let u = ((p[i][0] - p[a][0]) * dx + (p[i][1] - p[a][1]) * dy) / l; u = Math.max(0, Math.min(1, u)); const d = Math.hypot(p[i][0] - p[a][0] - dx * u, p[i][1] - p[a][1] - dy * u); if (d > w) { w = d; wi = i; } }
    if (wi >= 0 && w > tol) { keep[wi] = 1; st.push([a, wi], [wi, b]); }
  }
  return p.filter((q, i) => keep[i]);
}
{
  const face = a => { while (a > 180) a -= 360; while (a < -180) a += 360; return Math.abs(a) <= 45 ? 'East' : Math.abs(a) >= 135 ? 'West' : a > 0 ? 'South' : 'North'; };
  const F = ['# Taxi routes of EHAM laid exactly over the painted yellow lines (cm, map coordinates). Made by Tools/Lines, put into the map by ATC.ApplyLines.'];
  for (const s of Object.values(stands)) F.push('stand Stand.EHAM.' + s.ref + ' ' + (s.x * 100).toFixed(1) + ' ' + (s.y * 100).toFixed(1) + ' ' + s.yaw.toFixed(3) + ' ' + (s.face === undefined ? '-' : face(s.face)));
  let n = 0; for (const [tag, o] of result) { const p = fit(o.P, /Pushback/.test(tag) ? 11 : 19.5); n += p.length; F.push('route ' + tag + ' ' + p.length); for (const q of p) F.push((q[0] * 100).toFixed(0) + ' ' + (q[1] * 100).toFixed(0)); }
  L.fs.writeFileSync(__dirname + '/../../Data/Lines/EHAM.txt', F.join(String.fromCharCode(10)) + String.fromCharCode(10)); console.log('EHAM.txt', n, 'points, splines still too tight somewhere:', fitBad); for (const s of Object.values(stands)) if (s.face !== undefined) s.word = face(s.face);
}
L.fs.writeFileSync(L.dir + 'new_stands.json', JSON.stringify(stands, null, 1));
console.log('written', out.length, 'tight', tight);
