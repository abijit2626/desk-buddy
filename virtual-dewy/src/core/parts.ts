// The parts you can drag onto the canvas, and every pin on them.
import type { Honesty } from "./honesty";

export type PartType = "esp32s3" | "display" | "imu" | "mic" | "amp" | "wifi" | "cloud";
export type Rail = "v33" | "v5";

export type Signal =
  | "spi.mosi" | "spi.miso" | "spi.sclk" | "tft.cs" | "tft.dc" | "tft.rst" | "tft.bl" | "touch.cs" | "touch.irq"
  | "i2c.sda" | "i2c.scl" | "imu.int" | "i2s.bclk" | "i2s.ws" | "i2s.din" | "i2s.dout";

export const SIGNAL_NAMES: Record<Signal, string> = {
  "spi.mosi": "SPI MOSI", "spi.miso": "SPI MISO", "spi.sclk": "SPI clock", "tft.cs": "screen CS", "tft.dc": "screen DC", "tft.rst": "screen reset",
  "tft.bl": "backlight", "touch.cs": "touch CS", "touch.irq": "touch IRQ", "i2c.sda": "I2C SDA", "i2c.scl": "I2C SCL", "imu.int": "IMU interrupt",
  "i2s.bclk": "I2S bit clock", "i2s.ws": "I2S word select", "i2s.din": "I2S data in (mic)", "i2s.dout": "I2S data out (amp)",
};

interface PinBase {
  id: string;
  label: string;
  /** a short explanation shown on hover */
  title?: string;
  /** which column of the part card the pin sits in */
  side: "l" | "r";
  /** group used to judge whether a feature is wired: power, screen, touch, bus ... */
  group?: string;
  /** must be wired for the part to work (default: true for power and signal pins, false for straps) */
  required?: boolean;
}
export type PinDef = PinBase & (
  | { kind: "gpio"; gpio: number }
  | { kind: "rail"; rail: Rail }
  | { kind: "gnd" }
  | { kind: "en" }
  | { kind: "vin"; accepts: Rail[] }
  | { kind: "sig"; signal: Signal; alsoRail?: Rail }
  | { kind: "strap"; accepts: ("gnd" | Rail)[] }
);

export interface PartDef {
  type: PartType;
  /** fixed id used on the canvas (one of each part) */
  id: string;
  name: string;
  blurb: string;
  pins: PinDef[];
  /** how honest the simulation of this part is */
  honesty: Honesty;
  honestyNote: string;
  /** a virtual part with no pins */
  virtual?: boolean;
  /** starting position when you drag it out or load the reference wiring */
  home: { x: number; y: number };
}

// ---------------------------------------------------------------- ESP32-S3 DevKitC-1
const gp = (n: number, side: "l" | "r", label = String(n), title?: string): PinDef => ({ id: `GPIO${n}`, label, kind: "gpio", gpio: n, side, title });
const J1: PinDef[] = [
  { id: "3V3.1", label: "3V3", kind: "rail", rail: "v33", side: "l" },
  { id: "3V3.2", label: "3V3", kind: "rail", rail: "v33", side: "l" },
  { id: "RST", label: "RST", kind: "en", side: "l", title: "EN / reset: not used in this design" },
  gp(4, "l"), gp(5, "l"), gp(6, "l"), gp(7, "l"), gp(15, "l"), gp(16, "l"), gp(17, "l"), gp(18, "l"), gp(8, "l"), gp(3, "l"), gp(46, "l"),
  gp(9, "l"), gp(10, "l"), gp(11, "l"), gp(12, "l"), gp(13, "l"), gp(14, "l"),
  { id: "5V", label: "5V", kind: "rail", rail: "v5", side: "l" },
  { id: "GND.1", label: "GND", kind: "gnd", side: "l" },
];
const J3: PinDef[] = [
  { id: "GND.2", label: "GND", kind: "gnd", side: "r" },
  gp(43, "r", "TX", "UART0 TX: the serial monitor"), gp(44, "r", "RX", "UART0 RX: the serial monitor"),
  gp(1, "r"), gp(2, "r"), gp(42, "r"), gp(41, "r"), gp(40, "r"), gp(39, "r"), gp(38, "r"), gp(37, "r"), gp(36, "r"), gp(35, "r"),
  gp(0, "r"), gp(45, "r"), gp(48, "r"), gp(47, "r"), gp(21, "r"), gp(20, "r"), gp(19, "r"),
  { id: "GND.3", label: "GND", kind: "gnd", side: "r" },
  { id: "GND.4", label: "GND", kind: "gnd", side: "r" },
];

