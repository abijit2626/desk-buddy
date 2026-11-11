// The whole simulated Dewy in one object: the real firmware (WebAssembly), the virtual hardware around it, a mock
// cloud server, the voice loop and a virtual clock. No DOM in here, so the page and the tests drive the same thing.
import { DewyCore, prefsDecode, prefsEncode, prefsString, vec3Bytes, type CoreStatus } from "./wasm";
import { MockWorker, REGISTER_KEY, SERVER_HOST, mockFetch, type LogEntry, type Weather } from "./mock-server";
import { Imu, type PresetName } from "./imu";
import { VoiceSession, readChatResponse, type ChatResult, type VoicePhase } from "./voice";
import { FAKE_SOURCES, generate, sliderToLevel, type FakeSource } from "./audio-gen";
import { SAMPLE_RATE, dbfs, floatToPcm16, rms16 } from "./audio";
import { Vad, VAD_PARAMS, frames16, runVad, type VadFrame } from "./vad";
import { parseVirtualCommand, VIRTUAL_HELP } from "./virtual-commands";
import { WAKE_WORD, WAKE_WORD_LABEL } from "./wakeword";

export interface HardwareState { display: boolean; touch: boolean; imu: boolean; mic: boolean; amp: boolean }
export interface SerialLine { t: number; kind: "fw" | "in" | "virtual" | "link"; text: string }
/** what the virtual microphone hears when Dewy listens */
export interface MicState { source: FakeSource | "wav"; levelPct: number; wav: Float32Array | null; wavName: string; transcript: string; seed: number }
export const newMicState = (): MicState => ({ source: "speech", levelPct: 60, wav: null, wavName: "", transcript: "What's the weather?", seed: 1 });

export interface SimulatorOptions {
  wasm: BufferSource;
  hardware: HardwareState;
  /** real wall-clock time (ms since 1970) at power-on: used when the firmware's "internet time" arrives */
  epochMs: number;
  /** flash saved from an earlier run. When absent, the flash starts empty plus the seeds below. */
  prefs?: Uint8Array | null;
  /** with no saved flash: put the home Wi-Fi in flash (as if set up already) */
  wifiSaved?: boolean;
  /** with no saved flash: write the one-time "cal up" / "cal face" calibration (virtual mounting) */
  preCalibrated?: boolean;
  routerUp?: boolean;
  seed?: number;
}

const STEP_MS = 16;
const MAX_LINES = 1500;

export class Simulator {
  readonly server: MockWorker;
  readonly imu = new Imu();
  readonly voice: VoiceSession;
  core!: DewyCore;
  hardware: HardwareState;
  /** milliseconds since power-on (the clock the firmware's millis() reads) */
  nowMs = 1000;
  status!: CoreStatus;
  readonly lines: SerialLine[] = [];
  onLine?: (l: SerialLine) => void;
  onRestart?: () => void;
  onFlash?: (bytes: Uint8Array) => void;
  onVadFrame?: (f: VadFrame, elapsedMs: number) => void;
  onPhase?: (p: VoicePhase) => void;
  onReply?: (r: ChatResult & { durationMs: number }) => void;
  routerUp: boolean;
  weatherSet: Weather = "sun";
  /** the virtual microphone */
  mic: MicState = newMicState();
  /** the acceleration last reported to the firmware, in g */
  lastAccel: [number, number, number] = [0, 1, 0];
  wwDebug = false;

  private opts: SimulatorOptions;
  private partial = "";
  private actions: { at: number; fn: () => void }[] = [];
  private lastFlashHash = "";
  private wwTimer = 0;
  private acc = 0;
  readonly fetchMock: ReturnType<typeof mockFetch>;

  private constructor(opts: SimulatorOptions) {
    this.opts = opts;
    this.hardware = { ...opts.hardware };
    this.routerUp = opts.routerUp ?? true;
    this.server = new MockWorker({ now: () => Math.floor((this.opts.epochMs + (this.nowMs - 1000)) / 1000) });
    this.fetchMock = mockFetch(this.server);
    this.voice = new VoiceSession({
      firmware: (cmd) => { this.addLine("link", `→ ${cmd}`); this.core.serialIn(cmd + "\n"); },
      say: (text) => this.addLine("virtual", text),
      token: () => prefsString(prefsDecode(this.core.prefsDump()).token) || null,
      chat: (r) => this.chatRequest(r),
      micWired: () => this.hardware.mic,
      ampWired: () => this.hardware.amp,
      onPhase: (p) => this.onPhase?.(p),
      onFrame: (f, t) => this.onVadFrame?.(f, t),
      onReply: (r) => this.onReply?.(r),
    });
  }

