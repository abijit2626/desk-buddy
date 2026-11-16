# Dewy build log

What I worked on, what broke and how it got fixed. Newest at the bottom.

**Timeline:** idea in early November 2026 → prototype due 30 Nov (TinkerHub scholarship, first term) → finished
gift in December.

---

## Early November: the idea

After winning TinkerHub's Useless Projects hackathon, I joined their scholarship programme, with a project each
term. For the first one I chose a desk buddy: a small box with a face that reacts to you, shows reminders and
notes, and wakes up to its name. It's a gift for someone far away, so it had to run on batteries, work with an
iPhone and carry notes from me.

**Parts chosen:** ESP32-S3 N16R8, SmartElex 3.2" ILI9341 touch screen, GY-87 (MPU6050), INMP441 mic,
MAX98357A amp, a 3 W speaker, 2 × 18650 cells in a USB-C battery shield. The look is big pink two-tone eyes with
dark pupils, a small mouth, and a box body with a black visor.

## 10 Nov: firmware, cloud and the wake word

- **Firmware:** a face engine written from scratch in plain C++ (`face.h`), with 12 moods, blinking, glancing,
  breathing and 46 eye colour sets. The same code renders on a PC, so every face can be tested without hardware.
- **Cloud:** a Cloudflare Worker with a D1 database. It handles pairing by a code on the screen, settings, notes,
  timers and firmware updates, and the phone app is a home-screen web app.
- **Wake word:** "Hey Dewy" with microWakeWord, trained on my own RTX 4060 under WSL2. Round 1 used only
  synthetic voices and caught **1 of 10** real recordings. Getting the training to run took a string of fixes:
  library clashes, GPU memory, moved datasets.
- **Voice chat server:** speech to text, then a chat model with tools (weather, news, notes, timer), then spoken
  audio back. It runs on Cloudflare Workers AI, so it needs no paid account.

## 11 Nov: real voices, voice on the ESP32, a bug hunt

- **Wake word round 2:** trained with real recordings added. On held-out real clips it now catches **10 of 10**,
  with one false trigger in a noisy clip.
- **Desktop Dewy:** the whole voice loop running on a PC. Cloudflare blocked Python's default User-Agent
  (error 1010), so clients now send their own.
- Chose Dewy's voice: Aura "luna", sped up 1.15× to sound cuter.
- **Voice on the ESP32**, in three stages: a mic check, push-to-talk, then the on-device wake word with
  TensorFlow Lite Micro. The on-device features match the PC's exactly.
- A review found 18 bugs: a watchdog reboot, lost question starts, two HTTPS connections at once, and
  face/tap conflicts. Most are fixed, with PC tests added.

## 12 Nov: bond and alarms

- **Bond levels:** stranger → friend → close friend → best friend → soulmate. Dewy celebrates each level,
  gives bigger welcomes when you're closer, misses you after a day alone, and fades slowly (never below half).
- **On/off switch in the app:** "off" lets Dewy rest, but alarms still ring.
- **Alarms** beep louder and louder, and answer to "snooze" or "stop" while ringing. Touch works too.

## 13–14 Nov: first real hardware, and making the face fast

The first screen tests were on a spare Waveshare ESP32-S3-ETH board.

- The face crashed at once: it needed far more than the default **8 KB** loop stack. It now gets 32 KB.
- With the frame in PSRAM, the eyes showed **stripes**. The fix was to draw the frame in strips in internal RAM
  and send each strip by DMA while drawing the next.
- The SPI screen was too slow for smooth animation, so I moved to the screen's **8-bit parallel** mode.
- Then the drawing itself. A frame took **141.6 ms** (7 fps). Over many rounds, measured with a `prof` command on
  the board:
  - eyes drawn as solid runs instead of pixel by pixel,
  - integer colour blending, and cheap min/max instead of library calls,
  - edge maths only near the outlines,
  - text clipped to each strip.

  That took it to **57 ms**, then about **23 ms** for the sleeping face. The ESP32-S3 has no hardware float division or square root,
  so every one avoided per pixel counts.

## 16 Nov: the main board

- The screen stayed **dark** on the new N16R8 board. It turned out to have two causes:
  1. One mode pin (IM1) was wired to 5 V instead of GND. The screen survived.
  2. The board's "5Vin" pin gives **no power over USB** until two tiny **IN-OUT** pads are soldered together.
     Testing with only power wired, then on the other board's VBUS, narrowed it down.
- **Speed on the main board:** about **20 ms** for the plain face (50 fps). The sunny face (sunglasses) went from
  **35.9 → 20.4 ms** after a smarter rounded-corner routine. The 8-ball answer, one of the slowest screens, now takes
  **6.5 ms**, since there's no point drawing a face that's hidden behind the triangle. A `bench` command times every frame after
  any command.
- **Motion sensor:** shake, tilt, face down and upside down all work after a one-time calibration. **Pick-up** was
  reworked: it now needs 7° of tilt for 0.15 s from rest, and ignores taps and being put back down. A new test
  covers 11 movements at three sample rates.
- **Carrier PCB v0.1:** a 2-layer, 100 × 66 mm board that sits behind the screen inside the case, with every
  module soldered on and no jumper wires. It's routed by a small script and checked by KiCad (0 errors). Another
  script checks all 43 connections against the firmware's pin list.

## 17 Nov: online

- The cloud update went live: bond, on/off switch and alarms (100/100 tests first).
- Dewy couldn't register with the server: **"SSL - Memory allocation failed"**. Once Wi-Fi, the mic and the
  speaker were running, only 62 KB of internal RAM was left. Halving the screen strips to 24 rows freed
  **30 KB**, at about 1 ms per frame.
- Next came **watchdog restarts**. The always-on wake word and the HTTPS handshakes share one core, and the
  handshakes are seconds of maths with no hardware help for elliptic curves. The idle check now stops watching
  the idle tasks; the first attempt at this flooded the log with "task not found".
- **Dewy is online:** registered, syncing, and showing the weather.
- The manual is updated for the new features.

## Next

- [ ] Pair the phone, and find out why the alarm face didn't show
- [ ] Wire the mic and speaker (parts arrive 20 Nov): first spoken question, then wake word round 3 with Dewy's
      own mic
- [ ] Measure the screen board and finish the PCB, then order it
- [ ] Full-day test, and the prototype demo by 30 Nov
- [ ] December: solder onto the PCB, assemble the case, calibrate, and give the gift
