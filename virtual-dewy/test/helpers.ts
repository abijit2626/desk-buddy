import { readFileSync } from "node:fs";
import { DewyCore, type HttpHandler } from "../src/sim/wasm";

export const wasmBytes = readFileSync(new URL("../public/dewy.wasm", import.meta.url));
export const FIRMWARE_DIR = new URL("../../firmware/dewy/", import.meta.url).pathname;
export const CLOUD_DIR = new URL("../../cloud/", import.meta.url).pathname;

const notFound: HttpHandler = () => ({ status: 404, body: "{}" });

/** Boots the real firmware (compiled to WebAssembly) on virtual hardware. Time starts at 0 and only moves when you tick. */
export async function bootCore(o: { http?: HttpHandler; imu?: boolean; touch?: boolean; seed?: number; prefs?: Uint8Array | null; epochMs?: number } = {}) {
  const core = await DewyCore.instantiate(wasmBytes, o.http ?? notFound);
  const epochMs = o.epochMs ?? Date.UTC(2026, 10, 11, 9, 30, 0);
  core.boot({ seed: o.seed ?? 12345, imuPresent: o.imu ?? true, touchPresent: o.touch ?? true, nowMs: 1000, epochMs, prefs: o.prefs });
  let t = 1000;
  const state = {
    core,
    get now() { return t; },
    /** advance virtual time by ms in ~16 ms steps, like a 60 Hz display refresh; returns what the firmware printed meanwhile */
    run(ms: number) {
      let out = "";
      for (let left = ms; left > 0; left -= 16) { t += Math.min(16, left); core.tick(t, epochMs + (t - 1000)); out += core.serialOut(); }
      return out;
    },
    /** types a command into the serial monitor, runs a little, returns the output */
    cmd(line: string, ms = 200) { core.serialIn(line + "\n"); return state.run(ms); },
  };
  return state;
}
