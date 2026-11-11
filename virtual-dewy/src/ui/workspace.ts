// The canvas: parts you can drag around, and wires you draw from pin to pin.
import { PART_DEFS, SIGNAL_NAMES, describePin, BOARD_HAZARDS, type PartDef, type PartType, type PinDef, type Signal } from "../core/parts";
import { RESERVED_PINS, assignmentFor, assignmentsForGpio } from "../core/pinmap";
import { canAddWire, computeNets, makeRef, pinCtx, splitRef, type PinRef } from "../core/wiring";
import type { App } from "../app/app";
import { badge, clear, h, s } from "./dom";
import { buildPartBody, type PartBody } from "./part-bodies";

const SHORT: Record<Signal, string> = {
  "spi.mosi": "MOSI", "spi.miso": "MISO", "spi.sclk": "SCLK", "tft.cs": "CS", "tft.dc": "DC", "tft.rst": "RST", "tft.bl": "BL", "touch.cs": "T_CS", "touch.irq": "T_IRQ",
  "i2c.sda": "SDA", "i2c.scl": "SCL", "imu.int": "INT", "i2s.bclk": "BCLK", "i2s.ws": "WS", "i2s.din": "MIC SD", "i2s.dout": "AMP DIN",
};
const SIGNAL_COLOURS = ["#1f77b4", "#9467bd", "#17becf", "#2ca02c", "#bcbd22", "#e377c2", "#8c564b", "#7f7f7f", "#d62728", "#ff7f0e"];
const colourFor = (key: string) => { let n = 0; for (const c of key) n = (n * 31 + c.charCodeAt(0)) >>> 0; return SIGNAL_COLOURS[n % SIGNAL_COLOURS.length]; };

interface PartView { el: HTMLElement; body: PartBody; pins: Map<string, HTMLElement> }

export class Workspace {
  readonly el: HTMLElement;
  private stage = h("div", { class: "workspace" });
  private sizer = h("div", { class: "workspace-sizer" });
  private svg = s("svg", { class: "wires" });          // all wires, behind the parts
  private svgTop = s("svg", { class: "wires top" });   // the selected wire and the one being drawn, above the parts
  zoom = 1;
  private zoomTouched = false;
  private zoomLabel = h("span", { class: "mono" }, "100%");
  static readonly WIDTH = 1480; static readonly HEIGHT = 880;
  private views = new Map<string, PartView>();
  private hint = h("div", { class: "drop-hint" }, "Drag parts here from the Parts list, or press “Reference wiring”.");
  private pending: PinRef | null = null;       // first pin of a click-click wire
  private draft: { from: PinRef; path: SVGPathElement } | null = null;
  private removeBtn = h("button", { class: "small danger", title: "Remove this wire", "aria-label": "Remove selected wire", style: "position:absolute;z-index:6;display:none" }, "✕ wire");
  private dirty = true;
  private hoverRef: PinRef | null = null;

