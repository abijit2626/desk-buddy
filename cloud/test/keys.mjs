// Checks that the server refuses to work when REGISTER_KEY and ADMIN_KEY are the same text.
// (The register key is inside every Dewy's firmware, so an equal admin key would let anyone who reads the firmware
// publish firmware to all Dewys.) It starts its own throw-away server on port 8791 with equal keys and stops it again.
import { spawn, spawnSync } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const PORT = 8791, BASE = "http://127.0.0.1:" + PORT, KEY = "the-same-key-for-both-the-same-key-1234";
let passed = 0, failed = 0;
function check(name, cond, extra) {
  if (cond) { passed++; console.log("  ok   " + name); }
  else { failed++; console.log("  FAIL " + name + (extra !== undefined ? "  -> " + JSON.stringify(extra) : "")); }
}

console.log("Equal-keys check (starting a throw-away server on port " + PORT + ")");
const cwd = new URL("..", import.meta.url).pathname;
const state = mkdtempSync(join(tmpdir(), "dewy-keys-"));   // its own empty database, so the real local one is never touched
const migrated = spawnSync("npx", ["wrangler", "d1", "migrations", "apply", "dewy", "--local", "--persist-to", state, "-c", "wrangler.dev.toml"], { cwd, encoding: "utf8" });
if (migrated.status !== 0) { console.log(migrated.stdout + migrated.stderr); process.exit(1); }
const server = spawn("npx", ["wrangler", "dev", "-c", "wrangler.dev.toml", "--port", String(PORT), "--inspector-port", "9239",
  "--persist-to", state, "--var", "REGISTER_KEY:" + KEY, "--var", "ADMIN_KEY:" + KEY],
  { cwd, stdio: ["ignore", "pipe", "pipe"] });
let log = "";
server.stdout.on("data", (d) => (log += d));
server.stderr.on("data", (d) => (log += d));

async function up() {
  for (let i = 0; i < 90; i++) {
    try { await fetch(BASE + "/api/nothing"); return true; } catch { await new Promise((r) => setTimeout(r, 1000)); }
  }
  return false;
}
try {
  if (!(await up())) { console.log(log.slice(-1500)); throw new Error("the test server did not start"); }
  let r = await fetch(BASE + "/api/device/register", { method: "POST", headers: { "x-register-key": KEY, "content-type": "application/json" }, body: "{}" });
  const regBody = await r.clone().text();
  check("registering is refused while the keys are equal", r.status === 503, { status: r.status, regBody });
  check("and the answer says why", /must be different/.test(regBody));
  r = await fetch(BASE + "/api/admin/firmware", {
    method: "PUT", headers: { authorization: "Bearer " + KEY, "content-type": "application/json" },
    body: JSON.stringify({ version: "9.9.9", url: "https://example.com/evil.bin", sha256: "a".repeat(64), size: 1000 }),
  });
  check("publishing firmware is refused while the keys are equal", r.status === 503, r.status);
} finally {
  server.kill();
}
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed || passed < 3 ? 1 : 0);
