// The page's state and the glue between wiring, firmware sources and the running simulation.
import { PART_DEFS, type PartType } from "../core/parts";
import { PIN_MAP, pinMapFromSources, type PinAssignment } from "../core/pinmap";
import { analyseSources, isSafeFileName, isSecretFileName, parseFirmwarePins, type SourceFile } from "../core/firmware-source";
import { analyze, canAddWire, hardwareFrom, makeRef, parseWiring, serializeWiring, type Analysis, type Hardware, type PinRef, type Wiring } from "../core/wiring";
import { emptyWiring, placePart, referenceWiring } from "../core/presets";
import { Simulator, newMicState, type HardwareState, type MicState } from "../sim/simulator";
import { wavTo16kMono, type DecodedWav } from "../sim/audio";
import type { VadFrame } from "../sim/vad";
import type { VoicePhase } from "../sim/voice";
import { b64, download, store, toast } from "../ui/dom";

export interface BuildInfo {
  builtAt: string; zig: string; gitRev: string; dewyVersion: string | null; wasmBytes: number; note: string;
  files: { name: string; lines: number; sha256: string }[]; combinedSha256: string;
}
export interface FolderInfo { folder: string; secretsPresent: boolean; skipped: { name: string; why: string }[] }
export type FileMatch = "match" | "edited" | "new" | "not-in-build";

export type ChangeKind = "wiring" | "files" | "power" | "options" | "selection";

const BASE = import.meta.env.BASE_URL;

export class App {
  wiring: Wiring = emptyWiring();
  files: SourceFile[] = [];
  originalFiles: SourceFile[] = [];
  folder: FolderInfo | null = null;
  folderError = "";
  build: BuildInfo | null = null;
  wasm: ArrayBuffer | null = null;
  wasmError = "";
  pinMap: PinAssignment[] = [...PIN_MAP];
  analysis!: Analysis;
  sim: Simulator | null = null;
  powering = false;
  options = { wifiSaved: false, preCalibrated: true };
  selectedWire: string | null = null;
  /** the virtual microphone's settings (kept here so they survive power cycles) */
  mic: MicState = newMicState();
  routerUp = true;
  phase: VoicePhase = "idle";
  voiceFrame: (VadFrame & { at: number }) | null = null;
  lastReply: { heard: string; reply: string; wav: DecodedWav | null; durationMs: number; startedAtMs: number } | null = null;
  /** the clip the VAD kept for the last question, for the Voice tab's picture */
  lastClipLevels: number[] = [];
  fileHashes = new Map<string, string>();
  private listeners = new Set<(k: ChangeKind) => void>();
  readonly replyListeners = new Set<() => void>();
  private lastFrame = 0;

  on(fn: (k: ChangeKind) => void) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
  emit(k: ChangeKind) { for (const fn of this.listeners) fn(k); }

  // ---------------------------------------------------------------- start-up
  async init() {
    const saved = store.get("wiring");
    const loaded = saved ? parseWiring(saved) : null;
    this.wiring = loaded?.wiring && loaded.wiring.parts.length ? loaded.wiring : referenceWiring(this.pinMap);
    const opts = store.get("options");
    if (opts) { try { const o = JSON.parse(opts); this.options = { wifiSaved: !!o.wifiSaved, preCalibrated: o.preCalibrated !== false }; } catch { /* keep defaults */ } }
    this.recompute();
    await Promise.all([this.loadWasm(), this.loadFirmwareFolder(), this.loadBuildInfo()]);
    await this.refreshHashes();
  }

  private async loadWasm() {
    try {
      const r = await fetch(`${BASE}dewy.wasm`);
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      this.wasm = await r.arrayBuffer();
    } catch (e) { this.wasmError = `dewy.wasm could not be loaded (${String(e)}). Run "npm run build:wasm".`; }
  }
  private async loadBuildInfo() {
    try { const r = await fetch(`${BASE}dewy.build.json`); if (r.ok) this.build = await r.json() as BuildInfo; } catch { /* the page still works without it */ }
  }

  /** Loads the .ino and .h files from the folder the dev server was started with (never secrets.h). */
  async loadFirmwareFolder(): Promise<boolean> {
    try {
      const r = await fetch("/__firmware/list", { cache: "no-store" });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const list = await r.json() as FolderInfo & { files: { name: string }[] };
      const files: SourceFile[] = [];
      for (const f of list.files) {
        const fr = await fetch(`/__firmware/file/${encodeURIComponent(f.name)}`, { cache: "no-store" });
        if (fr.ok) files.push({ name: f.name, text: await fr.text() });
      }
      this.folder = { folder: list.folder, secretsPresent: list.secretsPresent, skipped: list.skipped };
      this.folderError = "";
      this.originalFiles = files.map((f) => ({ ...f }));
      this.setFiles(files, true);
      return true;
    } catch (e) {
      this.folderError = "The firmware folder isn't available from this server (paste your code into the editor instead).";
      void e;
      return false;
    }
  }

