// End-to-end check of the Dewy cloud API against a running server.
// Usage: npx wrangler dev   (in another terminal), then: node test/smoke.mjs
// Uses the keys from .dev.vars. BASE overrides the address.
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
async function call(method, path, { token, body, headers = {} } = {}) {
  const h = { ...headers };
  if (token) h.authorization = "Bearer " + token;
  if (body !== undefined) h["content-type"] = "application/json";
  const r = await fetch(BASE + path, { method, headers: h, body: body === undefined ? undefined : JSON.stringify(body) });
  const text = await r.text();
  let data; try { data = JSON.parse(text); } catch { data = text; }
  return { status: r.status, data };
}

console.log("Dewy cloud smoke test against " + BASE);

// --- Dewy registers and asks for a pairing code
let r = await call("POST", "/api/device/register", { body: { fw: "1.0.0" } });
check("register without the key is refused", r.status === 403, r);
r = await call("POST", "/api/device/register", { body: { fw: "1.0.0" }, headers: { "x-register-key": vars.REGISTER_KEY } });
check("register with the key", r.status === 201 && r.data.token, r);
const dev = r.data.token;
r = await call("POST", "/api/device/pair", { token: dev });
check("Dewy gets an 8-character pairing code", r.status === 200 && /^[2-9A-HJ-NP-Z]{8}$/.test(r.data.code), r);
const code = r.data.code;
r = await call("GET", "/api/device/sync?rev=0", { token: dev, headers: { "x-dewy-version": "1.0.0" } });
check("sync before pairing: not paired, shows the code, sends settings", r.status === 200 && r.data.paired === false && r.data.pairCode === code && r.data.settings?.name === "Dewy", r);

// --- her phone pairs
r = await call("POST", "/api/pair", { body: { code: "ZZZZZZZZ", label: "Her iPhone" } });
check("a wrong code is refused", r.status === 400, r);
r = await call("POST", "/api/pair", { body: { code: code.slice(0, 4) + "-" + code.slice(4).toLowerCase(), label: "Her iPhone" } });
check("the right code pairs (dashes and lower case are fine)", r.status === 201 && r.data.role === "owner", r);
const owner = r.data.token;
r = await call("POST", "/api/pair", { body: { code } });
check("a code works only once", r.status === 400, r);
r = await call("GET", "/api/me", { token: owner });
check("me: owner, Dewy online", r.status === 200 && r.data.role === "owner" && r.data.dewy.online === true && r.data.dewy.firmware === "1.0.0", r);
r = await call("GET", "/api/me", { token: "x".repeat(43) });
check("a made-up token is refused", r.status === 401, r);

// --- settings
r = await call("GET", "/api/settings", { token: owner });
check("read settings", r.status === 200 && r.data.rev === 1, r);
const s = r.data.settings;
r = await call("PUT", "/api/settings", { token: owner, body: { rev: 1, settings: { ...s, alarms: [{ id: "a1", time: "25:00", days: [], label: "", on: true }] } } });
check("an impossible alarm time is refused", r.status === 400, r);
r = await call("PUT", "/api/settings", { token: owner, body: { rev: 1, settings: { ...s, birthday: { month: 2, day: 30 } } } });
check("30 February is refused", r.status === 400, r);
const good = { ...s, alarms: [{ id: "a1", time: "07:00", days: [1, 2, 3, 4, 5], label: "College", on: true }], birthday: { month: 3, day: 14 } };
r = await call("PUT", "/api/settings", { token: owner, body: { rev: 1, settings: good } });
check("save settings with an alarm and a birthday", r.status === 200 && r.data.rev === 2, r);
r = await call("PUT", "/api/settings", { token: owner, body: { rev: 1, settings: good } });
check("saving over newer settings is caught", r.status === 409 && r.data.rev === 2, r);
r = await call("GET", "/api/device/sync?rev=1", { token: dev });
check("Dewy gets the new settings", r.data.rev === 2 && r.data.settings?.alarms?.[0]?.time === "07:00" && r.data.paired === true && !r.data.pairCode, r);
r = await call("GET", "/api/device/sync?rev=2", { token: dev });
check("no settings sent when nothing changed", r.data.settings === undefined, r);

