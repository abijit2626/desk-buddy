// Dewy phone app. Talks only to the Dewy server it was loaded from.
import { api, el, store, toast, ago, isIOS, isStandalone } from "./common.js";

const $ = (id) => document.getElementById(id);
const TOKEN_KEY = "dewy.token", TIMER_KEY = "dewy.timerEnds";

// POSIX time zones Dewy understands, by name
const ZONES = [
  ["India", "IST-5:30"],
  ["Thailand, Vietnam, Indonesia (west)", "<+07>-7"],
  ["Singapore, Malaysia, Philippines", "<+08>-8"],
  ["UAE, Oman", "<+04>-4"],
  ["Saudi Arabia, Qatar, Kuwait", "<+03>-3"],
  ["United Kingdom, Ireland", "GMT0BST,M3.5.0/1,M10.5.0"],
  ["Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"],
  ["US Eastern", "EST5EDT,M3.2.0,M11.1.0"],
  ["US Pacific", "PST8PDT,M3.2.0,M11.1.0"],
  ["Australia (Sydney, Melbourne)", "AEST-10AEDT,M10.1.0,M4.1.0/3"],
];
const MONTHS = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const DAY_SHORT = ["S", "M", "T", "W", "T", "F", "S"];
const DAY_NAMES = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

let token = store.get(TOKEN_KEY);
let me = null;          // who this phone is, and Dewy's status
let saved = null;       // settings as the server has them
let draft = null;       // settings being edited
let rev = 0;
let tab = "home";
let minutes = 25;
let editingAlarm = null; // alarm id being edited, or "" for a new one
let editDays = [];

const clone = (o) => JSON.parse(JSON.stringify(o));
const isDirty = () => draft && saved && JSON.stringify(draft) !== JSON.stringify(saved);
const pad = (n) => String(n).padStart(2, "0");
function changed() { $("savebar").hidden = !isDirty(); }

// ---------------------------------------------------------------- start
async function boot() {
  fillSelects();
  wire();
  if (!token) return showPair();
  showApp();
  await refreshAll();
}

function showPair() {
  $("app").hidden = true; $("tabs").hidden = true; $("savebar").hidden = true;
  $("pair").hidden = false;
  $("installTip").hidden = !(isIOS() && !isStandalone());
}
function showApp() {
  $("pair").hidden = true;
  $("app").hidden = false; $("tabs").hidden = false;
  showTab(tab);
}

async function refreshAll() {
  try {
    me = await api("GET", "/api/me", { token });
    if (me.role !== "owner") { toast("This phone was paired with a notes link. Open the link instead.", true); return forget(false); }
    const s = await api("GET", "/api/settings", { token });
    saved = s.settings; rev = s.rev;
    if (!isDirty()) draft = clone(saved);
    minutes = saved.studyMinutes;
    renderAll();
    loadNotes();
  } catch (e) {
    if (e.status === 401) { toast("This phone isn't paired any more. Pair it again.", true); return forget(false); }
    $("statusText").textContent = "Can't reach the server";
    toast(e.message, true);
  }
}

// ---------------------------------------------------------------- pairing
async function pair(ev) {
  ev.preventDefault();
  const code = $("pairCode").value.toUpperCase().replace(/[^0-9A-Z]/g, "");
  if (code.length !== 8) { toast("The code is 8 letters and numbers.", true); return; }
  $("pairBtn").disabled = true;
  try {
    const r = await api("POST", "/api/pair", { body: { code, label: $("pairLabel").value || "iPhone" } });
    token = r.token; store.set(TOKEN_KEY, token);
    toast("Paired with " + r.name + "!");
    showApp();
    await refreshAll();
  } catch (e) {
    toast(e.message, true);
  } finally {
    $("pairBtn").disabled = false;
  }
}
async function forget(tellServer = true) {
  if (tellServer && me) { try { await api("DELETE", "/api/access/" + encodeURIComponent(me.id), { token }); } catch { /* already gone */ } }
  store.del(TOKEN_KEY); store.del(TIMER_KEY);
  token = null; me = null; saved = draft = null;
  showPair();
}

// ---------------------------------------------------------------- tabs
function showTab(name) {
  tab = name;
  for (const t of ["home", "alarms", "reminders", "more"]) $("tab-" + t).hidden = t !== name;
  for (const b of document.querySelectorAll("nav.tabs button")) b.setAttribute("aria-current", b.dataset.tab === name ? "page" : "false");
  if (name === "more" && token) loadAccess();
  window.scrollTo(0, 0);
}

