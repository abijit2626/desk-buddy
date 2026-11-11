import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { Vad, VAD_PARAMS, frames16, runVad } from "../src/sim/vad";
import { floatToPcm16, rms16, SAMPLE_RATE, pcmToWav, decodeWav, wavTo16kMono, speedUp, resampleTo16k } from "../src/sim/audio";
import { generate, sliderToLevel } from "../src/sim/audio-gen";
import { PHASE_COMMAND, PHASE_FACE, VoiceSession, readChatResponse, type ChatResult, type VoiceDeps, type VoicePhase } from "../src/sim/voice";
import { MockWorker, REGISTER_KEY, SERVER_HOST, DIDNT_CATCH } from "../src/sim/mock-server";
import { parseVirtualCommand } from "../src/sim/virtual-commands";
import { WAKE_WORD } from "../src/sim/wakeword";
import { bootCore } from "./helpers";

const rmsOf = (x: Float32Array) => rms16(floatToPcm16(x));

describe("fake microphone sounds", () => {
  it("speech-like audio is loud enough, room noise and silence are not", () => {
    expect(rmsOf(generate("speech", 0.1))).toBeGreaterThan(VAD_PARAMS.silenceRms);
    expect(rmsOf(generate("noise", 1))).toBeLessThan(VAD_PARAMS.silenceRms);
    expect(rmsOf(generate("silence", 0.5))).toBe(0);
  });
  it("hits the level it is asked for", () => {
    for (const lvl of [0.02, 0.1, 0.3]) expect(rmsOf(generate("tone", lvl)) / 32768).toBeCloseTo(lvl, 2);
  });
  it("the bump is short and the long speech is longer than the server allows", () => {
    expect(generate("bump", 0.2).length / SAMPLE_RATE).toBeLessThan(0.4);
    expect(generate("long", 0.1).length / SAMPLE_RATE).toBeGreaterThan(12);
  });
  it("is the same every time for the same settings", () => {
    expect(generate("speech", 0.1, 5)).toEqual(generate("speech", 0.1, 5));
  });
  it("the level slider goes from below the threshold to loud", () => {
    expect(sliderToLevel(0) * 32768).toBeLessThan(VAD_PARAMS.silenceRms);
    expect(sliderToLevel(50)).toBeGreaterThan(0.01);
    expect(sliderToLevel(100)).toBeLessThanOrEqual(0.5);
  });
});

describe("level and silence logic (page-side stand-in, NOT vad.h)", () => {
  const pad = (x: Float32Array, lead: number, tail: number) => { const o = new Float32Array(lead + x.length + tail); o.set(x, lead); return o; };

  it("waits for speech, records it, and stops after a pause", () => {
    const audio = pad(generate("speech", 0.1), SAMPLE_RATE * 0.5, SAMPLE_RATE * 2);
    const { clip, frames } = runVad(floatToPcm16(audio));
    expect(frames[0].state).toBe("waiting");
    expect(frames.some((f) => f.state === "speech")).toBe(true);
    expect(frames.at(-1)!.state).toBe("done");
    expect(clip.reason).toBe("silence");
    expect(clip.nobody).toBe(false);
    expect(clip.tooShort).toBe(false);
    expect(clip.ms).toBeGreaterThan(2500);
    expect(clip.ms).toBeLessThan(5000);
  });

  it("keeps a little audio from before the first loud frame", () => {
    const { clip } = runVad(floatToPcm16(pad(generate("tone", 0.1), SAMPLE_RATE * 1, SAMPLE_RATE * 2)));
    expect(clip.ms).toBeGreaterThanOrEqual(VAD_PARAMS.preRollMs + 2000);
  });

  it("gives up when nobody speaks", () => {
    const { clip, frames } = runVad(floatToPcm16(generate("noise", 1)));
    expect(clip.reason).toBe("no-speech");
    expect(frames.at(-1)!.state).toBe("done");
  });

  it("flags a bump as too short, the way the Worker would answer 400", () => {
    const { clip } = runVad(floatToPcm16(pad(generate("bump", 0.3), 0, SAMPLE_RATE * 2)));
    expect(clip.speechMs).toBeLessThan(400);
    // the clip includes the 700 ms hang-over, so also check the raw speech length against the 0.4 s rule directly
    expect(clip.speechMs).toBeLessThan(VAD_PARAMS.minClipMs);
  });

  it("stops at the 12 second limit", () => {
    const { clip } = runVad(floatToPcm16(generate("long", 0.1)));
    expect(clip.reason).toBe("max");
    expect(clip.ms).toBeLessThanOrEqual(VAD_PARAMS.maxClipMs);
    expect(clip.pcm.length).toBeLessThanOrEqual(SAMPLE_RATE * 2 * 12);
  });

  it("measures frame level the same way the Worker measures a clip", () => {
    const pcm = floatToPcm16(generate("tone", 0.05));
    const v = new Vad();
    const f = v.push(pcm.subarray(0, 640));
    expect(f.rms).toBeCloseTo(rms16(pcm.subarray(0, 640)), 6);
    expect(f.isSpeech).toBe(true);
    expect(f.dbfs).toBeLessThan(0);
  });

  it("splits audio into 20 ms frames", () => {
    const n = [...frames16(new Uint8Array(SAMPLE_RATE * 2))].length;
    expect(n).toBe(50);
  });
});

