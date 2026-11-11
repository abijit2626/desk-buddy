import { describe, expect, it } from "vitest";
import { PIN_MAP, pinMapFromSources } from "../src/core/pinmap";
import { referenceWiring, placePart, emptyWiring } from "../src/core/presets";
import { analyze, canAddWire, hardwareFrom, parseWiring, serializeWiring, makeRef, type Wiring } from "../src/core/wiring";
import { PART_ORDER } from "../src/core/parts";

const full = (): Wiring => referenceWiring(PIN_MAP);
const board = (p: string) => makeRef("board", p);
const gpio = (n: number) => board(`GPIO${n}`);

/** all parts on the canvas, no wires */
const bare = () => PART_ORDER.reduce((w, t) => placePart(w, t), emptyWiring());
const verdict = (w: Wiring, a: string, b: string) => canAddWire(w, a, b);

describe("which pairs of pins may be wired", () => {
  it("accepts GND to GND and the power rails to the pins that take them", () => {
    const w = bare();
    expect(verdict(w, board("GND.1"), makeRef("lcd", "GND")).ok).toBe(true);
    expect(verdict(w, board("GND.1"), makeRef("amp", "GND")).ok).toBe(true);
    expect(verdict(w, board("3V3.1"), makeRef("lcd", "VCC")).ok).toBe(true);
    expect(verdict(w, board("3V3.2"), makeRef("mic", "VDD")).ok).toBe(true);
    expect(verdict(w, board("5V"), makeRef("amp", "VIN")).ok).toBe(true);
    expect(verdict(w, board("5V"), makeRef("imu", "VCC")).ok).toBe(true);   // GY-87 has a regulator
  });

  it("accepts a GPIO to a signal pin, whichever way round you drag", () => {
    const w = bare();
    expect(verdict(w, gpio(17), makeRef("mic", "SD")).ok).toBe(true);
    expect(verdict(w, makeRef("mic", "SD"), gpio(17)).ok).toBe(true);
    expect(verdict(w, gpio(4), makeRef("imu", "SDA")).ok).toBe(true);
  });

  it("refuses shorts", () => {
    const w = bare();
    const r1 = verdict(w, board("GND.1"), board("3V3.1"));
    expect(r1.ok).toBe(false);
    const r2 = verdict(w, board("3V3.1"), board("5V"));
    expect(r2.ok).toBe(false);
    expect(verdict(w, board("GND.1"), makeRef("lcd", "VCC")).ok).toBe(false);
    expect(verdict(w, board("3V3.1"), makeRef("lcd", "GND")).ok).toBe(false);
  });

  it("refuses 5 V into a 3.3 V-only part", () => {
    const w = bare();
    const r = verdict(w, board("5V"), makeRef("mic", "VDD"));
    expect(r).toEqual({ ok: false, reason: expect.stringMatching(/3\.3 V.*5 V would damage/) });
    expect(verdict(w, board("5V"), makeRef("lcd", "VCC")).ok).toBe(false);
  });

  it("refuses a GPIO to a GPIO, a GPIO to a rail, and a signal to a power pin", () => {
    const w = bare();
    expect(verdict(w, gpio(4), gpio(5)).ok).toBe(false);
    expect(verdict(w, gpio(4), board("3V3.1")).ok).toBe(false);
    expect(verdict(w, gpio(4), board("GND.1")).ok).toBe(false);
    expect(verdict(w, gpio(4), makeRef("lcd", "VCC")).ok).toBe(false);
    expect(verdict(w, gpio(4), makeRef("mic", "LR")).ok).toBe(false);      // strap pins go to GND or a rail
  });

  it("refuses mixing different signals: SD to SDA, SCK to the I2C clock, mosi to miso", () => {
    const w = bare();
    expect(verdict(w, makeRef("mic", "SD"), makeRef("imu", "SDA")).ok).toBe(false);
    expect(verdict(w, makeRef("mic", "SCK"), makeRef("imu", "SCL")).ok).toBe(false);
    expect(verdict(w, makeRef("lcd", "SDI"), makeRef("lcd", "SDO")).ok).toBe(false);
    // the same signal on two parts is a shared bus and is fine: mic SD to the amp's DIN is NOT (data in vs data out)
    expect(verdict(w, makeRef("lcd", "SDI"), makeRef("lcd", "T_DIN")).ok).toBe(true);
    expect(verdict(w, makeRef("mic", "SD"), makeRef("amp", "DIN")).ok).toBe(false);
  });

  it("only lets straps go where the datasheet allows", () => {
    const w = bare();
    expect(verdict(w, board("GND.1"), makeRef("lcd", "IM0")).ok).toBe(true);
    expect(verdict(w, board("3V3.1"), makeRef("lcd", "IM0")).ok).toBe(false);
    expect(verdict(w, board("3V3.1"), makeRef("lcd", "IM1")).ok).toBe(true);
    expect(verdict(w, board("GND.1"), makeRef("lcd", "IM1")).ok).toBe(false);
    expect(verdict(w, board("GND.1"), makeRef("mic", "LR")).ok).toBe(true);
    expect(verdict(w, board("3V3.1"), makeRef("mic", "LR")).ok).toBe(true);
  });

  it("lets the display backlight go to 3V3 (always on) as well as a GPIO", () => {
    const w = bare();
    expect(verdict(w, board("3V3.1"), makeRef("lcd", "LED")).ok).toBe(true);
    expect(verdict(w, gpio(7), makeRef("lcd", "LED")).ok).toBe(true);
    expect(verdict(w, board("3V3.1"), makeRef("lcd", "CS")).ok).toBe(false);
  });

  it("refuses RST, wiring a pin to itself, and duplicates", () => {
    let w = bare();
    expect(verdict(w, board("RST"), board("GND.1")).ok).toBe(false);
    expect(verdict(w, gpio(4), gpio(4)).ok).toBe(false);
    w = { ...w, wires: [{ id: "w1", a: gpio(4), b: makeRef("imu", "SDA") }] };
    expect(verdict(w, makeRef("imu", "SDA"), gpio(4)).ok).toBe(false);
  });

  it("catches shorts that only appear through other wires", () => {
    let w = bare();
    w = { ...w, wires: [{ id: "w1", a: board("GND.1"), b: makeRef("mic", "LR") }] };       // LR strapped to GND
    const r = verdict(w, board("3V3.1"), makeRef("mic", "LR"));                              // ...and now to 3V3 as well
    expect(r.ok).toBe(false);
    expect(r.ok === false && r.reason).toMatch(/short|tied/i);

    w = { ...bare(), wires: [{ id: "w1", a: gpio(11), b: makeRef("lcd", "SDI") }] };
    const two = verdict(w, gpio(12), makeRef("lcd", "SDI"));                                 // SDI would join GPIO 11 and 12
    expect(two.ok).toBe(false);
    expect(two.ok === false && two.reason).toMatch(/GPIO 11 and GPIO 12/);
  });

  it("refuses supply wires that would reach a part through another part", () => {
    let w = bare();
    w = { ...w, wires: [{ id: "w1", a: board("5V"), b: makeRef("amp", "VIN") }] };
    const r = verdict(w, makeRef("amp", "VIN"), makeRef("mic", "VDD"));                      // would put 5 V on the 3.3 V-only mic
    expect(r.ok).toBe(false);
  });
});

