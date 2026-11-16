import sys; sys.path.insert(0, '.')
import design as d, numpy as np
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle, FancyBboxPatch, Polygon
fig, (ax, az) = plt.subplots(1, 2, figsize=(17, 8.2), gridspec_kw={'width_ratios': [1.25, 1]})
def rrect(a, x0, y0, x1, y1, r, **k): a.add_patch(FancyBboxPatch((x0 + r, y0 + r), x1 - x0 - 2*r, y1 - y0 - 2*r, boxstyle=f"round,pad={r}", **k))
# ---------- back view ----------
rrect(ax, -59, -49, 59, 49, 10, fc='#e5e7eb', ec='#374151', lw=1.5)
rrect(ax, -56.5, -46.5, 56.5, 46.5, 7.5, fc='white', ec='#374151', lw=1)
for x, y in [(-52.5, -42.5), (52.5, -42.5), (-52.5, 42.5), (52.5, 42.5)]: ax.add_patch(Circle((x, y), 4.5, fc='#d1d5db', ec='#374151'))
ax.add_patch(Rectangle((-50.95, -28.8), 96, 61.6, fc='none', ec='#9ca3af', lw=1, ls='--'))            # fence
ax.add_patch(Rectangle((-48.2, -26.05), 90.5, 56.1, fc='#dbeafe', ec='#60a5fa', lw=1))                 # screen board
ax.text(40, -25, 'screen board\n(behind)', color='#2563eb', fontsize=8, ha='right', va='bottom')
ax.add_patch(Rectangle((-35, -46.4), 70, 16, fc='#11182722', ec='#111827', lw=1)); ax.text(0, -39, 'speaker (on the floor)', ha='center', fontsize=8)
ax.add_patch(Rectangle((-7, 40.5), 14, 6, fc='#fde68a', ec='#b45309')); ax.text(0, 43.5, 'mic', ha='center', va='center', fontsize=7)
ax.add_patch(Rectangle((38, 24), 14, 8, fc='none', ec='#f97316', lw=1.2, ls=':')); ax.text(53.5, 33.2, 'USB socket\nin the lid', color='#c2410c', fontsize=7, ha='right')
rrect(ax, d.X0, d.Y0, d.X1, d.Y1, d.CORNER_R, fc='#16a34a33', ec='#15803d', lw=2)
for n, net, x, y in d.j1_pads(): ax.add_patch(Circle((x, y), 0.85, fc='#fbbf24' if net else '#d4d4d4', ec='#92400e', lw=.3))
ax.text(d.J1_OUTER_X + 1.27, 29.5, 'J1 screen socket\n(on the other face)', fontsize=7, ha='center')
ep = d.esp_pads()
for n, net, x, y, row in ep: ax.add_patch(Circle((x, y), 0.85, fc='#fbbf24' if net else '#d4d4d4', ec='#92400e', lw=.3))
xs = [p[2] for p in ep]
ax.add_patch(Rectangle((min(xs) - 3.2, d.ESP_Y_TOP - 25.4 - 1.25), max(xs) - min(xs) + 3.2 + 7.5, 27.9, fc='#0ea5e922', ec='#0369a1', lw=1.2))
ax.text(17, 17.5, 'ESP32-S3 dev board\nantenna →   ← USB', ha='center', va='center', fontsize=9, color='#0369a1')
ax.add_patch(Rectangle((-36, 13), 24, 10, fc='none', ec='#f97316', ls=':', lw=1.2)); ax.text(-24, 14, 'USB plug (flashing)', fontsize=7, color='#c2410c', ha='center')
for h in d.HEADERS:
    for i, (pn, net) in enumerate(zip(h['pins'], h['nets'])): ax.add_patch(Circle((h['x'] + i*2.54, h['y']), 0.85, fc='#fbbf24' if net else '#d4d4d4', ec='#92400e', lw=.3))
    if h['body']:
        bx0, by0, bx1, by1 = h['body']; ax.add_patch(Rectangle((h['x'] + bx0, h['y'] + by1), bx1 - bx0, by0 - by1, fc='#a855f722', ec='#7e22ce'))
        ax.text(h['x'] + (bx0 + bx1)/2, h['y'] + (by0 + by1)/2, h['title'], ha='center', va='center', fontsize=8, color='#6b21a8')
    else: ax.text(h['x'] + (len(h['pins']) - 1)*1.27, h['y'] + 2.2, h['title'], ha='center', fontsize=7)