  async refreshHashes() {
    const map = new Map<string, string>();
    for (const f of this.files) map.set(f.name, await sha256hex(f.text));
    this.fileHashes = map;
    this.emit("files");
  }

  // ---------------------------------------------------------------- code
  setFiles(files: SourceFile[], silent = false) {
    this.files = files;
    const fp = parseFirmwarePins(files);
    this.pinMap = files.length ? pinMapFromSources(fp) : [...PIN_MAP];
    this.recompute();
    if (!silent) { this.emit("files"); void this.refreshHashes(); }
  }
  addOrReplaceFile(name: string, text: string): string | null {
    if (isSecretFileName(name)) return "secrets.h is private: this page won't load it. Use secrets.example.h (the firmware falls back to it).";
    if (!isSafeFileName(name)) return "Use a plain file name ending in .ino or .h (no folders).";
    if (text.length > 2 * 1024 * 1024) return "That file is too large.";
    const rest = this.files.filter((f) => f.name !== name);
    this.setFiles([...rest, { name, text }].sort((a, b) => a.name.localeCompare(b.name)));
    return null;
  }
  removeFile(name: string) { this.setFiles(this.files.filter((f) => f.name !== name)); }
  resetFiles() { this.setFiles(this.originalFiles.map((f) => ({ ...f }))); }
  get sourceAnalysis() { return analyseSources(this.files); }

  fileMatch(name: string): FileMatch {
    if (!this.build) return "new";
    const b = this.build.files.find((f) => f.name === name);
    if (!b) return "new";
    return this.fileHashes.get(name) === b.sha256 ? "match" : "edited";
  }
  /** true when every file the running firmware was built from is here, unchanged */
  get matchesBuild(): boolean {
    if (!this.build || !this.files.length) return false;
    return this.build.files.every((b) => this.fileHashes.get(b.name) === b.sha256) && this.files.every((f) => this.build!.files.some((b) => b.name === f.name));
  }

  // ---------------------------------------------------------------- wiring
  recompute() {
    this.analysis = analyze(this.wiring, this.pinMap);
    if (this.selectedWire && !this.wiring.wires.some((w) => w.id === this.selectedWire)) this.selectedWire = null;
  }
  setWiring(w: Wiring) {
    this.wiring = w;
    this.recompute();
    store.set("wiring", serializeWiring(w));
    this.emit("wiring");
  }
  get hardware(): Hardware { return hardwareFrom(this.wiring, this.analysis); }
  get needsRestart(): boolean {
    if (!this.sim) return false;
    const h = this.hardware, s = this.sim.hardware;
    return h.display !== s.display || h.touch !== s.touch || h.imu !== s.imu || h.mic !== s.mic || h.amp !== s.amp;
  }

  addWire(a: PinRef, b: PinRef): boolean {
    const v = canAddWire(this.wiring, a, b);
    if (!v.ok) { toast(`Can't wire that: ${v.reason}`, "warn"); return false; }
    const id = `w${Math.max(0, ...this.wiring.wires.map((w) => Number(w.id.slice(1)) || 0)) + 1}`;
    this.setWiring({ ...this.wiring, wires: [...this.wiring.wires, { id, a, b }] });
    this.selectedWire = id;
    // tell the person straight away if the wire disagrees with the firmware's pin map
    const bad = this.analysis.problems.find((p) => p.wireId === id && p.severity !== "info");
    if (bad) toast(bad.message, "warn", 7000);
    return true;
  }
  removeWire(id: string) { this.setWiring({ ...this.wiring, wires: this.wiring.wires.filter((w) => w.id !== id) }); }
  movePart(id: string, x: number, y: number) {
    this.wiring = { ...this.wiring, parts: this.wiring.parts.map((p) => (p.id === id ? { ...p, x, y } : p)) };
  }
  commitMove() { store.set("wiring", serializeWiring(this.wiring)); }
  addPart(type: PartType, at?: { x: number; y: number }) { this.setWiring(placePart(this.wiring, type, at)); }
  removePart(id: string) {
    this.setWiring({
      parts: this.wiring.parts.filter((p) => p.id !== id),
      wires: this.wiring.wires.filter((w) => !w.a.startsWith(id + ":") && !w.b.startsWith(id + ":")),
    });
  }
  useReferenceWiring(voice = true) { this.setWiring(referenceWiring(this.pinMap, { voice })); toast("Loaded the reference wiring: every pin where the firmware's pin map puts it."); }
  clearWiring() { this.setWiring(emptyWiring()); }
  loadWiringText(text: string) {
    const r = parseWiring(text);
    if (!r.wiring) { toast(r.issues[0] ?? "That wiring file couldn't be read.", "error", 7000); return false; }
    this.setWiring(r.wiring);
    if (r.issues.length) toast(`Loaded with ${r.issues.length} problem${r.issues.length === 1 ? "" : "s"}: ${r.issues.slice(0, 3).join(" ")}`, "warn", 9000);
    else toast(`Loaded ${r.wiring.parts.length} parts and ${r.wiring.wires.length} wires.`);
    return true;
  }
  saveWiringFile() { download("dewy-wiring.json", serializeWiring(this.wiring)); }
  pinRef(partId: string, pin: string) { return makeRef(partId, pin); }
  partName(type: PartType) { return PART_DEFS[type].name; }

