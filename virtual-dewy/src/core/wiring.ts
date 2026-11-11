// Wiring: which pins are joined, which wires are allowed, and what the firmware's pin map thinks of them.
//
// Three layers:
//   1. checkPair / canAddWire: physical rules. A wire that breaks them is refused (GND to 3V3, a GPIO to a GPIO,
//      SDA to a clock pin, 5 V into a 3.3 V part ...).
//   2. analyze: warnings for wires that are allowed but disagree with the firmware's pin map or the board
//      (SDA on the wrong GPIO, a strapping pin, the PSRAM pins ...).
//   3. partStatus: does each part have everything it needs for the simulation to treat it as connected?
import { BOARD_HAZARDS, PART_DEFS, SIGNAL_NAMES, findPin, type PartType, type PinDef, type Rail } from "./parts";
import { RESERVED_PINS, assignmentFor, assignmentsForGpio, type PinAssignment } from "./pinmap";

export type PinRef = string;                       // "partId:pinId", for example "board:GPIO11"
export interface PlacedPart { id: string; type: PartType; x: number; y: number }
export interface Wire { id: string; a: PinRef; b: PinRef }
export interface Wiring { parts: PlacedPart[]; wires: Wire[] }

export const makeRef = (partId: string, pinId: string): PinRef => `${partId}:${pinId}`;
export function splitRef(ref: PinRef): { partId: string; pinId: string } {
  const i = ref.indexOf(":");
  return { partId: ref.slice(0, i), pinId: ref.slice(i + 1) };
}

export interface PinCtx { part: PlacedPart; def: PinDef; ref: PinRef }
export function pinCtx(w: Wiring, ref: PinRef): PinCtx | undefined {
  const { partId, pinId } = splitRef(ref);
  const part = w.parts.find((p) => p.id === partId);
  const def = part && findPin(part.type, pinId);
  return part && def ? { part, def, ref } : undefined;
}

const railName = (r: Rail) => (r === "v33" ? "3.3 V" : "5 V");
const pinName = (c: PinCtx) => (c.def.kind === "gpio" ? `GPIO ${c.def.gpio}` : `${PART_DEFS[c.part.type].name} ${c.def.label}`);

export type Verdict = { ok: true } | { ok: false; reason: string };
const ok: Verdict = { ok: true };
const no = (reason: string): Verdict => ({ ok: false, reason });

// ---------------------------------------------------------------- 1a. pair rules
export function checkPair(a: PinCtx, b: PinCtx): Verdict {
  if (a.ref === b.ref) return no("That is the same pin.");
  const r = pairRule(a, b);
  return r ?? pairRule(b, a) ?? no(`${pinName(a)} and ${pinName(b)} can't be wired together.`);
}

