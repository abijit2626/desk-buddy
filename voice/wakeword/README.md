# "Hey Dewy" wake word

Dewy listens for **"Hey Dewy"** with [microWakeWord](https://github.com/kahrendt/microWakeWord): a tiny neural
network that runs on the ESP32-S3 itself, so nothing is sent anywhere until after the wake word.

The model is trained on computer-generated voices, so nobody has to record anything.

## Train it

You can train on your own NVIDIA PC (faster, no time limit) or in Google Colab (free, nothing to install).

### On your own PC (Windows with an NVIDIA GPU)

You need about 50 GB of free disk space and an up-to-date [NVIDIA driver](https://www.nvidia.com/drivers) installed on
Windows (the same driver also covers WSL).

1. Open PowerShell as administrator and run `wsl --install -d Ubuntu-24.04`. Restart if it asks, then open
   **Ubuntu** from the Start menu and choose a username and password.
2. Get this repo inside Ubuntu (not under `/mnt/c`, which is slow):
   ```sh
   git clone -b wake-word-hey-dewy https://github.com/abijit2626/desk-buddy ~/desk-buddy
   bash ~/desk-buddy/voice/wakeword/setup_wsl.sh
   ```
   It takes 15-30 minutes. At the end it says whether TensorFlow and PyTorch can see your GPU. If either says no, stop
   and tell me what it printed.
3. `bash ~/dewy-wakeword/start.sh`, then open the `http://localhost:8888/lab?token=...` link it prints in your
   Windows browser.
4. Open `train_hey_dewy.ipynb` and run the cells from the top. Skip the first cell if it says microWakeWord is already
   installed. Nothing needs restarting.
5. At the end, `hey_dewy_model.h`, `hey_dewy.json` and `stream_state_internal_quant.tflite` are in
   `~/dewy-wakeword`. To see them from Windows, run `explorer.exe ~/dewy-wakeword` in Ubuntu.

### In Google Colab

1. Open the notebook in Colab:
   [train_hey_dewy.ipynb](https://colab.research.google.com/github/abijit2626/desk-buddy/blob/main/voice/wakeword/train_hey_dewy.ipynb)
   (if it isn't on `main` yet, open it from this branch on GitHub and click "Open in Colab").
2. **Runtime > Change runtime type > T4 GPU.**
3. Run the cells top to bottom. Restart the session after the first one, as it says.

### Both routes

- Listen to the sample "Hey Dewy". If it sounds off, change `WAKE_SPELLINGS` and run that cell again.
- Training takes 1-2 hours on Colab's free GPU, and should be faster on your own.
- Colab downloads the three files at the end. On your PC they are saved in `~/dewy-wakeword`:
  - `hey_dewy_model.h`: the model as C code, for `firmware/dewy/`
  - `hey_dewy.json`: the settings it was trained with
  - `stream_state_internal_quant.tflite`: the raw model, in case we need it again

## Is the model good enough?

The last training step prints test results. Two numbers matter:

| Number | What it means | Aim for |
|---|---|---|
| Recall | How often it hears "Hey Dewy" | As high as possible (above 90%) |
| False accepts per hour | How often it wakes up by mistake on ordinary speech, TV and noise | Well under 1 |

A first model often misses one of these. That's normal. Change the cells marked **TRY** and train again:

| Problem | Try |
|---|---|
| Hard to wake | Lower `PROBABILITY_CUTOFF`, more `SAMPLES_PER_SPELLING`, more `training_steps` |
| Wakes up by mistake | Raise `negative_class_weight`, add the phrases that trigger it to `NEGATIVE_PHRASES` |
| Wakes on one particular word | Add that word to `NEGATIVE_PHRASES` |
| Doesn't hear her voice well | Add a spelling closer to how she says it |

Once it's on Dewy, test it for real: say it from across the room, with music or the TV on, and leave Dewy
listening during a long conversation to count false wake-ups.

## The trained model

The first model is in [`model/`](model/): `stream_state_internal_quant.tflite` (60 KB) and `hey_dewy.json` (its settings).
`python3 make_model_header.py` turns them into the C header for the firmware.

Test results of this first model (on the compressed model, with computer-made test voices, so real voices will do
worse): at cutoff 0.98 it hears about 82% of "Hey Dewy" with one false wake-up per 1.8 hours of dinner-party talk;
at 0.97, 84% and one per 53 minutes. It was trained without the extra AudioSet noise (that download failed, see
the notebook), so a second round with it should help. The real test is with the mic on Dewy.

## Next: on Dewy

The firmware side still has to be written, and gets built once the mic arrives. The ESP32 will:

1. Read the INMP441 mic at 16 kHz.
2. Turn every 10 ms of sound into 40 frequency features (the same "micro_speech" front end the model was
   trained with).
3. Run the model every 30 ms with TensorFlow Lite for Microcontrollers.
4. Average the last `HEY_DEWY_SLIDING_WINDOW` results. When the average passes `HEY_DEWY_PROBABILITY_CUTOFF`, Dewy
   switches to its listening face and starts recording the question for the AI chat.

## Credits

The voice generator is pinned to piper-sample-generator `v2.0.0`, because newer versions removed the script the notebook calls.
The notebook is adapted from microWakeWord's `basic_training_notebook.ipynb` by Kevin Ahrendt (Apache 2.0).
Voices come from [Piper sample generator](https://github.com/rhasspy/piper-sample-generator). The background
sound datasets it downloads have mixed licences, so the trained model is for personal, non-commercial use.


## Testing with your own voice

The model was tested on synthetic voices. `test_real_voice.py` runs the same steps as the firmware on real
recordings and tells you how often it hears you and how often it triggers by mistake, at several cutoffs.

```sh
sudo apt install ffmpeg                       # reads phone recordings (m4a, mp3, ...)
source ~/dewy-wakeword/venv/bin/activate      # or: pip install numpy pymicro-features ai-edge-litert
mkdir -p ~/real-test/yes ~/real-test/no
# put ~20 clips of you saying "Hey Dewy" in yes/, and other sound (talking, "hey siri", a song, a quiet room, a few minutes in total) in no/
python test_real_voice.py ~/real-test/yes ~/real-test/no
```

Sanity check of the script itself: run it with the synthetic clips from training as the `yes` folder
(`~/dewy-wakeword/generated_samples` or similar). Most should be heard. If they are not, the script is wrong, not the model.
