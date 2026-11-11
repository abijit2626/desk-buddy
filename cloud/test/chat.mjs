// End-to-end check of Dewy's voice chat against a running server (npx wrangler dev -c wrangler.dev.toml).
// The server must run with CHAT_FAKE_AI="1" (set in .dev.vars): scripted stand-ins replace Whisper, Claude and the voice,
// so everything else is real: routing, sign-in, database, daily limits, tools and the WAV answer.
// In this fake mode the "speech to text" simply reads the audio bytes as text, so test audio is text padded to length.
import { readFileSync } from "node:fs";

const BASE = process.env.BASE || "http://127.0.0.1:8787";
const vars = Object.fromEntries(
  readFileSync(new URL("../.dev.vars", import.meta.url), "utf8").split("\n").filter(Boolean).map((l) => {
    const i = l.indexOf("="); return [l.slice(0, i), l.slice(i + 1).replace(/^"|"$/g, "")];
  })
);

let passed = 0, failed = 0;
function check(name, cond, extra) {
  if (cond) { passed++; console.log("  ok   " + name); }
  else { failed++; console.log("  FAIL " + name + (extra !== undefined ? "  -> " + JSON.stringify(extra) : "")); }
}

async function register() {
  const r = await fetch(BASE + "/api/device/register", { method: "POST", headers: { "x-register-key": vars.REGISTER_KEY, "content-type": "application/json" }, body: "{}" });
  return (await r.json()).token;
}

// Sends a question; returns the answer as text plus whether it came back as a spoken WAV.
async function chat(token, body, type = "application/json") {
  const r = await fetch(BASE + "/api/device/chat", {
    method: "POST",
    headers: { ...(token ? { authorization: "Bearer " + token } : {}), "content-type": type },
    body: type === "application/json" && typeof body !== "string" ? JSON.stringify(body) : body,
  });
  const ct = r.headers.get("content-type") || "";
  if (ct.startsWith("audio/wav")) {
    const bytes = new Uint8Array(await r.arrayBuffer());
    return {
      status: r.status, wav: bytes, spoken: true,
      heard: decodeURIComponent(r.headers.get("x-dewy-heard") || ""),
      reply: decodeURIComponent(r.headers.get("x-dewy-reply") || ""),
    };
  }
  const data = await r.json().catch(() => null);
  return { status: r.status, spoken: false, ...data };
}
const ask = (token, text) => chat(token, { text });
const isWav = (b) => b && String.fromCharCode(...b.subarray(0, 4)) === "RIFF" && String.fromCharCode(...b.subarray(8, 12)) === "WAVE";

function paddedAudio(text, bytes = 16000) {
  const b = new Uint8Array(bytes).fill(0x20);
  b.set(new TextEncoder().encode(text));
  return b;
}

console.log("Dewy voice chat test against " + BASE);
const dev = await register();
check("registered a test Dewy", typeof dev === "string" && dev.length > 20, dev);

// --- sign-in and bad input
let r = await ask(null, "hello");
check("no sign-in is refused (401)", r.status === 401, r);
r = await ask("x".repeat(43), "hello");
check("an unknown device is refused (401)", r.status === 401, r);
r = await chat(dev, "not json {", "application/json");
check("invalid JSON is a 400", r.status === 400, r);
r = await ask(dev, "   ");
check("empty text is a 400", r.status === 400, r);
r = await ask(dev, "a".repeat(301));
check("text over 300 characters is a 400", r.status === 400, r);
r = await chat(dev, "hello", "text/plain");
check("an unsupported content type is a 415", r.status === 415, r);
r = await chat(dev, new Uint8Array(1000), "audio/pcm");
check("a recording under 0.4 s is a 400", r.status === 400, r);
r = await chat(dev, new Uint8Array(16000 * 2 * 13), "audio/pcm");
check("a recording over 12 s is a 413", r.status === 413, r);

// --- a text question gets a spoken answer
r = await ask(dev, "Hello Dewy, how are you?");
check("text question: 200 with a WAV answer", r.status === 200 && r.spoken && isWav(r.wav), { status: r.status, spoken: r.spoken });
check("answer headers carry what was heard and said", r.heard === "Hello Dewy, how are you?" && r.reply.startsWith("You said: Hello Dewy"), r);
if (r.wav) {
  const v = new DataView(r.wav.buffer, r.wav.byteOffset);
  check("the WAV is 16-bit mono PCM at 16 kHz", v.getUint16(20, true) === 1 && v.getUint16(22, true) === 1 && v.getUint32(24, true) === 16000 && v.getUint16(34, true) === 16, null);
}