// returns undefined when this ordering has no opinion (the caller then tries the other ordering)
function pairRule(a: PinCtx, b: PinCtx): Verdict | undefined {
  const x = a.def, y = b.def;
  if (x.kind === "en" || y.kind === "en") return no("RST (EN) isn't used in this design: wiring it would hold the board in reset.");

  switch (x.kind) {
    case "gnd":
      if (y.kind === "gnd") return ok;
      if (y.kind === "rail") return no(`GND to the ${railName(y.rail)} supply is a short circuit.`);
      if (y.kind === "vin") return no(`${pinName(b)} is a power input: it goes to a supply pin, not to GND.`);
      if (y.kind === "strap") return y.accepts.includes("gnd") ? ok : no(`${pinName(b)} must go to ${y.accepts.map(strapName).join(" or ")}, not GND.`);
      if (y.kind === "gpio") return no("GND only goes to GND pins. A GPIO pin carries a signal.");
      if (y.kind === "sig") return no(`${pinName(b)} is a signal pin, not a ground. Wire it to a GPIO.`);
      return undefined;

    case "rail":
      if (y.kind === "rail") return x.rail === y.rail ? no("Both pins are the same supply on the board already.") : no(`The ${railName(x.rail)} and ${railName(y.rail)} supplies must never be joined.`);
      if (y.kind === "vin") return y.accepts.includes(x.rail) ? ok : no(`${pinName(b)} takes ${y.accepts.map(railName).join(" or ")} only: ${railName(x.rail)} would damage it.`);
      if (y.kind === "strap") return y.accepts.includes(x.rail) ? ok : no(`${pinName(b)} must go to ${y.accepts.map(strapName).join(" or ")}.`);
      if (y.kind === "sig") return y.alsoRail === x.rail ? ok : no(`${pinName(b)} is a signal pin: wire it to a GPIO, not a supply.`);
      if (y.kind === "gpio") return no("A GPIO pin carries a signal. Power comes from a 3V3 or 5V pin.");
      return undefined;

    case "vin":
      if (y.kind === "vin") return x.accepts.some((r) => y.accepts.includes(r)) ? ok : no(`${pinName(a)} and ${pinName(b)} need different supply voltages.`);
      if (y.kind === "strap") return y.accepts.some((r) => r !== "gnd" && x.accepts.includes(r as Rail)) ? ok : no(`${pinName(b)} can't be tied to ${pinName(a)}'s supply.`);
      if (y.kind === "gpio" || y.kind === "sig") return no(`${pinName(a)} is a power input: it goes to a 3V3/5V pin, not a signal.`);
      return undefined;

    case "gpio":
      if (y.kind === "gpio") return no("Both are GPIO pins on the board. Wire a GPIO to a pin on a part.");
      if (y.kind === "sig") return ok;
      if (y.kind === "strap") return no(`${pinName(b)} is a strap: it goes to GND or a supply, not a GPIO.`);
      return undefined;

    case "sig":
      if (y.kind === "sig") return x.signal === y.signal ? ok : no(`Different signals: ${pinName(a)} is ${SIGNAL_NAMES[x.signal]}, ${pinName(b)} is ${SIGNAL_NAMES[y.signal]}.`);
      if (y.kind === "strap") return no(`${pinName(b)} is a strap: it goes to GND or a supply, not a signal.`);
      return undefined;

    case "strap":
      if (y.kind === "strap") return no("Two strap pins can't be wired to each other: wire each to GND or a supply.");
      return undefined;
  }
}
const strapName = (a: "gnd" | Rail) => (a === "gnd" ? "GND" : a === "v33" ? "3V3" : "5V");

// ---------------------------------------------------------------- nets (pins joined by wires)
export interface Nets { netOf: Map<PinRef, number>; members: PinRef[][] }
export function computeNets(w: Wiring, extra?: Wire): Nets {
  const parent = new Map<PinRef, PinRef>();
  const find = (x: PinRef): PinRef => { let r = x; while (parent.get(r) !== r) r = parent.get(r)!; parent.set(x, r); return r; };
  const add = (x: PinRef) => { if (!parent.has(x)) parent.set(x, x); };
  for (const wire of extra ? [...w.wires, extra] : w.wires) {
    add(wire.a); add(wire.b);
    const ra = find(wire.a), rb = find(wire.b);
    if (ra !== rb) parent.set(ra, rb);
  }
  const ids = new Map<PinRef, number>(), members: PinRef[][] = [], netOf = new Map<PinRef, number>();
  for (const ref of parent.keys()) {
    const root = find(ref);
    if (!ids.has(root)) { ids.set(root, members.length); members.push([]); }
    const id = ids.get(root)!;
    members[id].push(ref); netOf.set(ref, id);
  }
  return { netOf, members };
}

interface NetFacts { gnd: boolean; rails: Set<Rail>; gpios: Set<number>; sigs: Set<string>; vins: PinCtx[]; straps: PinCtx[] }
function netFacts(w: Wiring, refs: PinRef[]): NetFacts {
  const f: NetFacts = { gnd: false, rails: new Set(), gpios: new Set(), sigs: new Set(), vins: [], straps: [] };
  for (const ref of refs) {
    const c = pinCtx(w, ref);
    if (!c) continue;
    const isBoard = c.part.type === "esp32s3";
    if (c.def.kind === "gnd" && isBoard) f.gnd = true;
    else if (c.def.kind === "rail") f.rails.add(c.def.rail);
    else if (c.def.kind === "gpio") f.gpios.add(c.def.gpio);
    else if (c.def.kind === "sig") f.sigs.add(c.def.signal);
    else if (c.def.kind === "vin") f.vins.push(c);
    else if (c.def.kind === "strap") f.straps.push(c);
  }
  return f;
}

