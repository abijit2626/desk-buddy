// A tiny read-only route for the dev and preview servers: GET /__firmware/list and GET /__firmware/file/<name>.
// The folder is chosen when the server starts (DEWY_FIRMWARE_DIR, default ../firmware/dewy), never by the page.
import type { IncomingMessage, ServerResponse } from "node:http";
import { resolve } from "node:path";
import type { Plugin } from "vite";
import { listFirmware, readFirmwareFile } from "./firmware-files.ts";

type Next = () => void;

/** Only the page itself may ask: the Host must be this machine, and any Origin must match the Host (no other site can read your code through the browser). */
export function isLocalRequest(req: Pick<IncomingMessage, "headers">): boolean {
  const host = String(req.headers.host ?? "");
  if (!/^(127\.0\.0\.1|localhost|\[::1\])(:\d+)?$/.test(host)) return false;
  const origin = req.headers.origin;
  if (origin === undefined) return true;                          // same-origin GETs send no Origin
  try { return new URL(String(origin)).host === host; } catch { return false; }
}

export function firmwareHandler(dir: string) {
  return (req: IncomingMessage, res: ServerResponse, next: Next) => {
    const url = req.url ?? "";
    if (!url.startsWith("/__firmware/")) return next();
    const send = (status: number, type: string, body: string) => {
      res.statusCode = status;
      res.setHeader("content-type", type);
      res.setHeader("cache-control", "no-store");
      res.setHeader("x-content-type-options", "nosniff");
      res.end(body);
    };
    if (req.method !== "GET") return send(405, "text/plain; charset=utf-8", "read-only");
    if (!isLocalRequest(req)) return send(403, "text/plain; charset=utf-8", "local page only");
    const path = url.split("?")[0];
    try {
      if (path === "/__firmware/list") return send(200, "application/json; charset=utf-8", JSON.stringify(listFirmware(dir)));
      const m = /^\/__firmware\/file\/([^/]+)$/.exec(path);
      if (m) {
        let name: string;
        try { name = decodeURIComponent(m[1]); } catch { return send(400, "text/plain; charset=utf-8", "bad name"); }
        const text = readFirmwareFile(dir, name);
        return text === null ? send(404, "text/plain; charset=utf-8", "no such firmware file") : send(200, "text/plain; charset=utf-8", text);
      }
      return send(404, "text/plain; charset=utf-8", "not found");
    } catch {
      return send(500, "text/plain; charset=utf-8", "could not read the firmware folder");
    }
  };
}

export function firmwarePlugin(dir = process.env.DEWY_FIRMWARE_DIR || resolve(import.meta.dirname, "../../firmware/dewy")): Plugin {
  const handler = firmwareHandler(dir);
  return {
    name: "dewy-firmware-files",
    configureServer(server) { server.middlewares.use(handler); },
    configurePreviewServer(server) { server.middlewares.use(handler); },
  };
}
