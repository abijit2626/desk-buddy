// Dewy cloud: a small Cloudflare Worker.
// - Serves the phone app (the files in ../app).
// - Keeps Dewy's settings, alarms and reminders, and queues notes and study-timer commands.
// - Dewy checks in every few seconds over HTTPS and picks up whatever changed.
// - Answers Dewy's spoken questions (chat.ts).
// - Offers new firmware so Dewy can update itself from anywhere.

import { DEFAULT_SETTINGS, Settings, validateSettings, cleanText } from "./settings";
import { ChatEnv, handleChat } from "./chat";
import { HttpError, now, bumpUsage, queueEvent } from "./util";

export interface Env extends ChatEnv {
  DB: D1Database;
  ASSETS: Fetcher;
  REGISTER_KEY?: string;   // secret: Dewy sends it to register, so strangers can't create devices
  ADMIN_KEY?: string;      // secret: needed to publish new firmware
  NOTES_PER_DAY?: string;  // limit on notes per Dewy per day
}

type Role = "owner" | "notes";
interface DeviceRow {
  id: string; token_hash: string; created_at: number; last_seen: number | null; fw_version: string | null;
  pair_code: string | null; pair_expires: number | null; settings: string; settings_rev: number;
}
interface TokenRow { id: string; device_id: string; role: Role; label: string }

const PAIR_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";   // 32 characters, no 0/O/1/I
const PAIR_SECONDS = 600;
const ONLINE_SECONDS = 120;
const MAX_BODY = 8192;

// ---------------------------------------------------------------- small helpers
function json(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store", "x-content-type-options": "nosniff" },
  });
}

function base64url(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
function randomToken(bytes = 32): string {
  const b = new Uint8Array(bytes);
  crypto.getRandomValues(b);
  return base64url(b);
}
function pairCode(): string {
  const b = new Uint8Array(8);
  crypto.getRandomValues(b);
  return [...b].map((x) => PAIR_ALPHABET[x & 31]).join("");
}
async function sha256hex(text: string): Promise<string> {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}
function sameSecret(a: string, b: string): boolean {
  const x = new TextEncoder().encode(a), y = new TextEncoder().encode(b);
  if (x.length !== y.length) return false;
  return crypto.subtle.timingSafeEqual(x, y);
}

async function readJson(req: Request): Promise<Record<string, unknown>> {
  const text = await req.text();
  if (text.length > MAX_BODY) throw new HttpError(413, "request too large");
  if (!text) return {};
  let v: unknown;
  try { v = JSON.parse(text); } catch { throw new HttpError(400, "not valid JSON"); }
  if (typeof v !== "object" || v === null || Array.isArray(v)) throw new HttpError(400, "expected a JSON object");
  return v as Record<string, unknown>;
}

function bearer(req: Request): string {
  const h = req.headers.get("authorization") || "";
  const m = /^Bearer ([A-Za-z0-9_-]{20,100})$/.exec(h);
  if (!m) throw new HttpError(401, "sign in first");
  return m[1];
}

// newer("1.2.0", "1.1.9") === true
function newer(a: string, b: string | null): boolean {
  if (!b) return true;
  const pa = a.split(".").map(Number), pb = b.split(".").map(Number);
  for (let i = 0; i < 3; i++) { if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) > (pb[i] || 0); }
  return false;
}