// ---------------------------------------------------------------- 1b. can this wire be added?
export function canAddWire(w: Wiring, a: PinRef, b: PinRef): Verdict {
  const ca = pinCtx(w, a), cb = pinCtx(w, b);
  if (!ca || !cb) return no("That pin doesn't exist.");
  const pair = checkPair(ca, cb);
  if (!pair.ok) return pair;
  if (w.wires.some((x) => (x.a === a && x.b === b) || (x.a === b && x.b === a))) return no("Those two pins are already wired together.");

  // what the whole net would look like with this wire added: catches shorts that only appear through other wires
  const probe: Wire = { id: "probe", a, b };
  const nets = computeNets(w, probe);
  const refs = nets.members[nets.netOf.get(a)!];
  const f = netFacts(w, refs);
  if (f.gnd && f.rails.size) return no(`This would short GND to the ${[...f.rails].map(railName).join(" and ")} supply through the wires already in place.`);
  if (f.rails.size > 1) return no("This would join the 3.3 V and 5 V supplies through the wires already in place.");
  if (f.gpios.size > 1) return no(`This would connect GPIO ${[...f.gpios].sort((p, q) => p - q).join(" and GPIO ")} together through the wires already in place.`);
  for (const v of f.vins) {
    if (f.gnd) return no(`${pinName(v)} would end up tied to GND through other wires.`);
    for (const r of f.rails) if (v.def.kind === "vin" && !v.def.accepts.includes(r)) return no(`${pinName(v)} would end up on the ${railName(r)} supply through other wires, which it can't take.`);
  }
  for (const s of f.straps) {
    if (s.def.kind !== "strap") continue;
    if (f.gnd && !s.def.accepts.includes("gnd")) return no(`${pinName(s)} would end up tied to GND through other wires.`);
    for (const r of f.rails) if (!s.def.accepts.includes(r)) return no(`${pinName(s)} would end up on the ${railName(r)} supply through other wires.`);
  }
  return ok;
}

// ---------------------------------------------------------------- 2. warnings
export type Severity = "error" | "warn" | "info";
export interface Problem { severity: Severity; code: string; message: string; refs: PinRef[]; wireId?: string }

export interface Analysis { nets: Nets; problems: Problem[]; parts: Record<string, PartStatus> }

export function analyze(w: Wiring, pinMap: readonly PinAssignment[]): Analysis {
  const nets = computeNets(w);
  const problems: Problem[] = [];
  const seen = new Set<string>();
  const push = (p: Problem) => { const k = p.code + "|" + p.message; if (!seen.has(k)) { seen.add(k); problems.push(p); } };

  for (const wire of w.wires) {
    const ca = pinCtx(w, wire.a), cb = pinCtx(w, wire.b);
    if (!ca || !cb) { push({ severity: "error", code: "bad-wire", message: "A wire joins a pin that doesn't exist.", refs: [wire.a, wire.b], wireId: wire.id }); continue; }
    const verdict = checkPair(ca, cb);
    if (!verdict.ok) { push({ severity: "error", code: "bad-pair", message: verdict.reason, refs: [wire.a, wire.b], wireId: wire.id }); continue; }

    const gpioCtx = ca.def.kind === "gpio" ? ca : cb.def.kind === "gpio" ? cb : undefined;
    const partCtx = gpioCtx === ca ? cb : gpioCtx === cb ? ca : undefined;
    if (!gpioCtx || !partCtx || partCtx.def.kind !== "sig" || gpioCtx.def.kind !== "gpio") continue;
    const g = gpioCtx.def.gpio;
    const expected = assignmentFor(pinMap, partCtx.part.type, partCtx.def.id);

    if (expected && expected.gpio !== g) {
      push({ severity: "warn", code: "pin-mismatch", wireId: wire.id, refs: [wire.a, wire.b],
        message: `${pinName(partCtx)} is wired to GPIO ${g}, but the firmware pin map puts it on GPIO ${expected.gpio} (${expected.origin}${expected.source === "brief" ? ", UNVERIFIED" : ""}).` });
    }
    if (!expected && partCtx.def.signal === "imu.int") {
      push({ severity: "info", code: "unused-pin", wireId: wire.id, refs: [wire.a, wire.b], message: `${pinName(partCtx)} is not used by Dewy's firmware, so wiring it does nothing.` });
    }
    for (const other of assignmentsForGpio(pinMap, g)) {
      if (other.signal !== partCtx.def.signal && !(expected && expected.gpio === g)) {
        push({ severity: "warn", code: "gpio-conflict", wireId: wire.id, refs: [wire.a, wire.b],
          message: `GPIO ${g} is ${SIGNAL_NAMES[other.signal]} in the firmware (${other.origin}${other.source === "brief" ? ", UNVERIFIED" : ""}), but here it is wired to ${pinName(partCtx)} (${SIGNAL_NAMES[partCtx.def.signal]}).` });
      }
    }
    const reserved = RESERVED_PINS.find((r) => r.gpio === g);
    if (reserved) push({ severity: "warn", code: "reserved-pin", wireId: wire.id, refs: [wire.a, wire.b], message: `GPIO ${g} is reserved by the firmware for ${reserved.why}.` });
    const hz = BOARD_HAZARDS[g];
    if (hz) push({ severity: hz.severity, code: "board-hazard", wireId: wire.id, refs: [wire.a, wire.b], message: hz.text });
  }

  // one net carrying different kinds of signal (a bus shared by one kind of signal is fine)
  nets.members.forEach((refs) => {
    const f = netFacts(w, refs);
    if (f.sigs.size > 1) push({ severity: "warn", code: "mixed-signals", refs, message: `One net carries ${[...f.sigs].map((s) => SIGNAL_NAMES[s as keyof typeof SIGNAL_NAMES]).join(" and ")}: two different signals share GPIO ${[...f.gpios].join(", ") || "(no GPIO)"}.` });
  });

  const parts: Record<string, PartStatus> = {};
  for (const p of w.parts) parts[p.id] = partStatus(w, nets, pinMap, p);
  return { nets, problems, parts };
}