export interface BoardHazard { severity: "info" | "warn" | "error"; text: string }
/** Things worth knowing about specific pins of the ESP32-S3-DevKitC-1 N16R8. */
export const BOARD_HAZARDS: Record<number, BoardHazard> = {
  0: { severity: "warn", text: "GPIO 0 is a strapping pin (boot mode): a part pulling it low at reset stops the board from booting." },
  3: { severity: "warn", text: "GPIO 3 is a strapping pin (JTAG source) and is read at reset." },
  45: { severity: "warn", text: "GPIO 45 is a strapping pin (flash voltage) and is read at reset." },
  46: { severity: "warn", text: "GPIO 46 is a strapping pin (boot messages) and is read at reset." },
  19: { severity: "warn", text: "GPIO 19 is USB D- (the native USB / JTAG port). Using it can break USB." },
  20: { severity: "warn", text: "GPIO 20 is USB D+ (the native USB / JTAG port). Using it can break USB." },
  43: { severity: "warn", text: "GPIO 43 is UART0 TX, the serial monitor's console output." },
  44: { severity: "warn", text: "GPIO 44 is UART0 RX, the serial monitor's console input." },
  35: { severity: "error", text: "GPIO 35 is used by the octal PSRAM on N16R8 boards: do not use it." },
  36: { severity: "error", text: "GPIO 36 is used by the octal PSRAM on N16R8 boards: do not use it." },
  37: { severity: "error", text: "GPIO 37 is used by the octal PSRAM on N16R8 boards: do not use it." },
  38: { severity: "info", text: "On some DevKitC-1 revisions GPIO 38 also drives the on-board RGB LED (it is GPIO 48 on others). Check your board; it is harmless for an amplifier input, but the LED may flicker." },
  48: { severity: "info", text: "On some DevKitC-1 revisions GPIO 48 drives the on-board RGB LED (GPIO 38 on others)." },
};

