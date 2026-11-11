// The mock server must keep the same shape as the real Worker in cloud/. These tests read the Worker's own source and
// compare: constants parsed from the text, and (differentially) the helper functions the Worker exports.
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { CLOUD_DIR } from "./helpers";
import { DEFAULT_SETTINGS, DIDNT_CATCH, LIMITS, REGISTER_KEY, TOOL_NAMES, cleanText, headerText, localTimeText, parseHeadlines, speakable } from "../src/sim/mock-server";
import { VAD_PARAMS } from "../src/sim/vad";

const read = (f: string) => readFileSync(CLOUD_DIR + f, "utf8");
const chat = read("src/chat.ts"), index = read("src/index.ts"), wrangler = read("wrangler.toml");
const num = (src: string, name: string) => { const m = new RegExp(`const ${name}\\s*=\\s*([^;]+);`).exec(src); expect(m, name).not.toBeNull(); return Function(`const SAMPLE_RATE = 16000; return (${m![1]});`)() as number; };
const str = (src: string, name: string) => { const m = new RegExp(`const ${name}\\s*=\\s*"((?:[^"\\\\]|\\\\.)*)"`).exec(src); expect(m, name).not.toBeNull(); return JSON.parse(`"${m![1]}"`) as string; };
const toml = (name: string) => { const m = new RegExp(`^${name}\\s*=\\s*"(\\d+)"`, "m").exec(wrangler); expect(m, name).not.toBeNull(); return Number(m![1]); };

