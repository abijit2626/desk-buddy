#!/usr/bin/env node
// Builds public/dewy.wasm: Dewy's real firmware (firmware/dewy/dewy.ino and its headers, unmodified) compiled for the
// browser, with the stand-in hardware in wasm/host/.
//
//   npm run build:wasm
//
// Needs a C++ compiler for WebAssembly. It uses zig (`zig c++`), found in this order: $ZIG, `zig` on the PATH, or a
// pinned copy it installs into .zig-venv with pip (python3 and network access needed once). It also fetches
// ArduinoJson (the same version firmware/test/run_tests.sh uses) into .deps/ the first time.
//
// The real firmware/dewy/secrets.h is NEVER read: the sources are copied to .build/src without it, and a made-up
// secrets.h (wasm/virtual_secrets.h) is put in its place.
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, copyFileSync, readFileSync, writeFileSync, statSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { copyFirmwareSources, describeSources, sha256 } from "./lib.mjs";

const ZIG_VERSION = "0.17.1";          // the version this was developed with
const ARDUINOJSON_TAG = "v7.4.2";      // same as firmware/test/run_tests.sh
const here = dirname(fileURLToPath(import.meta.url));
const app = resolve(here, "..");
const repo = resolve(app, "..");
const fw = join(repo, "firmware");
const buildSrc = join(app, ".build", "src");
const deps = join(app, ".deps", "ArduinoJson");
const venv = join(app, ".zig-venv");

const run = (cmd, args, opts = {}) => {
  const r = spawnSync(cmd, args, { stdio: opts.capture ? ["ignore", "pipe", "pipe"] : "inherit", encoding: "utf8", ...opts });
  if (r.error) throw r.error;
  return r;
};
const fail = (msg) => { console.error("\nbuild:wasm failed: " + msg); process.exit(1); };

// ---- 1. find (or install) zig
function findZig() {
  const candidates = [];
  if (process.env.ZIG) candidates.push(process.env.ZIG.split(" "));
  candidates.push(["zig"]);
  const venvPy = join(venv, "bin", "python");
  if (existsSync(venvPy)) candidates.push([venvPy, "-m", "ziglang"]);
  for (const c of candidates) {
    const r = spawnSync(c[0], [...c.slice(1), "version"], { encoding: "utf8" });
    if (!r.error && r.status === 0) return { cmd: c, version: r.stdout.trim() };
  }
  console.log(`zig not found: installing ziglang==${ZIG_VERSION} into .zig-venv (once)...`);
  if (run("python3", ["-m", "venv", venv]).status !== 0) fail("could not create a python venv (is python3 installed?)");
  if (run(venvPy, ["-m", "pip", "install", "--quiet", `ziglang==${ZIG_VERSION}`]).status !== 0) fail("pip could not install ziglang");
  const c = [venvPy, "-m", "ziglang"];
  const r = spawnSync(c[0], [...c.slice(1), "version"], { encoding: "utf8" });
  if (r.status !== 0) fail("zig does not run");
  return { cmd: c, version: r.stdout.trim() };
}

// ---- 2. ArduinoJson
if (!existsSync(join(deps, "src", "ArduinoJson.h"))) {
  console.log(`fetching ArduinoJson ${ARDUINOJSON_TAG}...`);
  mkdirSync(dirname(deps), { recursive: true });
  if (run("git", ["clone", "--quiet", "--depth", "1", "--branch", ARDUINOJSON_TAG, "https://github.com/bblanchon/ArduinoJson.git", deps]).status !== 0)
    fail("could not clone ArduinoJson");
}

// ---- 3. sources: a copy of firmware/dewy without secrets.h, plus the made-up secrets.h
const names = copyFirmwareSources(join(fw, "dewy"), buildSrc);
copyFileSync(join(here, "virtual_secrets.h"), join(buildSrc, "secrets.h"));
if (!names.includes("dewy.ino")) fail("firmware/dewy/dewy.ino not found");
console.log(`firmware sources: ${names.join(", ")} (secrets.h is never copied)`);

// ---- 4. compile
const zig = findZig();
mkdirSync(join(app, "public"), { recursive: true });
const out = join(app, "public", "dewy.wasm");
const args = [
  ...zig.cmd.slice(1), "c++", "-target", "wasm32-wasi", "-mexec-model=reactor", "-O2", "-std=c++17", "-fno-exceptions", "-w",
  "-include", join(here, "host", "hosttime.h"),
  "-I", join(here, "host"), "-I", buildSrc,
  "-I", join(fw, "test", "mock"),        // only ArduinoOTA.h, Update.h, esp_heap_caps.h and the SHA-256 (the repo's own stand-ins)
  "-I", join(fw, "test", "fonts"),       // the 18 pt and 24 pt fonts
  "-I", join(deps, "src"),
  "-Wl,--strip-all",
  "-o", out, join(here, "host.cpp"),
];
console.log("compiling (about a minute)...");
const t0 = Date.now();
const r = run(zig.cmd[0], args);
if (r.status !== 0) fail("the compiler reported errors (see above)");

// ---- 5. record what was built, so the page can tell whether the code it shows matches the running firmware
const desc = describeSources(buildSrc, names.filter((n) => n !== "secrets.h"));
const cfg = readFileSync(join(buildSrc, "config.h"), "utf8");
const gitRev = run("git", ["-C", repo, "rev-parse", "--short", "HEAD"], { capture: true }).stdout.trim();
const info = {
  builtAt: new Date().toISOString(),
  zig: zig.version,
  gitRev,
  dewyVersion: (/#define\s+DEWY_VERSION\s+"([^"]+)"/.exec(cfg) || [])[1] || null,
  ...desc,
  hostSha256: sha256(readFileSync(join(here, "host.cpp"), "utf8")),
  wasmBytes: statSync(out).size,
  note: "secrets.h was not used: the build uses wasm/virtual_secrets.h (made-up values).",
};
writeFileSync(join(app, "public", "dewy.build.json"), JSON.stringify(info, null, 2) + "\n");
console.log(`done in ${((Date.now() - t0) / 1000).toFixed(0)} s: public/dewy.wasm (${(info.wasmBytes / 1024).toFixed(0)} KB)`);
