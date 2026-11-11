// The left-hand column: the parts list and the code panel.
import { PART_DEFS, PART_ORDER, type PartType } from "../core/parts";
import { PIN_MAP } from "../core/pinmap";
import { lineCount } from "../core/firmware-source";
import type { App } from "../app/app";
import { badge, clear, h, toast } from "./dom";
import type { Panel } from "./side-panels";

function setText(el: HTMLElement, text: string) { if (el.textContent !== text) el.textContent = text; }

// ---------------------------------------------------------------- parts
export function partsPanel(app: App): Panel {
  const list = h("div", { class: "palette" });
  const items = new Map<PartType, HTMLElement>();
  for (const type of PART_ORDER) {
    const def = PART_DEFS[type];
    const unverified = PIN_MAP.some((e) => e.partType === type && e.source === "brief");
    const add = h("button", { class: "small", onclick: () => app.addPart(type) }, "Add");
    const item = h("div", { class: "palette-item", draggable: "true", title: def.honestyNote, "data-type": type },
      h("div", { class: "t" }, def.name, badge(def.honesty, def.honestyNote), unverified ? h("span", { class: "unverified", title: "firmware/dewy has no voice.h yet: these pin numbers come from the brief." }, "UNVERIFIED pins") : null),
      h("div", { class: "b" }, def.blurb),
      h("div", { class: "row" }, add, def.virtual ? h("span", { class: "muted", style: "font-size:11px" }, "no pins: an indicator") : h("span", { class: "muted", style: "font-size:11px" }, `${def.pins.length} pins`)));
    item.addEventListener("dragstart", (e) => { if (item.getAttribute("aria-disabled") === "true") { e.preventDefault(); return; } e.dataTransfer?.setData("text/plain", "part:" + type); e.dataTransfer!.effectAllowed = "copy"; });
    items.set(type, item); list.append(item);
  }
  const el = h("div", { class: "vlist" },
    h("div", { class: "muted", style: "font-size:12px" }, "Drag a part onto the canvas (or press Add). One of each. Drag from a pin to another pin to wire them; click a pin and then another pin works too."),
    list);
  return {
    el,
    update() {
      const placed = new Set(app.wiring.parts.map((p) => p.type));
      for (const [type, item] of items) {
        const on = placed.has(type);
        if ((item.getAttribute("aria-disabled") === "true") !== on) { item.setAttribute("aria-disabled", String(on)); item.draggable = !on; item.querySelector("button")!.disabled = on; item.querySelector("button")!.textContent = on ? "On canvas" : "Add"; }
      }
    },
  };
}

