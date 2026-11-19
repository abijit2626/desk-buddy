"""Turns routed.json into dewy_carrier.kicad_pcb (KiCad 7), fills the ground pours and runs KiCad's own rule check."""
import sys, json, math
sys.path.insert(0, '.')
import pcbnew, design as d
mm = pcbnew.FromMM
OX, OY = 150.0, 100.0
def P(x, y): return pcbnew.VECTOR2I(mm(OX + x), mm(OY - y))
r = json.load(open('routed.json'))
b = pcbnew.BOARD()
ds = b.GetDesignSettings(); ds.m_MinClearance = mm(0.2); ds.m_TrackMinWidth = mm(0.2); ds.m_ViasMinSize = mm(0.6); ds.m_MinThroughDrill = mm(0.3); ds.m_CopperEdgeClearance = mm(0.4); ds.m_HoleToHoleMin = mm(0.25)
nets = {}
for n in sorted(r['netid']):
    ni = pcbnew.NETINFO_ITEM(b, n); b.Add(ni); nets[n] = ni
F, B = pcbnew.F_Cu, pcbnew.B_Cu

def text(parent, s, x, y, size=0.8, layer=pcbnew.F_SilkS, angle=0, thick=None, just=None):
    t = pcbnew.FP_TEXT(parent) if isinstance(parent, pcbnew.FOOTPRINT) else pcbnew.PCB_TEXT(parent)
    t.SetText(s); t.SetPosition(P(x, y)); t.SetLayer(layer); t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(thick or size * 0.15))
    t.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    if layer == pcbnew.B_SilkS: t.SetMirrored(True)
    if just == 'left': t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT if layer == pcbnew.B_SilkS else pcbnew.GR_TEXT_H_ALIGN_LEFT)
    if just == 'right': t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT if layer == pcbnew.B_SilkS else pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    parent.Add(t); return t
def line(x0, y0, x1, y1, layer=pcbnew.F_SilkS, w=0.15):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetStart(P(x0, y0)); s.SetEnd(P(x1, y1)); s.SetLayer(layer); s.SetWidth(mm(w)); b.Add(s)
def rect(x0, y0, x1, y1, layer=pcbnew.F_SilkS, w=0.15):
    for a in [(x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)]: line(*a, layer=layer, w=w)
def circle(x, y, rad, layer=pcbnew.F_SilkS, w=0.15):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_CIRCLE); s.SetCenter(P(x, y)); s.SetEnd(P(x + rad, y)); s.SetLayer(layer); s.SetWidth(mm(w)); b.Add(s)

# ---------------- footprints: one per reference, pads at their real places ----------------
fps = {}
desc = {'J1': 'Screen socket 2x20 2.54 mm, soldered on the SCREEN face', 'U1': 'ESP32-S3 dev board (22+22 pins)', 'U2': 'GY-87 motion sensor', 'U3': 'MAX98357A amplifier',
        'J2': 'INMP441 microphone wires', 'J3': '5 V in screw terminal 5.08 mm', 'C1': '220 uF electrolytic'}
count = {}
for ref, name, net, x, y, rad, drill in r['pads']:
    if ref not in fps:
        fp = pcbnew.FOOTPRINT(b); fp.SetReference(ref); fp.SetValue(desc.get(ref, ref)); fp.SetPosition(P(x, y)); fp.SetLayer(F)
        fp.Reference().SetVisible(False); fp.Value().SetVisible(False); fp.SetAttributes(pcbnew.FP_THROUGH_HOLE); b.Add(fp); fps[ref] = fp
    fp = fps[ref]; count[ref] = count.get(ref, 0) + 1
    p = pcbnew.PAD(fp); p.SetNumber(str(count[ref])); p.SetPinFunction(name); p.SetShape(pcbnew.PAD_SHAPE_CIRCLE); p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
    p.SetSize(pcbnew.VECTOR2I(mm(rad * 2), mm(rad * 2))); p.SetDrillSize(pcbnew.VECTOR2I(mm(drill), mm(drill))); p.SetLayerSet(p.PTHMask()); p.SetPosition(P(x, y)); p.SetPos0(pcbnew.VECTOR2I(P(x, y).x - fp.GetPosition().x, P(x, y).y - fp.GetPosition().y))
    if net: p.SetNet(nets[net])
    fp.Add(p)
