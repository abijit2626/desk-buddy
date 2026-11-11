// The right-hand column: serial monitor, voice loop, mock cloud.
import type { App } from "../app/app";
import { LIMITS } from "../sim/mock-server";
import { VAD_LABEL, VAD_PARAMS } from "../sim/vad";
import type { VoicePhase } from "../sim/voice";
import { dbfs } from "../sim/audio";
import { WAKE_WORD_LABEL } from "../sim/wakeword";
import type { SerialLine } from "../sim/simulator";
import { badge, clear, fmtTime, h, toast } from "./dom";

export interface Panel { el: HTMLElement; update?(): void }

function setText(el: HTMLElement, text: string) { if (el.textContent !== text) el.textContent = text; }

// ---------------------------------------------------------------- serial monitor
export function serialPanel(app: App): Panel {
  const out = h("div", { class: "serial-out", role: "log", "aria-live": "off", "aria-label": "Serial monitor output", tabindex: "0" });
  const input = h("input", { type: "text", placeholder: "type a command and press Enter (try: help)", "aria-label": "Serial command", style: "flex:1", autocomplete: "off", spellcheck: "false" }) as HTMLInputElement;
  const history: string[] = []; let hi = 0;
  const quick = ["help", "8ball", "time", "weather rain", "timer 1", "note Hello from the sim!", "bday", "sleep", "wake", "wifi", "ask what's the weather?", "mic", "ww debug"];
  let boundSim: unknown = null;

  const append = (l: SerialLine) => {
    const nearBottom = out.scrollHeight - out.scrollTop - out.clientHeight < 40;
    out.append(h("div", { class: `l-${l.kind}` }, h("span", { class: "ts" }, fmtTime(Math.max(0, l.t - 1000))), l.text));
    while (out.childElementCount > 1200) out.firstElementChild?.remove();
    if (nearBottom) out.scrollTop = out.scrollHeight;
  };
  const rebuild = () => {
    clear(out);
    const sim = app.sim;
    if (!sim) { out.append(h("div", { class: "l-link" }, "Powered off. Press “Power on” to start Dewy's firmware: its real serial output appears here.")); boundSim = null; return; }
    for (const l of sim.lines) append(l);
    sim.onLine = append;
    boundSim = sim;
  };
  const send = (text: string) => {
    const sim = app.sim;
    if (!sim) { toast("Power on first.", "warn"); return; }
    if (text.trim()) { history.push(text); hi = history.length; }
    sim.send(text);
  };
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { send(input.value); input.value = ""; }
    else if (e.key === "ArrowUp" && history.length) { hi = Math.max(0, hi - 1); input.value = history[hi] ?? ""; e.preventDefault(); }
    else if (e.key === "ArrowDown") { hi = Math.min(history.length, hi + 1); input.value = history[hi] ?? ""; e.preventDefault(); }
  });
  app.on((k) => { if (k === "power") rebuild(); });
  rebuild();

  const el = h("div", { style: "display:flex;flex-direction:column;gap:6px;height:100%;min-height:0" },
    h("div", { class: "row" }, badge("real", "Lines in white-green are printed by Dewy's own code."), h("span", { class: "muted", style: "font-size:11px" }, "115200 baud in spirit. "), h("span", { style: "color:#a57fd6;font-size:11px;font-style:italic" }, "[virtual] = added by the simulator"),
      h("button", { class: "small", style: "margin-left:auto", onclick: () => { clear(out); } }, "Clear")),
    out,
    h("div", { class: "row" }, input, h("button", { class: "primary", onclick: () => { send(input.value); input.value = ""; } }, "Send")),
    h("div", { class: "quick" }, quick.map((c) => h("button", { class: "small", onclick: () => send(c) }, c))));
  return { el, update() { if (app.sim !== boundSim) rebuild(); } };
}

