// Tells the Dewy server about a new firmware release, so every Dewy updates itself.
//
// 1. In the Arduino IDE: Sketch > Export Compiled Binary. It writes dewy.ino.bin into the sketch's build folder.
// 2. On GitHub: make a release (for example v1.1.0) and attach that .bin file. Copy the file's download link.
// 3. Run, from the cloud folder:
//      DEWY_SERVER=https://dewy.<you>.workers.dev DEWY_ADMIN_KEY=... node scripts/publish-firmware.mjs 1.1.0 path/to/dewy.ino.bin https://github.com/.../dewy.ino.bin
//
// Bump DEWY_VERSION in firmware/dewy/config.h to the same number before you build.
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";

const [version, file, url] = process.argv.slice(2);
const server = process.env.DEWY_SERVER, key = process.env.DEWY_ADMIN_KEY;
if (!version || !file || !url || !server || !key) {
  console.error("usage: DEWY_SERVER=... DEWY_ADMIN_KEY=... node scripts/publish-firmware.mjs <version> <file.bin> <download url>");
  process.exit(1);
}
if (!/^\d+\.\d+\.\d+$/.test(version)) { console.error("version must look like 1.2.3"); process.exit(1); }

const bin = readFileSync(file);
if (bin[0] !== 0xe9) { console.error(file + " doesn't look like an ESP32 firmware image (it should start with 0xE9)"); process.exit(1); }
const sha256 = createHash("sha256").update(bin).digest("hex");

// check the download link really serves this exact file before telling every Dewy to use it
const res = await fetch(url, { redirect: "follow" });
if (!res.ok) { console.error("couldn't download " + url + " (" + res.status + ")"); process.exit(1); }
const remote = Buffer.from(await res.arrayBuffer());
if (createHash("sha256").update(remote).digest("hex") !== sha256) { console.error("the file at the link is different from " + file); process.exit(1); }

const r = await fetch(server.replace(/\/$/, "") + "/api/admin/firmware", {
  method: "PUT",
  headers: { authorization: "Bearer " + key, "content-type": "application/json" },
  body: JSON.stringify({ version, url, sha256, size: bin.length }),
});
console.log(r.status, await r.text());
process.exit(r.ok ? 0 : 1);
