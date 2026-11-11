// The committed dewy.wasm must have been built from the firmware as it is now, and must not contain anyone's secrets.
import { mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import { copyFirmwareSources, describeSources, isSecretFile, sha256 } from "../wasm/lib.mjs";
import { FIRMWARE_DIR, wasmBytes } from "./helpers";

const info = JSON.parse(readFileSync(new URL("../public/dewy.build.json", import.meta.url), "utf8")) as {
  files: { name: string; lines: number; sha256: string }[]; combinedSha256: string; note: string; dewyVersion: string; wasmBytes: number; hostSha256: string;
};
const tmp = mkdtempSync(join(tmpdir(), "dewy-build-"));
afterAll(() => rmSync(tmp, { recursive: true, force: true }));

describe("the prebuilt firmware (public/dewy.wasm)", () => {
  it("was built from the firmware files as they are now (rebuild with: npm run build:wasm)", () => {
    const names = readdirSync(FIRMWARE_DIR).filter((n) => /\.(ino|h)$/.test(n) && !isSecretFile(n)).sort();
    const now = describeSources(FIRMWARE_DIR, names);
    const drift = now.files.filter((f: { name: string; sha256: string }) => info.files.find((b) => b.name === f.name)?.sha256 !== f.sha256).map((f: { name: string }) => f.name);
    expect(drift, "these firmware files changed since dewy.wasm was built").toEqual([]);
    expect(info.files.map((f) => f.name)).toEqual(names);
    expect(info.combinedSha256).toBe(now.combinedSha256);
  });

  it("was built from the current host code", () => {
    expect(info.hostSha256).toBe(sha256(readFileSync(new URL("../wasm/host.cpp", import.meta.url), "utf8")));
  });

  it("is the file the build info describes, and reports the firmware's version", async () => {
    expect(wasmBytes.length).toBe(info.wasmBytes);
    expect(info.dewyVersion).toMatch(/^\d+\.\d+\.\d+$/);
    expect(info.note).toMatch(/secrets\.h was not used/);
  });

  it("contains the made-up server details and nothing like a real address or key", () => {
    const text = Buffer.from(wasmBytes).toString("latin1");
    expect(text).toContain("dewy.virtual.invalid");
    expect(text).toContain("virtual-register-key");
    expect(text).not.toMatch(/workers\.dev/);                      // a real Cloudflare address would end like this
    expect(text).not.toMatch(/local-register-key-for-testing-only/);
    expect(text).not.toMatch(/Wokwi-GUEST/);
  });
});

describe("the build never reads or copies secrets.h", () => {
  it("copies sources without secrets.h even when one is there", () => {
    const src = join(tmp, "src"), dst = join(tmp, "dst");
    rmSync(src, { recursive: true, force: true });
    
    mkdirSync(src, { recursive: true });
    writeFileSync(join(src, "dewy.ino"), "// sketch\n");
    writeFileSync(join(src, "config.h"), "// cfg\n");
    writeFileSync(join(src, "secrets.h"), '#define WIFI_PASSWORD "real-password-1234"\n');
    writeFileSync(join(src, "SECRETS.H"), "// upper case\n");
    writeFileSync(join(src, "keys.secret"), "x");
    writeFileSync(join(src, "notes.txt"), "x");
    const copied = copyFirmwareSources(src, dst);
    expect(copied).toEqual(["config.h", "dewy.ino"]);
    expect(readdirSync(dst).sort()).toEqual(["config.h", "dewy.ino"]);
  });

  it("the build script copies from a clean folder and then adds only the made-up secrets.h", () => {
    const script = readFileSync(new URL("../wasm/build.mjs", import.meta.url), "utf8");
    expect(script).toContain("copyFirmwareSources(join(fw, \"dewy\"), buildSrc)");
    expect(script).toContain("virtual_secrets.h");
    expect(script).not.toMatch(/readFileSync\([^)]*secrets\.h/);   // it never opens the real file
    const fake = readFileSync(new URL("../wasm/virtual_secrets.h", import.meta.url), "utf8");
    expect(fake).toContain('#define WIFI_SSID       ""');
    expect(fake).toContain("dewy.virtual.invalid");
  });
});