describe("constants match cloud/src/chat.ts, index.ts and wrangler.toml", () => {
  it("audio limits and the silence rule", () => {
    expect(LIMITS.minAudioBytes).toBe(num(chat, "MIN_AUDIO_BYTES"));
    expect(LIMITS.maxAudioBytes).toBe(num(chat, "MAX_AUDIO_BYTES"));
    expect(LIMITS.silenceRms).toBe(num(chat, "SILENCE_RMS"));
    expect(LIMITS.maxHeard).toBe(num(chat, "MAX_HEARD"));
    expect(LIMITS.maxReplyChars).toBe(num(chat, "MAX_REPLY_CHARS"));
    expect(LIMITS.historySeconds).toBe(num(chat, "HISTORY_SECONDS"));
    expect(LIMITS.historyTurns).toBe(num(chat, "HISTORY_TURNS"));
    expect(LIMITS.maxRounds).toBe(num(chat, "MAX_ROUNDS"));
  });

  it("the page-side VAD uses the Worker's thresholds", () => {
    expect(VAD_PARAMS.silenceRms).toBe(num(chat, "SILENCE_RMS"));
    expect(VAD_PARAMS.minClipMs).toBe(num(chat, "MIN_AUDIO_BYTES") / 32);
    expect(VAD_PARAMS.maxClipMs).toBe(num(chat, "MAX_AUDIO_BYTES") / 32);
  });

  it("the daily limits, the default voice speed and the 'didn't catch that' text", () => {
    expect(LIMITS.chatsPerDay).toBe(toml("CHATS_PER_DAY"));
    expect(LIMITS.chatTokensPerDay).toBe(toml("CHAT_TOKENS_PER_DAY"));
    expect(LIMITS.ttsCharsPerDay).toBe(toml("TTS_CHARS_PER_DAY"));
    expect(chat).toMatch(new RegExp(`n >= 0\\.8 && n <= 1\\.5 \\? n : ${LIMITS.voiceSpeed}`));
    expect(DIDNT_CATCH).toBe(str(chat, "DIDNT_CATCH"));
  });

  it("the JSON size limit, the header limits and the error sentences", () => {
    expect(chat).toContain(`text.length > ${LIMITS.maxJsonBody}`);
    expect(chat).toContain("headerText(heard, 200)");
    expect(chat).toContain("headerText(reply, MAX_REPLY_CHARS)");
    for (const sentence of ["That recording is too short.", "That recording is too long: 12 seconds at most.", "send 16 kHz 16-bit mono audio, or JSON with text", "I've talked a lot today. Ask me again tomorrow!"]) {
      expect(chat, sentence).toContain(sentence);
    }
    expect(chat).toContain("That's ${chatLimit} questions today. Ask me again tomorrow!");
    expect(chat).toContain("Send text of up to ${MAX_HEARD} characters.");
    expect(chat).toContain('"x-dewy-heard"');
    expect(chat).toContain('"x-dewy-reply"');
  });

  it("the tools the model may call", () => {
    const names = [...chat.matchAll(/^\s*\{?\s*name: "(get_weather|get_news|read_notes|start_timer|stop_timer)"/gm)].map((m) => m[1]);
    expect(names.sort()).toEqual([...TOOL_NAMES].sort());
  });

  it("pairing, online and body limits, the pairing alphabet and the routes", () => {
    expect(LIMITS.pairSeconds).toBe(num(index, "PAIR_SECONDS"));
    expect(LIMITS.onlineSeconds).toBe(num(index, "ONLINE_SECONDS"));
    expect(LIMITS.maxBody).toBe(num(index, "MAX_BODY"));
    expect(index).toContain('"23456789ABCDEFGHJKLMNPQRSTUVWXYZ"');
    for (const route of ["/api/device/register", "/api/device/pair", "/api/device/sync", "/api/device/chat"]) expect(index, route).toContain(`"${route}"`);
    expect(index).toMatch(/x-register-key/);
    expect(index).toContain('"wrong register key"');
  });

  it("the sync answer has the same fields", () => {
    const m = /const out: Record<string, unknown> = \{([\s\S]*?)\};/.exec(index)!;
    for (const key of ["now", "rev", "paired", "events"]) expect(m[1], key).toMatch(new RegExp(`\\b${key}:`));
    for (const key of ["settings", "pairCode", "firmware"]) expect(index, key).toContain(`out.${key} =`);
  });

  it("the register key in the virtual build is made up, not the .dev.vars value", () => {
    const dev = read(".dev.vars.example");
    expect(dev).not.toContain(REGISTER_KEY);
    expect(REGISTER_KEY).toBe("virtual-register-key");
  });
});

// The Worker exports a few pure helpers. The mock's copies must give identical answers.
describe("helpers match the Worker's own exports", async () => {
  const workerPath = CLOUD_DIR + "src/chat.ts";
  const real = await import(/* @vite-ignore */ workerPath) as {
    localTimeText(tz: string, t: number): string; headerText(s: string, n: number): string; speakable(s: string): string; parseHeadlines(x: string, n?: number): string[];
  };
  const settings = await import(/* @vite-ignore */ CLOUD_DIR + "src/settings.ts") as { DEFAULT_SETTINGS: unknown; cleanText(v: unknown, n: number, e?: boolean): string | null };

  it("localTimeText", () => {
    for (const tz of ["IST-5:30", "UTC0", "EST5", "<+07>-7", "PST8PDT", "CET-1", "weird"]) {
      for (const t of [0, 1_700_000_000, 1_795_000_000, 1_767_225_600]) expect(localTimeText(tz, t), `${tz} @ ${t}`).toBe(real.localTimeText(tz, t));
    }
  });
  it("headerText (emoji must never be cut in half)", () => {
    for (const s of ["hello", "café ☕", "party 🎉🎉🎉", "a".repeat(400), "x\uD83C", "\uDF89x", ""]) {
      for (const n of [1, 2, 5, 200, 320]) expect(headerText(s, n), `${JSON.stringify(s)} / ${n}`).toBe(real.headerText(s, n));
    }
  });
  it("speakable", () => {
    const long = "This is a sentence. ".repeat(40);
    for (const s of ["**Hi** _there_ see https://x.test/a now", "line one\n\nline two", long, "word ".repeat(100), "🎉".repeat(400), "plain"]) expect(speakable(s)).toBe(real.speakable(s));
  });
  it("parseHeadlines", () => {
    const xml = '<rss><item><title><![CDATA[Rain &amp; wind]]></title></item><item><title>It&#39;s <b>on</b></title></item><item><title></title></item><item><title>third</title></item>';
    expect(parseHeadlines(xml)).toEqual(real.parseHeadlines(xml));
    expect(parseHeadlines(xml, 1)).toEqual(real.parseHeadlines(xml, 1));
  });
  it("cleanText", () => {
    for (const v of ["  a\tb\n c ", "", "x".repeat(400), 7, null, "🎉"]) for (const n of [5, 300]) for (const e of [false, true]) expect(cleanText(v, n, e)).toBe(settings.cleanText(v, n, e));
  });
  it("the default settings", () => {
    expect(DEFAULT_SETTINGS).toEqual(settings.DEFAULT_SETTINGS);
  });
});
