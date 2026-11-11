// Small audio helpers: 16-bit PCM and WAV files, the way Dewy's server talks (16 kHz, 16-bit, mono).
// Mirrors pcmToWav(), speedUp() and rms16() in cloud/src/chat.ts.

export const SAMPLE_RATE = 16000;

/** Root-mean-square level of 16-bit little-endian samples, on the 16-bit scale (0..32768). Same maths as the Worker's rms16(). */
export function rms16(pcm: Uint8Array): number {
  const n = pcm.length >> 1;
  if (!n) return 0;
  const v = new DataView(pcm.buffer, pcm.byteOffset, n * 2);
  let sum = 0;
  for (let i = 0; i < n; i++) { const s = v.getInt16(i * 2, true); sum += s * s; }
  return Math.sqrt(sum / n);
}

export function floatToPcm16(samples: Float32Array): Uint8Array {
  const out = new Uint8Array(samples.length * 2);
  const v = new DataView(out.buffer);
  for (let i = 0; i < samples.length; i++) {
    const s = Math.max(-1, Math.min(1, samples[i]));
    v.setInt16(i * 2, Math.round(s < 0 ? s * 32768 : s * 32767), true);
  }
  return out;
}
export function pcm16ToFloat(pcm: Uint8Array): Float32Array {
  const n = pcm.length >> 1;
  const v = new DataView(pcm.buffer, pcm.byteOffset, n * 2);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = v.getInt16(i * 2, true) / 32768;
  return out;
}

export function pcmToWav(pcm: Uint8Array, rate = SAMPLE_RATE): Uint8Array {
  const out = new Uint8Array(44 + pcm.length), v = new DataView(out.buffer);
  const tag = (o: number, s: string) => { for (let i = 0; i < 4; i++) out[o + i] = s.charCodeAt(i); };
  tag(0, "RIFF"); v.setUint32(4, 36 + pcm.length, true); tag(8, "WAVE"); tag(12, "fmt ");
  v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, rate, true); v.setUint32(28, rate * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
  tag(36, "data"); v.setUint32(40, pcm.length, true);
  out.set(pcm, 44);
  return out;
}

/** The Worker plays the same samples at a higher rate to make Dewy sound higher: only the header changes. */
export function speedUp(wav: Uint8Array, factor: number): Uint8Array {
  const tag = (o: number) => String.fromCharCode(wav[o], wav[o + 1], wav[o + 2], wav[o + 3]);
  if (wav.length < 44 || tag(0) !== "RIFF" || tag(12) !== "fmt ") return wav;
  const out = wav.slice(), v = new DataView(out.buffer);
  const rate = Math.round(v.getUint32(24, true) * factor);
  v.setUint32(24, rate, true);
  v.setUint32(28, rate * v.getUint16(32, true), true);
  return out;
}

export interface DecodedWav { rate: number; channels: number; samples: Float32Array /* first channel only */ }

/** Reads a WAV file: 16-bit PCM, 8-bit PCM, 24/32-bit PCM and 32-bit float, any rate. Throws a plain sentence on anything else. */
export function decodeWav(bytes: Uint8Array): DecodedWav {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (o: number) => (o + 4 <= bytes.length ? String.fromCharCode(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]) : "");
  if (bytes.length < 44 || tag(0) !== "RIFF" || tag(8) !== "WAVE") throw new Error("That isn't a WAV file.");
  let fmt: { format: number; channels: number; rate: number; bits: number } | null = null;
  let dataAt = -1, dataLen = 0;
  for (let o = 12; o + 8 <= bytes.length;) {
    const id = tag(o), len = v.getUint32(o + 4, true);
    if (id === "fmt " && o + 8 + 16 <= bytes.length) fmt = { format: v.getUint16(o + 8, true), channels: v.getUint16(o + 10, true), rate: v.getUint32(o + 12, true), bits: v.getUint16(o + 22, true) };
    if (id === "data") { dataAt = o + 8; dataLen = Math.min(len, bytes.length - dataAt); break; }       // the length field can be wrong (see cloud/README): trust the bytes
    o += 8 + len + (len & 1);
  }
  if (!fmt || dataAt < 0) throw new Error("That WAV file has no audio in it.");
  const { format, channels, rate, bits } = fmt;
  if (channels < 1 || channels > 8 || rate < 3000 || rate > 192000) throw new Error("That WAV file has an unusual format.");
  const bytesPer = bits >> 3;
  const frames = Math.floor(dataLen / (bytesPer * channels));
  const out = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const o = dataAt + i * bytesPer * channels;
    if (format === 1 && bits === 16) out[i] = v.getInt16(o, true) / 32768;
    else if (format === 1 && bits === 8) out[i] = (v.getUint8(o) - 128) / 128;
    else if (format === 1 && bits === 24) out[i] = ((v.getUint8(o) | (v.getUint8(o + 1) << 8) | (v.getInt8(o + 2) << 16)) / 8388608);
    else if (format === 1 && bits === 32) out[i] = v.getInt32(o, true) / 2147483648;
    else if (format === 3 && bits === 32) out[i] = v.getFloat32(o, true);
    else throw new Error(`That WAV file's format isn't supported (${bits}-bit, type ${format}). Use 16-bit PCM.`);
  }
  return { rate, channels, samples: out };
}

/** Linear-interpolation resampling to 16 kHz (good enough for a level meter and a mock; not studio quality). */
export function resampleTo16k(samples: Float32Array, rate: number): Float32Array {
  if (rate === SAMPLE_RATE) return samples;
  const n = Math.floor((samples.length * SAMPLE_RATE) / rate);
  const out = new Float32Array(n);
  const step = rate / SAMPLE_RATE;
  for (let i = 0; i < n; i++) {
    const p = i * step, i0 = Math.floor(p), f = p - i0;
    out[i] = (samples[i0] ?? 0) * (1 - f) + (samples[Math.min(i0 + 1, samples.length - 1)] ?? 0) * f;
  }
  return out;
}

/** Turns a loaded WAV into the 16 kHz mono samples a microphone would deliver. */
export function wavTo16kMono(bytes: Uint8Array): Float32Array {
  const d = decodeWav(bytes);
  return resampleTo16k(d.samples, d.rate);
}

export const dbfs = (level16: number) => (level16 <= 0 ? -Infinity : 20 * Math.log10(level16 / 32768));
