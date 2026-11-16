import json, sys; sys.path.insert(0, '.')
import design as d
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import Circle
r = json.load(open('routed.json')); fig, ax = plt.subplots(figsize=(20, 13.5))
for n, v in r['routes'].items():
    if n == '_gnd_missed': continue
    for l, x0, y0, x1, y1 in v['segs']: ax.plot([x0, x1], [y0, y1], color='#dc2626' if l == 0 else '#2563eb', lw=v['width'] * 6, alpha=.75, solid_capstyle='round')
    for x, y in v['vias']: ax.add_patch(Circle((x, y), 0.4, fc='#111', zorder=5))
for ref, name, net, x, y, rad, dr in r['pads']:
    ax.add_patch(Circle((x, y), rad, fc='#fbbf24' if net else '#d1d5db', ec='#444', lw=.3, zorder=4))
    ax.text(x, y, (net or '').replace('_GPIO', '\n').replace('SPARE', 'SP')[:12], fontsize=2.6, ha='center', va='center', zorder=6)
for x, y in r['routes'].get('_gnd_missed', []): ax.add_patch(Circle((x, y), 1.3, fc='none', ec='lime', lw=1.5, zorder=7))
for x, y in d.HOLES: ax.add_patch(Circle((x, y), 1.6, fc='white', ec='k'))
ax.plot([d.X0, d.X1, d.X1, d.X0, d.X0], [d.Y0, d.Y0, d.Y1, d.Y1, d.Y0], 'k-'); ax.set_aspect('equal'); ax.set_title('red = back face (F.Cu), blue = screen face (B.Cu)')
plt.tight_layout(); plt.savefig('route.png', dpi=100)