// ---------------------------------------------------------------- 3. is each part connected the way the firmware needs?
export interface PinCheck { pin: string; label: string; group?: string; state: "ok" | "unwired" | "wrong"; detail?: string }
export interface PartStatus {
  partId: string; type: PartType; checks: PinCheck[];
  /** a feature (power, screen, touch, bus, i2s ...) is wired when every required pin in its group is */
  groups: Record<string, boolean>;
  /** every required pin of the whole part is right */
  ok: boolean;
  /** some of the pins this part is checked against come from the brief, not from firmware code */
  unverified: boolean;
}

function partStatus(w: Wiring, nets: Nets, pinMap: readonly PinAssignment[], part: PlacedPart): PartStatus {
  const def = PART_DEFS[part.type];
  const checks: PinCheck[] = [];
  let unverified = false;
  for (const pin of def.pins) {
    if (!pin.required) continue;
    const ref = makeRef(part.id, pin.id);
    const netId = nets.netOf.get(ref);
    const facts = netId === undefined ? undefined : netFacts(w, nets.members[netId]);
    const base = { pin: pin.id, label: pin.label, group: pin.group };
    if (pin.kind === "gnd") {
      checks.push(facts?.gnd ? { ...base, state: "ok" } : { ...base, state: "unwired", detail: "not connected to a GND pin on the board" });
    } else if (pin.kind === "vin") {
      const good = facts && pin.accepts.some((r) => facts.rails.has(r));
      checks.push(good ? { ...base, state: "ok" } : { ...base, state: "unwired", detail: `not connected to ${pin.accepts.map(railName).join(" or ")} on the board` });
    } else if (pin.kind === "sig") {
      const want = assignmentFor(pinMap, part.type, pin.id);
      if (want?.source === "brief") unverified = true;
      if (!want) { checks.push({ ...base, state: "ok" }); continue; }
      if (pin.alsoRail && facts?.rails.has(pin.alsoRail)) { checks.push({ ...base, state: "ok", detail: "tied to 3V3: always on" }); continue; }
      if (facts?.gpios.has(want.gpio)) checks.push({ ...base, state: "ok" });
      else if (facts && facts.gpios.size) checks.push({ ...base, state: "wrong", detail: `wired to GPIO ${[...facts.gpios].join(", ")}; the firmware pin map expects GPIO ${want.gpio}` });
      else checks.push({ ...base, state: "unwired", detail: `not connected: expected GPIO ${want.gpio}` });
    }
  }
  const groups: Record<string, boolean> = {};
  for (const c of checks) groups[c.group ?? "other"] = (groups[c.group ?? "other"] ?? true) && c.state === "ok";
  return { partId: part.id, type: part.type, checks, groups, ok: checks.every((c) => c.state === "ok"), unverified };
}

