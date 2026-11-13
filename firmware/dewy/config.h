// Dewy settings. Change things here, not in the other files.
#pragma once

// ---------------- pins (your wiring table) ----------------
#if DEWY_ETH_BOARD
// Test build for the spare Waveshare ESP32-S3-ETH. Its Ethernet chip is wired to GPIO 9-14 and the SD card to 4-7,
// so the screen moves to free header pins. Build with: --build-property "compiler.cpp.extra_flags=-DDEWY_ETH_BOARD=1"
#define PIN_TFT_MOSI    1   // also touch T_DIN
#define PIN_TFT_SCLK    2   // also touch T_CLK
#define PIN_TFT_MISO    3   // also touch T_DOUT
#define PIN_TFT_CS     39
#define PIN_TFT_DC     40
#define PIN_TFT_RST    41
#define PIN_TFT_BL     42
#define PIN_TOUCH_CS   47
#define PIN_TOUCH_IRQ  48
#define PIN_I2C_SDA    44   // motion sensor, if you test it on this board too
#define PIN_I2C_SCL    43
#else
#define PIN_TFT_MOSI   11   // also touch T_DIN
#define PIN_TFT_SCLK   12   // also touch T_CLK
#define PIN_TFT_MISO   13   // also touch T_DOUT
#define PIN_TFT_CS     10
#define PIN_TFT_DC      9
#define PIN_TFT_RST     8
#define PIN_TFT_BL      7   // BK_LIGHT, for dimming
#define PIN_TOUCH_CS   14
#define PIN_TOUCH_IRQ   6
#define PIN_I2C_SDA     4   // GY-87
#define PIN_I2C_SCL     5
#endif

// ---------------- screen ----------------
#define SCREEN_ROTATION 1          // 1 or 3. If the face is upside down, change it to the other one.
#ifndef SPI_FREQ
#define SPI_FREQ        40000000   // 40 MHz is safe on jumper wires. 80 MHz halves the time to send a frame
#endif                             // (try -DSPI_FREQ=80000000; back to 40 if the picture glitches)
#define BRIGHTNESS_AWAKE 255
#define BRIGHTNESS_ASLEEP 40       // screen dims while it sleeps

// ---------------- touch ----------------
#define TOUCH_ENABLED   1          // 0 in the Wokwi simulator (it has no XPT2046)
#define TAP_MAX_MS      600        // shorter than this = a poke
#define PET_HOLD_MS     800        // hold this long = petting

// ---------------- behaviour ----------------
#define SHOW_MOUTH      1
#define CAT_CHANCE_PCT  30         // pupil shape on each wake-up: cat slits (with a cat mouth)...
#define ACORN_CHANCE_PCT 20        // ...or tall acorn pupils. The rest (50%) are round.
#define ALARM_TIME      ""         // a daily alarm, like "07:00". "" = no alarm (the phone app will set alarms later)
#define STUDY_MINUTES   25         // length of a study-timer session

// ---------------- Wi-Fi ----------------
// The Wi-Fi name, Wi-Fi password and update password live in secrets.h, which git never uploads.
// Copy secrets.example.h to secrets.h and fill it in.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif
#define TIMEZONE        "IST-5:30" // India
#define OTA_HOSTNAME    "dewy"

// ---------------- weather (Open-Meteo: free, no account) ----------------
#define WEATHER_LAT     10.7867    // Palakkad. Change these to her city.
#define WEATHER_LON     76.6548

// ---------------- dates and notes ----------------
#define BIRTHDAY_MONTH  0          // her birthday, e.g. 3 and 14 for 14 March. 0 = off
#define BIRTHDAY_DAY    0
#define GIFT_YEAR       2026       // the buddy's own birthday: the day you give it to her.
#define GIFT_MONTH      12         // it celebrates every year after that
#define GIFT_DAY        0          // 0 = off
#define NOTE_FROM       "Abijit"   // note cards say "From Abijit"

// ---------------- reminders (the phone app will set these later) ----------------
#define REMIND_WATER_MIN   90      // minutes between water reminders, 0 = off
#define REMIND_STRETCH_MIN 120     // minutes between stretch reminders, 0 = off
#define REMIND_FROM_HOUR   9       // only between these hours
#define REMIND_TO_HOUR     21

// ---------------- motion ----------------
#define TILT_GAIN       2.4f       // how far the pupils roll for a given lean (full at about 25 degrees)
#define TILT_ON         0.30f      // leans smaller than this are ignored...
#define TILT_OFF        0.20f      // ...and it goes back to looking around below this
#define EIGHT_BALL_WINDOW_MS 4000  // tap, then shake within this time = magic 8-ball

// ---------------- battery (needs the two 100k resistors to GPIO 1) ----------------
#define BATTERY_ENABLED 0          // 1 once the resistors are fitted
#define PIN_BATTERY     1
#define BATTERY_EMPTY_MV 3300      // 0%
#define BATTERY_FULL_MV  4150      // 100%

// ---------------- personality ----------------
#define AFFECTION_FULL  300.0f     // points for a full bond (soulmate at 90%): a pet is 1, a poke 0.2, a pick-up 0.1
// the bond (see bond.h): fades slowly after days apart, and Dewy misses you after a long time alone
#define BOND_GRACE_DAYS     2         // days apart before the bond starts to fade
#define BOND_FADE_PER_DAY   0.03f     // then this share of a full bond a day (never below half of what it was)
#define BOND_MISS_AFTER_H   20        // hours alone before it starts to miss you
#define BOND_WELCOME_AFTER_MIN 30     // minutes alone before it greets you when you come back
#define ALARM_RING_MAX_MS  (10UL * 60UL * 1000UL)   // an alarm nobody answers stops by itself after this
#define ALARM_HOLD_STOP_MS 1500                      // hold a finger on the screen this long to stop the alarm
#define REST_AWAKE_MS       (20UL * 1000UL)   // switched off in the app: a touch wakes it this long

// 1 = quick timings for testing (bored after 20 s, asleep after 40 s).
// 0 = real timings for the gift (bored after 30 min, asleep after 60 min).
#define QUICK_TIMINGS   1
#if QUICK_TIMINGS
  #define BORED_AFTER_MS  (20UL * 1000UL)
  #define SLEEP_AFTER_MS  (40UL * 1000UL)
#else
  #define BORED_AFTER_MS  (30UL * 60UL * 1000UL)
  #define SLEEP_AFTER_MS  (60UL * 60UL * 1000UL)
#endif

#define FRAME_MS        33         // about 30 frames a second
#define FRAME_IN_PSRAM  1          // keep the 150 KB frame in PSRAM, leaving internal RAM for Wi-Fi and HTTPS.
                                   // 0 = internal RAM (faster drawing, but Wi-Fi setup and talking can run short)

// ---------------- cloud (the phone app's server; address and key are in secrets.h) ----------------
#define DEWY_VERSION    "1.0.0"    // bump this for every firmware update you publish
#define SYNC_AWAKE_MS   8000       // how often Dewy checks in with the server while awake...
#define SYNC_ASLEEP_MS  60000      // ...and while asleep
#define SETUP_HOTSPOT   1          // open the "Dewy-Setup" Wi-Fi when there's no Wi-Fi to join
#define SETUP_AP_NAME   "Dewy-Setup"