  constructor(private app: App) {
    this.el = h("div", { class: "workspace-wrap", tabindex: "0", "aria-label": "Wiring canvas" }, this.sizer);
    this.sizer.append(this.stage);
    this.stage.append(this.svg, this.svgTop, this.hint, this.removeBtn);
    const bar = h("div", { class: "zoombar" },
      h("button", { class: "small", "aria-label": "Zoom out", onclick: () => this.setZoom(this.zoom - 0.1, true) }, "−"), this.zoomLabel,
      h("button", { class: "small", "aria-label": "Zoom in", onclick: () => this.setZoom(this.zoom + 0.1, true) }, "+"),
      h("button", { class: "small", title: "Fit the whole canvas in view", onclick: () => { this.zoomTouched = false; this.fit(); } }, "Fit"),
      h("label", { class: "row", style: "font-size:11px;gap:3px", title: "Draw wires over the parts, or behind them (hover a pin to light up its wires either way)" },
        h("input", { type: "checkbox", checked: true, onchange: (e: Event) => { this.stage.classList.toggle("wires-behind", !(e.target as HTMLInputElement).checked); } }), "wires on top"));
    this.el.append(bar);
    this.removeBtn.addEventListener("click", () => { if (app.selectedWire) app.removeWire(app.selectedWire); });
    this.stage.addEventListener("dragover", (e) => { if (e.dataTransfer?.types.includes("text/plain")) e.preventDefault(); });
    this.stage.addEventListener("drop", (e) => {
      const t = e.dataTransfer?.getData("text/plain") ?? "";
      if (!t.startsWith("part:")) return;
      e.preventDefault();
      const type = t.slice(5) as PartType;
      if (!(type in PART_DEFS)) return;
      const r = this.stage.getBoundingClientRect();
      app.addPart(type, { x: Math.max(0, Math.round((e.clientX - r.left) / this.zoom - 60)), y: Math.max(0, Math.round((e.clientY - r.top) / this.zoom - 12)) });
    });
    this.el.addEventListener("keydown", (e) => {
      if ((e.key === "Delete" || e.key === "Backspace") && app.selectedWire && !(e.target instanceof HTMLInputElement)) { e.preventDefault(); app.removeWire(app.selectedWire); }
      if (e.key === "Escape") { this.pending = null; this.refreshTargets(); }
    });
    this.stage.addEventListener("pointerdown", (e) => { if (e.target === this.stage || e.target === this.svg || e.target === this.svgTop) { app.selectedWire = null; this.pending = null; this.refreshTargets(); this.markDirty(); } });
    new ResizeObserver(() => { this.markDirty(); if (!this.zoomTouched) this.fit(); }).observe(this.el);
    window.addEventListener("resize", () => this.markDirty());
    this.applyZoom();
    app.on((k) => { if (k === "wiring" || k === "selection") this.render(); });
    this.render();
  }

  markDirty() { this.dirty = true; }

  private applyZoom() {
    this.stage.style.transform = `scale(${this.zoom})`;
    this.sizer.style.width = Math.round(Workspace.WIDTH * this.zoom) + "px";
    this.sizer.style.height = Math.round(Workspace.HEIGHT * this.zoom) + "px";
    this.zoomLabel.textContent = Math.round(this.zoom * 100) + "%";
    this.markDirty();
  }
  setZoom(z: number, byHand = false) { this.zoom = Math.max(0.4, Math.min(1.5, Math.round(z * 100) / 100)); if (byHand) this.zoomTouched = true; this.applyZoom(); }
  /** the whole canvas fits the visible area (never bigger than 100%) */
  fit() {
    const w = this.el.clientWidth - 4, hgt = this.el.clientHeight - 4;
    if (w <= 0 || hgt <= 0) return;
    this.setZoom(Math.min(1, w / Workspace.WIDTH, hgt / Workspace.HEIGHT));
  }

  /** called every animation frame: refresh live part contents, redraw wires if something moved */
  tick() {
    for (const v of this.views.values()) v.body.update?.();
    if (this.dirty) { this.dirty = false; this.drawWires(); }
  }

  // ---------------------------------------------------------------- parts
  render() {
    const { wiring } = this.app;
    this.hint.style.display = wiring.parts.length ? "none" : "grid";
    for (const [id, v] of this.views) if (!wiring.parts.some((p) => p.id === id)) { v.el.remove(); this.views.delete(id); }
    for (const part of wiring.parts) {
      let v = this.views.get(part.id);
      if (!v) { v = this.buildPart(PART_DEFS[part.type]); this.views.set(part.id, v); this.stage.append(v.el); }
      v.el.style.left = part.x + "px"; v.el.style.top = part.y + "px";
    }
    this.decoratePins();
    this.refreshTargets();
    this.markDirty();
    setTimeout(() => this.markDirty(), 60);
  }

