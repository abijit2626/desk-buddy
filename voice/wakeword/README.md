# "Hey Dewy" wake word

Dewy listens for **"Hey Dewy"** with [microWakeWord](https://github.com/kahrendt/microWakeWord): a tiny neural
network that runs on the ESP32-S3 itself, so nothing is sent anywhere until after the wake word.

The model is trained on computer-generated voices, so nobody has to record anything.

## Train it (Google Colab, free)

1. Open the notebook in Colab:
   [train_hey_dewy.ipynb](https://colab.research.google.com/github/abijit2626/desk-buddy/blob/main/voice/wakeword/train_hey_dewy.ipynb)
   (if it isn't on `main` yet, open it from this branch on GitHub and click "Open in Colab").
2. **Runtime > Change runtime type > T4 GPU.**
3. Run the cells top to bottom. Restart the session after the first one, as it says.
4. Listen to the sample "Hey Dewy". If it sounds off, change `WAKE_SPELLINGS` and run that cell again.
5. Wait for training (1-2 hours on the free GPU), then download the three files at the end:
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

## Next: on Dewy

The firmware side still has to be written, and gets built once the mic arrives. The ESP32 will:

1. Read the INMP441 mic at 16 kHz.
2. Turn every 10 ms of sound into 40 frequency features (the same "micro_speech" front end the model was
   trained with).
3. Run the model every 30 ms with TensorFlow Lite for Microcontrollers.
4. Average the last `HEY_DEWY_SLIDING_WINDOW` results. When the average passes `HEY_DEWY_PROBABILITY_CUTOFF`, Dewy
   switches to its listening face and starts recording the question for the AI chat.

## Credits

The notebook is adapted from microWakeWord's `basic_training_notebook.ipynb` by Kevin Ahrendt (Apache 2.0).
Voices come from [Piper sample generator](https://github.com/rhasspy/piper-sample-generator). The background
sound datasets it downloads have mixed licences, so the trained model is for personal, non-commercial use.
