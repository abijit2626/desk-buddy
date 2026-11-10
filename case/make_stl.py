"""Desk Buddy case v2 (prototype). Units mm.
Body coordinates: front face at z=0 (printed front-down), open back at z=D.
Seen from the FRONT, +x is on the viewer's LEFT (camera looks along +z).
"""
import numpy as np, trimesh
from shapely.geometry import box

def rr(w, h, r, cx=0, cy=0):
    return box(cx - w/2 + r, cy - h/2 + r, cx + w/2 - r, cy + h/2 - r).buffer(r, resolution=24)
def ext(poly, z0, z1):
    m = trimesh.creation.extrude_polygon(poly, z1 - z0); m.apply_translation([0, 0, z0]); return m
def cyl_y(x, z, y0, y1, r):
    m = trimesh.creation.cylinder(radius=r, height=y1 - y0, sections=32)
    m.apply_transform(trimesh.transformations.rotation_matrix(np.pi/2, [1, 0, 0])); m.apply_translation([x, (y0 + y1)/2, z]); return m
def cyl_z(x, y, z0, z1, r, sections=32):
    m = trimesh.creation.cylinder(radius=r, height=z1 - z0, sections=sections); m.apply_translation([x, y, (z0 + z1)/2]); return m
def union(ms): return trimesh.boolean.union(ms, engine='manifold')
def diff(a, bs): return trimesh.boolean.difference([a] + list(bs), engine='manifold')

W, H, D = 118.0, 98.0, 70.0
WALL = 2.5
FRONT = 3.0                      # front wall thickness (screen glass sits right behind it)
WY = 2.0                         # screen window centre height
WIN_W, WIN_H = 67.0, 51.0        # window, a little larger than a 3.2" ILI9341 active area (about 65 x 49)
# The SmartElex board's exact outline isn't published, so the holder is a roomy fence:
# boards up to about 95 x 58 mm fit with room to slide the picture into the middle of the window.
PCB_X = 0.0
CR_IN_W, CR_IN_H = 101.0, 62.0
CR_T = 2.5
BOSS_R = 4.5
BOSS = [(-52.5, -42.5), (52.5, -42.5), (-52.5, 42.5), (52.5, 42.5)]
BOSS_Z0 = D - 18                 # short posts at the back, so the floor corners stay free for boards
PIN_XY = [(-45.0, WY), (45.0, WY)]   # visor locating pins (symmetric, fits either way round)
CHARGE = (45.0, 28.0)            # USB-C panel-mount extension, body coords (seen from the BACK it is on the right)

# ---------------- body ----------------
body = ext(rr(W, H, 10), 0, D)
body = diff(body, [ext(rr(W - 2*WALL, H - 2*WALL, 7.5), FRONT, D + 1)])

rim = ext(rr(CR_IN_W + 2*CR_T, CR_IN_H + 2*CR_T, 2, PCB_X, WY).difference(rr(CR_IN_W, CR_IN_H, 1, PCB_X, WY)), FRONT - 0.2, FRONT + 5)
posts = []
for x, y in BOSS:
    posts.append(cyl_z(x, y, BOSS_Z0, D, BOSS_R))
    cone = trimesh.creation.cone(radius=BOSS_R, height=BOSS_R + 0.5, sections=32)   # 45° support under each post
    cone.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
    cone.apply_translation([x, y, BOSS_Z0 + 0.01])
    posts.append(cone)
body = union([body, rim] + posts)

cuts = [ext(rr(WIN_W, WIN_H, 3, 0, WY), -1, FRONT + 0.5)]          # screen window
for x, y in PIN_XY:                                                    # visor pin holes, 2.2 deep
    cuts.append(cyl_z(x, y, -1, 2.2, 1.25))
for i in range(10):                                                    # 10 dots under the visor
    cuts.append(cyl_z(-18 + i*4, -42.5, -1, FRONT + 0.5, 0.8))
cuts.append(cyl_y(0, 17, H/2 - WALL - 1, H/2 + 1, 1.5))               # mic hole, top, clear of the screen cradle
for i in range(12):                                                    # speaker grille in the floor
    for j in range(4):
        cuts.append(cyl_y(-27.5 + i*5, 14 + j*5, -H/2 - 1, -H/2 + WALL + 1, 1.25))
for x, z in [(-45, 12), (45, 12), (-45, 60), (45, 60)]:                # seats for stick-on rubber feet
    cuts.append(cyl_y(x, z, -H/2 - 1, -H/2 + 0.6, 5.1))
for x, y in BOSS:                                                      # M3 self-tap pilot holes
    cuts.append(cyl_z(x, y, D - 15, D + 1, 1.3))
body = diff(body, cuts)

# ---------------- visor (black) : front face on the bed, pins up ----------------
visor = ext(rr(106, 79, 10, 0, WY).difference(rr(WIN_W, WIN_H, 3, 0, WY)), 0, 1.5)
visor = union([visor] + [cyl_z(-x, y, 1.4, 3.1, 1.0, 24) for x, y in PIN_XY])   # mirrored because it flips onto the front

# ---------------- back panel : outer face on the bed, lip up ----------------
# lid local (x, y) = body (-x, y) because it flips over onto the back
lid = ext(rr(W, H, 10), 0, 3.0)
lip = ext(rr(W - 2*WALL - 1.6, H - 2*WALL - 1.6, 6.6).difference(rr(W - 2*WALL - 5.6, H - 2*WALL - 5.6, 4.6)), 2.9, 5.0)
lip = diff(lip, [cyl_z(x, y, 2.0, 6, BOSS_R + 0.6) for x, y in BOSS])
lid = union([lid, lip])
lcuts = []
for x, y in BOSS:                                                      # countersunk M3 holes
    prof = np.array([[0, -1], [4.4, -1], [1.7, 1.7], [1.7, 4.0], [0, 4.0]])
    c = trimesh.creation.revolve(prof, sections=48); c.apply_translation([x, y, 0]); lcuts.append(c)
lcuts.append(ext(rr(14, 8, 3, -CHARGE[0], CHARGE[1]), -1, 6))          # charging socket
for v in range(4):                                                     # vents behind the ESP32
    lcuts.append(ext(rr(26, 2, 0.9, 0, 20 + 5*v), -1, 6))
lid = diff(lid, lcuts)

if __name__ == '__main__':
    for name, m in [('deskbuddy_body', body), ('deskbuddy_visor', visor), ('deskbuddy_back_panel', lid)]:
        m.merge_vertices(); m.update_faces(m.nondegenerate_faces()); m.remove_unreferenced_vertices(); trimesh.repair.fix_normals(m)
        m.export(f'stl/{name}.stl')
        print(name, 'watertight', m.is_watertight, 'vol cm3 %.1f' % (m.volume/1000), 'size', np.round(m.extents, 1))
