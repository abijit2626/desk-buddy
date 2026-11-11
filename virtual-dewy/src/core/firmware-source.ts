// Reading Dewy's firmware source as text: which files, how many lines, which #includes, which pins.
// Pure functions (no DOM, no file access) so the page, the dev server and the tests can all share them.

export interface SourceFile { name: string; text: string }

/** Files that must never be read, loaded or shown. secrets.h holds the real Wi-Fi password and server keys. */
export const isSecretFileName = (name: string) => /^secrets\.h$/i.test(name) || /\.secret$/i.test(name) || /^\.env/i.test(name);
/** Only plain firmware file names: no folders, no dots-and-slashes tricks. */
export const isSafeFileName = (name: string) => /^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}\.(ino|h)$/.test(name) && !isSecretFileName(name);

export function lineCount(text: string): number {
  if (text.length === 0) return 0;
  return text.split("\n").length - (text.endsWith("\n") ? 1 : 0);
}

const stripComments = (s: string) => s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");

export interface Includes { quoted: string[]; angled: string[] }
export function parseIncludes(text: string): Includes {
  const quoted: string[] = [], angled: string[] = [];
  for (const m of stripComments(text).matchAll(/^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>)/gm)) {
    if (m[1]) quoted.push(m[1]); else if (m[2]) angled.push(m[2]);
  }
  return { quoted, angled };
}

export interface FileReport {
  name: string; lines: number;
  /** how the file ends up in the build */
  role: "sketch" | "included" | "unused";
  /** files that #include it */
  includedBy: string[];
  note?: string;
}

/**
 * Follows the sketch's #include "..." lines through the files you have. `secrets.h` is only ever mentioned, never
 * loaded: config.h falls back to secrets.example.h when it is missing, so the example file counts as included.
 */
export function analyseSources(files: SourceFile[], entry = "dewy.ino") {
  const byName = new Map(files.map((f) => [f.name, f]));
  const includedBy = new Map<string, Set<string>>();
  const order: string[] = [];
  const missing = new Set<string>();
  const system = new Set<string>();
  const visit = (name: string) => {
    const f = byName.get(name);
    if (!f || order.includes(name)) return;
    order.push(name);
    const inc = parseIncludes(f.text);
    inc.angled.forEach((a) => system.add(a));
    for (const q of inc.quoted) {
      if (isSecretFileName(q)) continue;                       // never followed
      if (q.includes("/")) { system.add(q); continue; }          // a library header written with quotes, like "mbedtls/sha256.h"
      if (!byName.has(q)) { missing.add(q); continue; }
      (includedBy.get(q) ?? includedBy.set(q, new Set()).get(q)!).add(name);
      visit(q);
    }
  };
  visit(entry);
  const mentionsSecrets = files.some((f) => /__has_include\s*\(\s*"secrets\.h"\s*\)/.test(f.text) || /#\s*include\s*"secrets\.h"/.test(f.text));
  const reports: FileReport[] = files.map((f) => ({
    name: f.name, lines: lineCount(f.text),
    role: f.name === entry ? "sketch" : order.includes(f.name) ? "included" : "unused",
    includedBy: [...(includedBy.get(f.name) ?? [])].sort(),
    note: f.name === "secrets.example.h" && mentionsSecrets ? "stand-in for secrets.h (your real secrets.h is never read)" : undefined,
  }));
  reports.sort((a, b) => (a.role === "sketch" ? -1 : b.role === "sketch" ? 1 : a.name.localeCompare(b.name)));
  return { reports, order, missing: [...missing].sort(), system: [...system].sort(), totalLines: reports.reduce((n, r) => n + r.lines, 0) };
}

/** `#define NAME 123` lines with plain integer values, from every file given. */
export function parseIntDefines(files: SourceFile[]): Record<string, number> {
  const out: Record<string, number> = {};
  for (const f of files) {
    for (const m of stripComments(f.text).matchAll(/^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+(-?\d+)[ \t]*$/gm)) out[m[1]] = Number(m[2]);
  }
  return out;
}

export interface FirmwarePins {
  /** every `#define PIN_*` with an integer value, e.g. PIN_TFT_MOSI -> 11 */
  defines: Record<string, number>;
  /** `cfg.pin_xxx = PIN_YYY;` lines in display.h, grouped by the driver config they belong to */
  display: { bus: Record<string, string>; panel: Record<string, string>; light: Record<string, string>; touch: Record<string, string> };
  /** Wire.begin(SDA, SCL, ...) in dewy.ino */
  wire: { sda: string | null; scl: string | null };
}

export function parseFirmwarePins(files: SourceFile[]): FirmwarePins {
  const defines = Object.fromEntries(Object.entries(parseIntDefines(files)).filter(([k]) => k.startsWith("PIN_")));
  const display: FirmwarePins["display"] = { bus: {}, panel: {}, light: {}, touch: {} };
  const dh = files.find((f) => f.name === "display.h");
  if (dh) {
    const text = stripComments(dh.text);
    // each block looks like:  auto cfg = _bus.config(); ... cfg.pin_sclk = PIN_TFT_SCLK; ... _bus.config(cfg);
    for (const m of text.matchAll(/auto\s+cfg\s*=\s*_(bus|panel|light|touch)\.config\(\);([\s\S]*?)_\1\.config\(cfg\);/g)) {
      const group = m[1] as keyof FirmwarePins["display"];
      for (const a of m[2].matchAll(/cfg\.(pin_\w+)\s*=\s*([A-Za-z_]\w*|-?\d+)\s*;/g)) display[group][a[1]] = a[2];
    }
  }
  const ino = files.find((f) => f.name === "dewy.ino");
  const w = ino && /Wire\.begin\(\s*([A-Za-z_]\w*|\d+)\s*,\s*([A-Za-z_]\w*|\d+)/.exec(stripComments(ino.text));
  return { defines, display, wire: { sda: w ? w[1] : null, scl: w ? w[2] : null } };
}

/** Looks a name up in the defines (or reads a plain number). */
export function resolvePin(token: string | null | undefined, defines: Record<string, number>): number | null {
  if (token == null) return null;
  if (/^-?\d+$/.test(token)) return Number(token);
  return token in defines ? defines[token] : null;
}
