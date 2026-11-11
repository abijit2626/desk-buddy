// What sits in the middle of each part card on the canvas: the live screen, sliders, meters, indicators.
import type { PartType } from "../core/parts";
import { FAKE_SOURCES, sliderToLevel, type FakeSource } from "../sim/audio-gen";
import { PRESETS, type PresetName } from "../sim/imu";
import { VAD_PARAMS } from "../sim/vad";
import { dbfs } from "../sim/audio";
import type { App } from "../app/app";
import { h, toast } from "./dom";

export interface PartBody { el: HTMLElement; update?(): void }

/** only touches the DOM when the text really changed (this runs every frame) */
function setText(el: HTMLElement, text: string) { if (el.textContent !== text) el.textContent = text; }
function setClass(el: HTMLElement, cls: string, on: boolean) { if (el.classList.contains(cls) !== on) el.classList.toggle(cls, on); }

export function buildPartBody(app: App, type: PartType): PartBody {
  switch (type) {
    case "esp32s3": return boardBody(app);
    case "display": return displayBody(app);
    case "imu": return imuBody(app);
    case "mic": return micBody(app);
    case "amp": return ampBody(app);
    case "wifi": return wifiBody(app);
    case "cloud": return cloudBody(app);
  }
}

// ---------------------------------------------------------------- the board
function boardBody(app: App): PartBody {
  const led = h("span", { class: "led" });
  const state = h("span", {}, "no power");
  const el = h("div", { class: "body" },
    h("div", { class: "board-art" },
      h("div", { class: "usb" }, "USB-C"),
      h("div", { class: "chip-art" }, "ESP32-S3-", h("br"), "WROOM-1", h("br"), "N16R8"),
      h("div", {}, led, " ", state),
      h("div", { class: "muted" }, "16 MB flash", h("br"), "8 MB PSRAM"),
      h("div", { class: "muted", style: "font-size:10px" }, "Pins are labelled with GPIO numbers. Blue chips show what Dewy's firmware uses each one for; dashed chips come from the brief only.")));
  return { el, update() { setClass(led, "on", !!app.sim); setText(state, app.sim ? `running ${app.build?.dewyVersion ?? ""}`.trim() : "off"); } };
}

// ---------------------------------------------------------------- the screen + touch
function displayBody(app: App): PartBody {
  const canvas = h("canvas", { class: "screen", width: 320, height: 240, "aria-label": "Dewy's screen: click to tap, hold to pet", tabindex: "0" });
  const ctx = canvas.getContext("2d")!;
  const overlay = h("div", { class: "overlay" });
  const mood = h("span", { class: "pill" }, "—");
  const info = h("span", { class: "muted" });
  let lastSerial = -1;
  let warnedTouch = false;

  const pos = (e: PointerEvent) => {
    const r = canvas.getBoundingClientRect();
    return { x: Math.max(0, Math.min(319, ((e.clientX - r.left) / r.width) * 320)), y: Math.max(0, Math.min(239, ((e.clientY - r.top) / r.height) * 240)) };
  };
  canvas.addEventListener("pointerdown", (e) => {
    const sim = app.sim; if (!sim) return;
    if (!sim.hardware.touch && !warnedTouch) { warnedTouch = true; toast("The touch chip isn't wired to the pins the firmware reads (T_CS 14, T_IRQ 6, shared SPI), so taps do nothing.", "warn", 6000); }
    canvas.setPointerCapture(e.pointerId);
    const p = pos(e); sim.touchDown(p.x, p.y);
  });
  canvas.addEventListener("pointermove", (e) => { if (app.sim && canvas.hasPointerCapture(e.pointerId)) { const p = pos(e); app.sim.touchMove(p.x, p.y); } });
  const up = () => app.sim?.touchUp();
  canvas.addEventListener("pointerup", up); canvas.addEventListener("pointercancel", up);
  canvas.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); app.sim?.tap(); } });

  const el = h("div", { class: "body" },
    h("div", { class: "screen-wrap" }, canvas, overlay),
    h("div", { class: "status-line" }, mood, info),
    h("div", { class: "row" },
      h("button", { class: "small", onclick: () => app.sim?.tap() }, "Tap"),
      h("button", { class: "small", onclick: () => app.sim?.pet() }, "Pet (hold 1.5 s)")),
    h("div", { class: "muted", style: "max-width:240px;font-size:11px" }, "Click the screen to tap (poke); hold for 0.8 s to pet. 240×320 panel shown at its rotation-1 size (320×240)."));

  return {
    el,
    update() {
      const sim = app.sim;
      if (!sim) { overlay.style.display = "grid"; overlay.className = "overlay"; setText(overlay, app.powering ? "Starting…" : "Powered off. Press “Power on”."); setText(mood, "—"); setText(info, ""); lastSerial = -1; return; }
      if (!sim.hardware.display) {
        overlay.style.display = "grid"; overlay.className = "overlay white";
        setText(overlay, "White screen: the display isn't wired to the pins the firmware drives (CS 10, DC 9, RST 8, MOSI 11, SCK 12, VCC, GND). Fix the wires, then restart.");
        setText(mood, sim.status.moodName); return;
      }
      overlay.style.display = "none";
      const st = sim.status;
      if (st.frameSerial !== lastSerial) { lastSerial = st.frameSerial; ctx.putImageData(new ImageData(sim.core.frame(), 320, 240), 0, 0); }
      canvas.style.opacity = String(Math.max(0.03, st.brightness / 255).toFixed(2));
      setText(mood, `mood: ${st.moodName}`);
      setText(info, `backlight ${Math.round((st.brightness / 255) * 100)}%${st.faceDown ? " · face down" : ""}${st.upsideDown ? " · upside down" : ""}`);
    },
  };
}

