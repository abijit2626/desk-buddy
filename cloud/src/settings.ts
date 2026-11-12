// Dewy's settings, as the phone app edits them and the firmware reads them.
// Everything coming from a phone is checked here before it is saved.

export interface Alarm {
  id: string;
  time: string;        // "HH:MM", 24-hour
  days: number[];      // 0 = Sunday ... 6 = Saturday; empty = every day
  label: string;
  on: boolean;
}

export interface Settings {
  name: string;
  timezone: string;    // POSIX TZ string the ESP32 understands, e.g. "IST-5:30"
  city: { name: string; lat: number; lon: number } | null;
  alarms: Alarm[];
  reminders: { water: number; stretch: number; from: number; to: number };
  studyMinutes: number;
  birthday: { month: number; day: number } | null;
  giftDate: { year: number; month: number; day: number } | null;
  powerOn: boolean;    // false = switched off in the app: Dewy rests (dark, quiet) until switched on
  snoozeMinutes: number;  // how long a snoozed alarm waits before ringing again
}

export const DEFAULT_SETTINGS: Settings = {
  name: "Dewy",
  timezone: "IST-5:30",
  city: { name: "Palakkad", lat: 10.7867, lon: 76.6548 },
  alarms: [],
  reminders: { water: 90, stretch: 120, from: 9, to: 21 },
  studyMinutes: 25,
  birthday: null,
  giftDate: null,
  powerOn: true,
  snoozeMinutes: 9,
};

export const LIMITS = { alarms: 10, name: 20, label: 24, city: 40 };

type Result<T> = { ok: true; value: T } | { ok: false; error: string };

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const isInt = (v: unknown, lo: number, hi: number): v is number => typeof v === "number" && Number.isInteger(v) && v >= lo && v <= hi;

// Printable text without control characters, trimmed, within a length limit.
export function cleanText(v: unknown, max: number, allowEmpty = false): string | null {
  if (typeof v !== "string") return null;
  const s = v.replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim();
  if (!allowEmpty && s.length === 0) return null;
  if ([...s].length > max) return null;
  return s;
}

function daysInMonth(month: number): number {
  return [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][month - 1];
}

export function validateSettings(input: unknown): Result<Settings> {
  if (!isObj(input)) return { ok: false, error: "settings must be an object" };
  const s = input;

  const name = cleanText(s.name, LIMITS.name);
  if (!name) return { ok: false, error: `name: 1-${LIMITS.name} characters` };

  if (typeof s.timezone !== "string" || s.timezone.length > 60 || !/^[A-Za-z0-9<>+\-,.:/]+$/.test(s.timezone))
    return { ok: false, error: "timezone: not a valid POSIX time zone" };

  let city: Settings["city"] = null;
  if (s.city !== null && s.city !== undefined) {
    if (!isObj(s.city)) return { ok: false, error: "city must be an object or null" };
    const cname = cleanText(s.city.name, LIMITS.city);
    const lat = s.city.lat, lon = s.city.lon;
    if (!cname || typeof lat !== "number" || typeof lon !== "number" || Math.abs(lat) > 90 || Math.abs(lon) > 180)
      return { ok: false, error: "city: name, lat (-90..90) and lon (-180..180)" };
    city = { name: cname, lat: Math.round(lat * 1e4) / 1e4, lon: Math.round(lon * 1e4) / 1e4 };
  }

  if (!Array.isArray(s.alarms) || s.alarms.length > LIMITS.alarms) return { ok: false, error: `alarms: up to ${LIMITS.alarms}` };
  const alarms: Alarm[] = [];
  for (const a of s.alarms) {
    if (!isObj(a)) return { ok: false, error: "alarm must be an object" };
    const id = typeof a.id === "string" && /^[a-z0-9]{1,16}$/.test(a.id) ? a.id : null;
    const time = typeof a.time === "string" && /^([01]\d|2[0-3]):[0-5]\d$/.test(a.time) ? a.time : null;
    const label = cleanText(a.label ?? "", LIMITS.label, true);
    if (!id || !time || label === null || typeof a.on !== "boolean" || !Array.isArray(a.days) || !a.days.every((d) => isInt(d, 0, 6)))
      return { ok: false, error: "alarm: id, time HH:MM, days 0-6, label, on" };
    alarms.push({ id, time, days: [...new Set(a.days as number[])].sort(), label, on: a.on });
  }

  const r = s.reminders;
  if (!isObj(r) || !isInt(r.water, 0, 480) || !isInt(r.stretch, 0, 480) || !isInt(r.from, 0, 23) || !isInt(r.to, 1, 24) || r.from >= r.to)
    return { ok: false, error: "reminders: water and stretch 0-480 minutes, from < to hours" };

  if (!isInt(s.studyMinutes, 1, 180)) return { ok: false, error: "studyMinutes: 1-180" };

  let birthday: Settings["birthday"] = null;
  if (s.birthday !== null && s.birthday !== undefined) {
    const b = s.birthday;
    if (!isObj(b) || !isInt(b.month, 1, 12) || !isInt(b.day, 1, daysInMonth(b.month as number)))
      return { ok: false, error: "birthday: month 1-12 and a real day" };
    birthday = { month: b.month as number, day: b.day as number };
  }

  let giftDate: Settings["giftDate"] = null;
  if (s.giftDate !== null && s.giftDate !== undefined) {
    const g = s.giftDate;
    if (!isObj(g) || !isInt(g.year, 2024, 2100) || !isInt(g.month, 1, 12) || !isInt(g.day, 1, daysInMonth(g.month as number)))
      return { ok: false, error: "giftDate: year, month and a real day" };
    giftDate = { year: g.year as number, month: g.month as number, day: g.day as number };
  }

  // older saved settings (and older apps) don't have the switch: that means "on"
  if (s.powerOn !== undefined && typeof s.powerOn !== "boolean") return { ok: false, error: "powerOn: true or false" };
  const powerOn = s.powerOn !== false;
  if (s.snoozeMinutes !== undefined && !isInt(s.snoozeMinutes, 1, 30)) return { ok: false, error: "snoozeMinutes: 1-30" };
  const snoozeMinutes = s.snoozeMinutes === undefined ? 9 : (s.snoozeMinutes as number);

  return {
    ok: true,
    value: {
      name, timezone: s.timezone, city, alarms,
      reminders: { water: r.water as number, stretch: r.stretch as number, from: r.from as number, to: r.to as number },
      studyMinutes: s.studyMinutes as number, birthday, giftDate, powerOn, snoozeMinutes,
    },
  };
}
