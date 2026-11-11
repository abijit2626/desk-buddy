// Reads Dewy's firmware source files for the code panel. Read-only, and deliberately narrow:
//   * only plain *.ino and *.h files that sit directly in the firmware folder,
//   * never secrets.h (or *.secret, .env*): it holds the real Wi-Fi password and server keys, so it is not even opened,
//   * no folders, no "..", no symlinks, nothing over 2 MB.
import { lstatSync, readFileSync, readdirSync } from "node:fs";
import { join, resolve } from "node:path";
import { isSafeFileName, isSecretFileName, lineCount } from "../src/core/firmware-source.ts";

export const MAX_FILE_BYTES = 2 * 1024 * 1024;

export interface FirmwareListing {
  /** the folder's own name (never the full path of the machine) */
  folder: string;
  files: { name: string; bytes: number; lines: number }[];
  /** a secrets.h exists in the folder: reported by name only, never read */
  secretsPresent: boolean;
  /** names skipped and why, so the page can say so */
  skipped: { name: string; why: string }[];
}

export function listFirmware(dir: string): FirmwareListing {
  const root = resolve(dir);
  const files: FirmwareListing["files"] = [];
  const skipped: FirmwareListing["skipped"] = [];
  let secretsPresent = false;
  for (const entry of readdirSync(root, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const name = entry.name;
    if (isSecretFileName(name)) { secretsPresent = secretsPresent || /^secrets\.h$/i.test(name); skipped.push({ name, why: "private settings: never read" }); continue; }
    if (!/\.(ino|h)$/i.test(name)) continue;
    if (!isSafeFileName(name)) { skipped.push({ name, why: "unusual file name" }); continue; }
    const st = lstatSafe(join(root, name));
    if (!st || !st.isFile()) { skipped.push({ name, why: "not a regular file" }); continue; }
    if (st.size > MAX_FILE_BYTES) { skipped.push({ name, why: "too large" }); continue; }
    files.push({ name, bytes: st.size, lines: lineCount(readFileSync(join(root, name), "utf8")) });
  }
  return { folder: root.split(/[\\/]/).filter(Boolean).pop() ?? "firmware", files, secretsPresent, skipped };
}

/** Returns the file's text, or null if the name is not one of the listed, safe, regular files. */
export function readFirmwareFile(dir: string, name: string): string | null {
  if (!isSafeFileName(name)) return null;
  const root = resolve(dir);
  // only names that really are in the folder listing are read: the request's text is never turned into a path by itself
  const real = readdirSync(root, { withFileTypes: true }).find((e) => e.name === name);
  if (!real) return null;
  const path = join(root, real.name);
  const st = lstatSafe(path);
  if (!st || !st.isFile() || st.size > MAX_FILE_BYTES) return null;
  return readFileSync(path, "utf8");
}

function lstatSafe(path: string) { try { return lstatSync(path); } catch { return null; } }
