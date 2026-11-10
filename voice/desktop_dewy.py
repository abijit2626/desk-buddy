#!/usr/bin/env python3
"""Dewy on your PC: the whole voice loop on your computer's mic and speakers, with no ESP32.

    Hey Dewy  ->  beep  ->  your question is recorded until you stop talking  ->  sent to your Dewy server
              ->  the spoken answer plays

It runs the same steps the ESP32 will: the Hey Dewy model listens to the mic in 30 ms steps, the average of the last
few answers is compared with a cutoff, then the question is uploaded as raw 16 kHz 16-bit audio to /api/device/chat.
Use it to hear Dewy's voice, to judge the wake word on your own mic and to time the whole round trip before the
hardware is here.

First time:   pip install numpy pymicro-features ai-edge-litert sounddevice
              (in WSL also: sudo apt install -y libportaudio2 libasound2-plugins pulseaudio-utils)
Run:          export DEWY_URL=https://dewy.<your-name>.workers.dev
              export DEWY_TOKEN=<a device token>            # or see "Token" below
              python desktop_dewy.py                        # say "Hey Dewy", then your question

Other ways to run it:
  --push-to-talk      press Enter instead of using the wake word (tests the server and the speakers)
  --text "hello"      send typed text and play the answer
  --file clip.m4a     send a recording (any format ffmpeg reads) and save the answer as reply.wav
  --list-devices      show the audio devices, then pick one with --input-device / --output-device
  --debug             print the wake word score whenever it is above 0.5

Token: --token, else the DEWY_TOKEN environment variable, else ~/.dewy-desktop-token, else it registers a new
device if the register key is in DEWY_REGISTER_KEY or ~/.dewy-register-key, and saves the token for next time.
"""
import argparse, collections, io, json, os, subprocess, sys, time, urllib.error, urllib.parse, urllib.request, wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).parent
MODEL_DIR = HERE / "wakeword" / "model"
RATE = 16000
FRAME = 480                 # 30 ms, one step of the wake word model
MAX_RECORD_S = 10.0         # the server accepts up to 12 s
START_WAIT_S = 3.0          # how long to wait for you to start talking after the beep
END_QUIET_S = 0.9           # this much quiet ends the question


# ---------------------------------------------------------------- the wake word, one 30 ms step at a time
class WakeWord:
    """The streaming Hey Dewy model, fed live audio. Same steps as test_real_voice.py, but one chunk at a time."""

    def __init__(self, model_path, window):
        from ai_edge_litert.interpreter import Interpreter
        from pymicro_features import MicroFrontend
        self.it = Interpreter(model_path=str(model_path))
        self.it.allocate_tensors()
        self.inp, self.out = self.it.get_input_details()[0], self.it.get_output_details()[0]
        for d in self.it.get_input_details():          # the model keeps its memory in extra inputs: start at zero
            self.it.set_tensor(d["index"], np.zeros(d["shape"], dtype=d["dtype"]))
        self.steps = self.inp["shape"][1]
        q = self.inp["quantization_parameters"]
        self.scale, self.zero = q["scales"][0], q["zero_points"][0]
        self.zp_out = self.out["quantization_parameters"]["zero_points"][0]
        self.fe, self.rows, self.spare = MicroFrontend(), [], b""
        self.recent = collections.deque(maxlen=window)

    def clear(self):
        self.recent.clear()
        self.rows, self.spare = [], b""

    def push(self, pcm: bytes) -> float:
        """Feed audio (16 kHz 16-bit mono bytes). Returns the highest average score reached in this call."""
        best, buf = 0.0, self.spare + pcm
        while len(buf) >= 320:                    # the front end takes at least 10 ms (160 samples) at a time
            r = self.fe.process_samples(buf)
            used = r.samples_read * 2
            if used == 0:
                break
            buf = buf[used:]
            if not r.features:
                continue
            self.rows.append(r.features)
            if len(self.rows) < self.steps:
                continue
            chunk = (np.array(self.rows, dtype=np.float32) / self.scale + self.zero).astype(self.inp["dtype"])
            self.rows = []
            self.it.set_tensor(self.inp["index"], chunk.reshape(self.inp["shape"]))
            self.it.invoke()
            self.recent.append((float(self.it.get_tensor(self.out["index"])[0][0]) - self.zp_out) / 255.0)
            if len(self.recent) == self.recent.maxlen:
                best = max(best, sum(self.recent) / len(self.recent))
        self.spare = buf
        return best


# ---------------------------------------------------------------- audio helpers
def rms16(pcm: bytes) -> float:
    x = np.frombuffer(pcm, dtype=np.int16).astype(np.float64)
    return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0