// ---------------------------------------------------------------- who is calling
async function deviceAuth(req: Request, env: Env): Promise<DeviceRow> {
  const hash = await sha256hex(bearer(req));
  const d = await env.DB.prepare("SELECT * FROM devices WHERE token_hash = ?1").bind(hash).first<DeviceRow>();
  if (!d) throw new HttpError(401, "unknown device");
  return d;
}
async function appAuth(req: Request, env: Env, roles: Role[]): Promise<TokenRow> {
  const hash = await sha256hex(bearer(req));
  const t = await env.DB.prepare("SELECT id, device_id, role, label FROM app_tokens WHERE token_hash = ?1").bind(hash).first<TokenRow>();
  if (!t) throw new HttpError(401, "this phone isn't paired any more");
  if (!roles.includes(t.role)) throw new HttpError(403, "this link can only send notes");
  await env.DB.prepare("UPDATE app_tokens SET last_used = ?1 WHERE id = ?2").bind(now(), t.id).run();
  return t;
}
async function loadDevice(env: Env, id: string): Promise<DeviceRow> {
  const d = await env.DB.prepare("SELECT * FROM devices WHERE id = ?1").bind(id).first<DeviceRow>();
  if (!d) throw new HttpError(404, "Dewy not found");
  return d;
}
async function addToken(env: Env, deviceId: string, role: Role, label: string): Promise<{ id: string; token: string }> {
  const token = randomToken(), id = randomToken(9);
  await env.DB.prepare("INSERT INTO app_tokens (id, token_hash, device_id, role, label, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)")
    .bind(id, await sha256hex(token), deviceId, role, label, now()).run();
  return { id, token };
}
async function newPairCode(env: Env, deviceId: string): Promise<string> {
  for (let i = 0; i < 3; i++) {
    const code = pairCode();
    try {
      await env.DB.prepare("UPDATE devices SET pair_code = ?1, pair_expires = ?2 WHERE id = ?3").bind(code, now() + PAIR_SECONDS, deviceId).run();
      return code;
    } catch { /* the same code is in use by another Dewy right now: try again */ }
  }
  throw new HttpError(503, "try again");
}
// ---------------------------------------------------------------- Dewy's own calls
async function deviceRegister(req: Request, env: Env): Promise<Response> {
  if (!env.REGISTER_KEY) throw new HttpError(503, "server not set up: REGISTER_KEY is missing");
  if (!sameSecret(req.headers.get("x-register-key") || "", env.REGISTER_KEY)) throw new HttpError(403, "wrong register key");
  const body = await readJson(req);
  const fw = typeof body.fw === "string" && /^\d+\.\d+\.\d+$/.test(body.fw) ? body.fw : null;
  const id = randomToken(9), token = randomToken();
  await env.DB.prepare("INSERT INTO devices (id, token_hash, created_at, last_seen, fw_version, settings) VALUES (?1, ?2, ?3, ?3, ?4, ?5)")
    .bind(id, await sha256hex(token), now(), fw, JSON.stringify(DEFAULT_SETTINGS)).run();
  return json({ deviceId: id, token }, 201);
}

async function devicePair(req: Request, env: Env): Promise<Response> {
  const d = await deviceAuth(req, env);
  return json({ code: await newPairCode(env, d.id), expiresIn: PAIR_SECONDS });
}

async function deviceSync(req: Request, env: Env, url: URL): Promise<Response> {
  const d = await deviceAuth(req, env);
  const rev = Number(url.searchParams.get("rev") || 0);
  const ack = Number(url.searchParams.get("ack") || 0);
  const fwHeader = req.headers.get("x-dewy-version") || "";
  const fw = /^\d+\.\d+\.\d+$/.test(fwHeader) ? fwHeader : d.fw_version;
  const t = now();

  const stmts = [env.DB.prepare("UPDATE devices SET last_seen = ?1, fw_version = ?2 WHERE id = ?3").bind(t, fw, d.id)];
  if (Number.isInteger(ack) && ack > 0)
    stmts.push(env.DB.prepare("UPDATE events SET delivered_at = ?1 WHERE device_id = ?2 AND id <= ?3 AND delivered_at IS NULL").bind(t, d.id, ack));
  await env.DB.batch(stmts);

  const [events, owners, firmware] = await Promise.all([
    env.DB.prepare("SELECT id, kind, body FROM events WHERE device_id = ?1 AND delivered_at IS NULL ORDER BY id LIMIT 10").bind(d.id).all<{ id: number; kind: string; body: string }>(),
    env.DB.prepare("SELECT COUNT(*) AS n FROM app_tokens WHERE device_id = ?1 AND role = 'owner'").bind(d.id).first<{ n: number }>(),
    env.DB.prepare("SELECT version, url, sha256, size FROM firmware WHERE id = 1").first<{ version: string; url: string; sha256: string; size: number }>(),
  ]);

  const out: Record<string, unknown> = {
    now: t,
    rev: d.settings_rev,
    paired: (owners?.n ?? 0) > 0,
    events: events.results.map((e) => ({ id: e.id, kind: e.kind, ...JSON.parse(e.body) })),
  };
  if (rev !== d.settings_rev) out.settings = JSON.parse(d.settings);
  if (d.pair_code && (d.pair_expires ?? 0) > t) out.pairCode = d.pair_code;
  if (firmware && newer(firmware.version, fw)) out.firmware = firmware;
  return json(out);
}