// ---------------------------------------------------------------- rendering
function renderAll() {
  renderStatus(); renderTimer(); renderAlarms(); renderReminders(); renderMore();
  changed();
}

function renderStatus() {
  if (!me) return;
  const name = (draft && draft.name) || me.dewy.name;
  $("dewyName").textContent = name;
  document.title = name;
  const st = $("status");
  st.classList.toggle("on", me.dewy.online);
  $("statusText").textContent = me.dewy.online ? name + " is online" : me.dewy.lastSeen ? "Offline, seen " + ago(me.dewy.lastSeen) : "Not connected yet";
}

// study timer: Dewy runs the real timer; the phone shows a countdown of its own
let tick = 0;
function renderTimer() {
  const ends = Number(store.get(TIMER_KEY) || 0);
  const running = ends > Date.now();
  if (!running && ends) store.del(TIMER_KEY);
  $("timerIdle").hidden = running; $("timerRun").hidden = !running; $("timerBadge").hidden = !running;
  $("minOut").textContent = minutes + " min";
  clearInterval(tick);
  if (running) {
    const draw = () => {
      const left = Math.max(0, Math.round((ends - Date.now()) / 1000));
      $("timerLeft").textContent = pad(Math.floor(left / 60)) + ":" + pad(left % 60);
      if (left === 0) { clearInterval(tick); store.del(TIMER_KEY); renderTimer(); toast("Break time!"); }
    };
    draw();
    tick = setInterval(draw, 1000);
  }
}
async function startTimer() {
  $("timerStart").disabled = true;
  try {
    await api("POST", "/api/timer", { token, body: { minutes } });
    store.set(TIMER_KEY, String(Date.now() + minutes * 60000));
    renderTimer();
    toast(me && me.dewy.online ? "Focus time! Dewy is starting the timer." : "Sent. Dewy starts the timer when it's back online.");
  } catch (e) { toast(e.message, true); } finally { $("timerStart").disabled = false; }
}
async function stopTimer() {
  try { await api("POST", "/api/timer/stop", { token }); store.del(TIMER_KEY); renderTimer(); toast("Timer stopped."); }
  catch (e) { toast(e.message, true); }
}

// notes
async function loadNotes() {
  try {
    const r = await api("GET", "/api/notes", { token });
    const list = $("noteList");
    list.replaceChildren();
    if (!r.notes.length) { list.append(el("p", { class: "empty", text: "No notes yet." })); return; }
    for (const n of r.notes.slice(0, 8)) {
      list.append(el("div", { class: "item" },
        el("div", { class: "grow" },
          el("p", { class: "note-text", text: n.text }),
          el("p", { class: "small muted", text: (n.from === "you" ? "From you" : "From " + n.from) + " · " + ago(n.sentAt) })),
        el("span", { class: n.deliveredAt ? "badge ok" : "badge", text: n.deliveredAt ? "Shown" : "Waiting" })));
    }
  } catch { /* keep the old list */ }
}
async function sendNote(ev) {
  ev.preventDefault();
  const text = $("noteText").value.trim();
  if (!text) { toast("Write something first.", true); return; }
  $("noteSend").disabled = true;
  try {
    await api("POST", "/api/notes", { token, body: { text } });
    $("noteText").value = ""; $("noteCount").textContent = "0 / 120";
    toast(me && me.dewy.online ? "Sent! It'll pop up on Dewy in a few seconds." : "Sent! Dewy will show it when it's back online.");
    loadNotes();
  } catch (e) { toast(e.message, true); } finally { $("noteSend").disabled = false; }
}