describe("the reference wiring", () => {
  it("is accepted wire by wire and has no warnings", () => {
    const w = full();
    expect(w.wires.length).toBeGreaterThan(30);
    const a = analyze(w, PIN_MAP);
    expect(a.problems.filter((p) => p.severity !== "info")).toEqual([]);
    // every wire passes the same rules a person dragging it would face
    const rebuilt = parseWiring(serializeWiring(w));
    expect(rebuilt.issues).toEqual([]);
    expect(rebuilt.wiring!.wires.length).toBe(w.wires.length);
  });

  it("gives the simulation every piece of hardware", () => {
    const w = full();
    expect(hardwareFrom(w, analyze(w, PIN_MAP))).toEqual({ boardPlaced: true, display: true, touch: true, imu: true, mic: true, amp: true });
  });

  it("notes that the amplifier pin may share the board's RGB LED", () => {
    const infos = analyze(full(), PIN_MAP).problems.filter((p) => p.severity === "info");
    expect(infos.some((p) => /GPIO 38/.test(p.message))).toBe(true);
  });

  it("marks the voice parts unverified and the rest verified", () => {
    const a = analyze(full(), PIN_MAP);
    expect(a.parts.mic.unverified).toBe(true);
    expect(a.parts.amp.unverified).toBe(true);
    expect(a.parts.lcd.unverified).toBe(false);
    expect(a.parts.imu.unverified).toBe(false);
  });
});