// Dewy asks a question out loud (or sends text): it gets the spoken answer back. See chat.ts.
async function deviceChat(req: Request, env: Env): Promise<Response> {
  const d = await deviceAuth(req, env);
  return handleChat(req, env, d);
}

// ---------------------------------------------------------------- the phone app's calls
async function appPair(req: Request, env: Env): Promise<Response> {
  const body = await readJson(req);
  const code = typeof body.code === "string" ? body.code.toUpperCase().replace(/[\s-]/g, "") : "";
  const label = cleanText(body.label ?? "iPhone", 24) || "iPhone";
  if (!/^[2-9A-HJ-NP-Z]{8}$/.test(code)) throw new HttpError(400, "The code is 8 letters and numbers. Check Dewy's screen.");
  const d = await env.DB.prepare("SELECT id FROM devices WHERE pair_code = ?1 AND pair_expires > ?2").bind(code, now()).first<{ id: string }>();
  if (!d) throw new HttpError(400, "That code didn't work. It may have run out: get a new one and try again.");
  await env.DB.prepare("UPDATE devices SET pair_code = NULL, pair_expires = NULL WHERE id = ?1").bind(d.id).run();
  const { id, token } = await addToken(env, d.id, "owner", label);
  const dev = await loadDevice(env, d.id);
  return json({ token, id, role: "owner", name: (JSON.parse(dev.settings) as Settings).name }, 201);
}

async function appMe(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner", "notes"]);
  const d = await loadDevice(env, t.device_id);
  const s = JSON.parse(d.settings) as Settings;
  return json({
    role: t.role, label: t.label, id: t.id,
    dewy: { name: s.name, online: (d.last_seen ?? 0) > now() - ONLINE_SECONDS, lastSeen: d.last_seen, firmware: d.fw_version },
  });
}

async function getSettings(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner"]);
  const d = await loadDevice(env, t.device_id);
  return json({ rev: d.settings_rev, settings: JSON.parse(d.settings) });
}

async function putSettings(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner"]);
  const body = await readJson(req);
  const v = validateSettings(body.settings);
  if (!v.ok) throw new HttpError(400, v.error);
  const d = await loadDevice(env, t.device_id);
  if (typeof body.rev === "number" && body.rev !== d.settings_rev)
    return json({ error: "Changed on another phone. Here are the latest settings.", rev: d.settings_rev, settings: JSON.parse(d.settings) }, 409);
  const r = await env.DB.prepare("UPDATE devices SET settings = ?1, settings_rev = settings_rev + 1 WHERE id = ?2 RETURNING settings_rev")
    .bind(JSON.stringify(v.value), d.id).first<{ settings_rev: number }>();
  return json({ rev: r!.settings_rev, settings: v.value });
}

async function startTimer(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner"]);
  const body = await readJson(req);
  const minutes = body.minutes;
  if (typeof minutes !== "number" || !Number.isInteger(minutes) || minutes < 1 || minutes > 180) throw new HttpError(400, "minutes: 1-180");
  await queueEvent(env, t.device_id, "timer_start", { minutes });
  return json({ ok: true, minutes, endsAt: now() + minutes * 60 });
}
async function stopTimer(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner"]);
  await queueEvent(env, t.device_id, "timer_stop", {});
  return json({ ok: true });
}

