# Dewy, the desk buddy

Dewy is a little desk robot with an animated face: an ESP32-S3, a 3.2" touch screen and a motion sensor
in a 3D-printed case. It blinks, gets bored, naps, wakes up with new eyes, reacts to pokes, pets, shakes
and tilts, and helps with study timers, reminders, the weather and notes.

| Folder | What's in it |
|---|---|
| [`firmware/`](firmware/) | The Arduino code for Dewy, a copy for the Wokwi simulator, and PC tests. Start with [firmware/README.md](firmware/README.md). |
| [`case/`](case/) | Printable STL files for the case (body, visor, back panel) and the scripts that generate them |
| [`docs/manual/`](docs/manual/) | The illustrated user manual (PDF) and its source |
| [`cloud/`](cloud/) | The Cloudflare server and the iPhone home-screen app (study timer, notes, alarms, reminders, settings). Start with [cloud/README.md](cloud/README.md). |

## Hardware

- ESP32-S3 DevKitC-1, N16R8 (16 MB flash, 8 MB PSRAM)
- 3.2" ILI9341 SPI screen with XPT2046 resistive touch
- GY-87 (MPU6050 accelerometer)
- INMP441 microphone, MAX98357A amplifier, 3 W speaker
- 2 × 18650 cells in a USB-C battery shield

## Private settings

Wi-Fi and update passwords go in `firmware/dewy/secrets.h`, which is listed in `.gitignore` and never
uploaded. Copy `secrets.example.h` to make it.
