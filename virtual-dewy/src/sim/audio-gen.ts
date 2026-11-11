// Fake microphone input: sounds made up in the page. None of this is a recording of a real INMP441.
import { SAMPLE_RATE } from "./audio";

export type FakeSource = "speech" | "tone" | "noise" | "silence" | "bump" | "long";
export const FAKE_SOURCES: { id: FakeSource; label: string; hint: string }[] = [
  { id: "speech", label: "Speech-like (3 s)", hint: "voiced syllables at about 4 per second: loud enough to count as speaking" },
  { id: "tone", label: "Steady tone (2 s)", hint: "a 440 Hz sine" },
  { id: "noise", label: "Room noise (3 s)", hint: "quiet hiss below the silence threshold: counted as nobody speaking" },
  { id: "silence", label: "Silence (3 s)", hint: "all zeros" },
  { id: "bump", label: "Table bump (0.2 s)", hint: "a short thump: too short to be a question" },
  { id: "long", label: "Very long speech (14 s)", hint: "longer than the 12 s the server accepts" },
];

// small deterministic random numbers, so the same settings always sound (and measure) the same
function rng(seed: number) { let s = seed >>> 0 || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; }

/** `level` is the target RMS as a fraction of full scale (0..1). */
export function generate(kind: FakeSource, level: number, seed = 1): Float32Array {
  const r = rng(seed);
  const make = (seconds: number, fn: (t: number, i: number) => number) => {
    const n = Math.round(seconds * SAMPLE_RATE), out = new Float32Array(n);
    for (let i = 0; i < n; i++) out[i] = fn(i / SAMPLE_RATE, i);
    return out;
  };
  const normalise = (x: Float32Array, rms: number) => {
    let s = 0; for (const v of x) s += v * v;
    const cur = Math.sqrt(s / Math.max(1, x.length));
    if (cur > 0) { const g = rms / cur; for (let i = 0; i < x.length; i++) x[i] = Math.max(-1, Math.min(1, x[i] * g)); }
    return x;
  };
  switch (kind) {
    case "silence": return new Float32Array(3 * SAMPLE_RATE);
    case "tone": return normalise(make(2, (t) => Math.sin(2 * Math.PI * 440 * t)), level);
    case "noise": return normalise(make(3, () => r() * 2 - 1), Math.min(level, 0.0015));          // always below RMS 60 (60/32768 = 0.0018)
    case "bump": return normalise(make(0.2, (t) => Math.sin(2 * Math.PI * 70 * t) * Math.exp(-t * 25) + (r() - 0.5) * 0.2 * Math.exp(-t * 40)), level);
    case "speech":
    case "long": {
      const seconds = kind === "speech" ? 3 : 14;
      // a buzzing "voice" (about 130 Hz, a sawtooth) shaped by three resonances, switched on and off like syllables
      const f0 = 130, formants = [[700, 1], [1200, 0.6], [2500, 0.25]] as const;
      const x = make(seconds, (t) => {
        const syll = Math.max(0, Math.sin(2 * Math.PI * 4 * t + 0.5 * Math.sin(2 * Math.PI * 0.7 * t)));
        const word = (Math.sin(2 * Math.PI * 0.45 * t) > -0.7) ? 1 : 0;                              // a short pause now and then
        const saw = 2 * ((t * f0) % 1) - 1;
        let v = 0;
        for (const [f, a] of formants) v += a * saw * Math.sin(2 * Math.PI * f * t + 3 * saw);
        return v * syll * word + (r() - 0.5) * 0.02;
      });
      return normalise(x, level);
    }
  }
}

/** Slider position 0..100 to a level (fraction of full scale): exponential, so the middle is a normal speaking level. */
export const sliderToLevel = (pct: number) => Math.min(0.5, 0.001 * Math.pow(500, pct / 100));