async function sendNote(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner", "notes"]);
  const body = await readJson(req);
  const text = cleanText(body.text, 120);
  if (!text) throw new HttpError(400, "Write a note of up to 120 characters.");
  const limit = Number(env.NOTES_PER_DAY || 30);
  if ((await bumpUsage(env, t.device_id, "note")) > limit) throw new HttpError(429, `That's ${limit} notes today. Dewy will be ready for more tomorrow.`);
  const id = await queueEvent(env, t.device_id, "note", { text, from: t.role === "notes" ? t.label : "you", by: t.id });
  return json({ ok: true, id }, 201);
}
async function listNotes(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner", "notes"]);
  const rows = await env.DB.prepare("SELECT id, body, created_at, delivered_at FROM events WHERE device_id = ?1 AND kind = 'note' ORDER BY id DESC LIMIT 40")
    .bind(t.device_id).all<{ id: number; body: string; created_at: number; delivered_at: number | null }>();
  const notes = rows.results
    .map((r) => ({ id: r.id, ...(JSON.parse(r.body) as { text: string; from: string; by: string }), sentAt: r.created_at, deliveredAt: r.delivered_at }))
    .filter((n) => t.role === "owner" || n.by === t.id)
    .slice(0, 20)
    .map(({ by, ...n }) => ({ ...n, mine: by === t.id }));
  return json({ notes });
}

// note links (for Abijit) and paired phones
async function createLink(req: Request, env: Env, url: URL): Promise<Response> {
  const t = await appAuth(req, env, ["owner"]);
  const body = await readJson(req);
  const label = cleanText(body.label, 20);
  if (!label) throw new HttpError(400, "Whose name goes on the notes? 1-20 characters.");
  const count = await env.DB.prepare("SELECT COUNT(*) AS n FROM app_tokens WHERE device_id = ?1 AND role = 'notes'").bind(t.device_id).first<{ n: number }>();
  if ((count?.n ?? 0) >= 10) throw new HttpError(400, "That's 10 note links already. Remove one first.");
  const { id, token } = await addToken(env, t.device_id, "notes", label);
  return json({ id, label, url: `${url.origin}/notes#${token}` }, 201);
}
async function listAccess(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner"]);
  const rows = await env.DB.prepare("SELECT id, role, label, created_at, last_used FROM app_tokens WHERE device_id = ?1 ORDER BY created_at")
    .bind(t.device_id).all<{ id: string; role: Role; label: string; created_at: number; last_used: number | null }>();
  return json({ access: rows.results.map((r) => ({ ...r, you: r.id === t.id })) });
}
async function removeAccess(req: Request, env: Env, id: string): Promise<Response> {
  const t = await appAuth(req, env, ["owner", "notes"]);
  if (t.role === "notes" && id !== t.id) throw new HttpError(403, "this link can only remove itself");
  const r = await env.DB.prepare("DELETE FROM app_tokens WHERE id = ?1 AND device_id = ?2").bind(id, t.device_id).run();
  if (!r.meta.changes) throw new HttpError(404, "not found");
  return json({ ok: true });
}
async function invite(req: Request, env: Env): Promise<Response> {
  const t = await appAuth(req, env, ["owner"]);
  return json({ code: await newPairCode(env, t.device_id), expiresIn: PAIR_SECONDS });
}

