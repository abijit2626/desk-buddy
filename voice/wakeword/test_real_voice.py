#!/usr/bin/env python3
"""Try the Hey Dewy model on real recordings, the way the firmware will run it.

The model was tested on synthetic voices only. This answers: does it hear YOU?

  1. Record yourself (phone voice memo or Windows Voice Recorder, any format ffmpeg reads):
       yes/   about 20 clips of "Hey Dewy", one per file, a second or two each, in different tones and distances
       no/    other things: normal talking, "hey siri", "hey buddy", "do we", a song, a TV, a quiet room.
              Longer is better here (a few minutes in total), it is used to count false triggers.
  2. Run:    python test_real_voice.py yes no

It runs the same steps as the ESP32: 10 ms audio features, the streaming model every 30 ms, then the average of the
last few answers compared with a cutoff. It also prints what would happen at other cutoffs, to help choose one.
Needs:  pip install numpy pymicro-features ai-edge-litert      and ffmpeg for anything that is not a 16 kHz mono WAV.
"""
import argparse, json, subprocess, sys
from pathlib import Path

import numpy as np
from ai_edge_litert.interpreter import Interpreter
from pymicro_features import MicroFrontend

HERE = Path(__file__).parent
AUDIO_EXT = {".wav", ".m4a", ".mp3", ".ogg", ".opus", ".flac", ".aac", ".mp4", ".webm", ".amr", ".wma", ".3gp"}


def load_audio(path: Path) -> np.ndarray:
    """Any audio file -> 16 kHz mono 16-bit samples."""
    cmd = ["ffmpeg", "-v", "error", "-i", str(path), "-f", "s16le", "-ac", "1", "-ar", "16000", "-"]
    try:
        raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    except FileNotFoundError:
        sys.exit("ffmpeg is not installed. In WSL: sudo apt install ffmpeg")
    except subprocess.CalledProcessError as e:
        print(f"  skipped {path.name}: {e.stderr.decode(errors='replace').strip()[:100]}")
        return np.zeros(0, dtype=np.int16)
    return np.frombuffer(raw, dtype=np.int16)


def features(samples: np.ndarray) -> np.ndarray:
    """One row of 40 numbers every 10 ms (the same front end the firmware runs)."""
    fe, rows, data, i = MicroFrontend(), [], samples.tobytes(), 0
    while i + 320 < len(data):
        r = fe.process_samples(data[i:i + 320])
        i += r.samples_read * 2
        if r.features:
            rows.append(r.features)
    return np.array(rows).astype(np.float32)


def run_model(model_path: str, spectrogram: np.ndarray) -> np.ndarray:
    """The model's answer (0 to 1) for every 30 ms step. A fresh model per clip, so no memory carries over."""
    it = Interpreter(model_path=model_path)
    it.allocate_tensors()
    inp, out = it.get_input_details()[0], it.get_output_details()[0]
    for d in it.get_input_details():   # the streaming model keeps its state in extra inputs: start at zero
        it.set_tensor(d["index"], np.zeros(d["shape"], dtype=d["dtype"]))
    steps = inp["shape"][1]
    scale, zero = inp["quantization_parameters"]["scales"][0], inp["quantization_parameters"]["zero_points"][0]
    zp_out = out["quantization_parameters"]["zero_points"][0]
    probs = []
    for end in range(steps, len(spectrogram) + 1, steps):
        chunk = (spectrogram[end - steps:end] / scale + zero).astype(inp["dtype"])
        it.set_tensor(inp["index"], chunk.reshape(inp["shape"]))
        it.invoke()
        probs.append((float(it.get_tensor(out["index"])[0][0]) - zp_out) / 255.0)
    return np.array(probs)


def averaged(probs: np.ndarray, window: int) -> np.ndarray:
    if len(probs) < window:
        return np.zeros(0)
    return np.convolve(probs, np.ones(window) / window, mode="valid")


def triggers(avg: np.ndarray, cutoff: float, cooldown: int = 33) -> int:
    """How many separate detections (a new one only after ~1 s of quiet below the cutoff)."""
    count, last = 0, -cooldown
    for i, v in enumerate(avg):
        if v >= cutoff and i - last >= cooldown:
            count += 1
            last = i
        elif v >= cutoff:
            last = i
    return count


def clips_in(folder: str):
    files = sorted(p for p in Path(folder).rglob("*") if p.suffix.lower() in AUDIO_EXT)
    if not files:
        sys.exit(f"No audio files found in {folder}")
    return files


def main():
    cfg = json.loads((HERE / "model" / "hey_dewy.json").read_text())
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("yes", help="folder of clips where you say Hey Dewy")
    ap.add_argument("no", help="folder of clips of anything else")
    ap.add_argument("--model", default=str(HERE / "model" / "stream_state_internal_quant.tflite"))
    ap.add_argument("--cutoff", type=float, default=cfg["probability_cutoff"])
    ap.add_argument("--window", type=int, default=cfg["sliding_window_size"])
    a = ap.parse_args()

    results = {"yes": [], "no": []}
    for kind, folder in (("yes", a.yes), ("no", a.no)):
        print(f"\n{kind.upper()} clips ({folder})")
        for f in clips_in(folder):
            x = load_audio(f)
            if len(x) < 8000:
                continue
            avg = averaged(run_model(a.model, features(x)), a.window)
            peak = float(avg.max()) if len(avg) else 0.0
            seconds = len(x) / 16000
            n = triggers(avg, a.cutoff)
            results[kind].append((f.name, peak, seconds, avg))
            note = ("HEARD" if n else "missed") if kind == "yes" else (f"{n} false trigger(s)" if n else "ok")
            print(f"  {f.name[:40]:40s} {seconds:6.1f}s  peak {peak:.2f}  {note}")

    ys, ns = results["yes"], results["no"]
    hours = sum(r[2] for r in ns) / 3600
    print(f"\nWith the cutoff {a.cutoff}, averaging {a.window} answers:")
    print(f"  Hey Dewy heard in {sum(r[1] >= a.cutoff for r in ys)} of {len(ys)} clips")
    print(f"  false triggers: {sum(triggers(r[3], a.cutoff) for r in ns)} in {hours * 60:.1f} minutes of other sound")
    print("\nOther cutoffs (lower hears you more often but also triggers by mistake):")
    print("  cutoff   heard      false triggers per hour of other sound")
    for c in (0.5, 0.7, 0.8, 0.9, 0.95, 0.98, 0.99):
        heard = sum(r[1] >= c for r in ys)
        fa = sum(triggers(r[3], c) for r in ns)
        per_hour = f"{fa / hours:6.1f}" if hours > 0.01 else "   n/a (record more 'no' audio)"
        print(f"  {c:.2f}     {heard:2d}/{len(ys):<3d}     {per_hour}")
    print("\nAbout 1 false trigger per hour or fewer is a good target. For a long recording of other sound, record at least 10 minutes.")


if __name__ == "__main__":
    main()