def parse_wav(data: bytes):
    """A WAV file from the server -> (samples, rate, channels). The data length in the header can be unreliable
    (streamed WAVs say 0 or 0xFFFFFFFF), so the data chunk is read to the end of the file when it doesn't fit."""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    pos, rate, channels, bits = 12, RATE, 1, 16
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], int.from_bytes(data[pos + 4:pos + 8], "little")
        body = pos + 8
        if tag == b"fmt ":
            channels = int.from_bytes(data[body + 2:body + 4], "little")
            rate = int.from_bytes(data[body + 4:body + 8], "little")
            bits = int.from_bytes(data[body + 14:body + 16], "little")
        elif tag == b"data":
            end = body + size if 0 < size <= len(data) - body else len(data)
            if bits != 16:
                raise ValueError(f"expected 16-bit audio, got {bits}-bit")
            raw = data[body:end]
            return np.frombuffer(raw[:len(raw) & ~1], dtype=np.int16), rate, channels
        pos = body + size + (size & 1)
    raise ValueError("no audio in the WAV file")


def tone(freq: float, seconds: float, rate: int = 22050):
    t = np.arange(int(rate * seconds)) / rate
    fade = np.minimum(1, np.minimum(t, seconds - t) / 0.01)
    return (6000 * np.sin(2 * np.pi * freq * t) * fade).astype(np.int16), rate


class Audio:
    """The PC's mic and speakers through sounddevice."""

    def __init__(self, input_device=None, output_device=None):
        try:
            import sounddevice as sd
        except (ImportError, OSError) as e:
            sys.exit(f"sounddevice is not working ({e}).\nInstall it with: pip install sounddevice\n"
                     "In WSL also: sudo apt install -y libportaudio2 libasound2-plugins pulseaudio-utils")
        self.sd, self.out_dev = sd, output_device
        self.stream = sd.RawInputStream(samplerate=RATE, channels=1, dtype="int16", blocksize=FRAME, device=input_device)
        self.stream.start()

    def read(self) -> bytes:
        data, overflowed = self.stream.read(FRAME)
        return bytes(data)

    def drain(self):
        """Throw away what the mic heard while Dewy was talking, so it can't wake itself up."""
        while self.stream.read_available >= FRAME:
            self.stream.read(FRAME)

    def play(self, samples, rate, channels=1):
        self.sd.play(samples.reshape(-1, channels), rate, device=self.out_dev)
        self.sd.wait()


# ---------------------------------------------------------------- recording a question
def record_question(audio: Audio, noise: float):
    """Records until you stop talking. Returns the audio, or None if you never started."""
    threshold = max(250.0, noise * 3)
    frames, started, quiet = [], False, 0.0
    while True:
        f = audio.read()
        frames.append(f)
        level, step = rms16(f), FRAME / RATE
        elapsed = len(frames) * step
        if level > threshold:
            started, quiet = True, 0.0
        elif started:
            quiet += step
        if started and quiet >= END_QUIET_S:
            break
        if not started and elapsed >= START_WAIT_S:
            return None
        if elapsed >= MAX_RECORD_S:
            break
    return b"".join(frames)


# ---------------------------------------------------------------- the server
def http(url, data, headers, timeout=45):
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def get_token(args) -> str:
    token_file, key_file = Path.home() / ".dewy-desktop-token", Path.home() / ".dewy-register-key"
    token = args.token or os.environ.get("DEWY_TOKEN") or (token_file.read_text().strip() if token_file.exists() else "")
    if token:
        return token
    key = os.environ.get("DEWY_REGISTER_KEY") or (key_file.read_text().strip() if key_file.exists() else "")
    if not key:
        sys.exit("No device token. Pass --token, set DEWY_TOKEN, or put your register key in ~/.dewy-register-key "
                 "so this can register itself.")
    status, _, body = http(args.url + "/api/device/register", b"{}", {"content-type": "application/json", "x-register-key": key})
    if status not in (200, 201):
        sys.exit(f"Registering failed ({status}): {body[:200].decode(errors='replace')}")
    token = json.loads(body)["token"]
    token_file.write_text(token)
    token_file.chmod(0o600)
    print(f"Registered a new device. Token saved in {token_file}")
    return token