// --- memory of the last few turns
r = await ask(dev, "and what about tomorrow?");
check("a follow-up sees the earlier turns", r.reply?.includes("follow-up, I remember 2 earlier"), r.reply);

// --- audio from the microphone
r = await chat(dev, paddedAudio("what is the weather like"), "audio/pcm");
check("audio question is transcribed and answered", r.status === 200 && r.spoken && r.heard === "what is the weather like", r);
check("the weather tool ran (28 degrees, 60 percent rain)", /28 degrees/.test(r.reply) && /60/.test(r.reply) && /Palakkad/.test(r.reply), r.reply);
r = await chat(dev, new Uint8Array(16000), "audio/pcm");
check("a silent recording gets a friendly 'didn't catch that'", r.status === 200 && /didn't catch/i.test(r.reply ?? ""), r);

// --- the tools
r = await ask(dev, "tell me the news");
check("news tool: headlines are read, entities decoded", /Rain & wind hit the coast/.test(r.reply) && /'this year'/.test(r.reply), r.reply);
r = await ask(dev, "any tech news?");
check("news with a topic works", r.status === 200 && /headlines/.test(r.reply), r.reply);

const appToken = await (async () => {
  // get a notes link for this Dewy: pair a phone, then send two notes
  const pair = await (await fetch(BASE + "/api/device/pair", { method: "POST", headers: { authorization: "Bearer " + dev } })).json();
  const phone = await (await fetch(BASE + "/api/pair", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ code: pair.code, label: "Test phone" }) })).json();
  return phone.token;
})();
r = await ask(dev, "read my notes");
check("no notes yet: says so", /no notes yet/i.test(r.reply), r.reply);
for (const text of ["Buy milk on the way home", "Call grandma at six"]) {
  await fetch(BASE + "/api/notes", { method: "POST", headers: { authorization: "Bearer " + appToken, "content-type": "application/json" }, body: JSON.stringify({ text }) });
}
r = await ask(dev, "read my notes please");
check("notes tool reads the newest notes first", /Call grandma at six/.test(r.reply) && /Buy milk/.test(r.reply) && r.reply.indexOf("grandma") < r.reply.indexOf("milk"), r.reply);

r = await ask(dev, "start a 25 minute timer");
check("timer tool: confirms", r.status === 200 && /25 minute/.test(r.reply), r.reply);
let sync = await (await fetch(BASE + "/api/device/sync?rev=0", { headers: { authorization: "Bearer " + dev } })).json();
check("the timer shows up as an event on Dewy's next check-in", sync.events.some((e) => e.kind === "timer_start" && e.minutes === 25), sync.events);
r = await ask(dev, "stop the timer");
sync = await (await fetch(BASE + "/api/device/sync?rev=0", { headers: { authorization: "Bearer " + dev } })).json();
check("stopping the timer queues timer_stop", sync.events.some((e) => e.kind === "timer_stop"), sync.events);

r = await ask(dev, "what day is it today?");
check("the current date and time reach the model", /I know it is .*20\d\d, \d\d?:\d\d [ap]m/.test(r.reply), r.reply);

// --- long replies are cut at a sentence, nothing odd is spoken
r = await ask(dev, "give me a long answer");
check("a long reply is cut to 320 characters", r.reply.length <= 320 && /[.!?]$/.test(r.reply), r.reply.length);

// --- the spoken-voice budget: after it runs out Dewy still gets the words
const tts = await register();
let spokenCount = 0, textOnly;
for (let i = 0; i < 25 && !textOnly; i++) {
  const x = await ask(tts, "long " + i);
  if (x.spoken) spokenCount++; else textOnly = x;
}
check("after the spoken-characters cap the answer comes back as text", !!textOnly && textOnly.status === 200 && textOnly.audio === false && textOnly.reply.length > 50, { spokenCount, textOnly });
check("the cap only kicked in after several spoken answers", spokenCount >= 3, spokenCount);

// --- the daily question limit (a fresh Dewy: no spoken budget used up, so keep replies short)
const lim = await register();
let last, firstLimited = 0;
for (let i = 1; i <= 41; i++) {
  last = await ask(lim, "hi " + i);
  if (last.status === 429 && !firstLimited) firstLimited = i;
}
check("question number 41 in a day is refused with 429", firstLimited === 41, firstLimited);
check("the refusal is friendly", /tomorrow/i.test(last.error ?? ""), last);

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
