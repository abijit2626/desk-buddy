// The pin map: which ESP32-S3 GPIO each part pin must be wired to.
//
// This is the single table the wiring panel uses. Each entry says where its number comes from:
//   source "firmware": a `#define PIN_*` in firmware/dewy/config.h (the tests fail if the number here drifts from it)
//   source "brief":    given in the project brief only. firmware/dewy has no voice.h yet, so there is nothing to
//                      check these against. They are marked UNVERIFIED everywhere they appear.
import type { PartType, Signal } from "./parts";
import { resolvePin, type FirmwarePins } from "./firmware-source";

export type PinSource = "firmware" | "brief";

export interface PinAssignment {
  gpio: number;
  partType: PartType;
  partPin: string;
  signal: Signal;
  source: PinSource;
  /** config.h macro this number comes from (firmware entries only) */
  define?: string;
  /** where it comes from, for display */
  origin: string;
}

const fw = (gpio: number, define: string, partType: PartType, partPin: string, signal: Signal): PinAssignment =>
  ({ gpio, partType, partPin, signal, source: "firmware", define, origin: `config.h ${define}` });
const brief = (gpio: number, partType: PartType, partPin: string, signal: Signal): PinAssignment =>
  ({ gpio, partType, partPin, signal, source: "brief", origin: "project brief (no voice.h in firmware/dewy yet)" });

export const PIN_MAP: readonly PinAssignment[] = [
  // screen (SPI) and touch share MOSI / MISO / SCLK
  fw(11, "PIN_TFT_MOSI", "display", "SDI", "spi.mosi"),
  fw(12, "PIN_TFT_SCLK", "display", "SCK", "spi.sclk"),
  fw(13, "PIN_TFT_MISO", "display", "SDO", "spi.miso"),
  fw(10, "PIN_TFT_CS", "display", "CS", "tft.cs"),
  fw(9, "PIN_TFT_DC", "display", "DC", "tft.dc"),
  fw(8, "PIN_TFT_RST", "display", "RESET", "tft.rst"),
  fw(7, "PIN_TFT_BL", "display", "LED", "tft.bl"),
  fw(12, "PIN_TFT_SCLK", "display", "T_CLK", "spi.sclk"),
  fw(11, "PIN_TFT_MOSI", "display", "T_DIN", "spi.mosi"),
  fw(13, "PIN_TFT_MISO", "display", "T_DO", "spi.miso"),
  fw(14, "PIN_TOUCH_CS", "display", "T_CS", "touch.cs"),
  fw(6, "PIN_TOUCH_IRQ", "display", "T_IRQ", "touch.irq"),
  // motion sensor (GY-87, MPU6050 at I2C address 0x68)
  fw(4, "PIN_I2C_SDA", "imu", "SDA", "i2c.sda"),
  fw(5, "PIN_I2C_SCL", "imu", "SCL", "i2c.scl"),
  // INMP441 microphone and MAX98357A amplifier: from the brief, UNVERIFIED
  brief(15, "mic", "SCK", "i2s.bclk"),
  brief(16, "mic", "WS", "i2s.ws"),
  brief(17, "mic", "SD", "i2s.din"),
  brief(18, "amp", "BCLK", "i2s.bclk"),
  brief(21, "amp", "LRC", "i2s.ws"),
  brief(38, "amp", "DIN", "i2s.dout"),
];

/** GPIOs the firmware reserves but that no part in the panel uses. */
export interface ReservedPin { gpio: number; define: string; why: string; enabled: boolean }
export const RESERVED_PINS: readonly ReservedPin[] = [
  { gpio: 1, define: "PIN_BATTERY", why: "battery voltage sense (two 100k resistors); switched off by BATTERY_ENABLED 0 in config.h", enabled: false },
];

/** The same table, with every firmware entry's number re-read from the code you loaded (config.h edits flow through). */
export function pinMapFromSources(fp: FirmwarePins): PinAssignment[] {
  return PIN_MAP.map((e) => {
    if (e.source !== "firmware" || !e.define) return { ...e };
    const n = resolvePin(e.define, fp.defines);
    return n == null ? { ...e } : { ...e, gpio: n };
  });
}

export const assignmentFor = (map: readonly PinAssignment[], partType: PartType, partPin: string) =>
  map.find((e) => e.partType === partType && e.partPin === partPin);
export const assignmentsForGpio = (map: readonly PinAssignment[], gpio: number) => map.filter((e) => e.gpio === gpio);