  // ---------------------------------------------------------------- power
  async powerOn() {
    if (this.sim || this.powering) return;
    if (!this.wasm) { toast(this.wasmError || "The firmware (dewy.wasm) isn't loaded yet.", "error"); return; }
    if (!this.hardware.boardPlaced) { toast("Drag the ESP32-S3 board onto the canvas first.", "warn"); return; }
    this.powering = true; this.emit("power");
    try {
      const flashText = store.get("flash");
      const prefs = flashText ? b64.dec(flashText) : null;
      const h = this.hardware;
      const sim = await Simulator.create({
        wasm: this.wasm, hardware: toState(h), epochMs: Date.now(), prefs,
        wifiSaved: this.options.wifiSaved, preCalibrated: this.options.preCalibrated,
      });
      sim.mic = this.mic;
      sim.routerUp = this.routerUp;
      sim.onFlash = (bytes) => { store.set("flash", b64.enc(bytes)); };
      sim.onRestart = () => this.emit("power");
      sim.onPhase = (p) => { this.phase = p; if (p === "listening") this.lastClipLevels = []; };
      sim.onVadFrame = (f, t) => { this.voiceFrame = { ...f, at: t }; this.lastClipLevels.push(f.rms); if (this.lastClipLevels.length > 600) this.lastClipLevels.shift(); };
      sim.onReply = (r) => { this.lastReply = { heard: r.heard ?? "", reply: r.reply ?? "", wav: r.wav ?? null, durationMs: r.durationMs, startedAtMs: sim.nowMs }; this.replyListeners.forEach((fn) => fn()); };
      this.sim = sim;
      this.lastFrame = performance.now();
    } catch (e) {
      toast(`Couldn't start the firmware: ${String(e)}`, "error", 8000);
    } finally { this.powering = false; this.emit("power"); }
  }
  powerOff() { if (!this.sim) return; this.sim.voice.cancel(); this.sim = null; this.phase = "idle"; this.emit("power"); }
  setRouter(up: boolean) { this.routerUp = up; this.sim?.setRouter(up); this.emit("options"); }
  /** Loads a WAV file as the microphone's input. Returns an error sentence, or null. */
  loadWav(bytes: Uint8Array, name: string): string | null {
    try { this.mic.wav = wavTo16kMono(bytes); this.mic.wavName = name.slice(0, 60); this.mic.source = "wav"; this.emit("options"); return null; }
    catch (e) { return e instanceof Error ? e.message : "That file couldn't be read."; }
  }
  async restart() {
    if (!this.sim) return;
    this.sim.setHardware(toState(this.hardware));
    await this.sim.restart("power cycle");
    this.emit("power");
  }
  /** Forgets the saved flash (Wi-Fi, calibration, affection): the next power-on is a brand new board. */
  eraseFlash() { store.del("flash"); toast("Flash erased. The next power-on starts like a brand new board."); }
  setOption<K extends keyof App["options"]>(k: K, v: App["options"][K]) { this.options[k] = v; store.set("options", JSON.stringify(this.options)); this.emit("options"); }
  get flashSaved() { return store.get("flash") !== null; }

  // ---------------------------------------------------------------- one animation frame
  frame(now: number) {
    const dt = this.lastFrame ? now - this.lastFrame : 16;
    this.lastFrame = now;
    this.sim?.advance(dt);
  }
}

export function toState(h: Hardware): HardwareState { return { display: h.display, touch: h.touch, imu: h.imu, mic: h.mic, amp: h.amp }; }

async function sha256hex(text: string): Promise<string> {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}
