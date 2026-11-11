import { mkdtempSync, mkdirSync, readFileSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import { listFirmware, readFirmwareFile, MAX_FILE_BYTES } from "../server/firmware-files";
import { firmwareHandler, isLocalRequest } from "../server/firmware-plugin";
import { analyseSources, isSafeFileName, isSecretFileName, lineCount, parseIncludes } from "../src/core/firmware-source";
import { FIRMWARE_DIR } from "./helpers";

const SECRET = "#define WIFI_PASSWORD \"hunter2-real-password\"\n";
const root = mkdtempSync(join(tmpdir(), "dewy-fw-"));
const outside = mkdtempSync(join(tmpdir(), "dewy-outside-"));
afterAll(() => { rmSync(root, { recursive: true, force: true }); rmSync(outside, { recursive: true, force: true }); });

writeFileSync(join(root, "dewy.ino"), '#include "config.h"\nvoid setup() {}\nvoid loop() {}\n');
writeFileSync(join(root, "config.h"), '#if __has_include("secrets.h")\n#include "secrets.h"\n#else\n#include "secrets.example.h"\n#endif\n');
writeFileSync(join(root, "secrets.example.h"), "#define WIFI_SSID \"\"\n");
writeFileSync(join(root, "secrets.h"), SECRET);
writeFileSync(join(root, "notes.txt"), "not firmware");
writeFileSync(join(root, "huge.h"), "x".repeat(MAX_FILE_BYTES + 1));
writeFileSync(join(outside, "stolen.h"), "OUTSIDE FILE");
symlinkSync(join(outside, "stolen.h"), join(root, "link.h"));
mkdirSync(join(root, "sub.h"));

describe("the firmware loader never reads secrets.h or anything outside the folder", () => {
  it("lists only plain firmware files, with line counts, and reports secrets.h by name only", () => {
    const l = listFirmware(root);
    expect(l.files.map((f) => f.name)).toEqual(["config.h", "dewy.ino", "secrets.example.h"]);
    expect(l.files.find((f) => f.name === "dewy.ino")!.lines).toBe(3);
    expect(l.secretsPresent).toBe(true);
    expect(JSON.stringify(l)).not.toContain("hunter2");
    expect(l.folder).not.toContain("/");
    expect(l.skipped.map((s) => s.name).sort()).toEqual(["huge.h", "link.h", "secrets.h", "sub.h"]);
  });

  it("refuses to read secrets.h in any spelling", () => {
    for (const n of ["secrets.h", "SECRETS.H", "Secrets.h", "wifi.secret", ".env", ".env.local"]) expect(readFirmwareFile(root, n), n).toBeNull();
  });

  it("refuses traversal, absolute paths, folders, symlinks, huge and non-firmware files", () => {
    for (const n of ["../secrets.h", "..%2Fsecrets.h", "../../etc/passwd", "/etc/passwd", "sub/x.h", "sub.h", "link.h", "huge.h", "notes.txt", "", "a\0.h", "dewy.ino/", "CON:.h"]) {
      expect(readFirmwareFile(root, n), JSON.stringify(n)).toBeNull();
    }
  });

  it("reads the real files", () => {
    expect(readFirmwareFile(root, "dewy.ino")).toContain("void setup()");
    expect(readFirmwareFile(root, "secrets.example.h")).toContain("WIFI_SSID");
  });

  it("the file-name rules agree", () => {
    expect(isSafeFileName("config.h")).toBe(true);
    expect(isSafeFileName("secrets.h")).toBe(false);
    expect(isSafeFileName("../config.h")).toBe(false);
    expect(isSecretFileName("secrets.h")).toBe(true);
    expect(isSecretFileName("secrets.example.h")).toBe(false);
  });

  it("works on the real firmware folder: secrets.example.h yes, secrets.h never", () => {
    const l = listFirmware(FIRMWARE_DIR);
    const names = l.files.map((f) => f.name);
    expect(names).toEqual(expect.arrayContaining(["dewy.ino", "config.h", "face.h", "motion.h", "display.h", "cloud.h", "portal.h", "secrets.example.h"]));
    expect(names).not.toContain("secrets.h");
    expect(l.files.find((f) => f.name === "dewy.ino")!.lines).toBe(688);
  });
});

// a minimal fake of Node's request and response, to run the handler without opening a port
function call(handler: ReturnType<typeof firmwareHandler>, url: string, o: { method?: string; headers?: Record<string, string> } = {}) {
  const out: { status: number; headers: Record<string, string>; body: string; nexted: boolean } = { status: 0, headers: {}, body: "", nexted: false };
  const req = { url, method: o.method ?? "GET", headers: { host: "127.0.0.1:5173", ...(o.headers ?? {}) } } as never;
  const res = { statusCode: 0, setHeader: (k: string, v: string) => { out.headers[k.toLowerCase()] = v; }, end: (b: string) => { out.body = b; out.status = (res as { statusCode: number }).statusCode; } } as never;
  handler(req, res, () => { out.nexted = true; });
  return out;
}

describe("the /__firmware/ route", () => {
  const h = firmwareHandler(root);

  it("ignores other URLs", () => { expect(call(h, "/index.html").nexted).toBe(true); });

  it("lists and serves files with safe headers", () => {
    const list = call(h, "/__firmware/list");
    expect(list.status).toBe(200);
    expect(JSON.parse(list.body).files).toHaveLength(3);
    expect(list.headers["cache-control"]).toBe("no-store");
    expect(list.headers["x-content-type-options"]).toBe("nosniff");
    const file = call(h, "/__firmware/file/dewy.ino");
    expect(file.status).toBe(200);
    expect(file.headers["content-type"]).toBe("text/plain; charset=utf-8");
    expect(file.body).toContain("void loop()");
  });

  it("404s secrets.h and traversal attempts, however they are written", () => {
    for (const u of ["/__firmware/file/secrets.h", "/__firmware/file/..%2Fsecrets.h", "/__firmware/file/%2e%2e%2fsecrets.h", "/__firmware/file/../secrets.h", "/__firmware/file/sub/x.h", "/__firmware/file/%00.h", "/__firmware/file/link.h", "/__firmware/file/", "/__firmware/other"]) {
      const r = call(h, u);
      expect([400, 404], u).toContain(r.status);
      expect(r.body, u).not.toContain("hunter2");
      expect(r.body, u).not.toContain("OUTSIDE FILE");
    }
  });

  it("is read-only", () => { expect(call(h, "/__firmware/list", { method: "POST" }).status).toBe(405); expect(call(h, "/__firmware/file/dewy.ino", { method: "DELETE" }).status).toBe(405); });

  it("only answers the local page: foreign hosts and other sites' origins are refused", () => {
    expect(call(h, "/__firmware/list", { headers: { host: "evil.example" } }).status).toBe(403);
    expect(call(h, "/__firmware/list", { headers: { host: "127.0.0.1.evil.example" } }).status).toBe(403);
    expect(call(h, "/__firmware/list", { headers: { origin: "https://evil.example" } }).status).toBe(403);
    expect(call(h, "/__firmware/list", { headers: { origin: "http://127.0.0.1:5173" } }).status).toBe(200);
    expect(isLocalRequest({ headers: { host: "localhost:4173" } })).toBe(true);
    expect(isLocalRequest({ headers: { host: "[::1]:5173" } })).toBe(true);
    expect(isLocalRequest({ headers: {} })).toBe(false);
  });
});

describe("the dev server itself", () => {
  it("refuses to serve secrets.h, build folders and dependency folders", () => {
    const cfg = readFileSync(new URL("../vite.config.ts", import.meta.url), "utf8");
    for (const pattern of ['"**/secrets.h"', '"**/.build/**"', '"**/.deps/**"', '"**/.zig-venv/**"', '"**/.git/**"', '".env"']) expect(cfg, pattern).toContain(pattern);
    expect(cfg).toContain('host: "127.0.0.1"');                         // never listens on the network
  });
});

describe("what the code panel shows", () => {
  it("counts lines", () => { expect(lineCount("")).toBe(0); expect(lineCount("a")).toBe(1); expect(lineCount("a\n")).toBe(1); expect(lineCount("a\nb\n")).toBe(2); expect(lineCount("a\n\n")).toBe(2); });

  it("finds #includes and ignores commented-out ones", () => {
    expect(parseIncludes('#include <Arduino.h>\n#include "config.h"\n// #include "no.h"\n/* #include "nor.h" */\n  #  include "face.h"')).toEqual({ quoted: ["config.h", "face.h"], angled: ["Arduino.h"] });
  });

  it("follows includes, never follows secrets.h, and counts secrets.example.h as the stand-in", () => {
    const files = [
      { name: "dewy.ino", text: '#include "config.h"\n#include "face.h"\n' },
      { name: "config.h", text: '#if __has_include("secrets.h")\n#include "secrets.h"\n#else\n#include "secrets.example.h"\n#endif\n' },
      { name: "face.h", text: "// plain\n" }, { name: "secrets.example.h", text: "#define A 1\n" }, { name: "stray.h", text: "// unused\n" },
    ];
    const a = analyseSources(files);
    expect(a.reports.map((r) => [r.name, r.role])).toEqual([["dewy.ino", "sketch"], ["config.h", "included"], ["face.h", "included"], ["secrets.example.h", "included"], ["stray.h", "unused"]]);
    expect(a.reports.find((r) => r.name === "secrets.example.h")!.note).toMatch(/real secrets\.h is never read/);
    expect(a.missing).toEqual([]);
    expect(analyseSources([{ name: "dewy.ino", text: '#include "gone.h"\n' }]).missing).toEqual(["gone.h"]);
  });
});
