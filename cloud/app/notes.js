// The send-only notes page. Its link carries a token after the "#", which never goes to the server in the URL.
import { api, el, toast, ago } from "./common.js";

const $ = (id) => document.getElementById(id);
const token = (location.hash || "").slice(1);
let online = false;

async function refresh() {
  try {
    const me = await api("GET", "/api/me", { token });
    online = me.dewy.online;
    $("title").textContent = "Send " + me.dewy.name + " a note";
    document.title = $("title").textContent;
    $("status").classList.toggle("on", online);
    $("statusText").textContent = online ? me.dewy.name + " is online" : me.dewy.lastSeen ? "Offline, seen " + ago(me.dewy.lastSeen) : "Not connected yet";
    $("noteForm").hidden = false; $("sentCard").hidden = false; $("tip").hidden = false; $("noLink").hidden = true;
    const r = await api("GET", "/api/notes", { token });
    const list = $("noteList");
    list.replaceChildren();
    if (!r.notes.length) list.append(el("p", { class: "empty", text: "Nothing sent yet." }));
    for (const n of r.notes) {
      list.append(el("div", { class: "item" },
        el("div", { class: "grow" }, el("p", { class: "note-text", text: n.text }), el("p", { class: "small muted", text: ago(n.sentAt) })),
        el("span", { class: n.deliveredAt ? "badge ok" : "badge", text: n.deliveredAt ? "Shown" : "Waiting" })));
    }
  } catch (e) {
    if (e.status === 401 || e.status === 403) { showNoLink(); return; }
    $("statusText").textContent = "Can't reach the server";
  }
}
function showNoLink() {
  $("noLink").hidden = false; $("noteForm").hidden = true; $("sentCard").hidden = true; $("tip").hidden = true;
  $("statusText").textContent = "No link";
}

$("noteText").addEventListener("input", (e) => { $("noteCount").textContent = e.target.value.length + " / 120"; });
$("noteForm").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const text = $("noteText").value.trim();
  if (!text) { toast("Write something first.", true); return; }
  $("noteSend").disabled = true;
  try {
    await api("POST", "/api/notes", { token, body: { text } });
    $("noteText").value = ""; $("noteCount").textContent = "0 / 120";
    toast(online ? "Sent! It'll pop up in a few seconds." : "Sent! It'll show when Dewy is back online.");
    refresh();
  } catch (e) { toast(e.message, true); } finally { $("noteSend").disabled = false; }
});

if (!/^[A-Za-z0-9_-]{20,100}$/.test(token)) showNoLink();
else { refresh(); setInterval(() => { if (document.visibilityState === "visible") refresh(); }, 20000); }
