// The simulated voice loop: wake -> listen -> think -> talk -> happy.
//
// HONEST LABEL: Dewy's firmware has no voice code yet (no voice.h, no I2S). This is the page's own stand-in for it:
//   * it drives Dewy's REAL face by sending the firmware's own `listen`, `think`, `talk` and `reset` serial commands;
//   * the level and silence logic is vad.ts (NOT vad.h);
//   * it talks to the mock server with the device token the real cloud.h registered with;
//   * timings ("thinking" delay, how long the reply plays) are approximate.
import { Vad, VAD_PARAMS, frames16, type VadFrame } from "./vad";
import { SAMPLE_RATE, decodeWav, floatToPcm16, type DecodedWav } from "./audio";

export type VoicePhase = "idle" | "listening" | "thinking" | "speaking";
/** which of the face states each phase shows: listen, think, talk, happy */
export const PHASE_FACE: Record<VoicePhase, "happy" | "listen" | "think" | "talk"> = { idle: "happy", listening: "listen", thinking: "think", speaking: "talk" };
/** the firmware serial command that puts the real face into each state */
export const PHASE_COMMAND: Record<VoicePhase, string> = { idle: "reset", listening: "listen", thinking: "think", speaking: "talk" };

export interface ChatResult {
  ok: boolean; status: number;
  heard?: string; reply?: string;
  /** the WAV the server sent (undefined for text-only answers and errors) */
  wav?: DecodedWav; wavBytes?: Uint8Array;
  error?: string;
}

export interface VoiceDeps {
  /** send a serial command to the real firmware (listen / think / talk / reset) */
  firmware(cmd: string): void;
  /** a [virtual] line for the serial monitor */
  say(line: string): void;
  /** the device token cloud.h registered with, or null if it hasn't registered */
  token(): string | null;
  /** POSTs to /api/device/chat on the mock server */
  chat(req: { token: string; pcm?: Uint8Array; text?: string; transcript?: string }): ChatResult;
  micWired(): boolean;
  ampWired(): boolean;
  onPhase?(p: VoicePhase): void;
  onFrame?(f: VadFrame, elapsedMs: number): void;
  onReply?(r: ChatResult & { durationMs: number }): void;
}

export interface VoiceOptions { thinkMs?: number }

export class VoiceSession {
  phase: VoicePhase = "idle";
  private deps: VoiceDeps;
  private thinkMs: number;
  private frames: Generator<Uint8Array> | null = null;
  private vad: Vad | null = null;
  private budget = 0; private elapsed = 0; private timer = 0;
  private pending: { fn: () => void; at: number } | null = null;
  private transcript = "";
  private tail = new Uint8Array((SAMPLE_RATE * VAD_PARAMS.frameMs) / 1000 * 2);   // a quiet frame once the clip has run out

  constructor(deps: VoiceDeps, o: VoiceOptions = {}) { this.deps = deps; this.thinkMs = o.thinkMs ?? 900; }

  get busy() { return this.phase !== "idle"; }

  private setPhase(p: VoicePhase) {
    this.phase = p; this.timer = 0;
    this.deps.firmware(PHASE_COMMAND[p]);
    this.deps.onPhase?.(p);
  }

  /** Someone said "Hey Dewy" (or pressed the button) and then spoke: `samples` is what the microphone hears from now on. */
  startListening(samples: Float32Array, transcript: string): boolean {
    if (this.busy) { this.deps.say("[virtual] Dewy is busy: wait until it is happy again."); return false; }
    if (!this.deps.micWired()) { this.deps.say("[virtual] the microphone isn't wired to the pins the voice code expects (GPIO 15, 16, 17): nothing to listen with."); return false; }
    if (!this.deps.token()) { this.deps.say("[virtual] Dewy isn't registered with the server yet (needs Wi-Fi and the cloud): it would not be able to ask anything."); return false; }
    this.vad = new Vad();
    this.frames = frames16(floatToPcm16(samples));
    this.transcript = transcript; this.budget = 0; this.elapsed = 0;
    this.setPhase("listening");
    return true;
  }

  /** `ask <text>`: typed instead of spoken, so the VAD and the microphone are skipped (the Worker accepts JSON text for tests). */
  askText(text: string): boolean {
    if (this.busy) { this.deps.say("[virtual] Dewy is busy: wait until it is happy again."); return false; }
    const token = this.deps.token();
    if (!token) { this.deps.say("[virtual] Dewy isn't registered with the server yet (needs Wi-Fi and the cloud): it would not be able to ask anything."); return false; }
    this.setPhase("thinking");
    const res = this.deps.chat({ token, text });
    this.schedule(() => this.finish(res, text), this.thinkMs);
    return true;
  }