// ---------------------------------------------------------------- the motion sensor
function imuBody(app: App): PartBody {
  const found = h("span", { class: "pill" }, "I2C: —");
  const read = h("span", { class: "mono" }, "—");
  const rollIn = h("input", { type: "range", min: "-180", max: "180", step: "1", value: "0", "aria-label": "Roll: lean left or right" }) as HTMLInputElement;
  const pitchIn = h("input", { type: "range", min: "-90", max: "90", step: "1", value: "0", "aria-label": "Pitch: tip back or forward" }) as HTMLInputElement;
  const rollV = h("span", { class: "mono" }, "0°"), pitchV = h("span", { class: "mono" }, "0°");
  const sync = () => { if (app.sim) app.sim.imu.setAngles(Number(rollIn.value), Number(pitchIn.value)); };
  rollIn.addEventListener("input", sync); pitchIn.addEventListener("input", sync);
  const preset = (name: PresetName) => h("button", { class: "small", onclick: () => { app.sim?.preset(name); if (!app.sim) toast("Power on first.", "warn"); } }, PRESETS[name].label);
  const el = h("div", { class: "body", style: "max-width:205px" },
    h("div", { class: "status-line" }, found, read),
    h("div", { class: "grid2" }, (Object.keys(PRESETS) as PresetName[]).map(preset)),
    h("div", { class: "slider-row" }, h("span", {}, "Roll"), rollIn, rollV),
    h("div", { class: "slider-row" }, h("span", {}, "Pitch"), pitchIn, pitchV),
    h("div", { class: "row" },
      h("button", { class: "small", onclick: () => app.sim?.shake() }, "Shake"),
      h("button", { class: "small", onclick: () => app.sim?.pickUp() }, "Pick up")),
    h("div", { class: "muted", style: "font-size:11px" }, "x = right, y = up the screen, z = out of the screen. Pick-up only registers after it has been still for 0.8 s (motion.h). No sensor noise is simulated."));
  return {
    el,
    update() {
      const sim = app.sim;
      if (!sim) { setText(found, "I2C: powered off"); setText(read, "—"); return; }
      const st = sim.status;
      setText(found, st.motionAvailable ? "I2C: found 0x68" : "I2C: no answer");
      found.className = "pill " + (st.motionAvailable ? "ok" : "err");
      const [x, y, z] = sim.lastAccel;
      setText(read, `${x.toFixed(2)} ${y.toFixed(2)} ${z.toFixed(2)} g`);
      // follow the preset buttons
      if (document.activeElement !== rollIn) { const r = Math.round(sim.imu.targetRoll); if (rollIn.value !== String(r)) rollIn.value = String(r); }
      if (document.activeElement !== pitchIn) { const p = Math.round(sim.imu.targetPitch); if (pitchIn.value !== String(p)) pitchIn.value = String(p); }
      setText(rollV, `${Math.round(sim.imu.roll)}°`); setText(pitchV, `${Math.round(sim.imu.pitch)}°`);
    },
  };
}