// ---------------------------------------------------------------- voice loop
export function voicePanel(app: App): Panel {
  const phases: { id: VoicePhase; label: string }[] = [{ id: "listening", label: "listen" }, { id: "thinking", label: "think" }, { id: "speaking", label: "talk" }, { id: "idle", label: "happy" }];
  const chips = new Map(phases.map((p) => [p.id, h("span", { class: "pill", style: "padding:2px 10px;font-size:12px" }, p.label)]));
  const realMood = h("span", { class: "pill" }, "face: —");
  const checks = h("div", { class: "status-line" });
  const vadLine = h("div", { class: "mono muted", style: "font-size:11px;min-height:15px" }, "—");
  const canvas = h("canvas", { width: 640, height: 120, style: "width:100%;height:60px;background:var(--panel-2);border-radius:4px;border:1px solid var(--line)", "aria-label": "Recorded loudness per 20 ms" });
  const ctx = canvas.getContext("2d")!;
  const heard = h("div", { style: "min-height:18px" }), reply = h("div", { style: "min-height:34px" });
  const ask = h("input", { type: "text", value: "What day is it?", maxlength: "300", "aria-label": "Question to type", style: "flex:1" }) as HTMLInputElement;
  let lastDrawn = -1;

  const need = (): string | null => {
    const sim = app.sim;
    if (!sim) return "Power on first.";
    return null;
  };
  const wake = h("button", { class: "primary", title: "The wake word is NOT simulated: this button just starts a question.", onclick: () => { const m = need(); if (m) return toast(m, "warn"); app.sim!.wake(); } }, "“Hey Dewy” (button)");
  const askBtn = h("button", { onclick: () => { const m = need(); if (m) return toast(m, "warn"); app.sim!.ask(ask.value.trim() || "hello"); } }, "Ask (typed)");
  const stop = h("button", { class: "small", onclick: () => app.sim?.voice.cancel() }, "Cancel");
  ask.addEventListener("keydown", (e) => { if (e.key === "Enter") askBtn.click(); });

  const el = h("div", { class: "vlist" },
    h("div", { class: "row" }, h("b", {}, "Voice loop"), badge("approx", "Dewy's firmware has no voice code yet. This page drives the real face with the firmware's own listen / think / talk / reset commands."), badge("none", WAKE_WORD_LABEL)),
    h("div", { class: "row" }, [...chips.values()], realMood),
    h("div", { class: "row" }, wake, stop),
    h("div", { class: "row" }, ask, askBtn),
    checks,
    h("div", { class: "muted", style: "font-size:11px" }, "Press the button: the virtual mic produces what the INMP441 card is set to (change it there, or load a WAV). The level and silence check stops the recording after a pause."),
    vadLine, canvas,
    h("dl", { class: "kv" }, h("dt", {}, "heard"), h("dd", {}, heard), h("dt", {}, "reply"), h("dd", {}, reply)),
    h("div", { class: "muted", style: "font-size:11px" }, VAD_LABEL, " Network and model latency are replaced by a fixed 0.9 s “thinking” pause."));

  const draw = () => {
    const W = canvas.width, H = canvas.height, lv = app.lastClipLevels;
    ctx.clearRect(0, 0, W, H);
    const y = (db: number) => H - Math.max(0, Math.min(1, (db + 90) / 90)) * H;
    ctx.strokeStyle = "#d33"; ctx.setLineDash([4, 4]); ctx.beginPath(); ctx.moveTo(0, y(dbfs(VAD_PARAMS.silenceRms))); ctx.lineTo(W, y(dbfs(VAD_PARAMS.silenceRms))); ctx.stroke(); ctx.setLineDash([]);
    ctx.fillStyle = "#3aa0a8";
    const barW = Math.max(1, W / 600);
    lv.forEach((r, i) => { const top = y(dbfs(r)); ctx.fillStyle = r >= VAD_PARAMS.silenceRms ? "#2c9a5b" : "#8a98a4"; ctx.fillRect(i * barW, top, Math.max(1, barW - 0.2), H - top); });
  };

  return {
    el,
    update() {
      const sim = app.sim;
      for (const p of phases) { const c = chips.get(p.id)!; const on = app.phase === p.id; if (c.classList.contains("on") !== on) c.classList.toggle("on", on); }
      setText(realMood, sim ? `real face: ${sim.status.moodName}` : "face: off");
      const hw = app.hardware;
      const items: [string, boolean][] = [["mic wired", hw.mic], ["amp wired", hw.amp], ["Wi-Fi", !!sim && sim.status.wifiStatus === 3], ["registered", !!sim && sim.status.cloudReady]];
      const key = items.map(([, v]) => (v ? 1 : 0)).join("");
      if (checks.dataset.k !== key) { checks.dataset.k = key; clear(checks); for (const [n, v] of items) checks.append(h("span", { class: `pill ${v ? "ok" : "warn"}` }, `${v ? "✓" : "✗"} ${n}`)); }
      const f = app.voiceFrame;
      setText(vadLine, app.phase === "listening" && f ? `listening ${(f.recordedMs / 1000).toFixed(1)} s · ${f.dbfs.toFixed(0)} dBFS · ${f.isSpeech ? "speech" : "quiet"} · speech ${f.speechMs} ms · pause ${f.quietMs}/${VAD_PARAMS.hangoverMs} ms` : app.phase === "idle" ? "idle" : app.phase);
      const r = app.lastReply;
      if (r) { setText(heard, `“${r.heard}”`); setText(reply, `“${r.reply}”`); }
      if (app.lastClipLevels.length !== lastDrawn) { lastDrawn = app.lastClipLevels.length; draw(); }
    },
  };
}

