// Loads public/dewy.wasm (Dewy's REAL firmware compiled for the browser, see wasm/) and wraps its exports.
// Works in the browser and in Node (the tests use it), so it never touches the DOM.

export type HttpHandler = (req: { method: string; url: string; headers: Record<string, string>; body: string }) => { status: number; body: string | Uint8Array };

/** Indices of the status array the firmware host reports (see dewy_status() in wasm/host.cpp). */
export interface CoreStatus {
  frameSerial: number;
  brightness: number;       // 0..255, what the firmware asked the backlight for
  mood: number;
  moodName: string;
  faceDown: boolean;
  upsideDown: boolean;
  timerActive: boolean;
  wifiStatus: number;       // Arduino wl_status_t: 0 idle (connecting), 3 connected, 6 disconnected
  portalOn: boolean;        // the "Dewy-Setup" hotspot is open
  cloudReady: boolean;      // registered with the server
  pairedKnown: boolean;
  paired: boolean;
  banner: number;           // 0 none, 1 "Set up my Wi-Fi", 2 "Pair your phone"
  restartRequested: boolean;
  clockIsReal: boolean;
  motionAvailable: boolean;
  calibrated: boolean;
  tiltShown: boolean;
  tiltX: number;
  tiltY: number;
  busy: boolean;
  overlayActive: boolean;
  hasWifiCredentials: boolean;
  apOn: boolean;
  cloudConfigured: boolean;
  mouth: boolean;
  notePending: boolean;
  notesQueued: number;
  weather: number;          // 0 none, 1 sun, 2 cloud, 3 rain
}

/** The pin numbers Dewy's own code passed to its drivers (-1 = never set). */
export interface PinReport {
  i2cSda: number; i2cScl: number;
  tftSclk: number; tftMosi: number; tftMiso: number; tftDc: number; tftCs: number; tftRst: number; tftBl: number;
  touchCs: number; touchIrq: number; touchSclk: number; touchMosi: number; touchMiso: number;
  battery: number;
}

export interface ConfigReport {
  quickTimings: boolean; boredAfterMs: number; sleepAfterMs: number; tapMaxMs: number; petHoldMs: number;
  eightBallWindowMs: number; frameMs: number; touchEnabled: boolean; screenRotation: number; batteryEnabled: boolean;
}

export const FRAME_W = 320, FRAME_H = 240;
export const WEATHER_NAMES = ["none", "sun", "cloud", "rain"] as const;

interface Exports {
  memory: WebAssembly.Memory;
  _initialize(): void;
  dewy_io(): number; dewy_io_size(): number;
  dewy_boot(seed: number, hw: number, nowMs: number, epochMs: number): void;
  dewy_tick(nowMs: number, epochMs: number): void;
  dewy_serial_in(ptr: number, n: number): void; dewy_serial_take(): number; dewy_serial_ptr(): number;
  dewy_frame(): number;
  dewy_touch(down: number, x: number, y: number): void;
  dewy_accel(x: number, y: number, z: number): void;
  dewy_set_router(up: number): void; dewy_set_router_creds(ssid: number, pass: number): void;
  dewy_status(): number; dewy_mood_name(i: number): number; dewy_mood_count(): number;
  dewy_pins(): number; dewy_config(): number; dewy_version(): number;
  dewy_prefs_dump(): number; dewy_prefs_ptr(): number; dewy_prefs_load(ptr: number, n: number): void;
  dewy_portal_page(): number; dewy_portal_save(s: number, o: number, p: number): number; dewy_portal_msg(): number;
}

const enc = new TextEncoder();
const dec = new TextDecoder();

export class DewyCore {
  private readonly x: Exports;
  private lastHttp: Uint8Array = new Uint8Array(0);
  /** anything the wasm module wrote to its own stdout/stderr (should stay empty: Serial goes through the monitor) */
  stdio = "";
  private booted = false;

  private constructor(instance: WebAssembly.Instance) {
    this.x = instance.exports as unknown as Exports;
  }