for k, (x, y) in enumerate(d.HOLES):
    fp = pcbnew.FOOTPRINT(b); fp.SetReference('H%d' % (k + 1)); fp.SetValue('M3 hole'); fp.SetPosition(P(x, y)); fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
    fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES); b.Add(fp)
    p = pcbnew.PAD(fp); p.SetNumber(''); p.SetShape(pcbnew.PAD_SHAPE_CIRCLE); p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH); p.SetSize(pcbnew.VECTOR2I(mm(3.2), mm(3.2))); p.SetDrillSize(pcbnew.VECTOR2I(mm(3.2), mm(3.2)))
    p.SetLayerSet(p.UnplatedHoleMask()); p.SetPosition(P(x, y)); p.SetPos0(pcbnew.VECTOR2I(0, 0)); fp.Add(p)
    circle(x, y, 3.0, pcbnew.F_SilkS, 0.12); circle(x, y, 3.0, pcbnew.B_SilkS, 0.12)

# ---------------- tracks and vias ----------------
for n, v in r['routes'].items():
    if n == '_gnd_missed': continue
    n = v.get('net', n)
    for l, x0, y0, x1, y1 in v['segs']:
        t = pcbnew.PCB_TRACK(b); t.SetStart(P(x0, y0)); t.SetEnd(P(x1, y1)); t.SetWidth(mm(v['width'])); t.SetLayer(F if l == 0 else B); t.SetNet(nets[n]); b.Add(t)
    for x, y in v['vias']:
        via = pcbnew.PCB_VIA(b); via.SetPosition(P(x, y)); via.SetWidth(mm(0.8)); via.SetDrill(mm(0.4)); via.SetViaType(pcbnew.VIATYPE_THROUGH); via.SetLayerPair(F, B); via.SetNet(nets[n]); b.Add(via)

# ---------------- outline ----------------
x0, x1, y0, y1 = d.X0, d.X1, d.Y0, d.Y1; E = pcbnew.Edge_Cuts; RB, RT = d.CORNER_R, d.CORNER_R_TOP
for a in [(x0 + RB, y0, x1 - RB, y0), (x1, y0 + RB, x1, y1 - RT), (x1 - RT, y1, x0 + RT, y1), (x0, y1 - RT, x0, y0 + RB)]: line(*a, layer=E, w=0.1)
def arc(cx, cy, rad, a0, a1):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_ARC); pt = lambda a: P(cx + rad * math.cos(math.radians(a)), cy + rad * math.sin(math.radians(a)))
    s.SetArcGeometry(pt(a0), pt((a0 + a1) / 2), pt(a1)); s.SetLayer(E); s.SetWidth(mm(0.1)); b.Add(s)
arc(x0 + RB, y0 + RB, RB, 180, 270); arc(x1 - RB, y0 + RB, RB, 270, 360); arc(x1 - RT, y1 - RT, RT, 0, 90); arc(x0 + RT, y1 - RT, RT, 90, 180)

# ---------------- ground pours and the antenna keep-out ----------------
def zone(layer, pts, net=None, keepout=False):
    z = pcbnew.ZONE(b); z.SetLayer(layer); o = z.Outline(); o.NewOutline()
    for x, y in pts: o.Append(mm(OX + x), mm(OY - y))
    if keepout:
        z.SetIsRuleArea(True); z.SetDoNotAllowCopperPour(True); z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True); z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET(); ls.AddLayer(F); ls.AddLayer(B); z.SetLayerSet(ls)
    else:
        z.SetNet(net); z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.25)); z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL); z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.5))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    b.Add(z); return z
box = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
zone(F, box, nets['GND']); zone(B, box, nets['GND'])
ka = d.ANTENNA_KEEPOUT; zone(F, [(ka[0], ka[1]), (ka[2] + 1, ka[1]), (ka[2] + 1, ka[3]), (ka[0], ka[3])], keepout=True)