// ---------------------------------------------------------------- the microphone
function micBody(app: App): PartBody {
  const select = h("select", { "aria-label": "What the microphone hears" },
    FAKE_SOURCES.map((s) => h("option", { value: s.id, title: s.hint }, s.label)), h("option", { value: "wav" }, "WAV file…")) as HTMLSelectElement;
  const file = h("input", { type: "file", accept: ".wav,audio/wav,audio/x-wav", style: "display:none" }) as HTMLInputElement;
  const level = h("input", { type: "range", min: "0", max: "100", value: String(app.mic.levelPct), "aria-label": "Microphone level" }) as HTMLInputElement;
  const levelV = h("span", { class: "mono" });
  const transcript = h("input", { type: "text", value: app.mic.transcript, maxlength: "300", "aria-label": "What the speaker says (mock speech to text)", style: "width:100%" }) as HTMLInputElement;
  const fill = h("div", { class: "fill" });
  const thr = h("div", { class: "thr", title: `Silence level: RMS ${VAD_PARAMS.silenceRms} (from the Worker)` });
  const meterTxt = h("span", { class: "mono muted" }, "—");
  const wired = h("span", { class: "pill" }, "—");
  const hint = h("div", { class: "muted", style: "font-size:11px" });
  const dbToPct = (db: number) => Math.max(0, Math.min(100, ((db + 90) / 90) * 100));
  thr.style.left = dbToPct(dbfs(VAD_PARAMS.silenceRms)) + "%";

  select.value = app.mic.source;
  select.addEventListener("change", () => {
    if (select.value === "wav") { file.click(); select.value = app.mic.source; return; }
    app.mic.source = select.value as FakeSource; app.emit("options");
  });
  file.addEventListener("change", async () => {
    const f = file.files?.[0]; if (!f) return;
    if (f.size > 20 * 1024 * 1024) { toast("That WAV file is larger than 20 MB.", "warn"); return; }
    const err = app.loadWav(new Uint8Array(await f.arrayBuffer()), f.name);
    if (err) toast(err, "warn"); else select.value = "wav";
    file.value = "";
  });
  level.addEventListener("input", () => { app.mic.levelPct = Number(level.value); });
  transcript.addEventListener("input", () => { app.mic.transcript = transcript.value; });

  const el = h("div", { class: "body", style: "max-width:205px" },
    h("div", { class: "status-line" }, wired, h("span", { class: "unverified", title: "firmware/dewy has no voice.h yet: GPIO 15, 16, 17 come from the brief and can't be checked against code." }, "UNVERIFIED pins")),
    h("label", {}, "Hears: ", select), file,
    h("div", { class: "slider-row" }, h("span", {}, "Level"), level, levelV),
    h("div", { class: "meter", "aria-label": "Live microphone level while Dewy is listening" }, fill, thr), meterTxt,
    h("label", { class: "muted", style: "font-size:11px" }, "Mock speech to text: what the speaker says"), transcript, hint);

  return {
    el,
    update() {
      const part = app.analysis.parts["mic"];
      const ok = part?.ok;
      setText(wired, ok ? "wired (15, 16, 17)" : "not wired right"); wired.className = "pill " + (ok ? "ok" : "err");
      if (select.value !== app.mic.source && document.activeElement !== select) select.value = app.mic.source;
      setText(levelV, app.mic.source === "wav" || app.mic.source === "silence" ? "—" : `${dbfs(sliderToLevel(app.mic.levelPct) * 32768).toFixed(0)} dB`);
      level.disabled = app.mic.source === "wav" || app.mic.source === "silence";
      setText(hint, app.mic.source === "wav" ? `File: ${app.mic.wavName}. The transcript above is what the mock server "hears": it can't read speech.` : FAKE_SOURCES.find((x) => x.id === app.mic.source)?.hint ?? "");
      const f = app.voiceFrame;
      const live = app.phase === "listening" && f;
      const pct = live ? dbToPct(f.dbfs) : 0;
      fill.style.width = pct + "%";
      fill.style.background = live && f.isSpeech ? "var(--ok)" : "var(--ink-2)";
      setText(meterTxt, live ? `${f.dbfs.toFixed(0)} dBFS · RMS ${f.rms.toFixed(0)} · ${f.isSpeech ? "speech" : "quiet"} · ${f.state}` : "live level shows while Dewy listens");
    },
  };
}