  static async create(opts: SimulatorOptions): Promise<Simulator> {
    const sim = new Simulator(opts);
    await sim.powerOn(opts.prefs ?? null, true);
    return sim;
  }

  // ---------------------------------------------------------------- power
  private async powerOn(prefs: Uint8Array | null, first: boolean) {
    this.core = await DewyCore.instantiate(this.opts.wasm, (req) => {
      const r = this.server.handle({ method: req.method, url: req.url, headers: req.headers, body: req.body });
      return { status: r.status, body: r.body };
    });
    let flash = prefs;
    if (!flash) {
      const seeds: Record<string, Uint8Array> = {};
      const enc = new TextEncoder();
      if (this.opts.wifiSaved) { seeds.wifi_ssid = enc.encode("Virtual-Home"); seeds.wifi_pass = enc.encode("dewy1234"); }
      if (this.opts.preCalibrated ?? true) { seeds.calUp = vec3Bytes(0, 1, 0); seeds.calFace = vec3Bytes(0, 0, -1); }
      flash = prefsEncode(seeds);
    }
    this.core.setRouter(this.routerUp);
    const [x, y, z] = this.imu.step(0);
    this.core.accel(x, y, z);
    this.core.boot({ seed: this.opts.seed ?? ((Math.random() * 0xffffffff) >>> 0), imuPresent: this.hardware.imu, touchPresent: this.hardware.touch, nowMs: this.nowMs, epochMs: this.opts.epochMs + (this.nowMs - 1000), prefs: flash });
    this.status = this.core.status();
    this.flush();
    if (!first) this.onRestart?.();
  }

  /** Reboots the board (what pressing RST or the firmware's own ESP.restart() does). Flash survives. */
  async restart(reason = "restart") {
    const flash = this.core.prefsDump();
    this.addLine("link", `— board ${reason} —`);
    this.voice.cancel();
    this.actions = [];
    await this.powerOn(flash, false);
  }

  /** Changes what is wired. Takes effect at the next power-on, like moving a wire on a real board. */
  setHardware(h: HardwareState) { this.hardware = { ...h }; }

  // ---------------------------------------------------------------- time
  /** Advances the simulation by `dtMs` of virtual time, in small steps. Returns true if the board asked to restart. */
  advance(dtMs: number): boolean {
    let restartAsked = false;
    this.acc += Math.min(dtMs, 250);
    while (this.acc >= STEP_MS) {
      this.acc -= STEP_MS;
      this.nowMs += STEP_MS;
      while (this.actions.length && this.actions[0].at <= this.nowMs) this.actions.shift()!.fn();
      const [x, y, z] = this.imu.step(STEP_MS);
      this.lastAccel = [x, y, z];
      this.core.accel(x, y, z);
      this.core.setRouter(this.routerUp);
      this.core.tick(this.nowMs, this.opts.epochMs + (this.nowMs - 1000));
      this.voice.advance(STEP_MS);
      this.wwTick(STEP_MS);
      this.flush();
      this.status = this.core.status();
      if (this.status.restartRequested) { restartAsked = true; break; }
    }
    if (restartAsked) void this.restart("restarted itself");
    return restartAsked;
  }

  private at(delayMs: number, fn: () => void) {
    this.actions.push({ at: this.nowMs + delayMs, fn });
    this.actions.sort((a, b) => a.at - b.at);
  }

  // ---------------------------------------------------------------- serial
  private addLine(kind: SerialLine["kind"], text: string) {
    const l = { t: this.nowMs, kind, text };
    this.lines.push(l);
    if (this.lines.length > MAX_LINES) this.lines.splice(0, this.lines.length - MAX_LINES);
    this.onLine?.(l);
  }
  private flush() {
    const out = this.core.serialOut();
    if (out) {
      this.partial += out;
      const parts = this.partial.split("\n");
      this.partial = parts.pop() ?? "";
      for (const p of parts) this.addLine("fw", p.replace(/\r$/, ""));
    }
    const flash = this.core.prefsDump();
    const h = flash.length + ":" + flash.reduce((a, b) => (a * 31 + b) | 0, 7);
    if (h !== this.lastFlashHash) { this.lastFlashHash = h; this.onFlash?.(flash); }
  }