// alarms
function daysText(days) {
  if (!days.length || days.length === 7) return "Every day";
  const k = days.join(",");
  if (k === "1,2,3,4,5") return "Weekdays";
  if (k === "0,6") return "Weekends";
  return days.map((d) => DAY_NAMES[d]).join(", ");
}
function renderAlarms() {
  if (!draft) return;
  const list = $("alarmList");
  list.replaceChildren();
  const alarms = [...draft.alarms].sort((a, b) => a.time.localeCompare(b.time));
  if (!alarms.length) list.append(el("p", { class: "empty", text: "No alarms yet. Tap Add to make one." }));
  for (const a of alarms) {
    const sw = el("input", { type: "checkbox", role: "switch", "aria-label": "Alarm at " + a.time + " on" });
    sw.checked = a.on;
    sw.addEventListener("change", () => { draft.alarms.find((x) => x.id === a.id).on = sw.checked; changed(); });
    list.append(el("div", { class: "item" },
      el("button", { type: "button", class: "grow plain", "aria-label": "Edit alarm at " + a.time, onclick: () => editAlarm(a.id) },
        el("span", { class: "when block", text: a.time }),
        el("span", { class: "small muted block", text: (a.label ? a.label + " · " : "") + daysText(a.days) })),
      el("label", { class: "switch" }, sw, el("span"))));
  }
  $("addAlarm").disabled = draft.alarms.length >= 10;
}
function renderDays() {
  const box = $("alarmDays");
  box.replaceChildren();
  DAY_SHORT.forEach((d, i) => {
    box.append(el("button", { type: "button", "aria-pressed": editDays.includes(i) ? "true" : "false", "aria-label": DAY_NAMES[i], text: d,
      onclick: () => { editDays = editDays.includes(i) ? editDays.filter((x) => x !== i) : [...editDays, i].sort(); renderDays(); } }));
  });
}
function editAlarm(id) {
  const a = id ? draft.alarms.find((x) => x.id === id) : null;
  editingAlarm = id || "";
  $("alarmEditorTitle").textContent = a ? "Edit alarm" : "New alarm";
  $("alarmTime").value = a ? a.time : "07:00";
  $("alarmLabel").value = a ? a.label : "";
  editDays = a ? [...a.days] : [];
  $("alarmDelete").hidden = !a;
  renderDays();
  $("alarmEditor").hidden = false;
  $("alarmEditor").scrollIntoView({ behavior: "smooth", block: "start" });
}
function saveAlarm(ev) {
  ev.preventDefault();
  const time = $("alarmTime").value;
  if (!/^\d{2}:\d{2}$/.test(time)) { toast("Pick a time.", true); return; }
  const item = { id: editingAlarm || Math.random().toString(36).slice(2, 10), time, days: editDays, label: $("alarmLabel").value.trim().slice(0, 24), on: true };
  if (editingAlarm) { const i = draft.alarms.findIndex((x) => x.id === editingAlarm); item.on = draft.alarms[i].on; draft.alarms[i] = item; }
  else draft.alarms.push(item);
  $("alarmEditor").hidden = true;
  renderAlarms(); changed();
}
function deleteAlarm() {
  draft.alarms = draft.alarms.filter((x) => x.id !== editingAlarm);
  $("alarmEditor").hidden = true;
  renderAlarms(); changed();
}

// reminders
function fillSelects() {
  const opt = (v, t) => el("option", { value: v, text: t });
  const every = [[0, "Off"], [30, "Every 30 minutes"], [45, "Every 45 minutes"], [60, "Every hour"], [90, "Every 1½ hours"], [120, "Every 2 hours"], [180, "Every 3 hours"]];
  for (const id of ["remWater", "remStretch"]) $(id).replaceChildren(...every.map(([v, t]) => opt(v, t)));
  const hour = (h) => (h === 0 || h === 24 ? "12 am" : h < 12 ? h + " am" : h === 12 ? "12 pm" : h - 12 + " pm");
  $("remFrom").replaceChildren(...Array.from({ length: 24 }, (_, h) => opt(h, hour(h))));
  $("remTo").replaceChildren(...Array.from({ length: 24 }, (_, i) => opt(i + 1, i + 1 === 24 ? "midnight" : hour(i + 1))));
  $("studyLen").replaceChildren(...[15, 20, 25, 30, 40, 45, 50, 60, 90].map((m) => opt(m, m + " minutes")));
  $("setTz").replaceChildren(...ZONES.map(([n, z]) => opt(z, n)));
  $("bdMonth").replaceChildren(opt("", "Not set"), ...MONTHS.map((m, i) => opt(i + 1, m)));
  $("bdDay").replaceChildren(...Array.from({ length: 31 }, (_, i) => opt(i + 1, String(i + 1))));
}
function ensureOption(sel, value, label) {
  if (![...sel.options].some((o) => o.value === String(value))) sel.append(el("option", { value, text: label }));
  sel.value = String(value);
}
function renderReminders() {
  if (!draft) return;
  const r = draft.reminders;
  ensureOption($("remWater"), r.water, "Every " + r.water + " minutes");
  ensureOption($("remStretch"), r.stretch, "Every " + r.stretch + " minutes");
  $("remFrom").value = String(r.from); $("remTo").value = String(r.to);
  ensureOption($("studyLen"), draft.studyMinutes, draft.studyMinutes + " minutes");
}

