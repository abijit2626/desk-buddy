# Dewy firmware

The animated face for Dewy, the ESP32-S3 desk buddy, plus everything it does:

- 12 moods, blinking, looking around, breathing
- A new eye colour (46 sets, including the rare Kite and 16 odd-eyed sets) and a new pupil shape (round 50%, acorn 20%, cat 30%) on every wake-up
- Poke and pet on the touch screen; shake and pick-up with the GY-87
- **Lean it:** the pupils roll toward the low side (tip it back and it looks down at you)
- **Face down:** quiet time, asleep with the backlight off. **Upside down:** it says "Flip me back!"
- **Magic 8-ball:** tap, then shake within 4 seconds
- **Wi-Fi:** the real time, **weather in its eyes** (sunglasses, cloud, rain) and **updates over Wi-Fi**
- **Study timer**, **water and stretch reminders**, **notes**, **birthday party**, **a personality that grows**, **low-battery face**

```
dewy/                   open this in the Arduino IDE (the real hardware)
  dewy.ino              setup, loop, touch, motion, Wi-Fi, weather, calendar, serial test commands
  config.h              pins and settings: change things here
  secrets.example.h     copy to secrets.h for your Wi-Fi and update passwords (never uploaded)
  face.h                the face engine and drawing (plain C++)
  display.h             screen + touch setup (LovyanGFX)
  motion.h              shake and pick-up detection (MPU6050)
wokwi/                  the same code set up for the Wokwi simulator
test/                   PC tests: renders, motion and orientation checks, compile checks with stand-in libraries
```

## Try it now in the Wokwi simulator

1. Go to wokwi.com, start a new project and pick **ESP32-S3**.
2. Replace `sketch.ino` and `diagram.json` with the files from the `wokwi` folder.
3. Add the other files with the **+** button next to the file tabs: `config.h`, `face.h`, `display.h`, `motion.h`, `libraries.txt`.
4. Press play. The face appears on the screen after a few seconds. The Wokwi copy joins the simulator's own Wi-Fi, so the clock and weather work there too.
5. Click the serial monitor and type commands (press Enter after each). Type `help` for the full list. Some to try:
   - `8ball`, `weather sun` (or `cloud`, `rain`), `timer 1`, `remind water`, `remind stretch`
   - `note Good luck today!`, `bday`, `bday own`, `battery 15`, `battery 4`, `affection 0.8`
   - `tilt 1 0`, `tilt off`, `facedown on`, `facedown off`, `upside on`, `upside off`
   - `sleep` then `wake` a few times to see the eye colours change
6. Click the MPU6050 and drag its sliders quickly to test shake and pick-up.

The simulator draws slower than the real screen, and it has no touch chip, so use `poke` and `pet` there.

## Flash it to the real buddy (when the parts arrive)

1. Arduino IDE 2, then **Boards Manager**: install **esp32 by Espressif Systems**.
2. **Library Manager**: install **LovyanGFX**.
3. Open `dewy/dewy.ino`. Copy `secrets.example.h` to `secrets.h` in the same folder and fill it in (leave it empty to keep Wi-Fi off).
4. Tools menu, these settings matter:
   - Board: **ESP32S3 Dev Module**
   - Flash Size: **16MB (128Mb)**
   - Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)** (the code with Wi-Fi is too big for the default 1.2 MB, and this one leaves room for updates over Wi-Fi)
   - PSRAM: **OPI PSRAM** (your N16R8 has 8 MB octal PSRAM; the wrong setting makes it reboot over and over)
   - USB CDC On Boot: **Disabled** if you plug into the port marked **COM/UART**, **Enabled** if you use the one marked **USB**
5. Plug the board in, pick its port, press Upload.
6. Open the Serial Monitor at **115200** baud. It says whether it found the motion sensor. Type `help` for the commands.

## Settings in config.h