describe("warnings against the firmware's pin map", () => {
  const withWire = (a: string, b: string, base: Wiring = bare()): Wiring => ({ ...base, wires: [...base.wires, { id: "x", a, b }] });

  it("warns when I2C SDA is not on the GPIO the firmware uses", () => {
    const a = analyze(withWire(gpio(9), makeRef("imu", "SDA")), PIN_MAP);
    const w = a.problems.find((p) => p.code === "pin-mismatch");
    expect(w?.severity).toBe("warn");
    expect(w?.message).toMatch(/GPIO 9.*GPIO 4.*PIN_I2C_SDA/);
  });

  it("warns when a firmware GPIO is reused for something else", () => {
    const a = analyze(withWire(gpio(4), makeRef("mic", "SD")), PIN_MAP);
    const c = a.problems.find((p) => p.code === "gpio-conflict");
    expect(c?.message).toMatch(/GPIO 4 is I2C SDA in the firmware/);
  });

  it("does not warn when the wire matches the pin map", () => {
    const a = analyze(withWire(gpio(4), makeRef("imu", "SDA")), PIN_MAP);
    expect(a.problems.filter((p) => p.severity !== "info")).toEqual([]);
  });

  it("flags unverified voice pins when they are wired differently from the brief", () => {
    const a = analyze(withWire(gpio(16), makeRef("mic", "SD")), PIN_MAP);
    expect(a.problems.some((p) => p.code === "pin-mismatch" && /UNVERIFIED/.test(p.message))).toBe(true);
  });

  it("warns about board hazards: PSRAM, strapping, USB, the battery pin", () => {
    const msg = (n: number, part = makeRef("mic", "SD")) => analyze(withWire(gpio(n), part), PIN_MAP).problems.map((p) => p.severity + ": " + p.message).join("\n");
    expect(msg(35)).toMatch(/error: .*PSRAM/);
    expect(msg(0)).toMatch(/warn: .*strapping/);
    expect(msg(19)).toMatch(/warn: .*USB/);
    expect(msg(1)).toMatch(/GPIO 1 is reserved by the firmware for battery/);
  });

  it("follows edited firmware pins", () => {
    const edited = pinMapFromSources({ defines: { PIN_I2C_SDA: 9 }, display: { bus: {}, panel: {}, light: {}, touch: {} }, wire: { sda: null, scl: null } });
    expect(analyze(withWire(gpio(9), makeRef("imu", "SDA")), edited).problems.filter((p) => p.severity !== "info")).toEqual([]);
    expect(analyze(withWire(gpio(4), makeRef("imu", "SDA")), edited).problems.some((p) => p.code === "pin-mismatch")).toBe(true);
  });
});