// ---------------------------------------------------------------- the amplifier and speaker
function ampBody(app: App): PartBody {
  const canvas = h("canvas", { width: 440, height: 110, style: "width:220px;height:55px;background:var(--panel-2);border-radius:4px;border:1px solid var(--line)", "aria-label": "Reply audio waveform" });
  const ctx = canvas.getContext("2d")!;
  const wired = h("span", { class: "pill" }, "—");
  const text = h("div", { style: "max-width:220px;font-size:12px;min-height:34px" }, "No reply yet. Ask something in the Voice tab.");
  const playChk = h("input", { type: "checkbox", id: "play-sound" }) as HTMLInputElement;
  let audio: AudioContext | null = null;
  let drawn: unknown = null, lastPct = -1;
  app.replyListeners.add(() => {
    const r = app.lastReply;
    if (!playChk.checked || !r?.wav || !app.sim?.hardware.amp) return;
    try {
      audio ??= new AudioContext();
      const buf = audio.createBuffer(1, r.wav.samples.length, r.wav.rate);
      buf.copyToChannel(new Float32Array(r.wav.samples), 0);
      const src = audio.createBufferSource(); src.buffer = buf; src.connect(audio.destination); src.start();
    } catch { toast("This browser wouldn't play the sound.", "warn"); }
  });
  const el = h("div", { class: "body", style: "max-width:230px" },
    h("div", { class: "status-line" }, wired, h("span", { class: "unverified", title: "firmware/dewy has no voice.h yet: GPIO 18, 21, 38 come from the brief." }, "UNVERIFIED pins")),
    canvas, text,
    h("label", { class: "row", style: "font-size:12px" }, playChk, "Play the (synthetic) reply through my speakers"),
    h("div", { class: "muted", style: "font-size:11px" }, "The reply audio is a made-up babble from the mock server, not a real voice. Loudness and gain are not modelled."));

  const draw = (progress: number) => {
    const r = app.lastReply;
    const W = canvas.width, H = canvas.height;
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = getComputedStyle(canvas).getPropertyValue("background-color") || "#eee";
    if (!r?.wav) return;
    const s = r.wav.samples, step = Math.max(1, Math.floor(s.length / W));
    ctx.strokeStyle = "#3aa0a8"; ctx.beginPath();
    for (let x = 0; x < W; x++) { let mx = 0, mn = 0; for (let i = x * step; i < Math.min(s.length, (x + 1) * step); i++) { mx = Math.max(mx, s[i]); mn = Math.min(mn, s[i]); } ctx.moveTo(x, H / 2 - mx * H * 0.48); ctx.lineTo(x, H / 2 - mn * H * 0.48); }
    ctx.stroke();
    if (progress >= 0 && progress <= 1) { ctx.strokeStyle = "#e0572b"; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(progress * W, 0); ctx.lineTo(progress * W, H); ctx.stroke(); ctx.lineWidth = 1; }
  };
  return {
    el,
    update() {
      const ok = app.analysis.parts["amp"]?.ok;
      setText(wired, ok ? "wired (18, 21, 38)" : "not wired right"); wired.className = "pill " + (ok ? "ok" : "err");
      const r = app.lastReply;
      if (r && drawn !== r) { drawn = r; setText(text, `“${r.reply}”`); lastPct = -2; }
      const speaking = app.phase === "speaking" && app.sim && r && r.durationMs > 0;
      const pct = speaking ? Math.min(1, (app.sim!.nowMs - r!.startedAtMs) / r!.durationMs) : -1;
      if (pct !== lastPct && (Math.abs(pct - lastPct) > 0.01 || pct === -1)) { lastPct = pct; draw(pct); }
    },
  };
}

