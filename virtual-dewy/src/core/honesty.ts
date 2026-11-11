// How much to trust each piece of the simulation. Shown as small badges all over the page.
export type Honesty = "real" | "approx" | "mock" | "none";

export const HONESTY: Record<Honesty, { label: string; title: string }> = {
  real: { label: "REAL", title: "Dewy's own firmware code, compiled unmodified and running in this page." },
  approx: { label: "APPROX", title: "A stand-in: it behaves roughly like the real thing but is not it. Check the note before trusting numbers or timing." },
  mock: { label: "MOCK", title: "A pretend service in this page that copies the real one's response shape. The real service is never called." },
  none: { label: "NOT SIMULATED", title: "Not simulated at all. Anything shown for it is a placeholder." },
};

/** One honest list of what is real and what is not, for the "What is real?" panel and the README. */
export const HONESTY_TABLE: { what: string; level: Honesty; note: string }[] = [
  { what: "Face drawing, moods, blinking, eye colours", level: "real", note: "face.h compiled unmodified. Frame rate is the browser's, not the SPI screen's." },
  { what: "Serial commands (help, 8ball, timer, note, wifi, ...)", level: "real", note: "dewy.ino's own handleCommand()." },
  { what: "Tap, poke, pet, bored and asleep timing", level: "real", note: "dewy.ino's pollTouch() and checkIdle(), with config.h's timings (QUICK_TIMINGS)." },
  { what: "Shake, pick-up, lean, face-down, upside-down", level: "real", note: "motion.h and dewy.ino's orientation code fed with the acceleration you set. No sensor noise or drift." },
  { what: "8-ball, timers, reminders, notes, birthdays", level: "real", note: "The firmware's own logic. The clock is virtual: it starts at the real time once Wi-Fi 'connects'." },
  { what: "The clock face's date line", level: "approx", note: "The small date under the time is drawn with an 18 pt font scaled down: the real 12 pt font isn't in the repo's test fonts. The big time digits are face.h's own." },
  { what: "Wi-Fi", level: "approx", note: "Dewy's pollWifi() and Dewy-Setup page are real; the radio is a pretend router." },
  { what: "Cloud registration, sync, pairing, weather", level: "real", note: "cloud.h's own functions, talking to the mock server below. Scheduling mirrors netTask() but is driven by the page (no second core)." },
  { what: "Cloud server (/api/device/*)", level: "mock", note: "Mock that copies the Worker's response shapes. Speech to text, the language model and the voice are scripted stand-ins." },
  { what: "Microphone audio and levels", level: "approx", note: "Generated or loaded in the page. The real INMP441's 24-bit samples, gain and noise are not modelled." },
  { what: "Level and silence logic (VAD)", level: "approx", note: "firmware/dewy has no vad.h. This copies the Worker's silence rule (RMS under 60 on the 16-bit scale, 0.4 to 12 s) plus a page-side end-of-speech timer. It is NOT vad.h." },
  { what: "Voice states (listen, think, talk, happy)", level: "approx", note: "The page drives the real face with the firmware's own listen / think / talk commands. The firmware has no voice code yet." },
  { what: "Wake word 'Hey Dewy'", level: "none", note: "The model in voice/wakeword is not run. The 'Hey Dewy' button just starts a question." },
  { what: "`ask`, `mic`, `ww debug` commands", level: "approx", note: "Virtual commands handled by the page (marked [virtual]). They do not exist in dewy.ino." },
  { what: "Speaker", level: "approx", note: "Shows the mock server's reply audio. Loudness, gain and distortion are not modelled." },
  { what: "Timing and speed", level: "approx", note: "WebAssembly in a browser, not an ESP32 at 240 MHz. Do not read performance or memory use from it." },
  { what: "Battery, OTA updates, brown-outs, flash wear", level: "none", note: "Not simulated (the battery is off in config.h)." },
];
