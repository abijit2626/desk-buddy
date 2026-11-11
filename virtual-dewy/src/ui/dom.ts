// A tiny helper for building DOM without a framework.
import { HONESTY, type Honesty } from "../core/honesty";

type Child = Node | string | number | null | undefined | false;
type Attrs = Record<string, string | number | boolean | null | undefined | ((e: Event) => void)>;

export function h<K extends keyof HTMLElementTagNameMap>(tag: K, attrs: Attrs = {}, ...children: (Child | Child[])[]): HTMLElementTagNameMap[K] {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (typeof v === "function") el.addEventListener(k.replace(/^on/, "").toLowerCase(), v as EventListener);
    else if (k === "class") el.className = String(v);
    else if (v === true) el.setAttribute(k, "");
    else el.setAttribute(k, String(v));
  }
  for (const c of children.flat()) if (c !== null && c !== undefined && c !== false) el.append(c instanceof Node ? c : String(c));
  return el;
}

export const svgNS = "http://www.w3.org/2000/svg";
export function s<K extends keyof SVGElementTagNameMap>(tag: K, attrs: Record<string, string | number> = {}): SVGElementTagNameMap[K] {
  const el = document.createElementNS(svgNS, tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v));
  return el;
}

export const badge = (level: Honesty, extra?: string) =>
  h("span", { class: `badge badge-${level}`, title: HONESTY[level].title + (extra ? " " + extra : "") }, HONESTY[level].label);

export function clear(el: Element) { while (el.firstChild) el.removeChild(el.firstChild); }

export function toast(message: string, kind: "info" | "warn" | "error" = "info", ms = 4500) {
  let host = document.getElementById("toasts");
  if (!host) { host = h("div", { id: "toasts", role: "status", "aria-live": "polite" }); document.body.append(host); }
  const t = h("div", { class: `toast toast-${kind}` }, message);
  host.append(t);
  setTimeout(() => t.remove(), ms);
}

export function download(name: string, text: string, type = "application/json") {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = h("a", { href: url, download: name });
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export const fmtTime = (ms: number) => { const t = Math.floor(ms / 1000); return `${String(Math.floor(t / 60)).padStart(2, "0")}:${String(t % 60).padStart(2, "0")}.${String(Math.floor((ms % 1000) / 100))}`; };

/** safe localStorage: the page must work when storage is blocked */
export const store = {
  get(key: string): string | null { try { return localStorage.getItem("virtual-dewy." + key); } catch { return null; } },
  set(key: string, value: string) { try { localStorage.setItem("virtual-dewy." + key, value); } catch { /* storage blocked or full: carry on without it */ } },
  del(key: string) { try { localStorage.removeItem("virtual-dewy." + key); } catch { /* ignore */ } },
};
export const b64 = {
  enc(bytes: Uint8Array): string { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); },
  dec(text: string): Uint8Array | null { try { return Uint8Array.from(atob(text), (c) => c.charCodeAt(0)); } catch { return null; } },
};