  /** Types a line into the serial monitor, like pressing Enter in the Arduino IDE. */
  send(line: string) {
    const text = line.trim();
    if (!text) return;
    this.addLine("in", "> " + text);
    const v = parseVirtualCommand(text);
    if (v) { this.runVirtual(v); return; }
    this.core.serialIn(text + "\n");
    if (text.toLowerCase() === "help") for (const l of VIRTUAL_HELP) this.at(50, () => this.addLine("virtual", l));
  }

  private runVirtual(v: NonNullable<ReturnType<typeof parseVirtualCommand>>) {
    switch (v.kind) {
      case "ask": {
        const text = v.text || this.mic.transcript;
        this.addLine("virtual", `ask: "${text}"`);
        this.voice.askText(text);
        break;
      }
      case "mic": this.micReport(v.seconds); break;
      case "ww-trigger": this.addLine("virtual", "[ww] pretending the wake word fired (it is NOT simulated)."); this.wake(); break;
      case "ww-debug": {
        this.wwDebug = v.on === null ? !this.wwDebug : v.on;
        this.addLine("virtual", `[ww] debug ${this.wwDebug ? "on" : "off"}`);
        if (this.wwDebug) {
          this.addLine("virtual", `[ww] ${WAKE_WORD_LABEL}`);
          this.addLine("virtual", `[ww] model settings (voice/wakeword/model/hey_dewy.json): phrase "${WAKE_WORD.phrase}", cutoff ${WAKE_WORD.probabilityCutoff}, average the last ${WAKE_WORD.slidingWindow} results, ${WAKE_WORD.featureStepMs} ms feature step, ${WAKE_WORD.modelBytes} bytes.`);
          this.addLine("virtual", "[ww] below: the virtual microphone's level once a second. Probability is not computed.");
        }
        break;
      }
      case "ww-help": this.addLine("virtual", "[ww] usage: ww debug [on|off], ww trigger"); break;
    }
  }

  // ---------------------------------------------------------------- the microphone and the voice loop
  micSamples(): Float32Array {
    if (this.mic.source === "wav") return this.mic.wav ?? new Float32Array(SAMPLE_RATE);
    const body = generate(this.mic.source, sliderToLevel(this.mic.levelPct), this.mic.seed);
    // half a second of room before the person starts talking
    const out = new Float32Array(Math.round(SAMPLE_RATE * 0.5) + body.length);
    out.set(body, Math.round(SAMPLE_RATE * 0.5));
    return out;
  }
  sourceLabel() { return this.mic.source === "wav" ? `WAV file "${this.mic.wavName}"` : FAKE_SOURCES.find((s) => s.id === this.mic.source)!.label; }

  /** "Hey Dewy" (a button, or `ww trigger`): Dewy starts listening to whatever the virtual mic produces. */
  wake(): boolean { return this.voice.startListening(this.micSamples(), this.mic.transcript); }
  ask(text: string) { return this.voice.askText(text); }

  /** The `mic` command: listen for `seconds` and print what the level and silence logic make of it. */
  private micReport(seconds: number) {
    if (!this.hardware.mic) { this.addLine("virtual", "[mic] not wired to GPIO 15, 16, 17: nothing to listen to."); return; }
    const all = floatToPcm16(this.micSamples());
    const n = Math.min(all.length, Math.round(seconds * SAMPLE_RATE) * 2);
    const pcm = all.subarray(0, n);
    const v = new Vad();
    let loud = 0, total = 0, peak = 0;
    const view = new DataView(pcm.buffer, pcm.byteOffset, pcm.length);
    for (let i = 0; i + 1 < pcm.length; i += 2) peak = Math.max(peak, Math.abs(view.getInt16(i, true)));
    for (const f of frames16(pcm)) { const r = v.push(f); total++; if (r.isSpeech) loud++; }
    const lvl = rms16(pcm);
    this.addLine("virtual", `[mic] ${this.sourceLabel()}, first ${(pcm.length / 2 / SAMPLE_RATE).toFixed(1)} s: rms ${lvl.toFixed(0)} (${dbfs(lvl).toFixed(1)} dBFS), peak ${peak} (${dbfs(peak).toFixed(1)} dBFS)`);
    this.addLine("virtual", `[mic] ${loud} of ${total} frames of ${VAD_PARAMS.frameMs} ms are above the silence level (rms ${VAD_PARAMS.silenceRms}) -> ${lvl < VAD_PARAMS.silenceRms ? "the server would call this nobody speaking" : "counts as speech"}`);
    this.addLine("virtual", "[mic] APPROXIMATE: generated audio and a stand-in for vad.h (which doesn't exist yet), not an INMP441.");
  }

