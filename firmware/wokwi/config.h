// Dewy settings. Change things here, not in the other files.
#pragma once

// ---------------- pins (your wiring table) ----------------
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

// ---------------- screen ----------------
#define SCREEN_ROTATION 1          // 1 or 3. If the face is upside down, change it to the other one.
#define SPI_FREQ        40000000   // 40 MHz is safe on jumper wires
#define BRIGHTNESS_AWAKE 255
#define BRIGHTNESS_ASLEEP 40       // screen dims while it sleeps

// ---------------- touch ----------------
#define TOUCH_ENABLED   0          // the Wokwi simulator has no XPT2046 touch chip; use the "poke" and "pet" commands
#define TAP_MAX_MS      600        // shorter than this = a poke
#define PET_HOLD_MS     800        // hold this long = petting

// ---------------- behaviour ----------------
#define SHOW_MOUTH      1
#define CAT_CHANCE_PCT  30         // pupil shape on each wake-up: cat slits (with a cat mouth)...
#define ACORN_CHANCE_PCT 20        // ...or tall acorn pupils. The rest (50%) are round.
#define ALARM_TIME      ""         // a daily alarm, like "07:00". "" = no alarm (the phone app will set alarms later)
#define STUDY_MINUTES   25         // length of a study-timer session

// ---------------- Wi-Fi ----------------
#define WIFI_SSID       "Wokwi-GUEST"   // the simulator's own Wi-Fi (no password)
#define WIFI_PASSWORD   ""
#define OTA_PASSWORD    ""              // updates over Wi-Fi are off in the simulator
#define DEWY_SERVER        ""           // set to your server's address to try the phone app with the simulator
#define DEWY_REGISTER_KEY  ""
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
#define AFFECTION_FULL  300.0f     // points for "best friends": a pet is 1, a poke 0.2, a pick-up 0.1

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

// ---------------- cloud (the phone app's server; address and key are in secrets.h) ----------------
#define DEWY_VERSION    "1.0.0"    // bump this for every firmware update you publish
#define SYNC_AWAKE_MS   8000       // how often Dewy checks in with the server while awake...
#define SYNC_ASLEEP_MS  60000      // ...and while asleep
#define SETUP_HOTSPOT   0          // no phone can join the simulator's hotspot
#define SETUP_AP_NAME   "Dewy-Setup"