// --- timer and notes reach Dewy, once
r = await call("POST", "/api/timer", { token: owner, body: { minutes: 25 } });
check("start a study timer", r.status === 200, r);
r = await call("POST", "/api/notes", { token: owner, body: { text: "  Good luck\ntoday!  " } });
check("send a note", r.status === 201, r);
r = await call("POST", "/api/notes", { token: owner, body: { text: "x".repeat(121) } });
check("a note over 120 characters is refused", r.status === 400, r);
r = await call("GET", "/api/device/sync?rev=2", { token: dev });
const evs = r.data.events || [];
check("Dewy receives the timer and the note", evs.length === 2 && evs[0].kind === "timer_start" && evs[0].minutes === 25 && evs[1].kind === "note" && evs[1].text === "Good luck today!" && evs[1].from === "you", evs);
r = await call("GET", "/api/device/sync?rev=2&ack=" + evs[evs.length - 1].id, { token: dev });
check("after Dewy confirms, they aren't sent again", r.data.events.length === 0, r.data);

// --- a notes link for Abijit
r = await call("POST", "/api/links", { token: owner, body: { label: "Abijit" } });
check("make a notes link", r.status === 201 && r.data.url.includes("/notes#"), r);
const linkToken = r.data.url.split("#")[1], linkId = r.data.id;
r = await call("POST", "/api/notes", { token: linkToken, body: { text: "Hi from Abijit" } });
check("the link can send a note", r.status === 201, r);
r = await call("GET", "/api/settings", { token: linkToken });
check("the link can't read settings", r.status === 403, r);
r = await call("POST", "/api/timer", { token: linkToken, body: { minutes: 5 } });
check("the link can't start a timer", r.status === 403, r);
r = await call("GET", "/api/notes", { token: linkToken });
check("the link only sees its own notes", r.status === 200 && r.data.notes.length === 1 && r.data.notes[0].text === "Hi from Abijit", r);
r = await call("GET", "/api/notes", { token: owner });
check("the owner sees all notes", r.status === 200 && r.data.notes.length === 2, r);
r = await call("GET", "/api/device/sync?rev=2", { token: dev });
check("Dewy gets the note with Abijit's name on it", r.data.events?.[0]?.from === "Abijit", r.data);

// --- a second phone, and removing access
r = await call("POST", "/api/invite", { token: owner });
check("invite another phone", r.status === 200, r);
r = await call("POST", "/api/pair", { body: { code: r.data.code, label: "Abijit's phone" } });
check("the second phone pairs", r.status === 201, r);
const owner2 = r.data.token;
r = await call("GET", "/api/access", { token: owner2 });
check("access list: 2 phones and 1 link", r.data.access?.length === 3, r);
r = await call("DELETE", "/api/access/" + linkId, { token: owner2 });
check("remove the notes link", r.status === 200, r);
r = await call("POST", "/api/notes", { token: linkToken, body: { text: "still here?" } });
check("the removed link stops working", r.status === 401, r);

r = await call("DELETE", "/api/access/%E0%A4%A", { token: owner2 });
check("a broken %-escape in an id is a 400, not a server error", r.status === 400, r);