for c in d.CAPS: ax.add_patch(Circle((c['x'] + 1.75, c['y']), 4, fc='#fca5a533', ec='#b91c1c')); ax.text(c['x'] + 1.75, c['y'], c['ref'], ha='center', va='center', fontsize=7)
t = d.TERMINAL; ax.add_patch(Rectangle((t['x'] - 2.54, t['y'] - 4.2), 10.16, 8.2, fc='#22c55e33', ec='#15803d')); ax.text(t['x'] + 2.54, t['y'], '5V IN', ha='center', va='center', fontsize=7)
for x, y in d.HOLES: ax.add_patch(Circle((x, y), 1.6, fc='white', ec='black')); ax.add_patch(Circle((x, y), 3.0, fc='none', ec='black', lw=.5, ls=':'))
k = d.ANTENNA_KEEPOUT; ax.add_patch(Rectangle((k[0], k[1]), k[2]-k[0], k[3]-k[1], fc='none', ec='#dc2626', hatch='///', lw=.5))
ax.set_xlim(-62, 62); ax.set_ylim(-52, 52); ax.set_aspect('equal'); ax.set_title('Seen from the back of the case (lid off). Green = the carrier board, 99.7 x 66 mm')
ax.set_xlabel('mm'); ax.grid(alpha=.2)
# ---------- side section (looking from the right side, front of the case at the left) ----------
az.add_patch(Rectangle((0, -49), 70, 98, fc='#e5e7eb', ec='#374151', lw=1.5)); az.add_patch(Rectangle((3, -46.5), 67, 93, fc='white', ec='#374151', lw=1))
az.add_patch(Rectangle((70, -49), 3, 98, fc='#d1d5db', ec='#374151')); az.text(71.5, 0, 'lid', rotation=90, ha='center', va='center', fontsize=8)
az.add_patch(Rectangle((3, -28.8), 5, 2.5, fc='#9ca3af')); az.add_patch(Rectangle((3, 30.3), 5, 2.5, fc='#9ca3af'))
az.add_patch(Rectangle((3.05, -25.5), 2.85, 55, fc='#93c5fd', ec='#2563eb')); az.add_patch(Rectangle((5.9, -26.05), 1.6, 56.1, fc='#2563eb'))
az.text(1.4, 2, 'screen', rotation=90, ha='center', va='center', fontsize=8)
az.add_patch(Rectangle((7.5, -22), 2.54, 48, fc='#111827')); az.add_patch(Rectangle((10.04, -22), 8.5, 48, fc='#4b5563')); az.text(14.3, 2, '40-pin socket', rotation=90, color='white', ha='center', va='center', fontsize=7)
zb = 18.54
az.add_patch(Rectangle((zb, d.Y0), 1.6, d.Y1 - d.Y0, fc='#16a34a', ec='#14532d')); az.text(zb + 0.8, -30.5, 'new board', ha='center', fontsize=7, color='#14532d')
az.add_patch(Rectangle((zb + 1.6, d.ESP_Y_TOP - 26.6), 2.54, 27.9, fc='#111827')); az.add_patch(Rectangle((zb + 4.14, d.ESP_Y_TOP - 26.6), 1.6, 27.9, fc='#0369a1'))
az.add_patch(Rectangle((zb + 5.74, d.ESP_Y_TOP - 22), 3.3, 18, fc='#94a3b8')); az.text(zb + 13, 16, 'ESP32', fontsize=8, color='#0369a1')
az.add_patch(Rectangle((zb + 1.6, -17), 2.54, 15, fc='#111827')); az.add_patch(Rectangle((zb + 4.14, -20), 1.6, 19, fc='#7e22ce')); az.add_patch(Rectangle((zb + 5.74, -20), 9, 7, fc='#22c55e')); az.text(zb + 16, -17, 'amp + screw\nterminal', fontsize=7)
az.add_patch(Rectangle((zb + 1.6, -18), 11.5, 8, fc='#fca5a5', ec='#b91c1c')); az.text(zb + 14, -14, '220 µF', fontsize=7)
az.add_patch(Rectangle((9, -46.4), 30, 16, fc='#11182733', ec='#111827')); az.text(24, -38.5, 'speaker', ha='center', va='center', fontsize=8)
az.add_patch(Rectangle((10, 40.5), 14, 6, fc='#fde68a', ec='#b45309')); az.text(17, 43.5, 'mic', ha='center', va='center', fontsize=7)
az.add_patch(Rectangle((43.4, -36), 26.5, 48.3, fc='none', ec='#ef4444', ls='--')); az.text(56.6, -12, 'battery board\n(if used)', ha='center', va='center', fontsize=8, color='#b91c1c')
az.add_patch(Rectangle((52, 38), 18, 9, fc='#d1d5db', ec='#374151')); az.add_patch(Rectangle((52, -47), 18, 9, fc='#d1d5db', ec='#374151'))
az.annotate('', (zb, 34), (7.5, 34), arrowprops=dict(arrowstyle='<->', lw=.8)); az.text(13, 35, '11 mm gap', ha='center', fontsize=7)
az.set_xlim(-4, 78); az.set_ylim(-52, 52); az.set_aspect('equal'); az.set_title('Side cut: front of the case at the left, lid at the right'); az.set_xlabel('mm from the front'); az.grid(alpha=.2)
plt.tight_layout(); plt.savefig('fit.png', dpi=110)