/** What the simulation should treat as present, from the wiring. */
export interface Hardware { display: boolean; touch: boolean; imu: boolean; mic: boolean; amp: boolean; boardPlaced: boolean }
export function hardwareFrom(w: Wiring, a: Analysis): Hardware {
  const get = (type: PartType) => { const p = w.parts.find((x) => x.type === type); return p ? a.parts[p.id] : undefined; };
  const d = get("display"), i = get("imu"), m = get("mic"), s = get("amp");
  return {
    boardPlaced: w.parts.some((p) => p.type === "esp32s3"),
    display: !!d && !!d.groups.power && !!d.groups.screen,
    touch: !!d && !!d.groups.power && !!d.groups.touch,
    imu: !!i && i.ok,
    mic: !!m && m.ok,
    amp: !!s && s.ok,
  };
}

// ---------------------------------------------------------------- saving and loading
export const WIRING_FORMAT = "virtual-dewy-wiring";
export const WIRING_VERSION = 1;
const MAX_PARTS = 16, MAX_WIRES = 400, MAX_COORD = 5000;

export function serializeWiring(w: Wiring): string {
  return JSON.stringify({
    format: WIRING_FORMAT, version: WIRING_VERSION,
    parts: w.parts.map((p) => ({ id: p.id, type: p.type, x: Math.round(p.x), y: Math.round(p.y) })),
    wires: w.wires.map((x) => ({ from: x.a, to: x.b })),
  }, null, 2) + "\n";
}

export interface LoadResult { wiring: Wiring | null; issues: string[] }

/** Reads saved wiring. The input is untrusted: every field is checked and bad wires are skipped with a message. */
export function parseWiring(text: string): LoadResult {
  const issues: string[] = [];
  if (text.length > 200_000) return { wiring: null, issues: ["That file is too large to be a wiring file."] };
  let raw: unknown;
  try { raw = JSON.parse(text); } catch { return { wiring: null, issues: ["That isn't valid JSON."] }; }
  if (!isObj(raw) || raw.format !== WIRING_FORMAT) return { wiring: null, issues: [`Not a Virtual Dewy wiring file (format must be "${WIRING_FORMAT}").`] };
  if (raw.version !== WIRING_VERSION) return { wiring: null, issues: [`Unsupported wiring file version ${String(raw.version)} (this page reads version ${WIRING_VERSION}).`] };
  if (!Array.isArray(raw.parts) || !Array.isArray(raw.wires)) return { wiring: null, issues: ['The file needs a "parts" list and a "wires" list.'] };
  if (raw.parts.length > MAX_PARTS || raw.wires.length > MAX_WIRES) return { wiring: null, issues: ["Too many parts or wires."] };

  const parts: PlacedPart[] = [];
  for (const [i, p] of raw.parts.entries()) {
    if (!isObj(p) || typeof p.type !== "string" || !(p.type in PART_DEFS)) { issues.push(`Part ${i + 1}: unknown part type.`); continue; }
    const type = p.type as PartType;
    const def = PART_DEFS[type];
    if (p.id !== def.id) { issues.push(`Part ${i + 1}: "${String(p.id)}" isn't the id of a ${def.name} (expected "${def.id}").`); continue; }
    if (parts.some((q) => q.type === type)) { issues.push(`Part ${i + 1}: a second ${def.name} was skipped (one of each part).`); continue; }
    const x = num(p.x), y = num(p.y);
    if (x === null || y === null) { issues.push(`Part ${i + 1} (${def.name}): bad position.`); continue; }
    parts.push({ id: def.id, type, x, y });
  }

  const wiring: Wiring = { parts, wires: [] };
  for (const [i, x] of raw.wires.entries()) {
    if (!isObj(x) || typeof x.from !== "string" || typeof x.to !== "string" || x.from.length > 64 || x.to.length > 64) { issues.push(`Wire ${i + 1}: needs "from" and "to" pins.`); continue; }
    const v = canAddWire(wiring, x.from, x.to);
    if (!v.ok) { issues.push(`Wire ${i + 1} (${x.from} to ${x.to}) skipped: ${v.reason}`); continue; }
    wiring.wires.push({ id: `w${wiring.wires.length + 1}`, a: x.from, b: x.to });
  }
  return { wiring, issues };
}

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const num = (v: unknown): number | null => (typeof v === "number" && Number.isFinite(v) && Math.abs(v) <= MAX_COORD ? v : null);