// ---------------------------------------------------------------- the mock cloud and the pretend phone
export function cloudPanel(app: App): Panel {
  const state = h("div", { class: "status-line" });
  const note = h("input", { type: "text", value: "Good luck today!", maxlength: "120", "aria-label": "Note text", style: "flex:1" }) as HTMLInputElement;
  const mins = h("input", { type: "text", value: "25", size: "3", "aria-label": "Timer minutes", style: "width:48px" }) as HTMLInputElement;
  const weather = h("select", { "aria-label": "Weather the mock reports" }, ["sun", "cloud", "rain"].map((w) => h("option", { value: w }, w))) as HTMLSelectElement;
  const log = h("div", { class: "log", role: "log", "aria-label": "Mock server requests" });
  const usage = h("span", { class: "muted" });
  let lastLen = -1;
  const need = () => { if (!app.sim) { toast("Power on first.", "warn"); return false; } return true; };

  weather.addEventListener("change", () => { if (app.sim) app.sim.setWeather(weather.value as "sun"); });
  const el = h("div", { class: "vlist" },
    h("div", { class: "row" }, h("b", {}, "Cloud"), badge("mock", "A mock in this page. The live Worker is never called."), badge("real", "cloud.h's register, sync, pairing and weather code is the firmware's own.")),
    state,
    h("div", { class: "muted", style: "font-size:11px" }, "Dewy talks to https://dewy.virtual.invalid, an address that can't exist. This page answers it with a mock that copies the Worker's response shapes (checked against cloud/src by the tests)."),
    h("h3", {}, "Pretend phone"),
    h("div", { class: "row" }, h("button", { onclick: () => { if (!need()) return; const r = app.sim!.pairPhone(); if (r.ok) toast("Phone paired. Dewy notices within a few seconds."); else toast(r.error, "warn", 6000); } }, "Pair with Dewy's code"),
      h("button", { onclick: () => { if (!need()) return; app.sim!.server.unpair(); toast("Phone unpaired."); } }, "Unpair")),
    h("div", { class: "row" }, note, h("button", { onclick: () => { if (!need()) return; app.sim!.server.queueNote(note.value.trim() || "Hi!", "Phone"); toast("Note queued. Dewy picks it up at its next check-in (every 8 s)."); } }, "Send note")),
    h("div", { class: "row" }, "Study timer", mins, "min",
      h("button", { onclick: () => { if (!need()) return; const m = Math.max(1, Math.min(180, Math.round(Number(mins.value) || 25))); app.sim!.server.queueTimer(m); } }, "Start"),
      h("button", { onclick: () => { if (!need()) return; app.sim!.server.queueTimerStop(); } }, "Stop")),
    h("div", { class: "row" }, "Weather the mock reports", weather),
    h("div", { class: "row" }, h("button", { class: "danger small", title: "As if the server's database was wiped: Dewy gets a 401 and registers again.", onclick: () => { if (!need()) return; app.sim!.server.forgetDevice(); toast("Server forgot this Dewy. Watch it register again in the serial monitor."); } }, "Forget this Dewy (server wipe)"), usage),
    h("h3", {}, "Requests the firmware and the voice loop made"), log);

  return {
    el,
    update() {
      const sim = app.sim;
      const st = sim?.status;
      const items: [string, boolean][] = [["Wi-Fi", !!st && st.wifiStatus === 3], ["registered", !!sim && sim.server.registered], ["phone paired", !!sim && sim.server.paired]];
      const key = items.map(([, v]) => (v ? 1 : 0)).join("") + (sim?.server.pairCode ?? "");
      if (state.dataset.k !== key) {
        state.dataset.k = key; clear(state);
        for (const [n, v] of items) state.append(h("span", { class: `pill ${v ? "ok" : "warn"}` }, `${v ? "✓" : "·"} ${n}`));
        if (sim?.server.pairCode) state.append(h("span", { class: "pill on mono" }, `code ${sim.server.pairCode}`));
      }
      if (sim) setText(usage, `questions today: ${sim.server.chatsToday()} / ${LIMITS.chatsPerDay}`);
      if (sim && weather.value !== sim.weatherSet && document.activeElement !== weather) weather.value = sim.weatherSet;
      const entries = sim?.serverLog ?? [];
      if (entries.length !== lastLen) {
        lastLen = entries.length; clear(log);
        for (const e of entries.slice(-40)) {
          const cls = e.status < 0 ? "sneg" : "s" + Math.floor(e.status / 100);
          log.append(h("div", { class: cls }, `${e.method === "-" ? "·" : e.method} ${e.path} ${e.status > 0 ? e.status : e.status < 0 ? "no answer" : ""}${e.note ? "  (" + e.note + ")" : ""}`));
        }
        log.scrollTop = log.scrollHeight;
        if (!entries.length) log.textContent = "Nothing yet.";
      }
    },
  };
}