// ---------------------------------------------------------------- Wi-Fi
function wifiBody(app: App): PartBody {
  const dot = h("span", { class: "led" });
  const label = h("b", {}, "—");
  const detail = h("div", { class: "muted", style: "font-size:11px" });
  const router = h("input", { type: "checkbox", id: "router-up", checked: app.routerUp }) as HTMLInputElement;
  router.addEventListener("change", () => app.setRouter(router.checked));
  const saved = h("input", { type: "checkbox", id: "wifi-saved", checked: app.options.wifiSaved }) as HTMLInputElement;
  saved.addEventListener("change", () => app.setOption("wifiSaved", saved.checked));
  const ssidSel = h("select", { "aria-label": "Network" }, ["Virtual-Home", "Neighbour-5G", "Cafe <Guest>"].map((n) => h("option", { value: n }, n))) as HTMLSelectElement;
  const pass = h("input", { type: "password", placeholder: "password", value: "dewy1234", "aria-label": "Wi-Fi password", style: "width:110px" }) as HTMLInputElement;
  const msg = h("div", { style: "font-size:11px;min-height:14px" });
  const form = h("div", { class: "vlist", style: "border-top:1px solid var(--line);padding-top:6px" },
    h("b", { style: "font-size:11px" }, "On a phone joined to “Dewy-Setup”:"),
    h("div", { class: "row" }, ssidSel, pass),
    h("div", { class: "row" },
      h("button", { class: "small", onclick: () => { const sim = app.sim; if (!sim) return; const code = sim.submitSetupForm(ssidSel.value, pass.value); setText(msg, code === 200 ? "Dewy says: connecting…" : sim.core.portalMessage()); } }, "Connect"),
      h("button", { class: "small", onclick: () => showPage() }, "Show the page")), msg);
  const dlg = h("dialog", {}, h("div", { class: "dh" }, h("b", {}, "Dewy-Setup page (served by the firmware's portal.h)"), h("button", { onclick: () => dlg.close() }, "Close")), h("div", { class: "db" }));
  const showPage = () => {
    const html = app.sim?.core.portalPage();
    const body = dlg.querySelector(".db")!;
    body.textContent = "";
    if (!html) { body.textContent = "The setup hotspot isn't open."; }
    else body.append(h("iframe", { sandbox: "", srcdoc: html, title: "Dewy-Setup page", style: "width:100%;height:60vh;border:1px solid var(--line);border-radius:6px;background:#fff" }));
    if (!dlg.open) dlg.showModal();
  };
  const el = h("div", { class: "body", style: "max-width:260px" },
    h("div", { class: "status-line" }, dot, label), detail,
    h("label", { class: "row" }, router, "Router “Virtual-Home” is on (password dewy1234, made up)"),
    h("label", { class: "row", title: "Applies when the board starts with empty flash (first power-on, or after Erase flash)." }, saved, "Home Wi-Fi already saved at first power-on"),
    h("div", { class: "muted", style: "font-size:11px" }, "No real Wi-Fi is involved. The state below comes from the firmware's own pollWifi()."),
    form, dlg);
  return {
    el,
    update() {
      const sim = app.sim;
      let text = "off", cls = "", more = "Power on to start the firmware.";
      if (sim) {
        const st = sim.status;
        if (st.wifiStatus === 3) { text = "Connected to Virtual-Home"; cls = "on"; more = st.clockIsReal ? "The firmware has the real time." : "Waiting for the time…"; }
        else if (st.portalOn) { text = "Hotspot “Dewy-Setup” open"; more = "Waiting for a phone to pick a network (or the saved one to come back)."; }
        else if (st.hasWifiCredentials && st.wifiStatus === 0) { text = "Connecting…"; more = ""; }
        else if (st.hasWifiCredentials) { text = "Not connected"; more = app.routerUp ? "Wrong password or the network isn't there." : "The router is off."; }
        else { text = "Wi-Fi off"; more = "No Wi-Fi saved and no hotspot."; }
      }
      setText(label, text); setClass(dot, "on", cls === "on"); setText(detail, more);
      form.style.display = sim?.status.portalOn ? "grid" : "none";
      if (router.checked !== app.routerUp) router.checked = app.routerUp;
      if (saved.checked !== app.options.wifiSaved) saved.checked = app.options.wifiSaved;
    },
  };
}

// ---------------------------------------------------------------- the cloud link
function cloudBody(app: App): PartBody {
  const dot = h("span", { class: "led" });
  const label = h("b", {}, "—");
  const code = h("div", { class: "mono", style: "font-size:15px;letter-spacing:.15em" });
  const detail = h("div", { class: "muted", style: "font-size:11px" });
  const el = h("div", { class: "body", style: "max-width:260px" },
    h("div", { class: "status-line" }, dot, label), code, detail,
    h("div", { class: "muted", style: "font-size:11px" }, "A mock server in this page answers. The live Worker is never called. Use the Cloud tab for the pretend phone."));
  return {
    el,
    update() {
      const sim = app.sim; const st = sim?.status;
      let text = "off", on = false, more = "Power on to start the firmware.", c = "";
      if (sim && st) {
        if (!st.cloudConfigured) { text = "Cloud off"; more = "No server address in the build."; }
        else if (st.wifiStatus !== 3) { text = "Waiting for Wi-Fi"; more = "The cloud code only runs once Wi-Fi is connected."; }
        else if (!st.cloudReady) { text = "Connecting to the mock server…"; more = ""; }
        else if (!st.paired) { text = "Registered, no phone paired"; on = true; c = sim.server.pairCode ? sim.server.pairCode.replace(/(.{4})/, "$1 ") : ""; more = c ? "Dewy shows this code on its screen. Pair the pretend phone in the Cloud tab." : "Asking for a pairing code…"; }
        else { text = "Paired with a phone"; on = true; more = "Notes and timers from the phone arrive within a few seconds."; }
      }
      setText(label, text); setClass(dot, "on", on); setText(code, c); setText(detail, more);
    },
  };
}