// --- firmware updates
r = await call("PUT", "/api/admin/firmware", { token: "wrong-admin-key-wrong-admin-key", body: {} });
check("publishing firmware needs the admin key", r.status === 403, r);
const fwBody = { version: "1.1.0", url: "https://github.com/abijit2626/desk-buddy/releases/download/v1.1.0/dewy.bin", sha256: "a".repeat(64), size: 1500000 };
r = await call("PUT", "/api/admin/firmware", { token: vars.ADMIN_KEY, body: fwBody });
check("publish firmware 1.1.0", r.status === 200, r);
r = await call("GET", "/api/device/sync?rev=2", { token: dev, headers: { "x-dewy-version": "1.0.0" } });
check("Dewy on 1.0.0 is offered 1.1.0", r.data.firmware?.version === "1.1.0", r.data);
r = await call("GET", "/api/device/sync?rev=2", { token: dev, headers: { "x-dewy-version": "1.1.0" } });
check("Dewy on 1.1.0 is not offered it again", r.data.firmware === undefined, r.data);

// --- daily note limit
let last;
for (let i = 0; i < 30; i++) last = await call("POST", "/api/notes", { token: owner, body: { text: "note " + i } });
check("the 31st note of the day is refused", last.status === 429, last);

// --- on/off switch and the bond
r = await call("GET", "/api/settings", { token: owner });
check("settings include the switch, on by default", r.status === 200 && r.data.settings.powerOn === true, r);
let cur = r.data;
r = await call("PUT", "/api/settings", { token: owner, body: { rev: cur.rev, settings: { ...cur.settings, powerOn: "no" } } });
check("a switch that isn't true/false is refused", r.status === 400, r);
r = await call("PUT", "/api/settings", { token: owner, body: { rev: cur.rev, settings: { ...cur.settings, powerOn: false } } });
check("switch Dewy off", r.status === 200 && r.data.settings.powerOn === false, r);
r = await call("GET", "/api/device/sync?rev=" + cur.rev, { token: dev });
check("Dewy hears it is switched off", r.data.settings?.powerOn === false, r.data);
const { powerOn: _drop, ...oldShape } = r.data.settings;
r = await call("GET", "/api/settings", { token: owner });
r = await call("PUT", "/api/settings", { token: owner, body: { rev: r.data.rev, settings: oldShape } });
check("an older app without the switch saves as on", r.status === 200 && r.data.settings.powerOn === true, r);
r = await call("GET", "/api/settings", { token: owner });
check("snooze is 9 minutes by default", r.data.settings.snoozeMinutes === 9, r.data);
cur = r.data;
r = await call("PUT", "/api/settings", { token: owner, body: { rev: cur.rev, settings: { ...cur.settings, snoozeMinutes: 45 } } });
check("snooze longer than 30 minutes is refused", r.status === 400, r);
r = await call("PUT", "/api/settings", { token: owner, body: { rev: cur.rev, settings: { ...cur.settings, snoozeMinutes: 5 } } });
check("snooze can be set to 5 minutes", r.status === 200 && r.data.settings.snoozeMinutes === 5, r);
r = await call("GET", "/api/me", { token: owner });
check("no bond shown before Dewy reports one", r.data.dewy.bond === null && r.data.dewy.powerOn === true, r.data);
r = await call("GET", "/api/device/sync?rev=0&bond=420", { token: dev });
r = await call("GET", "/api/me", { token: owner });
check("Dewy's bond reaches the app", r.data.dewy.bond === 0.42, r.data);
r = await call("GET", "/api/device/sync?rev=0&bond=5000", { token: dev });
r = await call("GET", "/api/device/sync?rev=0&bond=-3", { token: dev });
r = await call("GET", "/api/me", { token: owner });
check("bad bond numbers are ignored", r.data.dewy.bond === 0.42, r.data);
r = await call("POST", "/api/links", { token: owner, body: { label: "Abijit" } });
r = await call("GET", "/api/me", { token: r.data.url.split("#")[1] });
check("a notes link doesn't see the bond", r.status === 200 && r.data.dewy.bond === null, r.data);

// --- misc
r = await call("GET", "/api/nothing");
check("unknown API path is a 404", r.status === 404, r);
r = await fetch(BASE + "/");
check("the phone app is served", r.status === 200 && (r.headers.get("content-type") || "").includes("text/html"));

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