  static async instantiate(wasm: BufferSource, http: HttpHandler): Promise<DewyCore> {
    let core!: DewyCore;
    const mem = () => new Uint8Array(core.x.memory.buffer);
    const cstr = (ptr: number) => { const m = mem(); let e = ptr; while (m[e] !== 0) e++; return dec.decode(m.subarray(ptr, e)); };
    const wasi = {
      fd_close: () => 0,
      fd_seek: () => 70,                                   // ESPIPE
      fd_write: (fd: number, iovs: number, n: number, out: number) => {
        const dv = new DataView(core.x.memory.buffer);
        let total = 0;
        for (let i = 0; i < n; i++) {
          const ptr = dv.getUint32(iovs + i * 8, true), len = dv.getUint32(iovs + i * 8 + 4, true);
          if (fd === 1 || fd === 2) core.stdio += dec.decode(new Uint8Array(core.x.memory.buffer, ptr, len));
          total += len;
        }
        dv.setUint32(out, total, true);
        return 0;
      },
      environ_sizes_get: (countPtr: number, sizePtr: number) => { const dv = new DataView(core.x.memory.buffer); dv.setUint32(countPtr, 0, true); dv.setUint32(sizePtr, 0, true); return 0; },
      environ_get: () => 0,
      proc_exit: (code: number) => { throw new Error(`the firmware module exited (code ${code})`); },
    };
    const env = {
      host_http: (method: number, url: number, body: number, headers: number): number => {
        const hdrs: Record<string, string> = {};
        for (const line of cstr(headers).split("\n")) { const i = line.indexOf(":"); if (i > 0) hdrs[line.slice(0, i).trim().toLowerCase()] = line.slice(i + 1).trim(); }
        let res: ReturnType<HttpHandler>;
        try { res = http({ method: cstr(method), url: cstr(url), headers: hdrs, body: cstr(body) }); }
        catch (e) { core.stdio += `mock server error: ${String(e)}\n`; res = { status: -1, body: "" }; }
        core.lastHttp = typeof res.body === "string" ? enc.encode(res.body) : res.body;
        return res.status;
      },
      host_http_body: (dst: number, cap: number): number => {
        if (dst && cap > 0) mem().set(core.lastHttp.subarray(0, cap), dst);
        return core.lastHttp.length;
      },
    };
    const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env });
    core = new DewyCore(instance);
    core.x._initialize();
    return core;
  }

  // ---- memory helpers
  private mem() { return new Uint8Array(this.x.memory.buffer); }
  private io() { return { ptr: this.x.dewy_io(), size: this.x.dewy_io_size() }; }
  /** writes NUL-terminated strings one after another into the scratch buffer, returns their addresses */
  private putStrings(...strs: string[]): number[] {
    const { ptr, size } = this.io();
    const m = this.mem();
    const out: number[] = [];
    let at = 0;
    for (const s of strs) {
      const b = enc.encode(s);
      if (at + b.length + 1 > size) throw new Error("text too long for the firmware's scratch buffer");
      out.push(ptr + at);
      m.set(b, ptr + at); m[ptr + at + b.length] = 0;
      at += b.length + 1;
    }
    return out;
  }
  private readCString(ptr: number) { const m = this.mem(); let e = ptr; while (m[e] !== 0) e++; return dec.decode(m.subarray(ptr, e)); }

  // ---- lifecycle
  /** Powers the virtual board on: runs the firmware's setup(). `prefs` is the saved flash from an earlier run. */
  boot(o: { seed: number; imuPresent: boolean; touchPresent: boolean; nowMs: number; epochMs: number; prefs?: Uint8Array | null }) {
    if (o.prefs && o.prefs.length) {
      if (o.prefs.length > this.io().size) throw new Error("saved settings are too large");
      const { ptr } = this.io();
      this.mem().set(o.prefs, ptr);
      this.x.dewy_prefs_load(ptr, o.prefs.length);
    }
    this.x.dewy_boot(o.seed >>> 0, (o.imuPresent ? 1 : 0) | (o.touchPresent ? 2 : 0), o.nowMs >>> 0, o.epochMs);
    this.booted = true;
  }
  get isBooted() { return this.booted; }
  tick(nowMs: number, epochMs: number) { this.x.dewy_tick(nowMs >>> 0, epochMs); }

  // ---- serial
  serialIn(text: string) {
    const b = enc.encode(text);
    const { ptr, size } = this.io();
    for (let i = 0; i < b.length; i += size) {
      const chunk = b.subarray(i, i + size);
      this.mem().set(chunk, ptr);
      this.x.dewy_serial_in(ptr, chunk.length);
    }
  }
  /** everything the firmware printed since the last call */
  serialOut(): string {
    const n = this.x.dewy_serial_take();
    return n ? dec.decode(this.mem().slice(this.x.dewy_serial_ptr(), this.x.dewy_serial_ptr() + n)) : "";
  }

  // ---- screen
  /** the current 320x240 frame as RGBA (a copy) */
  frame(): Uint8ClampedArray<ArrayBuffer> {
    const p = this.x.dewy_frame();
    return new Uint8ClampedArray(this.x.memory.buffer.slice(p, p + FRAME_W * FRAME_H * 4));
  }

  // ---- virtual hardware
  touch(down: boolean, x: number, y: number) { this.x.dewy_touch(down ? 1 : 0, Math.round(x), Math.round(y)); }
  accel(x: number, y: number, z: number) { this.x.dewy_accel(x, y, z); }
  setRouter(up: boolean) { this.x.dewy_set_router(up ? 1 : 0); }
  setRouterCredentials(ssid: string, pass: string) { const [a, b] = this.putStrings(ssid, pass); this.x.dewy_set_router_creds(a, b); }

  // ---- reports
  private ints(ptr: number, n: number) { return Array.from(new Int32Array(this.x.memory.buffer, ptr, n)); }
  status(): CoreStatus {
    const s = this.ints(this.x.dewy_status(), 32);
    return {
      frameSerial: s[0], brightness: s[1], mood: s[2], moodName: this.moodName(s[2]), faceDown: !!s[3], upsideDown: !!s[4], timerActive: !!s[5],
      wifiStatus: s[6], portalOn: !!s[7], cloudReady: !!s[8], pairedKnown: !!s[9], paired: !!s[10], banner: s[11], restartRequested: !!s[12],
      clockIsReal: !!s[13], motionAvailable: !!s[14], calibrated: !!s[15], tiltShown: !!s[16], tiltX: s[17] / 1000, tiltY: s[18] / 1000,
      busy: !!s[19], overlayActive: !!s[20], hasWifiCredentials: !!s[21], apOn: !!s[22], cloudConfigured: !!s[23], mouth: !!s[24],
      notePending: !!s[25], notesQueued: s[26], weather: s[27],
    };
  }
  moodName(i: number) { return this.readCString(this.x.dewy_mood_name(i)); }
  moodNames(): string[] { return Array.from({ length: this.x.dewy_mood_count() }, (_, i) => this.moodName(i)); }
  pins(): PinReport {
    const p = this.ints(this.x.dewy_pins(), 16);
    return { i2cSda: p[0], i2cScl: p[1], tftSclk: p[2], tftMosi: p[3], tftMiso: p[4], tftDc: p[5], tftCs: p[6], tftRst: p[7], tftBl: p[8],
      touchCs: p[9], touchIrq: p[10], touchSclk: p[11], touchMosi: p[12], touchMiso: p[13], battery: p[14] };
  }
  config(): ConfigReport {
    const c = this.ints(this.x.dewy_config(), 16);
    return { quickTimings: !!c[0], boredAfterMs: c[1], sleepAfterMs: c[2], tapMaxMs: c[3], petHoldMs: c[4], eightBallWindowMs: c[5], frameMs: c[6],
      touchEnabled: !!c[7], screenRotation: c[8], batteryEnabled: !!c[9] };
  }
  version(): string { return this.readCString(this.x.dewy_version()); }

  // ---- flash storage
  prefsDump(): Uint8Array {
    const n = this.x.dewy_prefs_dump();
    const p = this.x.dewy_prefs_ptr();
    return this.mem().slice(p, p + n);
  }

  // ---- the Dewy-Setup page (the firmware's own portalRoot() / portalSave())
  portalPage(): string { return this.readCString(this.x.dewy_portal_page()); }
  /** Submits the form like the phone would. Returns the HTTP status the firmware answered (303 = refused, 200 = connecting), or -1 if the hotspot is off. */
  portalSave(ssidFromList: string, ssidTyped: string, password: string): number {
    const [a, b, c] = this.putStrings(ssidFromList, ssidTyped, password);
    return this.x.dewy_portal_save(a, b, c);
  }
  portalMessage(): string { return this.readCString(this.x.dewy_portal_msg()); }
}