// more
function renderMore() {
  if (!draft) return;
  $("setName").value = draft.name;
  $("cityNow").textContent = draft.city ? draft.city.name : "Not set (no weather)";
  ensureOption($("setTz"), draft.timezone, "Custom: " + draft.timezone);
  $("bdMonth").value = draft.birthday ? String(draft.birthday.month) : "";
  $("bdDay").value = draft.birthday ? String(draft.birthday.day) : "1";
  $("bdDay").disabled = !draft.birthday;
  $("giftDate").value = draft.giftDate ? `${draft.giftDate.year}-${pad(draft.giftDate.month)}-${pad(draft.giftDate.day)}` : "";
  if (me) $("aboutText").textContent = `Paired as "${me.label}". Dewy's software: ${me.dewy.firmware || "unknown"}. Last check-in: ${ago(me.dewy.lastSeen)}.`;
}
async function searchCity(ev) {
  ev.preventDefault();
  const q = $("cityQuery").value.trim();
  const box = $("cityResults");
  box.replaceChildren();
  if (q.length < 2) return;
  try {
    const r = await fetch("https://geocoding-api.open-meteo.com/v1/search?count=5&language=en&format=json&name=" + encodeURIComponent(q));
    const data = await r.json();
    const results = data.results || [];
    if (!results.length) { box.append(el("p", { class: "empty", text: "No places found." })); return; }
    for (const p of results) {
      const label = [p.name, p.admin1, p.country].filter(Boolean).join(", ");
      box.append(el("button", { type: "button", text: label, onclick: () => {
        draft.city = { name: p.name.slice(0, 40), lat: p.latitude, lon: p.longitude };
        box.replaceChildren(); $("cityQuery").value = "";
        renderMore(); changed();
      } }));
    }
  } catch { box.append(el("p", { class: "empty", text: "Couldn't search right now." })); }
}
async function loadAccess() {
  try {
    const r = await api("GET", "/api/access", { token });
    const list = $("accessList");
    list.replaceChildren();
    for (const a of r.access) {
      const what = a.role === "owner" ? "Phone" : "Notes link";
      list.append(el("div", { class: "item" },
        el("div", { class: "grow" },
          el("p", {}, a.label + " ", a.you ? el("span", { class: "badge", text: "this phone" }) : null),
          el("p", { class: "small muted", text: what + " · last used " + ago(a.last_used) })),
        a.you ? null : el("button", { type: "button", class: "danger", text: "Remove", onclick: () => removeAccess(a) })));
    }
  } catch (e) { toast(e.message, true); }
}
async function removeAccess(a) {
  if (!confirm(`Remove "${a.label}"? It won't be able to reach Dewy any more.`)) return;
  try { await api("DELETE", "/api/access/" + encodeURIComponent(a.id), { token }); toast("Removed."); loadAccess(); }
  catch (e) { toast(e.message, true); }
}
async function makeLink(ev) {
  ev.preventDefault();
  const label = $("linkName").value.trim();
  if (!label) { toast("Whose name goes on the notes?", true); return; }
  try {
    const r = await api("POST", "/api/links", { token, body: { label } });
    $("linkUrl").textContent = r.url; $("linkMade").hidden = false;
    $("linkShare").hidden = !navigator.share;
    loadAccess();
  } catch (e) { toast(e.message, true); }
}
async function invite() {
  try {
    const r = await api("POST", "/api/invite", { token });
    $("inviteCode").textContent = r.code.slice(0, 4) + " " + r.code.slice(4);
    $("inviteOut").hidden = false;
  } catch (e) { toast(e.message, true); }
}

// ---------------------------------------------------------------- saving
async function save() {
  $("saveBtn").disabled = true;
  try {
    const r = await api("PUT", "/api/settings", { token, body: { rev, settings: draft } });
    saved = r.settings; rev = r.rev; draft = clone(saved);
    renderAll();
    toast(me && me.dewy.online ? "Saved! Dewy picks it up in a few seconds." : "Saved! Dewy gets it when it's back online.");
  } catch (e) {
    if (e.status === 409) { saved = e.data.settings; rev = e.data.rev; draft = clone(saved); renderAll(); toast(e.message, true); }
    else toast(e.message, true);
  } finally { $("saveBtn").disabled = false; }
}
function undo() { draft = clone(saved); renderAll(); $("alarmEditor").hidden = true; }

