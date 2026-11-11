// Checks that the pin numbers the wiring panel uses match Dewy's real firmware: dewy.ino and the files it includes,
// the config.h macros, display.h's driver setup, and (as the final proof) the pins the compiled firmware reports at run time.
import { readFileSync, readdirSync, existsSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { analyseSources, isSecretFileName, parseFirmwarePins, resolvePin, type SourceFile } from "../src/core/firmware-source";
import { PIN_MAP, RESERVED_PINS, assignmentFor } from "../src/core/pinmap";
import { PART_DEFS } from "../src/core/parts";
import { FIRMWARE_DIR, bootCore } from "./helpers";

// load dewy.ino and every other .ino/.h next to it, never secrets.h
const files: SourceFile[] = readdirSync(FIRMWARE_DIR)
  .filter((n) => /\.(ino|h)$/.test(n) && !isSecretFileName(n))
  .map((name) => ({ name, text: readFileSync(join(FIRMWARE_DIR, name), "utf8") }));
const fp = parseFirmwarePins(files);
const gpioOf = (type: "display" | "imu" | "mic" | "amp", pin: string) => assignmentFor(PIN_MAP, type, pin)!.gpio;

describe("dewy.ino and the wiring panel agree on pin numbers", () => {
  it("loads dewy.ino and follows its #includes", () => {
    const a = analyseSources(files);
    expect(a.reports[0]).toMatchObject({ name: "dewy.ino", role: "sketch" });
    for (const h of ["config.h", "display.h", "face.h", "motion.h", "cloud.h", "portal.h", "cacerts.h"]) {
      expect(a.reports.find((r) => r.name === h)?.role, h).toBe("included");
    }
    expect(a.missing).toEqual([]);
    expect(a.system).toContain("mbedtls/sha256.h");       // a library header, not a file in the folder
  });

  it("every firmware pin the panel uses is the number in config.h", () => {
    const firmwareEntries = PIN_MAP.filter((e) => e.source === "firmware");
    expect(firmwareEntries.length).toBeGreaterThan(10);
    for (const e of firmwareEntries) {
      expect(fp.defines[e.define!], `${e.define} is defined in config.h`).toBeDefined();
      expect(e.gpio, `${e.partType} ${e.partPin} (${e.define})`).toBe(fp.defines[e.define!]);
    }
  });

  it("every PIN_ macro in config.h is accounted for by the panel (or reserved on purpose)", () => {
    const covered = new Set([...PIN_MAP.map((e) => e.define), ...RESERVED_PINS.map((r) => r.define)]);
    for (const name of Object.keys(fp.defines)) expect(covered.has(name), `${name} in config.h but not in the panel's pin map`).toBe(true);
    for (const r of RESERVED_PINS) expect(fp.defines[r.define], r.define).toBe(r.gpio);
  });

  it("display.h hands the screen and touch pins to the driver the way the panel wires them", () => {
    const d = fp.display, def = fp.defines;
    const n = (group: keyof typeof d, key: string) => resolvePin(d[group][key], def);
    expect(n("bus", "pin_sclk")).toBe(gpioOf("display", "SCK"));
    expect(n("bus", "pin_mosi")).toBe(gpioOf("display", "SDI"));
    expect(n("bus", "pin_miso")).toBe(gpioOf("display", "SDO"));
    expect(n("bus", "pin_dc")).toBe(gpioOf("display", "DC"));
    expect(n("panel", "pin_cs")).toBe(gpioOf("display", "CS"));
    expect(n("panel", "pin_rst")).toBe(gpioOf("display", "RESET"));
    expect(n("light", "pin_bl")).toBe(gpioOf("display", "LED"));
    expect(n("touch", "pin_cs")).toBe(gpioOf("display", "T_CS"));
    expect(n("touch", "pin_int")).toBe(gpioOf("display", "T_IRQ"));
    expect(n("touch", "pin_sclk")).toBe(gpioOf("display", "T_CLK"));
    expect(n("touch", "pin_mosi")).toBe(gpioOf("display", "T_DIN"));
    expect(n("touch", "pin_miso")).toBe(gpioOf("display", "T_DO"));
  });

  it("dewy.ino starts the I2C bus on the pins the panel gives the motion sensor", () => {
    expect(resolvePin(fp.wire.sda, fp.defines)).toBe(gpioOf("imu", "SDA"));
    expect(resolvePin(fp.wire.scl, fp.defines)).toBe(gpioOf("imu", "SCL"));
  });

  it("motion.h talks to the MPU6050 at address 0x68, which is what the part is", () => {
    expect(files.find((f) => f.name === "motion.h")!.text).toMatch(/beginTransmission\(0x68\)/);
    expect(PART_DEFS.imu.name).toMatch(/MPU6050/);
  });

  it("the firmware's pins are all different GPIOs unless they are the shared SPI bus", () => {
    const byGpio = new Map<number, Set<string>>();
    for (const e of PIN_MAP.filter((x) => x.source === "firmware")) (byGpio.get(e.gpio) ?? byGpio.set(e.gpio, new Set()).get(e.gpio)!).add(e.signal);
    for (const [g, sigs] of byGpio) expect(sigs.size, `GPIO ${g} carries ${[...sigs].join(", ")}`).toBe(1);
  });

  it("the compiled firmware reports the same pins at run time (what display.h and Wire.begin really did)", async () => {
    const d = await bootCore();
    const p = d.core.pins();
    expect(p).toMatchObject({
      i2cSda: gpioOf("imu", "SDA"), i2cScl: gpioOf("imu", "SCL"),
      tftSclk: gpioOf("display", "SCK"), tftMosi: gpioOf("display", "SDI"), tftMiso: gpioOf("display", "SDO"), tftDc: gpioOf("display", "DC"),
      tftCs: gpioOf("display", "CS"), tftRst: gpioOf("display", "RESET"), tftBl: gpioOf("display", "LED"),
      touchCs: gpioOf("display", "T_CS"), touchIrq: gpioOf("display", "T_IRQ"), touchSclk: gpioOf("display", "T_CLK"),
      touchMosi: gpioOf("display", "T_DIN"), touchMiso: gpioOf("display", "T_DO"),
    });
    expect(p.battery).toBe(-1);        // BATTERY_ENABLED is 0
  });
});

describe("the voice pins (UNVERIFIED: from the brief, not from firmware code)", () => {
  const voiceFile = join(FIRMWARE_DIR, "voice.h");
  const hasVoice = existsSync(voiceFile);

  it("are the numbers in the brief", () => {
    expect([gpioOf("mic", "SCK"), gpioOf("mic", "WS"), gpioOf("mic", "SD")]).toEqual([15, 16, 17]);
    expect([gpioOf("amp", "BCLK"), gpioOf("amp", "LRC"), gpioOf("amp", "DIN")]).toEqual([18, 21, 38]);
  });

  it("don't clash with any pin the firmware actually uses", () => {
    const used = new Set(Object.values(fp.defines));
    for (const e of PIN_MAP.filter((x) => x.source === "brief")) expect(used.has(e.gpio), `GPIO ${e.gpio}`).toBe(false);
  });

  it.runIf(!hasVoice)("cannot be checked yet: firmware/dewy has no voice.h and dewy.ino has no I2S code", () => {
    expect(files.some((f) => /driver\/i2s|ESP_I2S|i2s_/i.test(f.text))).toBe(false);
    expect(analyseSources(files).order).not.toContain("voice.h");
  });

  // when voice.h appears, the brief's numbers must show up in it
  it.runIf(hasVoice)("appear in voice.h", () => {
    const text = readFileSync(voiceFile, "utf8");
    for (const e of PIN_MAP.filter((x) => x.source === "brief")) expect(text, `GPIO ${e.gpio}`).toMatch(new RegExp(`\\b${e.gpio}\\b`));
  });
});
