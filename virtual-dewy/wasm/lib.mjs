// Helpers shared by build.mjs and the tests. Plain JavaScript so Node can run it without a build step.
import { createHash } from "node:crypto";
import { copyFileSync, mkdirSync, readdirSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";

// Files that must never be read, copied or shown. secrets.h holds the real Wi-Fi password and server keys.
export const isSecretFile = (name) => /^secrets\.h$/i.test(name) || /\.secret$/i.test(name) || /^\.env/i.test(name);

// Firmware source files the virtual build uses: the sketch and its headers.
export const isFirmwareSource = (name) => /\.(ino|h)$/i.test(name) && !isSecretFile(name);

// Copies the sketch and headers (never secrets.h) into destDir. Returns the file names copied, sorted.
// Only names are inspected for the denied files, so a real secrets.h is never even opened.
export function copyFirmwareSources(srcDir, destDir) {
  rmSync(destDir, { recursive: true, force: true });
  mkdirSync(destDir, { recursive: true });
  const names = readdirSync(srcDir, { withFileTypes: true }).filter((e) => e.isFile() && isFirmwareSource(e.name)).map((e) => e.name).sort();
  for (const n of names) copyFileSync(join(srcDir, n), join(destDir, n));
  return names;
}

export const sha256 = (bufOrString) => createHash("sha256").update(bufOrString).digest("hex");
export const countLines = (text) => (text.length === 0 ? 0 : text.split("\n").length - (text.endsWith("\n") ? 1 : 0));

// Describes the files a build used: name, line count, SHA-256, plus one hash over all of them (in name order).
export function describeSources(dir, names) {
  const files = names.map((name) => {
    const text = readFileSync(join(dir, name), "utf8");
    return { name, lines: countLines(text), sha256: sha256(text) };
  });
  return { files, combinedSha256: sha256(files.map((f) => `${f.name}:${f.sha256}`).join("\n")) };
}