  private buildPart(def: PartDef): PartView {
    const body = buildPartBody(this.app, def.type);
    const pins = new Map<string, HTMLElement>();
    const col = (side: "l" | "r") => {
      const c = h("div", { class: `pins ${side}` });
      for (const pin of def.pins.filter((p) => p.side === side)) {
        const dot = h("button", { class: "dot", type: "button", "data-ref": makeRef(def.id, pin.id), "aria-label": `${def.name} pin ${pin.label}`, title: this.pinTitle(def, pin) });
        const row = h("div", { class: `pin ${pin.kind === "rail" ? "rail-" + (pin.rail === "v33" ? "33" : "5") : pin.kind === "gnd" ? "gnd" : ""}`, "data-pin": pin.id }, dot, h("span", { class: "lab" }, pin.label));
        pins.set(pin.id, row);
        this.bindPin(dot, makeRef(def.id, pin.id));
        dot.addEventListener("pointerenter", () => { this.hoverRef = makeRef(def.id, pin.id); this.markDirty(); });
        dot.addEventListener("pointerleave", () => { this.hoverRef = null; this.markDirty(); });
        c.append(row);
      }
      return c;
    };
    const header = h("div", { class: "ph", "data-drag": def.id }, h("span", { class: "name" }, def.name), badge(def.honesty, def.honestyNote),
      h("button", { class: "small", title: `Remove ${def.name} from the canvas`, "aria-label": `Remove ${def.name}`, onclick: () => this.app.removePart(def.id) }, "×"));
    const el = h("div", { class: "part", "data-part": def.id }, header, h("div", { class: "pb" }, col("l"), body.el, col("r")));
    this.bindDrag(header, def.id, el);
    return { el, body, pins };
  }

  private pinTitle(def: PartDef, pin: PinDef): string {
    const lines = [`${def.name} · ${pin.label} (${describePin(pin)})`];
    if (pin.title) lines.push(pin.title);
    const map = this.app.pinMap;
    if (pin.kind === "sig") { const a = assignmentFor(map, def.type, pin.id); if (a) lines.push(`Firmware pin map: GPIO ${a.gpio} (${a.origin}${a.source === "brief" ? ", UNVERIFIED" : ""})`); }
    if (pin.kind === "gpio") {
      for (const a of assignmentsForGpio(map, pin.gpio)) lines.push(`Firmware: ${SIGNAL_NAMES[a.signal]} for ${PART_DEFS[a.partType].name} ${a.partPin}${a.source === "brief" ? " (UNVERIFIED, from the brief)" : ""}`);
      const r = RESERVED_PINS.find((x) => x.gpio === pin.gpio); if (r) lines.push(`Reserved: ${r.why}`);
      const hz = BOARD_HAZARDS[pin.gpio]; if (hz) lines.push(hz.text);
    }
    return lines.join("\n");
  }

  /** chips on the board's pins that the firmware uses, and warning colours from the analysis */
  private decoratePins() {
    const { analysis, wiring, pinMap } = this.app;
    const used = new Set<PinRef>();
    for (const w of wiring.wires) { used.add(w.a); used.add(w.b); }
    const flags = new Map<PinRef, "error" | "warn">();
    for (const p of analysis.problems) if (p.severity !== "info") for (const r of p.refs) { if (p.refs.length <= 2 || p.severity === "error") flags.set(r, p.severity === "error" || flags.get(r) === "error" ? "error" : "warn"); }
    for (const [partId, v] of this.views) {
      const part = wiring.parts.find((p) => p.id === partId)!;
      const def = PART_DEFS[part.type];
      for (const [pinId, row] of v.pins) {
        const ref = makeRef(partId, pinId);
        row.classList.toggle("used", used.has(ref));
        row.classList.remove("flag-error", "flag-warn");
        const f = flags.get(ref); if (f) row.classList.add("flag-" + f);
        const dot = row.querySelector("button")!;
        dot.title = this.pinTitle(def, def.pins.find((p) => p.id === pinId)!);
        row.querySelectorAll(".chip").forEach((c) => c.remove());
        const pin = def.pins.find((p) => p.id === pinId)!;
        if (part.type === "esp32s3" && pin.kind === "gpio") {
          const as = assignmentsForGpio(pinMap, pin.gpio);
          if (as.length) {
            const sig = as[0].signal;
            row.append(h("span", { class: `chip${as.every((a) => a.source === "brief") ? " brief" : ""}`, title: as.map((a) => `${PART_DEFS[a.partType].name} ${a.partPin} (${a.origin})`).join("\n") }, SHORT[sig]));
          } else if (RESERVED_PINS.some((r) => r.gpio === pin.gpio)) row.append(h("span", { class: "chip brief", title: RESERVED_PINS.find((r) => r.gpio === pin.gpio)!.why }, "BAT (off)"));
        }
      }
    }
  }