def ask(args, token, pcm=None, text=None):
    """Sends a question. Returns (heard, reply, (samples, rate, channels) or None, seconds)."""
    if pcm is not None:
        data, ctype = pcm, "audio/pcm"
    else:
        data, ctype = json.dumps({"text": text}).encode(), "application/json"
    t0 = time.time()
    status, headers, body = http(args.url + "/api/device/chat", data, {"authorization": "Bearer " + token, "content-type": ctype})
    took = time.time() - t0
    headers = {k.lower(): v for k, v in headers.items()}
    if headers.get("content-type", "").startswith("audio/wav"):
        return (urllib.parse.unquote(headers.get("x-dewy-heard", "")), urllib.parse.unquote(headers.get("x-dewy-reply", "")),
                parse_wav(body), took)
    try:
        j = json.loads(body)
    except ValueError:
        j = {"error": body[:200].decode(errors="replace")}
    if status != 200:
        print(f"  server said ({status}): {j.get('error')}")
        return None, j.get("error"), None, took
    return j.get("heard"), j.get("reply"), None, took      # voice budget used up: the words only


def show(heard, reply, took):
    if heard is None:                      # the server refused: the reason was already printed
        return
    print(f"  you:  {heard}\n  Dewy: {reply}\n  ({took:.1f} s from sending to getting the answer)")


def load_file(path: str) -> bytes:
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-ac", "1", "-ar", str(RATE), "-"]
    try:
        return subprocess.run(cmd, capture_output=True, check=True).stdout
    except FileNotFoundError:
        sys.exit("ffmpeg is not installed. In WSL: sudo apt install ffmpeg")
    except subprocess.CalledProcessError as e:
        sys.exit("could not read the file: " + e.stderr.decode(errors="replace")[:200])


def save_wav(path, samples, rate, channels):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels); w.setsampwidth(2); w.setframerate(rate); w.writeframes(samples.tobytes())


# ---------------------------------------------------------------- the main loops
def main():
    cfg = json.loads((MODEL_DIR / "hey_dewy.json").read_text())
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=os.environ.get("DEWY_URL", ""), help="your server, e.g. https://dewy.name.workers.dev")
    ap.add_argument("--token", default="")
    ap.add_argument("--model", default=str(MODEL_DIR / "stream_state_internal_quant.tflite"))
    ap.add_argument("--cutoff", type=float, default=cfg["probability_cutoff"])
    ap.add_argument("--window", type=int, default=cfg["sliding_window_size"])
    ap.add_argument("--push-to-talk", action="store_true")
    ap.add_argument("--text")
    ap.add_argument("--file")
    ap.add_argument("--list-devices", action="store_true")
    ap.add_argument("--input-device")
    ap.add_argument("--output-device")
    ap.add_argument("--debug", action="store_true")
    args = ap.parse_args()
    args.url = args.url.rstrip("/")

    if args.list_devices:
        import sounddevice as sd
        print(sd.query_devices())
        return
    if not args.url:
        sys.exit("Say where your server is: --url https://dewy.<name>.workers.dev (or set DEWY_URL)")
    token = get_token(args)

    # headless: one question, answer saved to reply.wav (and played if there are speakers)
    if args.text or args.file:
        pcm = load_file(args.file) if args.file else None
        heard, reply, wav, took = ask(args, token, pcm=pcm, text=args.text)
        show(heard, reply, took)
        if wav:
            save_wav("reply.wav", *wav)
            print("  answer saved as reply.wav")
        return

    dev = lambda s: (int(s) if s and s.isdigit() else s) if s else None
    audio = Audio(dev(args.input_device), dev(args.output_device))
    noise_levels = collections.deque(maxlen=66)        # the last two seconds, to know how noisy the room is

    def converse():
        noise = float(np.percentile(noise_levels, 20)) if noise_levels else 100.0
        pcm = record_question(audio, noise)
        if pcm is None:
            print("  (didn't hear a question)")
            audio.play(*tone(330, 0.2))
            return
        print("  thinking...")
        heard, reply, wav, took = ask(args, token, pcm=pcm)
        show(heard, reply, took)
        if wav:
            audio.play(*wav)
        else:
            audio.play(*tone(330, 0.3))

    if args.push_to_talk:
        while True:
            input("\nPress Enter, then ask your question... ")
            audio.drain()
            audio.play(*tone(880, 0.12))
            audio.drain()
            converse()
        return

    ww = WakeWord(args.model, args.window)
    print(f"Say \"Hey Dewy\" (cutoff {args.cutoff}, average of {args.window}). Ctrl+C to stop.")
    last_wake = 0.0
    while True:
        f = audio.read()
        noise_levels.append(rms16(f))
        p = ww.push(f)
        if args.debug and p > 0.5:
            print(f"  score {p:.2f}")
        if p >= args.cutoff and time.time() - last_wake > 2.0:
            print(f"\n[Hey Dewy] score {p:.2f}")
            audio.play(*tone(880, 0.12))
            audio.drain()
            converse()
            audio.drain()
            ww.clear()
            last_wake = time.time()
            print("\nListening...")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nBye!")
