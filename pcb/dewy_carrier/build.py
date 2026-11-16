"""Places the parts, routes the board and writes dewy_carrier.kicad_pcb. Run: python3 build.py"""
import sys, json, math, random
sys.path.insert(0, '.')
import design as d, router as R

PAD_R, PAD_DRILL = 0.85, 1.0
W = {'+5V': 0.6, '+3V3': 0.4, 'GND': 0.4}
SIG_W = 0.3

# ---------------- collect pads: (ref, name, net or None, x, y, radius, drill) ----------------
pads = []
for name, net, x, y in d.j1_pads(): pads.append(('J1', name, net, x, y, PAD_R, PAD_DRILL))
for name, net, x, y, row in d.esp_pads(): pads.append(('U1', name, net, x, y, PAD_R, PAD_DRILL))
for h in d.HEADERS:
    for i, (pn, net) in enumerate(zip(h['pins'], h['nets'])): pads.append((h['ref'], pn, net, h['x'] + i * 2.54, h['y'], PAD_R, PAD_DRILL))
for c in d.CAPS:
    pads.append((c['ref'], '+', '+5V', c['x'], c['y'], 0.9, 0.9)); pads.append((c['ref'], '-', 'GND', c['x'] + 3.81, c['y'], 0.9, 0.9))
t = d.TERMINAL
pads.append((t['ref'], '+5V', '+5V', t['x'], t['y'], 1.3, 1.3)); pads.append((t['ref'], 'GND', 'GND', t['x'] + 5.08, t['y'], 1.3, 1.3))

names = sorted({p[2] for p in pads if p[2]})
netid = {n: k + 1 for k, n in enumerate(names)}
def build_board():
    b = R.Board(clearance=0.25); fake = 1000
    for ref, name, net, x, y, r, dr in pads:
        if net: b.pad(netid[net], x, y, r)
        else: fake += 1; b.pad(fake, x, y, r)
    b.block_edge(0.75)
    for x, y in d.HOLES: b.block_circle(x, y, 3.5)
    k = d.ANTENNA_KEEPOUT; b.block_rect(k[0] - 0.3, k[1] - 0.3, k[2] + 0.3, k[3] + 0.3)
    return b

def attempt(seed, gnd_first=()):
    rnd = random.Random(seed); b = build_board(); out = {}
    def span(n):
        pts = [(p[3], p[4]) for p in pads if p[2] == n]; xs = [q[0] for q in pts]; ys = [q[1] for q in pts]
        return (max(xs) - min(xs)) + (max(ys) - min(ys))
    def order_pts(n):
        pts = [(p[3], p[4]) for p in pads if p[2] == n]
        pts.sort(key=lambda q: (q[0], q[1]))
        if n == 'GND':      # grow the ground tree from the 5V IN terminal, which sits in open space
            g0 = (d.TERMINAL['x'] + 5.08, d.TERMINAL['y']); pts.sort(key=lambda q: math.hypot(q[0] - g0[0], q[1] - g0[1]))
        return pts
    phases = [(['+5V'], W['+5V']), (['+3V3'], W['+3V3']), (['_GNDSTUBS'], W['GND']),
              (sorted([n for n in names if n not in W], key=lambda n: span(n) + rnd.random() * (0 if seed == 0 else 25)), SIG_W),
              (['GND'], W['GND'])]
    failed = []
    for nets, w in phases:
        own = b.ownmap(w / 2)
        for n in nets:
            if n == '_GNDSTUBS':
                gp = [(p[3], p[4]) for p in pads if p[2] == 'GND']
                for k, q in enumerate(gnd_first):       # hard-to-reach ground pads get a short link to their nearest ground pad first
                    near = min((g for g in gp if g != tuple(q)), key=lambda g: math.hypot(g[0] - q[0], g[1] - q[1]))
                    r = R.route_net(b, own, netid['GND'], [tuple(q), near], w / 2)
                    if r: out['_stub%d' % k] = dict(width=w, segs=r[0], vias=r[1], net='GND')
                continue
            pts = order_pts(n)
            if len(pts) < 2: continue
            missed = []
            r = R.route_net(b, own, netid[n], pts, w / 2, partial=(n == 'GND'), missed=missed)
            if r is None: failed.append(n); continue
            if missed: out.setdefault('_gnd_missed', []).extend(missed)
            out[n] = dict(width=w, segs=r[0], vias=r[1])
    return out, failed

if __name__ == '__main__':
    best = None
    clean = 0
    for seed in ([int(sys.argv[1])] if len(sys.argv) > 1 else range(40)):
        gf = []
        for _ in range(4):
            out, failed = attempt(seed, gf)
            miss = [tuple(q) for q in out.get('_gnd_missed', [])]
            if not miss: break
            gf = gf + [q for q in miss if q not in gf]
        rts = [v for k, v in out.items() if k != '_gnd_missed']; vias = sum(len(v['vias']) for v in rts); length = sum(math.hypot(s[3] - s[1], s[4] - s[2]) for v in rts for s in v['segs'])
        print(f'seed {seed}: failed {failed}, gnd pads left to the pour {[(round(q[0],1), round(q[1],1)) for q in out.get("_gnd_missed", [])]}, vias {vias}, track length {length:.0f} mm')
        score = (len(failed), len(out.get('_gnd_missed', [])), vias * 6 + length)
        if best is None or score < best[0]: best = (score, out, failed, seed)
        clean += (not failed and not out.get('_gnd_missed'))
        if clean >= 4: break
    _, out, failed, seed = best
    print('using seed', seed, 'failed', failed)
    json.dump(dict(pads=pads, routes=out, netid=netid), open('routed.json', 'w'))
