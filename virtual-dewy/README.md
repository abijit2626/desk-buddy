# Virtual Dewy

A local web app that runs **Dewy's real firmware logic in your browser**, with virtual hardware you can wire together
and watch. Open it, press **Power on**, and the face from `face.h` appears on a virtual screen, talking to a mock
cloud, a pretend router, a virtual motion sensor and a virtual microphone and speaker.

> Everything on the page is a **simulation**. Every part and panel carries a badge saying how much to trust it:
> **REAL** (Dewy's own code, unmodified), **APPROX** (a stand-in), **MOCK** (a pretend service) or **NOT SIMULATED**.
> Press **What is real?** on the page for the full list, or read [What is real](#what-is-real-and-what-is-not) below.

## Run it

```bash
cd virtual-dewy
npm install
npm run dev          # http://127.0.0.1:5173
```

Then press **Power on**. The reference wiring is loaded for you: every pin where the firmware's pin map puts it.
Nothing needs building: `public/dewy.wasm` (the compiled firmware) is committed.

To read a different firmware folder, start the server with `DEWY_FIRMWARE_DIR=/path/to/dewy npm run dev`
(the page itself can never pick a folder). The default is `../firmware/dewy`.

## How it works

Dewy's `dewy.ino` and its headers are compiled **unmodified** to WebAssembly (`wasm/host.cpp` includes the real
sketch), against stand-in versions of the Arduino, LovyanGFX, Wire, Wi-Fi, HTTP and flash libraries (`wasm/host/`,
adapted from the stand-ins the repo's own PC tests already use). The page feeds it virtual hardware:

| Virtual hardware | What the firmware sees |
|---|---|
| Screen | `pushImage()` frames, drawn on a canvas. Backlight level follows the firmware. |
| Touch | `getTouch()` answers from your clicks. The firmware's own tap, poke and pet timing decides what it was. |
| Motion sensor | An MPU6050 at I2C address 0x68. Registers hold the acceleration you set. motion.h detects shakes and pick-ups itself. |
| Wi-Fi | A pretend router called `Virtual-Home` (password `dewy1234`, made up). |
| Internet | A mock server in the page answers `https://dewy.virtual.invalid`, an address that cannot exist. |
| Flash | Saved in your browser's localStorage so settings survive a restart. |

If a part is wired wrongly, the firmware really does not see it: no motion sensor prints
`Motion sensor not found: check SDA 4, SCL 5...`, a badly wired display shows the white screen the README warns about.
Wiring changes need a power cycle (a banner offers **Restart Dewy**), as on a real board.

## The pin map

| GPIO | Signal | Where it comes from |
|---|---|---|
| 11 / 12 / 13 | SPI MOSI / SCLK / MISO (screen and touch share them) | `config.h` |
| 10 / 9 / 8 | screen CS / DC / RST | `config.h` |
| 7 | screen backlight | `config.h` |
| 14 / 6 | touch CS / IRQ | `config.h` |
| 4 / 5 | I2C SDA / SCL (GY-87, MPU6050) | `config.h` |
| 1 | battery sense (off: `BATTERY_ENABLED 0`) | `config.h` |
| 15 / 16 / 17 | INMP441 SCK / WS / SD | **the project brief only: UNVERIFIED** |
| 18 / 21 / 38 | MAX98357A BCLK / LRC / DIN | **the project brief only: UNVERIFIED** |

### Voice pins are unverified

`firmware/dewy` has no `voice.h`, no `vad.h` and no I2S code yet (`voice/wakeword/README.md` says the firmware side
"still has to be written"). The microphone and amplifier pins therefore come from the brief and can't be checked
against code. They are marked **UNVERIFIED** on the page, and `test/firmware-pins.test.ts` reports them as such. When a
`voice.h` appears the same test starts checking those numbers against it.

For the same reason, the voice loop is the page's own stand-in (see below), and `ask`, `mic` and `ww debug` are
**simulator-only** serial commands, printed with a `[virtual]` prefix. They do not exist in `dewy.ino`.

## The panels

- **Parts**: drag a part onto the canvas (or press Add). One of each: ESP32-S3 DevKitC-1 (every header pin labelled),
  3.2" ILI9341 + XPT2046 module (screen and touch are one board in your hardware, so one part), GY-87 IMU, INMP441 mic,
  MAX98357A amp and speaker, a Wi-Fi indicator and a cloud-link indicator.
- **Canvas**: drag from a pin to another pin to wire them. Or click one pin and then another (also works with the keyboard:
  Tab to a pin, Enter). Valid targets light up green while you drag. Click a wire and press Delete to remove it. Hover a
  pin to light up its wires. Zoom with the buttons.
- **Wiring rules**: a wire that breaks a physical rule is refused with a reason: GND to a supply, 3V3 to 5V, 5 V into a
  3.3 V-only part (the mic, the screen), a GPIO to a GPIO, SDA to a clock pin, a strap pin to a GPIO, a short that only
  appears through other wires. A wire that is *allowed* but disagrees with the firmware's pin map (SDA on the wrong GPIO,
  a firmware pin reused for something else) is kept and flagged. The board's own hazards are flagged too: strapping pins,
  USB, the octal PSRAM pins on N16R8, the pin that may share the RGB LED.
- **Save wiring / Load wiring**: JSON (`format: "virtual-dewy-wiring"`, `version: 1`). Loading is defensive: bad parts and
  wires are skipped with a message.
- **Code**: loads the `.ino` and `.h` files from the firmware folder (never `secrets.h`), shows which files the sketch
  includes and their line counts, and has an editor to paste code. Edits update the pin map and the wiring warnings; they
  do **not** change the running firmware, which is the prebuilt one (the panel says whether the code matches the build).
- **Serial**: the firmware's real serial output, with a command line. Try `help`, `8ball`, `timer 1`, `weather rain`,
  `note Hi`, `sleep`, `wake`, `wifi`.
- **Voice**: wake, listen, think, talk, happy (see below).
- **Cloud**: a pretend phone (pair with the code on Dewy's screen, send a note, start a timer), the mock weather, and a
  log of every request the firmware and voice loop made.

## The voice loop

Press **Hey Dewy (button)**, type `ww trigger`, or use `ask <question>`. The page:

1. sends the firmware's own `listen` command (the real face shows the listening eyes),
2. feeds frames from the virtual microphone to the level and silence logic (`src/sim/vad.ts`),
3. sends `think`, then POSTs to the mock `/api/device/chat` with the device token the real `cloud.h` registered with,
4. sends `talk` while the reply plays (the speaker card draws its waveform), then `reset` (happy).

The microphone can make speech-like audio, a tone, room noise, silence, a table bump or a 14 s monologue, or play a **WAV
file** you load (any rate, mixed down to 16 kHz mono). Speech to text is a **mock**: the transcript comes from a text box,
because the page can't recognise speech.

### The mock cloud

`src/sim/mock-server.ts` copies the shape of the Worker in `cloud/`: paths, status codes, headers
(`x-dewy-heard`, `x-dewy-reply`), the JSON fallback `{ heard, reply, audio: false }`, the error sentences, the 0.4 to
12 s audio window, the silence rule (RMS 60), the daily limits, the 5-minute memory, the five tools. The language model
is the Worker's own scripted test stand-in (keyword rules) and the voice is a made-up babble. **The live Worker is never
called**: the mock answers a host that cannot exist, and nothing in the page makes a network request to anywhere else.

`test/cloud-contract.test.ts` reads the real `cloud/src/*.ts` and `wrangler.toml` and fails if the constants drift, and it
imports the Worker's exported helpers (`localTimeText`, `headerText`, `speakable`, `parseHeadlines`, `cleanText`) and
checks the mock gives identical answers.

## What is real and what is not

| What | Level | Notes |
|---|---|---|
| Face drawing, moods, eye colours | REAL | `face.h`, unmodified. Frame rate is the browser's. |
| Serial commands | REAL | `dewy.ino`'s `handleCommand()`. |
| Tap, poke, pet, bored and asleep timing | REAL | `pollTouch()` and `checkIdle()` with `config.h`'s timings. |
| Shake, pick-up, lean, face down, upside down | REAL | `motion.h` and the orientation code, fed the numbers you set. No sensor noise or drift. |
| 8-ball, timers, reminders, notes, birthdays | REAL | The firmware's own logic. |
| Cloud register, sync, pairing, weather | REAL | `cloud.h`'s own functions. Scheduling mirrors `netTask()` but the page drives it (no second core). |
| Wi-Fi | APPROX | `pollWifi()` and the Dewy-Setup page are real; the radio is a pretend router. |
| Clock face's date line | APPROX | Drawn with the 18 pt font scaled down (the real 12 pt font isn't in the repo). |
| Cloud server | MOCK | Copies the Worker's shapes; speech to text, the model and the voice are stand-ins. |
| Microphone audio, levels | APPROX | Generated or loaded in the page. The INMP441's 24-bit samples, gain and noise aren't modelled. |
| Level and silence logic | APPROX | **Not `vad.h`** (it doesn't exist). The Worker's thresholds plus a page-side end-of-speech pause. |
| Voice states | APPROX | The page drives the real face with `listen`, `think`, `talk`, `reset`. |
| Wake word "Hey Dewy" | NOT SIMULATED | The model in `voice/wakeword` is not run. |
| Speaker | APPROX | Shows the mock server's reply audio. |
| Timing, speed, memory | APPROX | WebAssembly in a browser, not an ESP32. Don't read performance from it. |
| Battery, OTA updates, brown-outs | NOT SIMULATED | |

## Rebuilding the firmware

`public/dewy.wasm` is the firmware as it was when it was built (`public/dewy.build.json` lists the files and their
hashes; a test fails if `firmware/dewy` has changed since). Rebuild with:

```bash
npm run build:wasm
```

It needs `python3` and network access the first time: it installs a pinned `ziglang` into `.zig-venv` (or uses `zig` from
your PATH, or `$ZIG`) and fetches ArduinoJson (the version `firmware/test/run_tests.sh` uses) into `.deps`. It takes about
a minute.

**secrets.h is never read.** The build copies `firmware/dewy/*.ino` and `*.h` to `.build/src` *without* `secrets.h`, and
adds a made-up one (`wasm/virtual_secrets.h`: an empty Wi-Fi, the server `https://dewy.virtual.invalid` and a made-up
register key). The firmware falls back to those values, so your real Wi-Fi password and server address can never end up in
`dewy.wasm`. A test checks both the copy step and the finished binary.

## Tests

```bash
npm test          # about a minute: most of it is the real firmware running through virtual minutes
npm run check     # TypeScript
```

| File | What it checks |
|---|---|
| `pinmap.test.ts` | the pin map is consistent: real header GPIOs, no clashes, voice pins marked unverified |
| `wiring.test.ts` | which pairs of pins may be wired, shorts through other wires, warnings against the pin map, parts reported wired or not, saving and loading JSON |
| `firmware-pins.test.ts` | loads `dewy.ino` and its includes, and checks the pin numbers match what the wiring panel uses: `config.h`, `display.h`'s driver setup, `Wire.begin`, and the pins the *compiled firmware reports at run time* |
| `cloud-contract.test.ts` | the mock server against the real Worker's source and exports |
| `mock-server.test.ts` | the mock's behaviour: register, sync, pairing, chat, limits, errors |
| `voice.test.ts` | fake audio, the level and silence logic, WAV reading, the voice loop, virtual commands, wake-word settings |
| `simulator.test.ts` | the real firmware end to end: serial commands, touch, motion, Wi-Fi, the setup page, cloud, restarts, voice |
| `firmware-loader.test.ts` | the dev-server route never reads `secrets.h` or anything outside the folder |
| `build-info.test.ts` | the committed wasm matches the firmware and contains no secrets |

## Safety notes

- The page makes no network requests except to its own dev server. The mock cloud never leaves the page.
- The firmware route (`/__firmware/`) is read-only, answers only this machine, and serves only plain `.ino`/`.h` files in
  one folder. `secrets.h`, symlinks, folders and anything over 2 MB are refused.
- A wiring file or WAV file you load is treated as untrusted: sizes, types and ranges are checked.
- Pasting a file named `secrets.h` is refused, and a file that looks like it holds a real password gets a warning.

## Folder

```
src/core/     pin map, parts, wiring rules, firmware source parsing (no DOM)
src/sim/      the simulator: wasm wrapper, mock server, voice loop, level and silence logic, virtual IMU
src/ui/       the page
src/app/      state and wiring between them
server/       the read-only firmware route for the dev server
wasm/         host.cpp, the stand-in hardware headers, build script, made-up secrets
public/       dewy.wasm and dewy.build.json (committed)
test/         the tests above
```
