// Level and silence logic for the virtual microphone.
//
// HONEST LABEL: this is NOT vad.h. firmware/dewy has no vad.h (and no voice code at all yet), so there is nothing
// to port. What this does instead:
//   * the numbers marked "from the Worker" are copied from cloud/src/chat.ts, the only voice logic in the repo:
//     a clip quieter than RMS 60 (16-bit scale) is treated as nobody speaking, under 0.4 s is a bump on the table,
//     over 12 s is refused;
//   * everything marked "page-side choice" (frame size, how long a pause ends the question) is made up here so that
//     the demo can stop recording. The real firmware will decide these itself.
import { SAMPLE_RATE, rms16, dbfs } from "./audio";

export const VAD_PARAMS = {
  /** from the Worker (chat.ts SILENCE_RMS): quieter than this, on the 16-bit scale, is "nobody speaking" */
  silenceRms: 60,
  /** from the Worker (chat.ts MIN_AUDIO_BYTES = 16000 * 2 * 0.4) */
  minClipMs: 400,
  /** from the Worker (chat.ts MAX_AUDIO_BYTES = 16000 * 2 * 12) */
  maxClipMs: 12000,
  /** page-side choice */
  frameMs: 20,
  /** page-side choice: this much quiet after speech ends the question */
  hangoverMs: 700,
  /** page-side choice: nothing said for this long after "listening" starts = give up */
  waitForSpeechMs: 5000,
  /** page-side choice: keep this much audio from before the first loud frame so the first word isn't clipped */
  preRollMs: 200,
} as const;

export const VAD_LABEL = "NOT vad.h: firmware/dewy has no vad.h yet. Thresholds copied from cloud/src/chat.ts (RMS 60, 0.4 to 12 s); the end-of-speech pause is a page-side choice.";

export type VadState = "waiting" | "speech" | "done";
export type VadReason = "silence" | "max" | "no-speech";

export interface VadFrame {
  state: VadState;
  rms: number;             // 16-bit scale
  dbfs: number;
  isSpeech: boolean;
  speechMs: number;        // loud frames so far
  quietMs: number;         // quiet stretch since the last loud frame (while in "speech")
  recordedMs: number;
  reason?: VadReason;
}

export interface VadClip {
  pcm: Uint8Array;         // what would be sent to /api/device/chat
  ms: number;
  speechMs: number;
  reason: VadReason;
  /** the Worker would answer 400 "too short" */
  tooShort: boolean;
  /** the Worker would answer "I didn't catch that" without calling any AI */
  nobody: boolean;
}

/** Feeds 20 ms frames of 16-bit mono PCM and decides when the question is over. */
export class Vad {
  private state: VadState = "waiting";
  private waited = 0; private speech = 0; private quiet = 0; private recorded = 0;
  private reason: VadReason | undefined;
  private ring: Uint8Array[] = [];           // quiet frames before speech (pre-roll)
  private kept: Uint8Array[] = [];           // frames from the first loud one on

  get isDone() { return this.state === "done"; }

  push(frame: Uint8Array): VadFrame {
    const level = rms16(frame);
    const loud = level >= VAD_PARAMS.silenceRms;
    if (this.state === "done") return this.report(level, loud);

    if (this.state === "waiting") {
      this.waited += VAD_PARAMS.frameMs;
      if (loud) {
        this.state = "speech";
        this.kept.push(...this.ring); this.ring = [];
        this.kept.push(frame); this.speech = VAD_PARAMS.frameMs; this.recorded = (this.kept.length) * VAD_PARAMS.frameMs;
      } else {
        this.ring.push(frame);
        if (this.ring.length > VAD_PARAMS.preRollMs / VAD_PARAMS.frameMs) this.ring.shift();
        if (this.waited >= VAD_PARAMS.waitForSpeechMs) { this.state = "done"; this.reason = "no-speech"; }
      }
    } else {
      this.kept.push(frame); this.recorded += VAD_PARAMS.frameMs;
      if (loud) { this.speech += VAD_PARAMS.frameMs; this.quiet = 0; }
      else this.quiet += VAD_PARAMS.frameMs;
      if (this.quiet >= VAD_PARAMS.hangoverMs) { this.state = "done"; this.reason = "silence"; }
      else if (this.recorded >= VAD_PARAMS.maxClipMs) { this.state = "done"; this.reason = "max"; }
    }
    return this.report(level, loud);
  }

  private report(level: number, loud: boolean): VadFrame {
    return { state: this.state, rms: level, dbfs: dbfs(level), isSpeech: loud, speechMs: this.speech, quietMs: this.quiet, recordedMs: this.recorded, reason: this.reason };
  }

  /** The clip recorded so far (valid once done; before that it is whatever has been kept). */
  clip(): VadClip {
    const total = this.kept.reduce((n, f) => n + f.length, 0);
    const pcm = new Uint8Array(total);
    let at = 0; for (const f of this.kept) { pcm.set(f, at); at += f.length; }
    const maxBytes = (SAMPLE_RATE * 2 * VAD_PARAMS.maxClipMs) / 1000;
    const capped = pcm.length > maxBytes ? pcm.subarray(0, maxBytes) : pcm;
    const ms = (capped.length / 2 / SAMPLE_RATE) * 1000;
    return {
      pcm: capped, ms, speechMs: this.speech, reason: this.reason ?? "silence",
      tooShort: capped.length < (SAMPLE_RATE * 2 * VAD_PARAMS.minClipMs) / 1000,
      nobody: rms16(capped) < VAD_PARAMS.silenceRms,
    };
  }
}

/** Splits samples into 20 ms frames of 16-bit PCM (the last partial frame is padded with silence). */
export function* frames16(pcm: Uint8Array): Generator<Uint8Array> {
  const size = (SAMPLE_RATE * VAD_PARAMS.frameMs) / 1000 * 2;
  for (let i = 0; i < pcm.length; i += size) {
    if (i + size <= pcm.length) yield pcm.subarray(i, i + size);
    else { const last = new Uint8Array(size); last.set(pcm.subarray(i)); yield last; }
  }
}

/**
 * Runs a whole clip through the VAD at once (used by the `mic` command and the tests). The microphone keeps
 * listening after the clip ends, so quiet frames follow until the VAD decides.
 */
export function runVad(pcm: Uint8Array): { clip: VadClip; frames: VadFrame[] } {
  const vad = new Vad();
  const out: VadFrame[] = [];
  for (const f of frames16(pcm)) { out.push(vad.push(f)); if (vad.isDone) break; }
  const quiet = new Uint8Array((SAMPLE_RATE * VAD_PARAMS.frameMs) / 1000 * 2);
  while (!vad.isDone) out.push(vad.push(quiet));
  return { clip: vad.clip(), frames: out };
}
