import "./ui/style.css";
import { App } from "./app/app";
import { HONESTY_TABLE } from "./core/honesty";
import { Workspace } from "./ui/workspace";
import { codePanel, partsPanel, problemsPanel } from "./ui/left-panels";
import { cloudPanel, serialPanel, voicePanel, type Panel } from "./ui/side-panels";
import { badge, clear, h, toast } from "./ui/dom";

const app = new App();

function tabs(items: { id: string; label: string; panel: Panel }[], initial = 0): { el: HTMLElement; update(): void } {
  const bar = h("div", { class: "tabs", role: "tablist" });
  const bodies = items.map((it) => h("div", { class: "body", role: "tabpanel", hidden: true, style: "display:flex;flex-direction:column;min-height:0" }, it.panel.el));
  const btns = items.map((it, i) => h("button", { role: "tab", "aria-selected": "false", onclick: () => select(i) }, it.label));
  const select = (i: number) => { btns.forEach((b, j) => b.setAttribute("aria-selected", String(i === j))); bodies.forEach((b, j) => { b.hidden = i !== j; b.style.display = i === j ? "flex" : "none"; }); };
  btns.forEach((b) => bar.append(b));
  select(initial);
  const el = h("div", { class: "panel", style: "flex:1" }, bar, ...bodies);
  return { el, update() { items.forEach((it, i) => { if (!bodies[i].hidden) it.panel.update?.(); }); } };
}

function realDialog(): HTMLDialogElement {
  const dlg = h("dialog", { "aria-label": "What is real in this simulation" },
    h("div", { class: "dh" }, h("b", {}, "What is real, and what is not"), h("button", { onclick: () => dlg.close() }, "Close")),
    h("div", { class: "db" },
      h("p", {}, "Dewy's firmware code runs unmodified in this page (compiled to WebAssembly from firmware/dewy). Everything around it is virtual. Every part and panel carries one of these badges:"),
      h("div", { class: "row", style: "margin:6px 0" }, badge("real"), badge("approx"), badge("mock"), badge("none")),
      h("table", {}, h("tr", {}, h("th", {}, "What"), h("th", {}, ""), h("th", {}, "Notes")),
        HONESTY_TABLE.map((r) => h("tr", {}, h("td", {}, r.what), h("td", {}, badge(r.level)), h("td", {}, r.note)))),
      h("p", { class: "muted" }, "Not in the firmware yet: voice.h, vad.h, any microphone or speaker code, and the serial commands ask, mic and ww debug. The microphone and amplifier pins (15/16/17 and 18/21/38) come from the project brief and are marked UNVERIFIED.")));
  return dlg;
}

async function boot() {
  const root = document.getElementById("app")!;
  await app.init();

  const workspace = new Workspace(app);
  const parts = partsPanel(app), code = codePanel(app), problems = problemsPanel(app);
  const serial = serialPanel(app), voice = voicePanel(app), cloud = cloudPanel(app);

  const left = tabs([{ id: "parts", label: "Parts", panel: parts }, { id: "code", label: "Code", panel: code }]);
  const right = tabs([{ id: "serial", label: "Serial", panel: serial }, { id: "voice", label: "Voice", panel: voice }, { id: "cloud", label: "Cloud", panel: cloud }]);

  // ---- header
  const led = h("span", { class: "led", "aria-hidden": "true" });
  const powerBtn = h("button", { class: "primary", onclick: () => (app.sim ? app.powerOff() : void app.powerOn()) }, "Power on");
  const restartBtn = h("button", { onclick: () => void app.restart(), title: "Reboot the board (flash is kept)" }, "Restart");
  const eraseBtn = h("button", { class: "small", onclick: () => { app.eraseFlash(); } , title: "Forget saved Wi-Fi, calibration and affection" }, "Erase flash");
  const loadIn = h("input", { type: "file", accept: ".json,application/json", style: "display:none" }) as HTMLInputElement;
  loadIn.addEventListener("change", async () => { const f = loadIn.files?.[0]; if (f) { if (f.size > 200_000) toast("That file is too large to be a wiring file.", "warn"); else app.loadWiringText(await f.text()); } loadIn.value = ""; });
  const real = realDialog();
  const calib = h("input", { type: "checkbox", id: "cal", checked: app.options.preCalibrated }) as HTMLInputElement;
  calib.addEventListener("change", () => app.setOption("preCalibrated", calib.checked));
  const header = h("header", { class: "top" },
    h("h1", {}, "Virtual Dewy", h("span", { class: "sim-flag", title: "Everything here is simulated. See “What is real?”." }, "SIMULATION")),
    h("div", { class: "group" }, led, powerBtn, restartBtn, eraseBtn,
      h("label", { class: "row", title: "Writes the one-time 'cal up' / 'cal face' calibration into the fresh flash. Turn off to try those commands yourself." }, calib, h("span", { style: "font-size:12px" }, "pre-calibrated"))),
    h("div", { class: "spacer" }),
    h("div", { class: "group" },
      h("button", { onclick: () => app.useReferenceWiring(true) }, "Reference wiring"),
      h("button", { onclick: () => { app.clearWiring(); } }, "Clear"),
      h("button", { onclick: () => app.saveWiringFile() }, "Save wiring"),
      h("button", { onclick: () => loadIn.click() }, "Load wiring"), loadIn,
      h("button", { onclick: () => real.showModal() }, "What is real?")));

  const shell = h("main", { class: "shell" },
    h("div", { class: "col" }, left.el),
    h("div", { class: "col" }, h("div", { class: "panel", style: "flex:3" }, workspace.el), h("div", { class: "panel", style: "flex:2;min-height:120px" }, h("div", { class: "body" }, problems.el))),
    h("div", { class: "col" }, right.el));
  clear(root); root.append(header, shell, real);

  const refreshHeader = () => {
    const on = !!app.sim;
    powerBtn.textContent = app.powering ? "Starting…" : on ? "Power off" : "Power on";
    powerBtn.className = on ? "" : "primary";
    restartBtn.disabled = !on;
    led.classList.toggle("on", on);
    calib.checked = app.options.preCalibrated;
    eraseBtn.disabled = !app.flashSaved && !on;
  };
  app.on((k) => { if (k === "power" || k === "options") refreshHeader(); });
  refreshHeader();

  if (app.wasmError) toast(app.wasmError, "error", 10000);
  else if (app.build && !app.matchesBuild && app.files.length) toast("The firmware files here differ from the prebuilt firmware. Run “npm run build:wasm” to rebuild it.", "warn", 8000);

  // ---- one animation frame
  let slow = 0;
  const frame = (now: number) => {
    app.frame(now);
    workspace.tick();
    left.update(); right.update(); problems.update?.();
    slow++;
    if (slow % 30 === 0) { parts.update?.(); }
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
  parts.update?.();

  // for tests and curious people
  (window as unknown as { dewy: App }).dewy = app;
}

void boot();