// ---------------------------------------------------------------- the parts
const REQ = true;
export const PART_DEFS: Record<PartType, PartDef> = {
  esp32s3: {
    type: "esp32s3", id: "board", name: "ESP32-S3 DevKitC-1", blurb: "N16R8 board: 16 MB flash, 8 MB octal PSRAM. Powered over USB.",
    pins: [...J1, ...J3], honesty: "approx",
    honestyNote: "Pin labels follow the DevKitC-1 headers. The chip itself is not simulated: Dewy's code runs as WebAssembly, so CPU speed and memory are not those of an ESP32.",
    home: { x: 760, y: 20 },
  },
  display: {
    type: "display", id: "lcd", name: '3.2" ILI9341 + XPT2046 touch', blurb: "240x320 SPI screen with a resistive touch chip on the same board (LovyanGFX panel from display.h). Click the screen to tap, hold to pet.",
    honesty: "real",
    honestyNote: "The face is drawn by Dewy's real face.h. The screen refresh and SPI speed are not simulated.",
    pins: [
      { id: "VCC", label: "VCC", kind: "vin", accepts: ["v33"], side: "r", group: "power", required: REQ, title: "3.3 V only (README)" },
      { id: "GND", label: "GND", kind: "gnd", side: "r", group: "power", required: REQ },
      { id: "CS", label: "CS", kind: "sig", signal: "tft.cs", side: "r", group: "screen", required: REQ },
      { id: "RESET", label: "RESET", kind: "sig", signal: "tft.rst", side: "r", group: "screen", required: REQ },
      { id: "DC", label: "DC/RS", kind: "sig", signal: "tft.dc", side: "r", group: "screen", required: REQ },
      { id: "SDI", label: "SDI (MOSI)", kind: "sig", signal: "spi.mosi", side: "r", group: "screen", required: REQ },
      { id: "SCK", label: "SCK", kind: "sig", signal: "spi.sclk", side: "r", group: "screen", required: REQ },
      { id: "LED", label: "LED (BL)", kind: "sig", signal: "tft.bl", alsoRail: "v33", side: "r", group: "screen", required: REQ, title: "Backlight. On a GPIO the firmware can dim it; on 3V3 it is always on." },
      { id: "SDO", label: "SDO (MISO)", kind: "sig", signal: "spi.miso", side: "r", group: "bus", required: false, title: "Screen read-back: the firmware does not need it for drawing" },
      { id: "T_CLK", label: "T_CLK", kind: "sig", signal: "spi.sclk", side: "r", group: "touch", required: REQ },
      { id: "T_CS", label: "T_CS", kind: "sig", signal: "touch.cs", side: "r", group: "touch", required: REQ },
      { id: "T_DIN", label: "T_DIN", kind: "sig", signal: "spi.mosi", side: "r", group: "touch", required: REQ },
      { id: "T_DO", label: "T_DO", kind: "sig", signal: "spi.miso", side: "r", group: "touch", required: REQ },
      { id: "T_IRQ", label: "T_IRQ", kind: "sig", signal: "touch.irq", side: "r", group: "touch", required: REQ },
      { id: "IM0", label: "IM0", kind: "strap", accepts: ["gnd"], side: "r", title: "SPI mode: IM0 to GND (README)" },
      { id: "IM1", label: "IM1", kind: "strap", accepts: ["v33"], side: "r", title: "SPI mode: IM1 to 3.3 V (README)" },
      { id: "IM2", label: "IM2", kind: "strap", accepts: ["v33"], side: "r", title: "SPI mode: IM2 to 3.3 V (README)" },
    ],
    home: { x: 20, y: 20 },
  },
  imu: {
    type: "imu", id: "imu", name: "GY-87 (MPU6050 IMU)", blurb: "Accelerometer at I2C address 0x68. Dewy uses shake, pick-up, lean, face-down and upside-down from it.",
    honesty: "real",
    honestyNote: "Dewy's real motion.h reads this sensor's registers; the numbers are the ones you set. Sensor noise and drift are not simulated. Only the MPU6050 is modelled (not the compass or pressure chips on a GY-87).",
    pins: [
      { id: "VCC", label: "VCC", kind: "vin", accepts: ["v33", "v5"], side: "r", group: "power", required: REQ, title: "GY-87 has a regulator: 3.3 V or 5 V" },
      { id: "GND", label: "GND", kind: "gnd", side: "r", group: "power", required: REQ },
      { id: "SCL", label: "SCL", kind: "sig", signal: "i2c.scl", side: "r", group: "bus", required: REQ },
      { id: "SDA", label: "SDA", kind: "sig", signal: "i2c.sda", side: "r", group: "bus", required: REQ },
      { id: "INT", label: "INT", kind: "sig", signal: "imu.int", side: "r", group: "bus", required: false, title: "Not used by Dewy's firmware (the Wokwi diagram wires it to GPIO 2)" },
    ],
    home: { x: 430, y: 20 },
  },
  mic: {
    type: "mic", id: "mic", name: "INMP441 I2S microphone", blurb: "Digital MEMS microphone: SCK, WS and SD on GPIO 15, 16 and 17 (from the brief).",
    honesty: "approx",
    honestyNote: "UNVERIFIED pins: firmware/dewy has no voice.h yet. The audio is generated or loaded in the page; the real mic's 24-bit samples, gain and noise are not simulated.",
    pins: [
      { id: "VDD", label: "VDD", kind: "vin", accepts: ["v33"], side: "r", group: "power", required: REQ, title: "1.8-3.3 V: 5 V would damage it" },
      { id: "GND", label: "GND", kind: "gnd", side: "r", group: "power", required: REQ },
      { id: "SCK", label: "SCK", kind: "sig", signal: "i2s.bclk", side: "r", group: "i2s", required: REQ },
      { id: "WS", label: "WS", kind: "sig", signal: "i2s.ws", side: "r", group: "i2s", required: REQ },
      { id: "SD", label: "SD", kind: "sig", signal: "i2s.din", side: "r", group: "i2s", required: REQ },
      { id: "LR", label: "L/R", kind: "strap", accepts: ["gnd", "v33"], side: "r", title: "Channel select: GND = left, 3V3 = right" },
    ],
    home: { x: 430, y: 380 },
  },
  amp: {
    type: "amp", id: "amp", name: "MAX98357A amp + speaker", blurb: "I2S class-D amplifier driving a 3 W speaker: BCLK, LRC and DIN on GPIO 18, 21 and 38 (from the brief).",
    honesty: "approx",
    honestyNote: "UNVERIFIED pins: firmware/dewy has no voice.h yet. The waveform shown is the reply audio from the mock server; real loudness, distortion and gain are not simulated.",
    pins: [
      { id: "VIN", label: "VIN", kind: "vin", accepts: ["v33", "v5"], side: "l", group: "power", required: REQ, title: "2.5-5.5 V" },
      { id: "GND", label: "GND", kind: "gnd", side: "l", group: "power", required: REQ },
      { id: "BCLK", label: "BCLK", kind: "sig", signal: "i2s.bclk", side: "l", group: "i2s", required: REQ },
      { id: "LRC", label: "LRC", kind: "sig", signal: "i2s.ws", side: "l", group: "i2s", required: REQ },
      { id: "DIN", label: "DIN", kind: "sig", signal: "i2s.dout", side: "l", group: "i2s", required: REQ },
      { id: "GAIN", label: "GAIN", kind: "strap", accepts: ["gnd", "v33", "v5"], side: "l", title: "Gain select (left open = 9 dB)" },
      { id: "SD", label: "SD", kind: "strap", accepts: ["gnd", "v33", "v5"], side: "l", title: "Shutdown / channel mode (left open = enabled, mono mix)" },
    ],
    home: { x: 1170, y: 20 },
  },
  wifi: {
    type: "wifi", id: "wifi", name: "Wi-Fi status", blurb: "Shows what the firmware's Wi-Fi code is doing: hotspot, connecting, connected.",
    honesty: "approx", virtual: true, pins: [],
    honestyNote: "The state comes from Dewy's real pollWifi() logic, but the radio is a pretend router with a made-up name: no real Wi-Fi is involved.",
    home: { x: 1170, y: 310 },
  },
  cloud: {
    type: "cloud", id: "cloud", name: "Cloud link", blurb: "Shows the connection to Dewy's server (registered, paired) and lets a pretend phone send notes and timers.",
    honesty: "mock", virtual: true, pins: [],
    honestyNote: "The server is a mock in this page that copies the shape of cloud/src. The live Worker is never called. Registration, sync and pairing run through Dewy's real cloud.h.",
    home: { x: 1170, y: 650 },
  },
};

export const PART_ORDER: PartType[] = ["esp32s3", "display", "imu", "mic", "amp", "wifi", "cloud"];

export function findPin(type: PartType, pinId: string): PinDef | undefined {
  return PART_DEFS[type].pins.find((p) => p.id === pinId);
}

/** Everything a pin is allowed to carry, in words (for tooltips). */
export function describePin(p: PinDef): string {
  switch (p.kind) {
    case "gpio": return `GPIO ${p.gpio}`;
    case "rail": return p.rail === "v33" ? "3.3 V supply" : "5 V supply";
    case "gnd": return "ground";
    case "en": return "reset (EN)";
    case "vin": return `power input (${p.accepts.map((r) => (r === "v33" ? "3.3 V" : "5 V")).join(" or ")})`;
    case "sig": return SIGNAL_NAMES[p.signal];
    case "strap": return `strap to ${p.accepts.map((a) => (a === "gnd" ? "GND" : a === "v33" ? "3V3" : "5V")).join(" or ")}`;
  }
}
