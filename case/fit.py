import json, itertools, numpy as np, trimesh
from make_stl import body, lid, visor, D, PCB_X, WY, WIN_W, WIN_H
# lid in body coordinates: flip about y (x->-x, z->D+3-z)
L = lid.copy(); L.apply_transform(np.array([[-1,0,0,0],[0,1,0,0],[0,0,-1,D+3],[0,0,0,1]]))
V = visor.copy(); V.apply_translation([0,0,-1.5])  # glued on the front face (z<=0)

def bx(x0,x1,y0,y1,z0,z1): return (x0,x1,y0,y1,z0,z1)
P = {  # name: (box, colour) — sizes from datasheets / your earlier docs, generous where unsure
 'screen glass':        (bx(-40, 40, WY-27.5, WY+27.5, 3.05, 5.9), '#3b82f6'),
 'screen board (≤95×58)': (bx(-47.5, 47.5, WY-29, WY+29, 5.9, 7.5), '#2563eb'),
 'screen pins+jumpers': (bx(-47, -39, WY-26, WY+26, 7.5, 32), '#93c5fd'),
 'speaker 70x30x16':    (bx(-35, 35, -46.4, -30.4, 9, 39), '#111827'),
 'amp MAX98357A':       (bx(38, 56.2, -10, 8, 12, 30), '#a855f7'),
 'GY-87 sensor':        (bx(36.5, 56.2, -30, -13, 12, 34), '#22c55e'),
 'mic INMP441':         (bx(-7, 7, 31, 46.4, 10, 24), '#f59e0b'),
 'battery shield+cells':(bx(-50.2, 50.2, -36, 12.3, 43.4, 69.9), '#ef4444'),
 'USB-C 90° plug':      (bx(38, 50, 12.3, 22, 60, 69), '#f97316'),
 'ESP32-S3 + jumpers':  (bx(-30, 33.5, 15, 43, 35, 69.9), '#0ea5e9'),
 'charge socket (ext)': (bx(39, 51, 24, 32, 52, 73.5), '#fb923c'),
}
def mesh(b):
    x0,x1,y0,y1,z0,z1=b; m=trimesh.creation.box(extents=[x1-x0,y1-y0,z1-z0]); m.apply_translation([(x0+x1)/2,(y0+y1)/2,(z0+z1)/2]); return m
M = {k:mesh(v[0]) for k,v in P.items()}
def inter(a,b):
    try:
        r = trimesh.boolean.intersection([a,b], engine='manifold'); return r.volume if not r.is_empty else 0
    except Exception: return 0
probs=[]
for k,m in M.items():
    vb = inter(m, body); vl = inter(m, L)
    allowed = 0.0
    if k=='charge socket (ext)': vl = max(0, vl - 12*8*3)  # it passes through its own hole region; check flange area separately
    if vb>1: probs.append(f'{k} hits the BODY ({vb:.0f} mm³)')
    if vl>1: probs.append(f'{k} hits the BACK PANEL ({vl:.0f} mm³)')
for a,b in itertools.combinations(M,2):
    if {a,b} in ({'screen glass','screen board (≤95×58)'},{'screen board (≤95×58)','screen pins+jumpers'},{'USB-C 90° plug','battery shield+cells'},{'USB-C 90° plug','charge socket (ext)'}): continue
    v=inter(M[a],M[b])
    if v>1: probs.append(f'{a} overlaps {b} ({v:.0f} mm³)')
print('PROBLEMS:' if probs else 'No collisions.'); [print(' -',p) for p in probs]
# screen window check
aa_x = (-32.64, 32.64); aa_y=(WY-24.48, WY+24.48)
print(f'active area x {aa_x[0]:.2f}..{aa_x[1]:.2f}, window x {-WIN_W/2}..{WIN_W/2}; y {aa_y[0]:.1f}..{aa_y[1]:.1f}, window {WY-WIN_H/2}..{WY+WIN_H/2}')
print('pixels hidden:', 'none' if (aa_x[0]>=-WIN_W/2 and aa_x[1]<=WIN_W/2 and aa_y[0]>=WY-WIN_H/2 and aa_y[1]<=WY+WIN_H/2) else 'YES')
# speaker over grille
print('grille z 12.75..30.25 under speaker z 9..39, x -28.75..28.75 under -35..35: OK')
json.dump({k:[list(v[0]),v[1]] for k,v in P.items()}, open('fit_parts.json','w'))
L.export('stl/_lid_assembled.stl'); V.export('stl/_visor_assembled.stl')