  private wwTick(dt: number) {
    if (!this.wwDebug) return;
    this.wwTimer += dt;
    if (this.wwTimer < 1000) return;
    this.wwTimer = 0;
    if (!this.hardware.mic) { this.addLine("virtual", "[ww] mic not wired"); return; }
    const pcm = floatToPcm16(this.micSamples()).subarray(0, SAMPLE_RATE * 2);
    const lvl = rms16(pcm);
    this.addLine("virtual", `[ww] level rms ${lvl.toFixed(0)} (${dbfs(lvl).toFixed(1)} dBFS) ${lvl >= VAD_PARAMS.silenceRms ? "above" : "below"} the silence level; wake word probability: not simulated`);
  }

  private chatRequest(r: { token: string; pcm?: Uint8Array; text?: string; transcript?: string }): ChatResult {
    const headers: Record<string, string> = { authorization: "Bearer " + r.token };
    let body: Uint8Array | string;
    if (r.pcm) { headers["content-type"] = "audio/pcm"; headers["x-mock-transcript"] = encodeURIComponent(r.transcript ?? ""); body = r.pcm; }
    else { headers["content-type"] = "application/json"; body = JSON.stringify({ text: r.text }); }
    const res = this.server.handle({ method: "POST", url: `https://${SERVER_HOST}/api/device/chat`, headers, body });
    return readChatResponse(res.status, res.headers, res.body);
  }

  // ---------------------------------------------------------------- touch
  touchDown(x: number, y: number) { this.core.touch(true, x, y); }
  touchUp() { this.core.touch(false, 0, 0); }
  touchMove(x: number, y: number) { this.core.touch(true, x, y); }
  /** a quick tap in the middle of the screen (the firmware calls it a poke) */
  tap(x = 160, y = 120) { this.touchDown(x, y); this.at(120, () => this.touchUp()); }
  /** a long press (the firmware calls it petting) */
  pet(x = 160, y = 120, holdMs = 1500) { this.touchDown(x, y); this.at(holdMs, () => this.touchUp()); }

  // ---------------------------------------------------------------- motion
  preset(name: PresetName) { this.imu.preset(name); }
  shake() { this.imu.shake(); }
  pickUp() { this.imu.pickUp(); }

  // ---------------------------------------------------------------- the pretend phone and router
  setWeather(w: Weather) { this.weatherSet = w; this.server.weather = w; }
  setRouter(up: boolean) { this.routerUp = up; }
  get serverLog(): readonly LogEntry[] { return this.server.log; }
  /** Pairs the pretend phone with the code Dewy is showing. */
  pairPhone() {
    const code = this.server.pairCode;
    if (!code) return { ok: false as const, error: "Dewy has no pairing code right now (it needs Wi-Fi, a registered cloud link and no phone paired yet)." };
    return this.server.pairPhone(code);
  }
  /** Submits the Dewy-Setup page's form with the home network name and a password, like a phone joined to the hotspot would. */
  submitSetupForm(ssid: string, password: string) { return this.core.portalSave("", ssid, password); }

  get epochNow() { return this.opts.epochMs + (this.nowMs - 1000); }
  get registerKey() { return REGISTER_KEY; }
  /** the flash as saved so far (for the page to keep) */
  flash() { return this.core.prefsDump(); }
  vadClipOf(samples: Float32Array) { return runVad(floatToPcm16(samples)); }
}