describe("is each part connected well enough to work", () => {
  it("reports which pins are missing or wrong", () => {
    let w = full();
    w = { ...w, wires: w.wires.filter((x) => !(x.a === gpio(5) && x.b === makeRef("imu", "SCL"))) };    // unplug SCL
    w = { ...w, wires: [...w.wires, { id: "q", a: gpio(2), b: makeRef("lcd", "DC") }].filter((x) => !(x.a === gpio(9))) };   // DC on the wrong pin
    const a = analyze(w, PIN_MAP);
    expect(a.parts.imu.ok).toBe(false);
    expect(a.parts.imu.checks.find((c) => c.pin === "SCL")).toMatchObject({ state: "unwired" });
    expect(a.parts.lcd.checks.find((c) => c.pin === "DC")).toMatchObject({ state: "wrong", detail: expect.stringMatching(/GPIO 2.*expects GPIO 9/) });
    const hw = hardwareFrom(w, a);
    expect(hw.imu).toBe(false);
    expect(hw.display).toBe(false);
    expect(hw.touch).toBe(true);        // touch pins are still fine
  });

  it("needs power and ground", () => {
    const w0 = full();
    const w = { ...w0, wires: w0.wires.filter((x) => x.b !== makeRef("lcd", "VCC")) };
    expect(hardwareFrom(w, analyze(w, PIN_MAP)).display).toBe(false);
  });
});

describe("saving and loading wiring as JSON", () => {
  it("round-trips", () => {
    const w = full();
    const text = serializeWiring(w);
    const back = parseWiring(text);
    expect(back.issues).toEqual([]);
    expect(back.wiring!.parts).toEqual(w.parts);
    expect(back.wiring!.wires.map((x) => [x.a, x.b])).toEqual(w.wires.map((x) => [x.a, x.b]));
    expect(JSON.parse(text)).toMatchObject({ format: "virtual-dewy-wiring", version: 1 });
  });

  it("refuses files that are not wiring", () => {
    for (const bad of ["not json", "[]", "{}", '{"format":"virtual-dewy-wiring","version":9,"parts":[],"wires":[]}', '{"format":"virtual-dewy-wiring","version":1}']) {
      const r = parseWiring(bad);
      expect(r.wiring, bad).toBeNull();
      expect(r.issues.length).toBeGreaterThan(0);
    }
  });

  it("skips bad parts and bad wires and says why, keeping the good ones", () => {
    const text = JSON.stringify({
      format: "virtual-dewy-wiring", version: 1,
      parts: [{ id: "board", type: "esp32s3", x: 10, y: 10 }, { id: "imu", type: "imu", x: 300, y: 10 }, { id: "x", type: "toaster", x: 0, y: 0 }, { id: "lcd", type: "display", x: "far", y: 0 }],
      wires: [
        { from: "board:GPIO4", to: "imu:SDA" },
        { from: "board:GND.1", to: "board:3V3.1" },
        { from: "board:GPIO5", to: "imu:NOPE" },
        { from: "ghost:GPIO5", to: "imu:SCL" },
        { from: 7, to: "imu:SCL" },
      ],
    });
    const r = parseWiring(text);
    expect(r.wiring!.parts.map((p) => p.id)).toEqual(["board", "imu"]);
    expect(r.wiring!.wires).toHaveLength(1);
    expect(r.issues.join("\n")).toMatch(/toaster|unknown part/);
    expect(r.issues.join("\n")).toMatch(/short circuit/);
    expect(r.issues.filter((i) => /Wire/.test(i))).toHaveLength(4);
  });

  it("refuses huge inputs and crazy coordinates", () => {
    expect(parseWiring("x".repeat(300_000)).wiring).toBeNull();
    const r = parseWiring(JSON.stringify({ format: "virtual-dewy-wiring", version: 1, parts: [{ id: "board", type: "esp32s3", x: 1e9, y: 0 }], wires: [] }));
    expect(r.wiring!.parts).toHaveLength(0);
  });

  it("only keeps one of each part", () => {
    const r = parseWiring(JSON.stringify({ format: "virtual-dewy-wiring", version: 1, parts: [{ id: "imu", type: "imu", x: 1, y: 1 }, { id: "imu", type: "imu", x: 2, y: 2 }], wires: [] }));
    expect(r.wiring!.parts).toHaveLength(1);
  });
});