  cancel() { this.frames = null; this.vad = null; this.pending = null; if (this.phase !== "idle") this.setPhase("idle"); }

  /** Moves the simulation on by `dtMs` of virtual time. */
  advance(dtMs: number) {
    if (this.phase === "idle") return;
    this.timer += dtMs;
    if (this.phase === "listening" && this.vad && this.frames) {
      this.budget += dtMs;
      while (this.budget >= VAD_PARAMS.frameMs && !this.vad.isDone) {
        this.budget -= VAD_PARAMS.frameMs; this.elapsed += VAD_PARAMS.frameMs;
        const next = this.frames.next();
        const f = this.vad.push(next.done ? this.tail : next.value);      // after the clip, the mic just hears quiet
        this.deps.onFrame?.(f, this.elapsed);
      }
      if (this.vad.isDone) this.stopListening();
    } else if (this.pending && this.timer >= this.pending.at) {
      const p = this.pending; this.pending = null; p.fn();
    }
  }

  private schedule(fn: () => void, afterMs: number) { this.pending = { fn, at: afterMs }; this.timer = 0; }

  private stopListening() {
    const clip = this.vad!.clip();
    this.vad = null; this.frames = null;
    if (clip.reason === "no-speech") {
      this.deps.say(`[virtual] heard nothing for ${VAD_PARAMS.waitForSpeechMs / 1000} s: giving up.`);
      this.setPhase("idle"); return;
    }
    this.deps.say(`[virtual] captured ${(clip.ms / 1000).toFixed(1)} s (${clip.speechMs} ms of speech, ended by ${clip.reason === "silence" ? "a pause" : "the 12 s limit"})${clip.tooShort ? ", TOO SHORT for the server" : ""}${clip.nobody ? ", too quiet: the server will say it didn't catch that" : ""}.`);
    const token = this.deps.token()!;
    this.setPhase("thinking");
    const res = this.deps.chat({ token, pcm: clip.pcm, transcript: this.transcript });
    this.schedule(() => this.finish(res, this.transcript), this.thinkMs);
  }

  private finish(res: ChatResult, _asked: string) {
    if (!res.ok) {
      this.deps.say(`[virtual] server answered ${res.status < 0 ? "nothing (no network)" : res.status}: ${res.error ?? ""}`.trim());
      this.setPhase("idle"); return;
    }
    this.deps.say(`[virtual] heard: "${res.heard ?? ""}"`);
    this.deps.say(`[virtual] reply: "${res.reply ?? ""}"`);
    if (!res.wav) { this.deps.say("[virtual] (words only: no audio came back, so nothing is spoken)"); this.deps.onReply?.({ ...res, durationMs: 0 }); this.setPhase("idle"); return; }
    const durationMs = (res.wav.samples.length / res.wav.rate) * 1000;
    if (!this.deps.ampWired()) this.deps.say("[virtual] the amplifier isn't wired to the pins the voice code expects (GPIO 18, 21, 38): no sound would come out.");
    this.setPhase("speaking");
    this.deps.onReply?.({ ...res, durationMs });
    this.schedule(() => this.setPhase("idle"), durationMs);
  }
}

/** Turns the mock server's answer into a ChatResult (shared by the page and the tests). */
export function readChatResponse(status: number, headers: Record<string, string>, body: Uint8Array): ChatResult {
  const type = (headers["content-type"] ?? "").split(";")[0];
  if (status < 0) return { ok: false, status, error: "no network" };
  if (type === "audio/wav") {
    let wav: DecodedWav | undefined;
    try { wav = decodeWav(body); } catch { /* handled below */ }
    const dec = (h: string | undefined) => { try { return decodeURIComponent(h ?? ""); } catch { return ""; } };
    if (!wav) return { ok: false, status, error: "the reply wasn't a readable WAV file" };
    return { ok: true, status, heard: dec(headers["x-dewy-heard"]), reply: dec(headers["x-dewy-reply"]), wav, wavBytes: body };
  }
  let j: { heard?: string; reply?: string; audio?: boolean; error?: string } = {};
  try { j = JSON.parse(new TextDecoder().decode(body)); } catch { /* leave empty */ }
  if (status >= 400 || j.error) return { ok: false, status, error: j.error ?? "unknown error" };
  return { ok: true, status, heard: j.heard ?? "", reply: j.reply ?? "" };
}