| Setting | What to put |
|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` (in secrets.h) | Her Wi-Fi. Empty = Wi-Fi off. |
| `TIMEZONE` | `IST-5:30` for India |
| `OTA_PASSWORD` (in secrets.h) | A password for updates over Wi-Fi. Updates stay off while it's empty. |
| `WEATHER_LAT`, `WEATHER_LON` | Her city (Palakkad by default). Look it up on a map: right-click a spot to copy its coordinates. |
| `BIRTHDAY_MONTH`, `BIRTHDAY_DAY` | Her birthday. 0 = off. |
| `GIFT_YEAR`, `GIFT_MONTH`, `GIFT_DAY` | The day you give it to her. It celebrates its own birthday on that date every year after. |
| `NOTE_FROM` | The name on note cards |
| `ALARM_TIME` | A daily alarm like `"07:00"`, or `""` for none |
| `STUDY_MINUTES` | Length of a study-timer session |
| `REMIND_WATER_MIN`, `REMIND_STRETCH_MIN` | Minutes between reminders (0 = off), only between `REMIND_FROM_HOUR` and `REMIND_TO_HOUR` |
| `BATTERY_ENABLED` | `1` once the two 100k resistors are on the perfboard |
| `QUICK_TIMINGS` | `0` before gifting |

**Passwords go in `secrets.h`, never in config.h.** The repo's `.gitignore` keeps `secrets.h` off GitHub; don't share it anywhere else either.

## One-time calibration (for leaning, face down and upside down)

Do this after everything is closed up in the case, because it learns which way the motion sensor is mounted.

1. Serial Monitor open, buddy standing upright on a level desk: type `cal up`.
2. Lay it on its face: type `cal face`.
3. Stand it up, type `axes`, and lean it a little to check the numbers move. Done: it's saved and survives restarts.

Redo it only if the GY-87 moves inside the case.

## Updates over Wi-Fi

1. Set `WIFI_SSID`, `WIFI_PASSWORD` and `OTA_PASSWORD` in secrets.h, and upload once over USB.
2. From then on, with your laptop on the same Wi-Fi, the buddy shows up in **Tools > Port** as a network port called **dewy**. Pick it, press Upload, and type the update password when asked.
3. The buddy shows "Learning a new trick..." with a progress bar, then restarts.

Updates over Wi-Fi only work on the same Wi-Fi network, so do them when you're at her place (or before you give it). Updates from anywhere come with the phone app later.

## If something looks wrong

| What you see | Fix |
|---|---|
| Face upside down | In `config.h` set `SCREEN_ROTATION` to `3` (or back to `1`) |
| White or blank screen | Check CS 10, DC 9, RST 8, MOSI 11, SCK 12, VCC and GND. Set IM1 and IM2 to 3.3V and IM0 to GND for SPI mode |
| Colours look inverted | In `display.h` set `cfg.invert = true` |
| Red and blue swapped | In `display.h` set `cfg.rgb_order = true` |
| Glitchy picture | Lower `SPI_FREQ` in `config.h` to `27000000` (long jumper wires) |
| "Motion sensor not found" | GY-87: SDA 4, SCL 5, VCC, GND. The face still runs without it |
| Taps don't register | Check T_CS 14 and T_IRQ 6, and that touch shares MOSI/MISO/SCK with the screen |
| Shake or pick-up too easy / too hard | In `motion.h`, change `0.7f` (shake strength) or `0.18f` / `0.95f` (pick-up) |
| "Sketch too big" when uploading | Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)** |
| Eyes always look to one side | Redo `cal up` on a level desk |
| Leaning, face down or upside down do nothing | It isn't calibrated yet: `cal up`, then `cal face` |
| No weather, or the wrong time | Type `wifi` to see what's connected. The weather can take a minute to arrive after Wi-Fi connects |
| No **dewy** port in the IDE | Same Wi-Fi as the laptop, `OTA_PASSWORD` set, then restart the IDE |
| Battery percent looks wrong | Check the two resistors with a multimeter (about 100 kΩ each); adjust `BATTERY_EMPTY_MV` / `BATTERY_FULL_MV` |

## Before it becomes a gift

- In `config.h` set `QUICK_TIMINGS` to `0`, so it gets bored after 30 minutes instead of 20 seconds.
- Fill in her Wi-Fi, her city, her birthday and the gift date, then calibrate it in its case.
- Set `BATTERY_ENABLED` to `1` once the resistors are fitted.
- Eye colours and their chances are the `PALETTES` table at the top of `face.h`. The last number on each
  line is its chance per thousand (29 = 2.9%); add or change lines freely. They don't have to add up to
  exactly 1000: the code works out the odds from whatever is there.
- Starting the study timer, setting reminders and alarms, and sending notes from anywhere will come with the phone app. Until then, use the settings above and the Serial Monitor commands.
