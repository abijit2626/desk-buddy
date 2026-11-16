"""A small two-layer maze router on a 0.635 mm grid. Layers: 0 = back face (F.Cu), 1 = screen face (B.Cu)."""
import heapq, math, numpy as np
import design as d
G = d.G
NX = int(round((d.X1 - d.X0) / G)) + 1
NY = int(round((d.Y1 - d.Y0) / G)) + 1
def ci(x): return int(round((x - d.X0) / G))
def cj(y): return int(round((y - d.Y0) / G))
def px(i): return d.X0 + i * G
def py(j): return d.Y0 + j * G
XX, YY = np.meshgrid(d.X0 + np.arange(NX) * G, d.Y0 + np.arange(NY) * G)   # [j, i]

class Board:
    def __init__(self, clearance=0.25):
        self.clr = clearance
        self.objs = []            # (layer or -1 for both, net, kind, geometry...) circles and segments
        self.hard = np.zeros((NY, NX), bool)   # nothing may go here on either layer
        self.padcell = {}         # (j, i) -> net id of a through-hole pad centred there
        self.noVia = np.zeros((NY, NX), bool)
        self.nearPad = np.zeros((NY, NX), np.float32)   # extra cost for running right beside pads (keeps their exits open)
    def circle(self, layer, net, x, y, r): self.objs.append((layer, net, 'c', x, y, r))
    def seg(self, layer, net, x0, y0, x1, y1, hw): self.objs.append((layer, net, 's', x0, y0, x1, y1, hw))
    def pad(self, net, x, y, r):
        self.circle(-1, net, x, y, r); self.padcell[(cj(y), ci(x))] = net
        self.noVia |= (XX - x) ** 2 + (YY - y) ** 2 < 1.7 ** 2
        self.nearPad[(XX - x) ** 2 + (YY - y) ** 2 < 2.1 ** 2] = 0.6
    def block_circle(self, x, y, r): self.hard |= (XX - x) ** 2 + (YY - y) ** 2 < r * r
    def block_rect(self, x0, y0, x1, y1): self.hard |= (XX > x0) & (XX < x1) & (YY > y0) & (YY < y1)
    def block_edge(self, margin):
        inside = np.ones((NY, NX), bool)
        for ysel, r in ((YY < (d.Y0 + d.Y1) / 2, d.CORNER_R), (YY >= (d.Y0 + d.Y1) / 2, d.CORNER_R_TOP)):
            qx = np.maximum(np.maximum(d.X0 + r - XX, XX - (d.X1 - r)), 0); qy = np.maximum(np.maximum(d.Y0 + r - YY, YY - (d.Y1 - r)), 0)
            inside &= ~ysel | (np.hypot(qx, qy) <= r - margin)
        inside &= (XX >= d.X0 + margin) & (XX <= d.X1 - margin) & (YY >= d.Y0 + margin) & (YY <= d.Y1 - margin)
        self.hard |= ~inside
    @staticmethod
    def _dist(o):
        if o[2] == 'c': return np.hypot(XX - o[3], YY - o[4]), o[5]
        x0, y0, x1, y1, hw = o[3:8]; dx, dy = x1 - x0, y1 - y0; L2 = dx * dx + dy * dy
        t = np.clip(((XX - x0) * dx + (YY - y0) * dy) / L2, 0, 1) if L2 > 0 else 0
        return np.hypot(XX - (x0 + dx * t), YY - (y0 + dy * t)), hw
    def mark(self, own, o, hw_route):
        dist, r = self._dist(o); m = dist < r + self.clr + hw_route - 1e-6
        for l in ((0, 1) if o[0] < 0 else (o[0],)):
            a = own[l]; a[m & (a != 0) & (a != o[1])] = -1; a[m & (a == 0)] = o[1]
    def ownmap(self, hw_route):
        own = np.zeros((2, NY, NX), np.int32)
        for o in self.objs: self.mark(own, o, hw_route)
        own[:, self.hard] = -1
        return own