describe("WAV files", () => {
  it("round-trips PCM through a WAV file and reads it back as 16 kHz mono", () => {
    const x = generate("tone", 0.2);
    const wav = pcmToWav(floatToPcm16(x));
    const back = wavTo16kMono(wav);
    expect(back.length).toBe(x.length);
    expect(Math.abs(back[1000] - x[1000])).toBeLessThan(0.001);
  });
  it("resamples other rates", () => {
    const x = new Float32Array(44100).map((_, i) => Math.sin(i * 0.05));
    const wav = pcmToWav(floatToPcm16(x), 44100);
    expect(wavTo16kMono(wav).length).toBe(16000);
    expect(resampleTo16k(x, 44100).length).toBe(16000);
  });
  it("raises the sample rate in the header only, like the Worker's voice speed-up", () => {
    const wav = pcmToWav(floatToPcm16(generate("tone", 0.1)));
    const fast = speedUp(wav, 1.15);
    expect(decodeWav(fast).rate).toBe(18400);
    expect(fast.slice(44)).toEqual(wav.slice(44));
  });
  it("refuses things that aren't WAV files, with a plain sentence", () => {
    expect(() => decodeWav(new Uint8Array(10))).toThrow(/isn't a WAV/);
    const bad = pcmToWav(new Uint8Array(100)); bad[20] = 99;                  // an unknown encoding
    expect(() => decodeWav(bad)).toThrow(/isn't supported/);
  });
  it("trusts the bytes when the length field is wrong (the Worker's WAV can be like that)", () => {
    const wav = pcmToWav(floatToPcm16(generate("tone", 0.1))); new DataView(wav.buffer).setUint32(40, 0xffffffff, true);
    expect(decodeWav(wav).samples.length).toBe(32000);
  });
});

describe("the voice loop: listen, think, talk, happy", () => {
  function rig(opts: { mic?: boolean; amp?: boolean; token?: string | null } = {}) {
    let t = 1_794_000_000;
    const server = new MockWorker({ now: () => t });
    const reg = server.handle({ method: "POST", url: `https://${SERVER_HOST}/api/device/register`, headers: { "x-register-key": REGISTER_KEY }, body: "{}" });
    const token = JSON.parse(new TextDecoder().decode(reg.body)).token as string;
    const commands: string[] = [], said: string[] = [], phases: VoicePhase[] = [];
    let reply: (ChatResult & { durationMs: number }) | null = null;
    const deps: VoiceDeps = {
      firmware: (c) => commands.push(c), say: (l) => said.push(l),
      token: () => (opts.token === undefined ? token : opts.token),
      chat: (r) => {
        const headers: Record<string, string> = { authorization: "Bearer " + r.token };
        let body: Uint8Array | string;
        if (r.pcm) { headers["content-type"] = "audio/pcm"; headers["x-mock-transcript"] = encodeURIComponent(r.transcript ?? ""); body = r.pcm; }
        else { headers["content-type"] = "application/json"; body = JSON.stringify({ text: r.text }); }
        const res = server.handle({ method: "POST", url: `https://${SERVER_HOST}/api/device/chat`, headers, body });
        return readChatResponse(res.status, res.headers, res.body);
      },
      micWired: () => opts.mic ?? true, ampWired: () => opts.amp ?? true,
      onPhase: (p) => phases.push(p), onReply: (r) => { reply = r; },
    };
    const v = new VoiceSession(deps, { thinkMs: 500 });
    const run = (ms: number) => { for (let left = ms; left > 0; left -= 20) { v.advance(20); t += 0; } };
    return { v, server, commands, said, phases, run, get reply() { return reply; } };
  }
  const speech = () => { const s = generate("speech", 0.1), o = new Float32Array(SAMPLE_RATE * 4); o.set(s, 4000); return o; };

  it("goes listen -> think -> talk -> happy and drives the firmware's own commands", () => {
    const r = rig();
    expect(r.v.startListening(speech(), "what's the weather")).toBe(true);
    expect(r.v.phase).toBe("listening");
    r.run(30000);
    expect(r.phases).toEqual(["listening", "thinking", "speaking", "idle"]);
    expect(r.commands).toEqual(["listen", "think", "talk", "reset"]);
    expect(r.phases.map((p) => PHASE_FACE[p])).toEqual(["listen", "think", "talk", "happy"]);
    expect(r.reply?.heard).toBe("what's the weather");
    expect(r.reply?.reply).toMatch(/clear and sunny/);
    expect(r.reply!.durationMs).toBeGreaterThan(1000);
    expect(r.v.phase).toBe("idle");
  });

  it("holds the talking face for as long as the reply lasts", () => {
    const r = rig();
    r.v.startListening(speech(), "hello");
    let talkAt = -1, happyAt = -1, t = 0;
    for (; t < 20000; t += 20) {
      r.v.advance(20);
      if (r.v.phase === "speaking" && talkAt < 0) talkAt = t;
      if (talkAt >= 0 && r.v.phase === "idle" && happyAt < 0) happyAt = t;
    }
    expect(happyAt - talkAt).toBeGreaterThan(r.reply!.durationMs - 60);
    expect(happyAt - talkAt).toBeLessThan(r.reply!.durationMs + 60);
  });

  it("will not start without a wired microphone or a registered Dewy", () => {
    const noMic = rig({ mic: false });
    expect(noMic.v.startListening(speech(), "hi")).toBe(false);
    expect(noMic.said.join("\n")).toMatch(/microphone isn't wired.*GPIO 15, 16, 17/);
    const noToken = rig({ token: null });
    expect(noToken.v.startListening(speech(), "hi")).toBe(false);
    expect(noToken.said.join("\n")).toMatch(/isn't registered/);
    expect(noToken.commands).toEqual([]);
  });

  it("gives up quietly when nobody speaks", () => {
    const r = rig();
    r.v.startListening(generate("noise", 1), "hi");
    r.run(8000);
    expect(r.said.join("\n")).toMatch(/heard nothing/);
    expect(r.commands).toEqual(["listen", "reset"]);
  });

  it("shows the server's words-only answer for a too-quiet clip", () => {
    const r = rig();
    const quiet = new Float32Array(SAMPLE_RATE * 2);                         // speech just above the VAD threshold but below... use a tone at RMS 70
    for (let i = 0; i < quiet.length; i++) quiet[i] = (70 * Math.SQRT2 / 32768) * Math.sin(i * 0.3);
    r.v.startListening(quiet, "hi");
    r.run(8000);
    expect(r.commands.at(-1)).toBe("reset");
    expect(r.reply?.reply).toBeDefined();
  });

  it("`ask` goes straight to thinking, then talking", () => {
    const r = rig();
    expect(r.v.askText("start a 5 minute timer")).toBe(true);
    r.run(5000);
    expect(r.commands).toEqual(["think", "talk", "reset"]);
    expect(r.reply?.reply).toMatch(/Started a 5 minute timer/);
    expect(r.server.handle({ method: "GET", url: `https://${SERVER_HOST}/api/device/sync?rev=1&ack=0`, headers: { authorization: "Bearer " + "x".repeat(30) }, body: "" }).status).toBe(401);
  });

  it("reports server errors and doesn't get stuck", () => {
    const r = rig({ token: "x".repeat(30) });                                // a token the server doesn't know
    r.v.askText("hi");
    r.run(2000);
    expect(r.said.join("\n")).toMatch(/server answered 401: unknown device/);
    expect(r.v.phase).toBe("idle");
    expect(r.commands.at(-1)).toBe("reset");
  });

  it("says so when the amplifier isn't wired but still finishes", () => {
    const r = rig({ amp: false });
    r.v.askText("hello");
    r.run(8000);
    expect(r.said.join("\n")).toMatch(/amplifier isn't wired.*GPIO 18, 21, 38/);
    expect(r.v.phase).toBe("idle");
  });

  it("refuses a second question while busy", () => {
    const r = rig();
    r.v.askText("one");
    expect(r.v.askText("two")).toBe(false);
    expect(r.said.join("\n")).toMatch(/busy/);
  });

  it("uses the real command names the firmware knows", () => {
    expect(PHASE_COMMAND).toEqual({ idle: "reset", listening: "listen", thinking: "think", speaking: "talk" });
  });

  it("drives the REAL firmware face through the four states", async () => {
    const d = await bootCore();
    d.run(500);
    const moods: string[] = [];
    for (const p of ["listening", "thinking", "speaking", "idle"] as VoicePhase[]) { d.cmd(PHASE_COMMAND[p], 100); moods.push(d.core.status().moodName); }
    expect(moods).toEqual(["listening", "thinking", "talking", "happy"]);
  });

  it("DIDNT_CATCH is the Worker's sentence", () => { expect(DIDNT_CATCH).toBe("Sorry, I didn't catch that. Could you say it again?"); });
});

describe("simulator-only serial commands", () => {
  it("recognises ask, mic and ww", () => {
    expect(parseVirtualCommand("ask what time is it")).toEqual({ kind: "ask", text: "what time is it" });
    expect(parseVirtualCommand("ASK")).toEqual({ kind: "ask", text: "" });
    expect(parseVirtualCommand("mic")).toEqual({ kind: "mic", seconds: 1 });
    expect(parseVirtualCommand("mic 3")).toEqual({ kind: "mic", seconds: 3 });
    expect(parseVirtualCommand("mic 99")).toEqual({ kind: "mic", seconds: 12 });
    expect(parseVirtualCommand("ww debug")).toEqual({ kind: "ww-debug", on: null });
    expect(parseVirtualCommand("ww debug on")).toEqual({ kind: "ww-debug", on: true });
    expect(parseVirtualCommand("ww debug off")).toEqual({ kind: "ww-debug", on: false });
    expect(parseVirtualCommand("ww trigger")).toEqual({ kind: "ww-trigger" });
  });
  it("leaves every firmware command alone", () => {
    for (const c of ["help", "8ball", "timer 5", "note ask me later", "weather rain", "askew", "microphone", "wwx"]) expect(parseVirtualCommand(c), c).toBeNull();
  });
});

describe("the wake word settings shown by `ww debug`", () => {
  it("are the ones in voice/wakeword/model/hey_dewy.json", () => {
    const j = JSON.parse(readFileSync(new URL("../../voice/wakeword/model/hey_dewy.json", import.meta.url), "utf8"));
    expect(WAKE_WORD).toMatchObject({ phrase: j.wake_word, probabilityCutoff: j.probability_cutoff, slidingWindow: j.sliding_window_size, featureStepMs: j.feature_step_size, tensorArenaBytes: j.tensor_arena_size, modelBytes: j.model_bytes });
  });
});