/** Little-endian float32 triple, the way the firmware stores a calibration vector in flash. */
export function vec3Bytes(x: number, y: number, z: number): Uint8Array {
  const b = new Uint8Array(12); const dv = new DataView(b.buffer);
  dv.setFloat32(0, x, true); dv.setFloat32(4, y, true); dv.setFloat32(8, z, true);
  return b;
}

/** Saved-flash format helpers (the same layout dewy_prefs_dump() writes): [key length][key][value length u32][value]... */
export function prefsEncode(entries: Record<string, Uint8Array>): Uint8Array {
  const parts: number[] = [];
  for (const [k, v] of Object.entries(entries)) {
    const kb = enc.encode(k);
    parts.push(kb.length, ...kb, v.length & 255, (v.length >> 8) & 255, (v.length >> 16) & 255, (v.length >>> 24) & 255, ...v);
  }
  return Uint8Array.from(parts);
}
export function prefsDecode(bytes: Uint8Array): Record<string, Uint8Array> {
  const out: Record<string, Uint8Array> = {};
  let i = 0;
  while (i < bytes.length) {
    const kl = bytes[i++]; if (i + kl + 4 > bytes.length) break;
    const key = dec.decode(bytes.subarray(i, i + kl)); i += kl;
    const vl = new DataView(bytes.buffer, bytes.byteOffset + i, 4).getUint32(0, true); i += 4;
    if (i + vl > bytes.length) break;
    out[key] = bytes.slice(i, i + vl); i += vl;
  }
  return out;
}
export function prefsString(bytes: Uint8Array | undefined): string { return bytes ? dec.decode(bytes) : ""; }
