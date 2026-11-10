# Dewy's voice

- [`wakeword/`](wakeword/): training and testing the "Hey Dewy" model.
- [`desktop_dewy.py`](desktop_dewy.py): **Dewy on your PC.** The whole voice loop on your computer's mic and
  speakers, with no ESP32: wake word, recording, your server, the spoken answer. Use it to hear Dewy's voice, to
  try the wake word on your own mic, to time a round trip, and as a demo.
- The server side is in [`../cloud`](../cloud) (`POST /api/device/chat`).

## Try it (WSL)

```sh
sudo apt install -y libportaudio2 libasound2-plugins pulseaudio-utils ffmpeg
source ~/dewy-wakeword/venv/bin/activate
pip install sounddevice
export DEWY_URL=https://dewy.<your-name>.workers.dev
cd ~/desk-buddy/voice

python desktop_dewy.py --text "what day is it"        # no mic needed: the answer is saved as reply.wav
python desktop_dewy.py --push-to-talk                 # Enter, then speak: tests the mic, speakers and server
python desktop_dewy.py                                # say "Hey Dewy", then your question
```

The first run registers a device if `~/.dewy-register-key` exists, and saves the token in `~/.dewy-desktop-token`.
If WSL shows no microphone (`python desktop_dewy.py --list-devices`), `--text` and `--file` still work, and
the mic part can be tried on Windows instead.

`--debug` prints the wake word score whenever it passes 0.5. `--cutoff` changes how sure the model must be.
