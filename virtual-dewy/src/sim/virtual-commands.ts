// Serial commands that exist only in the simulator. The real dewy.ino doesn't know `ask`, `mic` or `ww`: the page
// catches them before they reach the firmware and prints every answer with a [virtual] prefix.
export type VirtualCommand =
  | { kind: "ask"; text: string }
  | { kind: "mic"; seconds: number }
  | { kind: "ww-debug"; on: boolean | null }
  | { kind: "ww-trigger" }
  | { kind: "ww-help" };

export const VIRTUAL_HELP = [
  "[virtual] Extra commands that exist only in this simulator (not in dewy.ino):",
  "[virtual]   ask <question>   type a question instead of speaking it (goes to the mock server)",
  "[virtual]   mic [seconds]    listen to the virtual microphone and print its level and the silence check",
  "[virtual]   ww debug [on|off]  show the wake word's settings and a live level readout (the wake word is NOT simulated)",
  "[virtual]   ww trigger       pretend the wake word fired: Dewy starts listening",
];

/** Recognises the simulator-only commands. Returns null for everything else (which goes to the firmware untouched). */
export function parseVirtualCommand(line: string): VirtualCommand | null {
  const t = line.trim();
  const lower = t.toLowerCase();
  const word = lower.split(/\s+/)[0];
  if (word === "ask") return { kind: "ask", text: t.slice(3).trim() };
  if (word === "mic") {
    const n = Number(lower.split(/\s+/)[1]);
    return { kind: "mic", seconds: Number.isFinite(n) && n > 0 ? Math.min(n, 12) : 1 };
  }
  if (word === "ww") {
    const [, sub, arg] = lower.split(/\s+/);
    if (sub === "debug") return { kind: "ww-debug", on: arg === "on" ? true : arg === "off" ? false : null };
    if (sub === "trigger") return { kind: "ww-trigger" };
    return { kind: "ww-help" };
  }
  return null;
}
