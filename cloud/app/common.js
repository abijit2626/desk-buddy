// Shared helpers for the Dewy app and the notes page.

export const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* private mode: lives only in this page */ } },
  del(k) { try { localStorage.removeItem(k); } catch { /* ignore */ } },
};

// Calls the Dewy server. Throws an Error with .status and a friendly message.
export async function api(method, path, { token, body } = {}) {
  const headers = {};
  if (token) headers.authorization = "Bearer " + token;
  if (body !== undefined) headers["content-type"] = "application/json";
  let res;
  try {
    res = await fetch(path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  } catch {
    const e = new Error("Can't reach Dewy's server. Check your internet and try again.");
    e.status = 0;
    throw e;
  }
  let data = null;
  try { data = await res.json(); } catch { /* not JSON */ }
  if (!res.ok) {
    const e = new Error((data && data.error) || "Something went wrong (" + res.status + ").");
    e.status = res.status;
    e.data = data;
    throw e;
  }
  return data;
}

// Builds DOM nodes. Text always goes in as text, never as HTML.
export function el(tag, attrs = {}, ...kids) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === false || v === null || v === undefined) continue;
    if (k === "class") n.className = v;
    else if (k === "text") n.textContent = v;
    else if (k.startsWith("on") && typeof v === "function") n.addEventListener(k.slice(2), v);
    else n.setAttribute(k, v === true ? "" : String(v));
  }
  for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false) n.append(kid instanceof Node ? kid : String(kid));
  return n;
}

let toastTimer = 0;
export function toast(msg, bad = false) {
  let t = document.getElementById("toast");
  if (!t) { t = el("div", { id: "toast", class: "toast", role: "status", "aria-live": "polite" }); document.body.append(t); }
  t.textContent = msg;
  t.className = "toast" + (bad ? " bad" : "");
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, bad ? 5000 : 2800);
}

export function ago(sec) {
  if (!sec) return "never";
  const d = Math.max(0, Math.floor(Date.now() / 1000) - sec);
  if (d < 60) return "just now";
  if (d < 3600) return Math.floor(d / 60) + " min ago";
  if (d < 86400) return Math.floor(d / 3600) + " h ago";
  return Math.floor(d / 86400) + " d ago";
}

export const isIOS = () => /iP(hone|ad|od)/.test(navigator.userAgent) || (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
export const isStandalone = () => window.navigator.standalone === true || window.matchMedia("(display-mode: standalone)").matches;