# ---------------- labels ----------------
FS, BS = pcbnew.F_SilkS, pcbnew.B_SilkS
text(b, 'DEWY carrier v0.1', 28.0, -24.6, 1.4, FS, thick=0.25); text(b, 'BACK FACE: all parts go on this side', 28.0, 36.3, 0.9, FS)
text(b, 'SCREEN FACE: only the 40-pin socket J1 goes on this side', 5, -24.5, 1.2, BS, thick=0.2)
# J1
j1 = d.j1_pads(); xo = min(p[2] for p in j1); ys = [p[3] for p in j1]
for layer in (FS, BS): rect(xo - 1.5, min(ys) - 1.5, xo + 2.54 + 1.5, max(ys) + 1.5, layer)
text(b, 'J1 SCREEN', xo + 10.2, max(ys) + 2.6, 0.9, FS); text(b, 'J1 2x20 SOCKET', xo + 11.7, max(ys) + 2.6, 0.9, BS)
for c, (a, b2) in enumerate(d.J1_COLS):
    yy = j1[2 * c][3]; lab = (a + ' ' + b2) if d.J1_VCC_ROW_OUTER else (b2 + ' ' + a)
    text(b, lab, xo + 2.54 + 1.9, yy, 0.8, FS, just='left', thick=0.13)
    text(b, ' '.join(reversed(lab.split(' '))), xo + 2.54 + 1.9, yy, 0.8, BS, just='left', thick=0.13)   # that face is seen mirrored
# ESP32
ep = d.esp_pads(); xs = [p[2] for p in ep]; ytop = d.ESP_Y_TOP; ybot = min(p[3] for p in ep); xm = (min(xs) + max(xs)) / 2
rect(min(xs) - 2.6, ybot - 1.3, max(xs) + 7.0, ytop + 1.3, FS)
text(b, 'U1  ESP32-S3 dev board', xm, ytop - 5.0, 1.4, FS, thick=0.22)
text(b, '<< USB end                              antenna end >>', xm, ytop - 8.0, 1.0, FS)
text(b, 'narrow board (22.86 mm): upper of the two rows', xm, ybot + 6.6, 0.8, FS); text(b, 'wide board (25.4 mm): lower row', xm, ybot + 5.2, 0.8, FS)
for name, net, x, y, row in ep:
    if row == 'top': text(b, name, x, y + 1.9, 0.8, FS, angle=90, just='left', thick=0.13)
    elif row.startswith('bot25'): text(b, name, x, y - 1.9, 0.8, FS, angle=90, just='right', thick=0.13)
# module headers
for h in d.HEADERS:
    n = len(h['pins']); xa, xb = h['x'], h['x'] + (n - 1) * 2.54
    if h['body']:
        bx0, by0, bx1, by1 = h['body']; rect(h['x'] + bx0, h['y'] + by0, h['x'] + bx1, h['y'] + by1, FS)
        text(b, h['ref'] + ' ' + h['title'], h['x'] + (bx0 + bx1) / 2, h['y'] + by1 / 2 - 1, 1.0, FS, thick=0.16)
        for i, pn in enumerate(h['pins']): text(b, pn, h['x'] + i * 2.54, h['y'] + 1.7, 0.8, FS, angle=90, just='left', thick=0.13)
    else:
        rect(xa - 1.3, h['y'] - 1.3, xb + 1.3, h['y'] + 1.3, FS); text(b, h['ref'] + ' ' + h['title'], xa - 1.9, h['y'], 0.9, FS, just='right')
        for i, pn in enumerate(h['pins']): text(b, pn, h['x'] + i * 2.54, h['y'] - 1.7, 0.8, FS, angle=90, just='right', thick=0.13)
for c in d.CAPS:
    circle(c['x'] + 1.905, c['y'], 4.1, FS); text(b, '+', c['x'] - 2.0, c['y'] + 2.2, 1.2, FS, thick=0.2); text(b, c['ref'] + ' ' + c['label'], c['x'] + 1.905, c['y'] - 5.1, 0.8, FS)
t = d.TERMINAL
rect(t['x'] - 2.6, t['y'] - 4.3, t['x'] + 7.7, t['y'] + 3.9, FS); text(b, 'J3 5V IN', t['x'] - 3.4, t['y'], 0.9, FS, just='right'); text(b, '+5V', t['x'], t['y'] + 2.6, 0.8, FS); text(b, 'GND', t['x'] + 5.08, t['y'] + 2.6, 0.8, FS)

pcbnew.SaveBoard('dewy_carrier.kicad_pcb', b)
print('saved; tracks', len(b.GetTracks()), 'footprints', len(b.GetFootprints()))
b = pcbnew.LoadBoard('dewy_carrier.kicad_pcb')
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard('dewy_carrier.kicad_pcb', b)
b = pcbnew.LoadBoard('dewy_carrier.kicad_pcb')
pcbnew.WriteDRCReport(b, 'drc.txt', pcbnew.EDA_UNITS_MILLIMETRES, True)
