import { describe, expect, it } from "vitest";
import { BOARD_HAZARDS, PART_DEFS, findPin } from "../src/core/parts";
import { PIN_MAP, RESERVED_PINS, assignmentsForGpio, pinMapFromSources } from "../src/core/pinmap";
import { parseFirmwarePins } from "../src/core/firmware-source";

// GPIOs that exist on the ESP32-S3-DevKitC-1 headers
const HEADER_GPIOS = new Set([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48]);

describe("pin map", () => {
  it("lists every pin number the brief and the firmware give", () => {
    const by = (type: string) => PIN_MAP.filter((e) => e.partType === type).map((e) => `${e.partPin}=${e.gpio}`).sort();
    expect(by("mic")).toEqual(["SCK=15", "SD=17", "WS=16"]);
    expect(by("amp")).toEqual(["BCLK=18", "DIN=38", "LRC=21"]);
    expect(by("imu")).toEqual(["SCL=5", "SDA=4"]);
  });

  it("only uses GPIOs that exist on the board's headers", () => {
    for (const e of PIN_MAP) expect(HEADER_GPIOS.has(e.gpio), `GPIO ${e.gpio} for ${e.partType} ${e.partPin}`).toBe(true);
  });

  it("refers to pins that exist on the part, with the same signal", () => {
    for (const e of PIN_MAP) {
      const pin = findPin(e.partType, e.partPin);
      expect(pin, `${e.partType} ${e.partPin}`).toBeDefined();
      expect(pin!.kind).toBe("sig");
      expect(pin!.kind === "sig" && pin!.signal).toBe(e.signal);
    }
  });

  it("every GPIO in the map has a matching pin on the board part", () => {
    for (const e of PIN_MAP) expect(findPin("esp32s3", `GPIO${e.gpio}`)?.kind).toBe("gpio");
  });

  it("gives each part pin one GPIO, and a GPIO is only shared by pins carrying the same signal (SPI bus)", () => {
    const seen = new Set<string>();
    for (const e of PIN_MAP) { const k = `${e.partType}:${e.partPin}`; expect(seen.has(k), k).toBe(false); seen.add(k); }
    for (const g of new Set(PIN_MAP.map((e) => e.gpio))) {
      const signals = new Set(assignmentsForGpio(PIN_MAP, g).map((e) => e.signal));
      expect(signals.size, `GPIO ${g}`).toBe(1);
    }
  });

  it("keeps clear of the PSRAM pins, the strapping pins and USB", () => {
    for (const e of PIN_MAP) expect(BOARD_HAZARDS[e.gpio]?.severity === "error" || [0, 3, 45, 46, 19, 20].includes(e.gpio), `GPIO ${e.gpio}`).toBe(false);
  });

  it("marks only the voice pins as unverified (they come from the brief)", () => {
    expect(PIN_MAP.filter((e) => e.source === "brief").map((e) => e.partType).sort()).toEqual(["amp", "amp", "amp", "mic", "mic", "mic"]);
    expect(PIN_MAP.filter((e) => e.source === "firmware").every((e) => e.define?.startsWith("PIN_"))).toBe(true);
  });

  it("derives the firmware's numbers from edited code", () => {
    const edited = pinMapFromSources(parseFirmwarePins([{ name: "config.h", text: "#define PIN_I2C_SDA 9\n#define PIN_I2C_SCL 10\n" }]));
    expect(edited.find((e) => e.partPin === "SDA" && e.partType === "imu")!.gpio).toBe(9);
    expect(edited.find((e) => e.partPin === "SCL" && e.partType === "imu")!.gpio).toBe(10);
    expect(edited.find((e) => e.partPin === "CS" && e.partType === "display")!.gpio).toBe(10);   // untouched macros keep their number
  });

  it("knows the battery pin is reserved but switched off", () => {
    expect(RESERVED_PINS).toEqual([expect.objectContaining({ gpio: 1, define: "PIN_BATTERY", enabled: false })]);
  });

  it("has a unique id for every part and pin", () => {
    const ids = new Set<string>();
    for (const def of Object.values(PART_DEFS)) {
      expect(ids.has(def.id)).toBe(false); ids.add(def.id);
      const pins = new Set<string>();
      for (const p of def.pins) { expect(pins.has(p.id), `${def.type} ${p.id}`).toBe(false); pins.add(p.id); }
    }
  });
});