  // ---------------------------------------------------------------- dragging parts
  private bindDrag(handle: HTMLElement, partId: string, el: HTMLElement) {
    handle.addEventListener("pointerdown", (e) => {
      if ((e.target as HTMLElement).closest("button")) return;
      const part = this.app.wiring.parts.find((p) => p.id === partId)!;
      const sx = e.clientX, sy = e.clientY, ox = part.x, oy = part.y;
      handle.setPointerCapture(e.pointerId);
      el.classList.add("dragging");
      const move = (ev: PointerEvent) => {
        const x = Math.max(0, Math.round(ox + (ev.clientX - sx) / this.zoom)), y = Math.max(0, Math.round(oy + (ev.clientY - sy) / this.zoom));
        this.app.movePart(partId, x, y);
        el.style.left = x + "px"; el.style.top = y + "px";
        this.markDirty();
      };
      const up = () => { handle.removeEventListener("pointermove", move); handle.removeEventListener("pointerup", up); handle.removeEventListener("pointercancel", up); el.classList.remove("dragging"); this.app.commitMove(); };
      handle.addEventListener("pointermove", move); handle.addEventListener("pointerup", up); handle.addEventListener("pointercancel", up);
    });
  }

  // ---------------------------------------------------------------- drawing wires
  private bindPin(dot: HTMLElement, ref: PinRef) {
    dot.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault(); e.stopPropagation();
      const startedPending = this.pending;
      let moved = false;
      this.beginDraft(ref, e);
      dot.setPointerCapture(e.pointerId);
      const move = (ev: PointerEvent) => { if (Math.hypot(ev.clientX - e.clientX, ev.clientY - e.clientY) > 4) moved = true; this.updateDraft(ev); };
      const up = (ev: PointerEvent) => {
        dot.removeEventListener("pointermove", move); dot.removeEventListener("pointerup", up); dot.removeEventListener("pointercancel", up);
        this.endDraft();
        if (moved) {
          const target = this.pinAt(ev.clientX, ev.clientY);
          this.pending = null;
          if (target && target !== ref) this.app.addWire(ref, target);
        } else if (startedPending && startedPending !== ref) {
          this.pending = null; this.app.addWire(startedPending, ref);                // click, then click another pin
        } else this.pending = this.pending === ref ? null : ref;                          // first click: wait for the second
        this.refreshTargets();
      };
      dot.addEventListener("pointermove", move); dot.addEventListener("pointerup", up); dot.addEventListener("pointercancel", up);
    });
    dot.addEventListener("keydown", (e) => {
      if (e.key !== "Enter" && e.key !== " ") return;
      e.preventDefault();
      if (this.pending && this.pending !== ref) { const a = this.pending; this.pending = null; this.app.addWire(a, ref); }
      else this.pending = this.pending === ref ? null : ref;
      this.refreshTargets();
    });
  }

  private pinAt(x: number, y: number): PinRef | null {
    const el = document.elementsFromPoint(x, y).find((n) => n instanceof HTMLElement && n.dataset.ref) as HTMLElement | undefined;
    return el?.dataset.ref ?? null;
  }

  private beginDraft(from: PinRef, e: PointerEvent) {
    const path = s("path", { class: "draft" });
    this.svgTop.append(path);
    this.draft = { from, path };
    this.refreshTargets(from);
    this.updateDraft(e);
  }
  private updateDraft(e: PointerEvent) {
    if (!this.draft) return;
    const a = this.pinCentre(this.draft.from), r = this.stage.getBoundingClientRect();
    const bx = (e.clientX - r.left) / this.zoom, by = (e.clientY - r.top) / this.zoom;
    this.draft.path.setAttribute("d", `M${a.x},${a.y} C${a.x + 40 * a.dir},${a.y} ${bx},${by} ${bx},${by}`);
    const over = this.pinAt(e.clientX, e.clientY);
    for (const v of this.views.values()) for (const row of v.pins.values()) row.classList.toggle("hot", over !== null && row.querySelector("button")?.dataset.ref === over);
  }
  private endDraft() { this.draft?.path.remove(); this.draft = null; }

  /** colour every pin by whether a wire from `from` (or the pending pin) could end there */
  private refreshTargets(from: PinRef | null = this.pending) {
    for (const v of this.views.values()) for (const [, row] of v.pins) {
      const ref = row.querySelector("button")!.dataset.ref as PinRef;
      row.classList.remove("ok-target", "bad-target", "hot");
      if (!from) continue;
      if (ref === from) { row.classList.add("hot"); continue; }
      const ok = canAddWire(this.app.wiring, from, ref).ok;
      row.classList.add(ok ? "ok-target" : "bad-target");
    }
  }

  private pinCentre(ref: PinRef): { x: number; y: number; dir: 1 | -1 } {
    const { partId } = splitRef(ref);
    const dot = this.views.get(partId)?.el.querySelector<HTMLElement>(`[data-ref="${CSS.escape(ref)}"]`);
    const r = this.stage.getBoundingClientRect();
    if (!dot) return { x: 0, y: 0, dir: 1 };
    const d = dot.getBoundingClientRect();
    const onRight = !!dot.closest(".pins.r");
    return { x: (d.left + d.width / 2 - r.left) / this.zoom, y: (d.top + d.height / 2 - r.top) / this.zoom, dir: onRight ? 1 : -1 };
  }

  private drawWires() {
    for (const n of [...this.svg.querySelectorAll("path"), ...this.svgTop.querySelectorAll("path.vis")]) n.remove();
    const { wiring, analysis } = this.app;
    const nets = computeNets(wiring);
    const warnWires = new Set(analysis.problems.filter((p) => p.severity !== "info" && p.wireId).map((p) => p.wireId!));
    const hoverNet = this.hoverRef ? nets.netOf.get(this.hoverRef) : undefined;
    for (const w of wiring.wires) {
      const a = this.pinCentre(w.a), b = this.pinCentre(w.b);
      if (!this.views.has(splitRef(w.a).partId) || !this.views.has(splitRef(w.b).partId)) continue;
      const reach = Math.max(50, Math.min(140, Math.abs(b.x - a.x) / 2));
      const d = `M${a.x},${a.y} C${a.x + reach * a.dir},${a.y} ${b.x + reach * b.dir},${b.y} ${b.x},${b.y}`;
      const colour = this.wireColour(nets.members[nets.netOf.get(w.a)!] ?? []);
      const vis = s("path", { class: `vis${this.app.selectedWire === w.id ? " sel" : ""}${warnWires.has(w.id) ? " warn" : ""}`, d, stroke: colour });
      const hit = s("path", { class: "hit", d });
      hit.addEventListener("click", (e) => { e.stopPropagation(); this.app.selectedWire = w.id; this.render(); this.el.focus(); });
      const title = s("title"); title.textContent = `${this.label(w.a)} ↔ ${this.label(w.b)}${warnWires.has(w.id) ? "\n⚠ " + analysis.problems.filter((p) => p.wireId === w.id).map((p) => p.message).join("\n⚠ ") : ""}`;
      hit.append(title);
      this.svg.append(vis, hit);
      if (hoverNet !== undefined && nets.netOf.get(w.a) === hoverNet && this.app.selectedWire !== w.id) this.svgTop.append(s("path", { class: "vis", d, stroke: colour }));
      if (this.app.selectedWire === w.id) {
        this.svgTop.append(s("path", { class: "vis sel", d, stroke: colour }));
        this.removeBtn.style.display = "block";
        this.removeBtn.style.left = (a.x + b.x) / 2 - 24 + "px"; this.removeBtn.style.top = (a.y + b.y) / 2 - 12 + "px";
      }
    }
    if (!this.app.selectedWire) this.removeBtn.style.display = "none";
  }

  private label(ref: PinRef): string {
    const c = pinCtx(this.app.wiring, ref);
    return c ? `${PART_DEFS[c.part.type].name} ${c.def.label}` : ref;
  }

  private wireColour(members: PinRef[]): string {
    const wiring = this.app.wiring;
    let gnd = false, v33 = false, v5 = false, sig: string | null = null;
    for (const r of members) {
      const c = pinCtx(wiring, r);
      if (!c) continue;
      if (c.def.kind === "gnd") gnd = true;
      else if (c.def.kind === "rail") { if (c.def.rail === "v33") v33 = true; else v5 = true; }
      else if (c.def.kind === "sig") sig = c.def.signal;
    }
    if (gnd) return "var(--wire-gnd)";
    if (v33) return "var(--wire-33)";
    if (v5) return "var(--wire-5)";
    return sig ? colourFor(sig) : "var(--accent)";
  }

  clearPending() { this.pending = null; this.refreshTargets(); }
  clearViews() { clear(this.stage); }
}