// ---------------------------------------------------------------- publishing firmware
async function publishFirmware(req: Request, env: Env): Promise<Response> {
  if (!env.ADMIN_KEY || !sameSecret(bearer(req), env.ADMIN_KEY)) throw new HttpError(403, "admin only");
  const b = await readJson(req);
  const version = typeof b.version === "string" && /^\d+\.\d+\.\d+$/.test(b.version) ? b.version : null;
  const url = typeof b.url === "string" && /^https:\/\/[^\s]{8,400}$/.test(b.url) ? b.url : null;
  const sha256 = typeof b.sha256 === "string" && /^[0-9a-f]{64}$/.test(b.sha256) ? b.sha256 : null;
  const size = typeof b.size === "number" && Number.isInteger(b.size) && b.size > 0 && b.size < 3_200_000 ? b.size : null;
  if (!version || !url || !sha256 || !size) throw new HttpError(400, "need version x.y.z, https url, sha256 (hex) and size");
  await env.DB.prepare(
    "INSERT INTO firmware (id, version, url, sha256, size, updated_at) VALUES (1, ?1, ?2, ?3, ?4, ?5) " +
    "ON CONFLICT(id) DO UPDATE SET version = ?1, url = ?2, sha256 = ?3, size = ?4, updated_at = ?5"
  ).bind(version, url, sha256, size, now()).run();
  return json({ ok: true, version });
}

// ---------------------------------------------------------------- routing
async function route(req: Request, env: Env): Promise<Response> {
  const url = new URL(req.url);
  const p = url.pathname, m = req.method;

  if (p === "/api/device/register" && m === "POST") return deviceRegister(req, env);
  if (p === "/api/device/pair" && m === "POST") return devicePair(req, env);
  if (p === "/api/device/sync" && m === "GET") return deviceSync(req, env, url);
  if (p === "/api/device/chat" && m === "POST") return deviceChat(req, env);

  if (p === "/api/pair" && m === "POST") return appPair(req, env);
  if (p === "/api/me" && m === "GET") return appMe(req, env);
  if (p === "/api/settings" && m === "GET") return getSettings(req, env);
  if (p === "/api/settings" && m === "PUT") return putSettings(req, env);
  if (p === "/api/timer" && m === "POST") return startTimer(req, env);
  if (p === "/api/timer/stop" && m === "POST") return stopTimer(req, env);
  if (p === "/api/notes" && m === "POST") return sendNote(req, env);
  if (p === "/api/notes" && m === "GET") return listNotes(req, env);
  if (p === "/api/links" && m === "POST") return createLink(req, env, url);
  if (p === "/api/access" && m === "GET") return listAccess(req, env);
  if (p.startsWith("/api/access/") && m === "DELETE") return removeAccess(req, env, decodeURIComponent(p.slice(12)));
  if (p === "/api/invite" && m === "POST") return invite(req, env);
  if (p === "/api/admin/firmware" && m === "PUT") return publishFirmware(req, env);

  if (p.startsWith("/api/")) throw new HttpError(404, "no such API");
  return env.ASSETS.fetch(req);
}

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    try {
      return await route(req, env);
    } catch (e) {
      if (e instanceof HttpError) return json({ error: e.message }, e.status);
      console.error(e);
      return json({ error: "Something went wrong on the server." }, 500);
    }
  },

  // nightly tidy-up: old delivered events, old counters, expired pairing codes
  async scheduled(_c: ScheduledController, env: Env): Promise<void> {
    const t = now();
    await env.DB.batch([
      env.DB.prepare("DELETE FROM events WHERE delivered_at IS NOT NULL AND delivered_at < ?1").bind(t - 30 * 86400),
      env.DB.prepare("DELETE FROM events WHERE delivered_at IS NULL AND created_at < ?1").bind(t - 60 * 86400),
      env.DB.prepare("DELETE FROM chat_turns WHERE created_at < ?1").bind(t - 86400),
      env.DB.prepare("DELETE FROM usage WHERE day < ?1").bind(new Date((t - 60 * 86400) * 1000).toISOString().slice(0, 10)),
      env.DB.prepare("UPDATE devices SET pair_code = NULL, pair_expires = NULL WHERE pair_expires < ?1").bind(t),
    ]);
  },
} satisfies ExportedHandler<Env>;