// ---------------------------------------------------------------- code
export function codePanel(app: App): Panel {
  let current = "dewy.ino";
  const files = h("div", { class: "files", role: "listbox", "aria-label": "Firmware files" });
  const summary = h("div", { class: "vlist" });
  const editor = h("textarea", { class: "editor", spellcheck: "false", "aria-label": "File contents", wrap: "off" }) as HTMLTextAreaElement;
  const editorTitle = h("div", { class: "row" });
  const status = h("div", { style: "font-size:12px" });
  const nameIn = h("input", { type: "text", placeholder: "new file name, e.g. voice.h", "aria-label": "New file name", style: "flex:1;min-width:0" }) as HTMLInputElement;
  const fileIn = h("input", { type: "file", multiple: true, accept: ".ino,.h", style: "display:none" }) as HTMLInputElement;
  let editing = false;

  const render = () => {
    const a = app.sourceAnalysis;
    clear(files);
    if (!app.files.length) files.append(h("div", { class: "muted" }, app.folderError || "No files yet: load the firmware folder, add files from your computer, or add a file and paste code into it."));
    for (const r of a.reports) {
      const m = app.fileMatch(r.name);
      files.append(h("div", { class: "file", role: "option", tabindex: "0", "aria-current": String(r.name === current), "aria-selected": String(r.name === current), onclick: () => { current = r.name; render(); }, onkeydown: (e: Event) => { if ((e as KeyboardEvent).key === "Enter") { current = r.name; render(); } }, title: r.note ?? (r.includedBy.length ? `included by ${r.includedBy.join(", ")}` : r.role === "sketch" ? "the sketch: setup() and loop()" : "not #included by the sketch") },
        h("span", { class: "nm" }, r.name),
        h("span", { class: "mono muted" }, `${r.lines} lines`),
        h("span", { class: "role" }, r.role === "sketch" ? "sketch" : r.role === "included" ? (m === "edited" ? "edited" : "included") : "unused")));
    }
    clear(summary);
    if (app.files.length) {
      summary.append(h("div", { class: "muted", style: "font-size:12px" }, `${a.reports.length} files, ${a.totalLines.toLocaleString()} lines. Included by the sketch: ${a.order.length}. Library headers it asks for: ${a.system.join(", ") || "none"}.`));
      if (a.missing.length) summary.append(h("div", { class: "pill err" }, `missing: ${a.missing.join(", ")}`));
    }
    const sel = app.files.find((f) => f.name === current) ?? app.files[0];
    if (sel) { current = sel.name; if (!editing && editor.value !== sel.text) editor.value = sel.text; }
    else editor.value = "";
    editor.disabled = !sel;
    setText(editorTitle, ""); editorTitle.append(h("b", { class: "mono" }, sel?.name ?? "—"), sel ? h("span", { class: "muted" }, `${lineCount(sel.text)} lines`) : "");
  };

  const refreshStatus = () => {
    clear(status);
    if (!app.files.length) return;
    const looksSecret = app.files.some((f) => /#\s*define\s+(WIFI_PASSWORD|OTA_PASSWORD|DEWY_REGISTER_KEY)\s+"[^"]+"/.test(f.text));
    if (app.matchesBuild) status.append(h("span", { class: "pill ok" }, "✓ matches the running firmware build"), " ", h("span", { class: "muted" }, app.build ? `built ${app.build.builtAt.slice(0, 16).replace("T", " ")} from ${app.build.gitRev}` : ""));
    else status.append(h("span", { class: "pill warn" }, "edited"), " ", h("span", { class: "muted" }, "The simulation still runs the prebuilt firmware. Edits here change the pin map and the wiring warnings only. Rebuild with “npm run build:wasm” to run them."));
    if (looksSecret) status.append(h("div", { class: "pill err", style: "margin-top:4px" }, "A file here contains what looks like a real password or key. Nothing is uploaded anywhere, but don't commit or screenshot it."));
    status.append(h("div", { class: "muted", style: "margin-top:4px" }, app.folder
      ? `Loaded from the “${app.folder.folder}” folder.${app.folder.secretsPresent ? " A secrets.h is there: it is ignored and never read (the build uses secrets.example.h's values)." : ""}`
      : app.folderError));
  };

  editor.addEventListener("focus", () => { editing = true; });
  editor.addEventListener("blur", () => { editing = false; });
  let t = 0;
  editor.addEventListener("input", () => {
    const f = app.files.find((x) => x.name === current); if (!f) return;
    f.text = editor.value;
    clearTimeout(t); t = window.setTimeout(() => { app.setFiles(app.files.map((x) => (x.name === current ? { ...x, text: editor.value } : x))); }, 250);
  });

  const addNamed = () => {
    const name = nameIn.value.trim();
    if (!name) { toast("Type a file name first, for example voice.h.", "warn"); return; }
    const err = app.addOrReplaceFile(name, app.files.find((f) => f.name === name)?.text ?? "// paste your code here\n");
    if (err) { toast(err, "warn", 7000); return; }
    current = name; nameIn.value = ""; editing = false; render(); editor.focus();
  };
  fileIn.addEventListener("change", async () => {
    for (const f of Array.from(fileIn.files ?? []).slice(0, 30)) {
      const err = app.addOrReplaceFile(f.name, await f.text());
      if (err) toast(`${f.name}: ${err}`, "warn", 7000);
    }
    fileIn.value = ""; render();
  });

  const el = h("div", { class: "vlist" },
    h("div", { class: "row" }, badge("real", "The simulation runs the real firmware that was compiled from these files."), h("span", { class: "muted", style: "font-size:11px" }, "secrets.h is never loaded")),
    h("div", { class: "row" },
      h("button", { class: "small", onclick: async () => { const ok = await app.loadFirmwareFolder(); if (!ok) toast(app.folderError, "warn", 6000); editing = false; render(); refreshStatus(); } }, "Load firmware folder"),
      h("button", { class: "small", onclick: () => fileIn.click() }, "Open files…"), fileIn,
      h("button", { class: "small", onclick: () => { app.resetFiles(); editing = false; render(); } }, "Reset")),
    files, summary, status,
    h("div", { class: "row" }, nameIn, h("button", { class: "small", onclick: addNamed }, "Add / paste file"),
      h("button", { class: "small danger", onclick: () => { if (app.files.some((f) => f.name === current)) { app.removeFile(current); editing = false; render(); } } }, "Remove")),
    editorTitle, editor,
    h("div", { class: "muted", style: "font-size:11px" }, "Paste code into the editor: pins like ", h("code", {}, "#define PIN_I2C_SDA 4"), " are read from it and used by the wiring warnings."));
  app.on((k) => { if (k === "files") { render(); refreshStatus(); } });
  render(); refreshStatus();
  return { el };
}

// ---------------------------------------------------------------- problems and wire list
export function problemsPanel(app: App): Panel {
  const banner = h("div", { class: "restart-banner", style: "display:none" });
  const list = h("div", {});
  const status = h("table", { class: "parts-status" });
  const sel1 = h("select", { "aria-label": "First pin" }) as HTMLSelectElement;
  const sel2 = h("select", { "aria-label": "Second pin" }) as HTMLSelectElement;
  const wires = h("div", { class: "vlist", style: "max-height:140px;overflow:auto" });
  let key = "";

  const fillSelects = () => {
    for (const sel of [sel1, sel2]) {
      const prev = sel.value; clear(sel);
      for (const part of app.wiring.parts) for (const pin of PART_DEFS[part.type].pins) sel.append(h("option", { value: `${part.id}:${pin.id}` }, `${PART_DEFS[part.type].name} · ${pin.label}${pin.kind === "gpio" && pin.label !== String(pin.gpio) ? ` (GPIO ${pin.gpio})` : ""}`));
      if (prev) sel.value = prev;
    }
  };
  const el = h("div", { class: "vlist" },
    banner,
    h("div", { class: "row" }, h("h3", {}, "Checks"), h("span", { class: "muted", style: "font-size:11px" }, "wires that break a rule are refused; wires that disagree with the pin map are allowed but flagged")),
    list, h("h3", {}, "Is each part wired the way the firmware needs?"), status,
    h("h3", {}, "Wire by name (keyboard friendly)"),
    h("div", { class: "row" }, sel1, "to", sel2, h("button", { class: "small", onclick: () => { if (sel1.value && sel2.value) app.addWire(sel1.value, sel2.value); } }, "Add wire")),
    wires);

  return {
    el,
    update() {
      const a = app.analysis;
      const k = JSON.stringify([a.problems.map((p) => p.message), Object.values(a.parts).map((p) => [p.partId, p.ok, p.checks.map((c) => c.state + (c.detail ?? ""))]), app.wiring.wires.map((w) => w.id + w.a + w.b), app.sim?.hardware, app.needsRestart]);
      if (k === key) return;
      key = k;
      const need = app.needsRestart;
      banner.style.display = need ? "flex" : "none"; clear(banner);
      if (need) banner.append("Wiring changed while powered on. Real boards need a power cycle too.", h("button", { class: "primary small", onclick: () => void app.restart() }, "Restart Dewy"));
      clear(list);
      if (!a.problems.length) list.append(h("div", { class: "muted" }, app.wiring.wires.length ? "No warnings: every wire agrees with the firmware's pin map." : "No wires yet."));
      for (const p of a.problems) list.append(h("div", { class: "problem" }, h("span", { class: `sev-${p.severity}`, title: p.severity }, p.severity === "error" ? "✖" : p.severity === "warn" ? "⚠" : "ℹ"), h("span", {}, p.message)));
      clear(status);
      status.append(h("tr", {}, h("th", {}, "Part"), h("th", {}, "State"), h("th", {}, "What is missing")));
      for (const part of app.wiring.parts) {
        const ps = a.parts[part.id]; const def = PART_DEFS[part.type];
        if (def.virtual) continue;
        const bad = ps.checks.filter((c) => c.state !== "ok");
        const sim = app.sim?.hardware;
        const running = sim && (part.type === "display" ? sim.display : part.type === "imu" ? sim.imu : part.type === "mic" ? sim.mic : part.type === "amp" ? sim.amp : true);
        status.append(h("tr", {},
          h("td", {}, def.name, ps.unverified ? h("span", { class: "unverified", style: "margin-left:4px" }, "UNVERIFIED") : null),
          h("td", {}, h("span", { class: `pill ${ps.ok ? "ok" : "err"}` }, ps.ok ? "wired" : "not wired"), sim ? h("span", { class: `pill ${running ? "ok" : "warn"}`, style: "margin-left:4px", title: "what the running board saw at power-on" }, running ? "found" : "absent") : null),
          h("td", {}, bad.length ? bad.map((c) => `${c.label}: ${c.detail}`).join("; ") : "—")));
      }
      fillSelects();
      clear(wires);
      for (const w of app.wiring.wires) {
        const lab = (ref: string) => { const [pid, pin] = [ref.slice(0, ref.indexOf(":")), ref.slice(ref.indexOf(":") + 1)]; const part = app.wiring.parts.find((p) => p.id === pid); const d = part && PART_DEFS[part.type].pins.find((x) => x.id === pin); return part && d ? `${PART_DEFS[part.type].name} ${d.label}` : ref; };
        wires.append(h("div", { class: "row", style: "font-size:11px" }, h("span", { style: "flex:1" }, `${lab(w.a)} ↔ ${lab(w.b)}`), h("button", { class: "small danger", "aria-label": `Remove wire ${lab(w.a)} to ${lab(w.b)}`, onclick: () => app.removeWire(w.id) }, "✕")));
      }
      if (!app.wiring.wires.length) wires.append(h("div", { class: "muted", style: "font-size:11px" }, "No wires."));
    },
  };
}