MOVES = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0), (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)]
def route_net(b, own, net, pads, hw, via_cost=14.0, via_r=0.4, partial=False, missed=None):
    """pads: [(x, y)]. Returns (segments [(layer, x0, y0, x1, y1)], vias [(x, y)]) or None. Marks what it adds."""
    ok = (own == 0) | (own == net)
    cells = [(cj(y), ci(x)) for x, y in pads]
    for j, i in cells: ok[:, j, i] = True
    def via_ok(j, i):
        if b.noVia[j, i] or j < 1 or i < 1 or j >= NY - 1 or i >= NX - 1: return False
        return bool(ok[:, j - 1:j + 2, i - 1:i + 2].all())
    pen = b.nearPad
    tree = {(0, cells[0][0], cells[0][1]), (1, cells[0][0], cells[0][1])}
    todo = set(cells[1:]) - {cells[0]}
    segs, vias = [], []
    while todo:
        dist = {s: 0.0 for s in tree}; prev = {}; heap = [(0.0, s) for s in tree]; heapq.heapify(heap); goal = None
        while heap:
            dcur, cur = heapq.heappop(heap)
            if dcur > dist.get(cur, 1e18): continue
            l, j, i = cur
            if (j, i) in todo: goal = cur; break
            for dj, di, c in MOVES:
                nj, ni = j + dj, i + di
                if nj < 0 or ni < 0 or nj >= NY or ni >= NX or not ok[l, nj, ni]: continue
                if dj and di and not (ok[l, j, ni] and ok[l, nj, i]): continue
                # a bend costs a little, so routes come out straight
                pc = prev.get(cur); bend = 0.0
                if pc is not None and pc[0] == l and (j - pc[1], i - pc[2]) != (dj, di): bend = 0.6
                nd = dcur + c * (1.0 + pen[nj, ni]) + bend; nxt = (l, nj, ni)
                if nd < dist.get(nxt, 1e18): dist[nxt] = nd; prev[nxt] = cur; heapq.heappush(heap, (nd, nxt))
            o = (1 - l, j, i)
            if ok[1 - l, j, i]:
                own_pad = b.padcell.get((j, i)) == net
                if own_pad or via_ok(j, i):
                    nd = dcur + (0.0 if own_pad else via_cost)
                    if nd < dist.get(o, 1e18): dist[o] = nd; prev[o] = cur; heapq.heappush(heap, (nd, o))
        if goal is None:
            if not partial: return None
            if missed is not None: missed.extend((px(i), py(j)) for j, i in todo)
            break
        path = [goal]
        while path[-1] in prev: path.append(prev[path[-1]])
        path.reverse()
        new = []
        k = 0
        while k < len(path) - 1:
            a, c = path[k], path[k + 1]
            if a[0] != c[0]:
                if b.padcell.get((a[1], a[2])) != net: vias.append((px(a[2]), py(a[1]))); new.append((-1, net, 'c', px(a[2]), py(a[1]), via_r))
                k += 1; continue
            dj, di = c[1] - a[1], c[2] - a[2]; m = k + 1
            while m < len(path) - 1 and path[m + 1][0] == a[0] and (path[m + 1][1] - path[m][1], path[m + 1][2] - path[m][2]) == (dj, di): m += 1
            e = path[m]; segs.append((a[0], px(a[2]), py(a[1]), px(e[2]), py(e[1]))); new.append((a[0], net, 's', px(a[2]), py(a[1]), px(e[2]), py(e[1]), hw))
            k = m
        for o in new: b.objs.append(o); b.mark(own, o, hw)
        ok = (own == 0) | (own == net)
        for j, i in cells: ok[:, j, i] = True
        tree.update(path); tree.add((0, goal[1], goal[2])); tree.add((1, goal[1], goal[2])); todo.discard((goal[1], goal[2]))
    return segs, vias
