// Ready-made wiring: Dewy as the firmware expects it, built from the pin map (so it never disagrees with it).
import { PART_DEFS, PART_ORDER, type PartType } from "./parts";
import { makeRef, type PinRef, type Wire, type Wiring } from "./wiring";
import type { PinAssignment } from "./pinmap";

export function placePart(w: Wiring, type: PartType, at?: { x: number; y: number }): Wiring {
  if (w.parts.some((p) => p.type === type)) return w;
  const def = PART_DEFS[type];
  return { ...w, parts: [...w.parts, { id: def.id, type, x: at?.x ?? def.home.x, y: at?.y ?? def.home.y }] };
}

/**
 * Every part on the canvas, every pin wired where the pin map says: the screen and touch on GPIO 6 to 14, the
 * motion sensor on GPIO 4 and 5, the mic on 15 to 17, the amplifier on 18, 21 and 38, plus power and ground.
 */
export function referenceWiring(pinMap: readonly PinAssignment[], opts: { voice?: boolean } = {}): Wiring {
  const voice = opts.voice ?? true;
  let w: Wiring = { parts: [], wires: [] };
  for (const t of PART_ORDER) if (voice || (t !== "mic" && t !== "amp")) w = placePart(w, t);

  const wires: Wire[] = [];
  const wire = (a: PinRef, b: PinRef) => wires.push({ id: `w${wires.length + 1}`, a, b });
  const board = (pin: string) => makeRef("board", pin);
  const gpio = (n: number) => board(`GPIO${n}`);

  for (const e of pinMap) {
    if (!voice && (e.partType === "mic" || e.partType === "amp")) continue;
    wire(gpio(e.gpio), makeRef(PART_DEFS[e.partType].id, e.partPin));
  }
  // power: left-hand parts use the left-hand 3V3 / GND pins; the amplifier is on the right
  wire(board("3V3.1"), makeRef("lcd", "VCC"));
  wire(board("GND.1"), makeRef("lcd", "GND"));
  wire(board("GND.1"), makeRef("lcd", "IM0"));
  wire(board("3V3.1"), makeRef("lcd", "IM1"));
  wire(board("3V3.1"), makeRef("lcd", "IM2"));
  wire(board("3V3.2"), makeRef("imu", "VCC"));
  wire(board("GND.1"), makeRef("imu", "GND"));
  if (voice) {
    wire(board("3V3.2"), makeRef("mic", "VDD"));
    wire(board("GND.1"), makeRef("mic", "GND"));
    wire(board("GND.1"), makeRef("mic", "LR"));
    wire(board("5V"), makeRef("amp", "VIN"));
    wire(board("GND.2"), makeRef("amp", "GND"));
  }
  return { ...w, wires };
}

export const emptyWiring = (): Wiring => ({ parts: [], wires: [] });