// ---------------------------------------------------------------- events
function wire() {
  $("pairForm").addEventListener("submit", pair);
  $("pairCode").addEventListener("input", (e) => {
    const v = e.target.value.toUpperCase().replace(/[^0-9A-Z]/g, "").slice(0, 8);
    e.target.value = v.length > 4 ? v.slice(0, 4) + " " + v.slice(4) : v;
  });
  for (const b of document.querySelectorAll("nav.tabs button")) b.addEventListener("click", () => showTab(b.dataset.tab));

  $("minLess").addEventListener("click", () => { minutes = Math.max(5, minutes - 5); renderTimer(); });
  $("minMore").addEventListener("click", () => { minutes = Math.min(180, minutes + 5); renderTimer(); });
  $("timerStart").addEventListener("click", startTimer);
  $("timerStop").addEventListener("click", stopTimer);
  $("noteForm").addEventListener("submit", sendNote);
  $("noteText").addEventListener("input", (e) => { $("noteCount").textContent = e.target.value.length + " / 120"; });

  $("addAlarm").addEventListener("click", () => editAlarm(null));
  $("alarmEditor").addEventListener("submit", saveAlarm);
  $("alarmCancel").addEventListener("click", () => { $("alarmEditor").hidden = true; });
  $("alarmDelete").addEventListener("click", deleteAlarm);

  const remind = () => {
    draft.reminders = { water: Number($("remWater").value), stretch: Number($("remStretch").value), from: Number($("remFrom").value), to: Number($("remTo").value) };
    if (draft.reminders.from >= draft.reminders.to) { draft.reminders.to = Math.min(24, draft.reminders.from + 1); $("remTo").value = String(draft.reminders.to); }
    changed();
  };
  for (const id of ["remWater", "remStretch", "remFrom", "remTo"]) $(id).addEventListener("change", remind);
  $("studyLen").addEventListener("change", () => { draft.studyMinutes = Number($("studyLen").value); minutes = draft.studyMinutes; renderTimer(); changed(); });

  $("setName").addEventListener("input", () => { const v = $("setName").value.trim(); if (v) { draft.name = v.slice(0, 20); renderStatus(); changed(); } });
  $("setTz").addEventListener("change", () => { draft.timezone = $("setTz").value; changed(); });
  $("cityForm").addEventListener("submit", searchCity);
  const bday = () => {
    const m = Number($("bdMonth").value);
    if (!m) draft.birthday = null;
    else {
      const max = new Date(2024, m, 0).getDate();
      const d = Math.min(Number($("bdDay").value) || 1, max);
      draft.birthday = { month: m, day: d };
    }
    renderMore(); changed();
  };
  $("bdMonth").addEventListener("change", bday);
  $("bdDay").addEventListener("change", bday);
  $("giftDate").addEventListener("change", () => {
    const v = $("giftDate").value;
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(v);
    draft.giftDate = m ? { year: Number(m[1]), month: Number(m[2]), day: Number(m[3]) } : null;
    changed();
  });
  $("linkForm").addEventListener("submit", makeLink);
  $("linkCopy").addEventListener("click", async () => {
    try { await navigator.clipboard.writeText($("linkUrl").textContent); toast("Copied."); } catch { toast("Press and hold the link to copy it.", true); }
  });
  $("linkShare").addEventListener("click", () => { navigator.share({ title: "Send Dewy a note", url: $("linkUrl").textContent }).catch(() => {}); });
  $("inviteBtn").addEventListener("click", invite);
  $("forgetBtn").addEventListener("click", () => { if (confirm("Forget this phone? You'll need a new code from Dewy to pair again.")) forget(true); });

  $("saveBtn").addEventListener("click", save);
  $("undoBtn").addEventListener("click", undo);

  // keep the status and notes fresh while the app is open
  setInterval(() => { if (token && document.visibilityState === "visible") quickRefresh(); }, 20000);
  document.addEventListener("visibilitychange", () => { if (token && document.visibilityState === "visible") quickRefresh(); });
}
async function quickRefresh() {
  try { me = await api("GET", "/api/me", { token }); renderStatus(); if (tab === "home") loadNotes(); }
  catch (e) { if (e.status === 401) forget(false); }
}

boot();
