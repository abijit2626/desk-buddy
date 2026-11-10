// Small helpers shared by the server's files.

export class HttpError extends Error {
  constructor(public status: number, message: string) { super(message); }
}

export const now = () => Math.floor(Date.now() / 1000);
export const today = () => new Date().toISOString().slice(0, 10);

// Adds to one of the daily counters used for limits (notes sent, chats, tokens, spoken characters).
export async function bumpUsage(env: { DB: D1Database }, deviceId: string, kind: string, by = 1): Promise<number> {
  const row = await env.DB.prepare(
    "INSERT INTO usage (device_id, day, kind, count) VALUES (?1, ?2, ?3, ?4) " +
    "ON CONFLICT(device_id, day, kind) DO UPDATE SET count = count + ?4 RETURNING count"
  ).bind(deviceId, today(), kind, by).first<{ count: number }>();
  return row?.count ?? by;
}

export async function readUsage(env: { DB: D1Database }, deviceId: string, kind: string): Promise<number> {
  const row = await env.DB.prepare("SELECT count FROM usage WHERE device_id = ?1 AND day = ?2 AND kind = ?3")
    .bind(deviceId, today(), kind).first<{ count: number }>();
  return row?.count ?? 0;
}

// Something for Dewy to do on its next check-in.
export async function queueEvent(env: { DB: D1Database }, deviceId: string, kind: string, body: Record<string, unknown>): Promise<number> {
  const r = await env.DB.prepare("INSERT INTO events (device_id, kind, body, created_at) VALUES (?1, ?2, ?3, ?4) RETURNING id")
    .bind(deviceId, kind, JSON.stringify(body), now()).first<{ id: number }>();
  return r!.id;
}
