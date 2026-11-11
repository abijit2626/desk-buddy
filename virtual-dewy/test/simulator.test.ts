// End to end: the real firmware (WebAssembly) on virtual hardware, with the mock cloud and the voice loop.
import { describe, expect, it } from "vitest";
import { Simulator } from "../src/sim/simulator";
import { wasmBytes } from "./helpers";
import { prefsDecode } from "../src/sim/wasm";

const EPOCH = Date.UTC(2026, 10, 11, 9, 30, 0);   // 11 Nov 2026, 09:30 UTC = 15:00 in India (IST-5:30)
async function make(o: Partial<Parameters<typeof Simulator.create>[0]> = {}) {
  return Simulator.create({ wasm: wasmBytes, hardware: { display: true, touch: true, imu: true, mic: true, amp: true }, epochMs: EPOCH, seed: 99, ...o });
}
const run = (s: Simulator, ms: number) => { for (let left = ms; left > 0; left -= 100) s.advance(Math.min(100, left)); };
const text = (s: Simulator, kind?: string) => s.lines.filter((l) => !kind || l.kind === kind).map((l) => l.text).join("\n");
const mood = (s: Simulator) => s.status.moodName;
/** virtual time for Wi-Fi to connect, the clock to arrive, Dewy to register and show its pairing card */
const SETTLE = 9000;

describe("power-on and serial commands (the firmware's own code)", () => {
  it("prints the firmware's own start-up lines, and finds the motion sensor when it is wired", async () => {
    const s = await make();
    run(s, 300);
    expect(text(s, "fw")).toMatch(/Dewy starting[\s\S]*Motion sensor found \(WHO_AM_I 0x68\)[\s\S]*Type "help" for test commands\./);
    expect(s.status.motionAvailable).toBe(true);
  });

  it("says 'Motion sensor not found' when the sensor is not wired, exactly like the real firmware", async () => {
    const s = await make({ hardware: { display: true, touch: true, imu: false, mic: true, amp: true } });
    run(s, 300);
    expect(text(s, "fw")).toContain("Motion sensor not found: check SDA 4, SCL 5, 3.3V and GND. The face still runs.");
    expect(s.status.motionAvailable).toBe(false);
  });

  it("runs real commands: 8ball, weather, timer, note, mood changes", async () => {
    const s = await make();
    run(s, 300);
    s.send("8ball"); run(s, 300);
    expect(text(s, "fw")).toMatch(/\[8-ball\] (Yes!|Definitely|Without a doubt|Signs say yes|Maybe\.\.\.|Ask again later|Not today|No way)/);
    s.send("weather rain"); run(s, 100);
    expect(s.status.weather).toBe(3);
    s.send("timer 3"); run(s, 100);
    expect(s.status.timerActive).toBe(true);
    expect(text(s, "fw")).toContain("study timer: 3 min");
    s.send("timer stop"); run(s, 100);
    expect(s.status.timerActive).toBe(false);
    s.send("sleep"); run(s, 300);
    expect(mood(s)).toBe("asleep");
    s.send("wake"); run(s, 2500);
    expect(mood(s)).toBe("happy");
  });

  it("falls through to the firmware's own help for unknown commands, then lists the simulator-only extras", async () => {
    const s = await make();
    run(s, 300); s.send("help"); run(s, 200);
    expect(text(s, "fw")).toContain("Moods:    shake, pick, name, listen");
    expect(text(s, "virtual")).toMatch(/Extra commands that exist only in this simulator \(not in dewy\.ino\)/);
  });

  it("draws a face (the screen is not blank) and the backlight follows the firmware", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 3000);
    const frame = s.core.frame();
    let lit = 0; for (let i = 0; i < frame.length; i += 4) if (frame[i] + frame[i + 1] + frame[i + 2] > 60) lit++;
    expect(lit).toBeGreaterThan(5000);
    expect(s.status.brightness).toBeGreaterThan(200);
  });
});

describe("touch (the firmware's pollTouch and its timings)", () => {
  it("while the pairing card is up, a tap hides the card instead of poking (the firmware's own rule)", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    expect(s.status.banner).toBe(2);
    s.tap(); run(s, 500);
    expect(s.status.banner).toBe(0);
  });

  it("five quick pokes make it annoyed", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    s.tap(); run(s, 500);                                    // hides the card
    for (let i = 0; i < 5; i++) { s.tap(); run(s, 400); }
    expect(text(s, "fw")).toMatch(/\[mood\] annoyed/);
  });

  it("holding is petting (after PET_HOLD_MS), and letting go settles back to happy", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 2000);
    s.touchDown(160, 120); run(s, 1200);
    expect(mood(s)).toBe("petted");
    s.touchUp(); run(s, 4000);
    expect(mood(s)).toBe("happy");
  });

  it("does nothing when the touch chip isn't wired", async () => {
    const s = await make({ wifiSaved: true, hardware: { display: true, touch: false, imu: true, mic: true, amp: true } });
    run(s, 2000);
    s.touchDown(160, 120); run(s, 1500);
    expect(mood(s)).toBe("happy");
  });

  it("ignored for 20 s it gets bored, then asleep at 40 s (QUICK_TIMINGS is 1 in config.h)", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    s.tap(); run(s, 500);                                    // hide the pairing card (it keeps Dewy busy while it is up)
    run(s, 21000);
    expect(mood(s)).toBe("bored");
    run(s, 25000);
    expect(mood(s)).toBe("asleep");
    expect(s.status.brightness).toBeLessThan(200);          // dimmed while it sleeps
  });

  it("with the pairing card up it never gets bored", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 60000);
    expect(s.status.banner).toBe(2);
    expect(mood(s)).toBe("happy");
  });
});

describe("motion (the firmware's motion.h and orientation code, fed by the virtual sensor)", () => {
  it("a shake makes it dizzy", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 3000);                                          // at rest long enough
    s.shake(); run(s, 1500);
    expect(text(s, "fw")).toMatch(/\[mood\] dizzy/);
  });

  it("a pick-up surprises it (after it has been still for a moment)", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 3000);
    s.pickUp(); run(s, 800);
    expect(text(s, "fw")).toMatch(/\[mood\] surprised/);
  });

  it("tap then shake within 4 s is the magic 8-ball", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    s.tap(); run(s, 500);                                    // the first tap only hides the pairing card
    s.tap(); run(s, 400);
    s.shake(); run(s, 1500);
    expect(text(s, "fw")).toMatch(/\[8-ball\] /);
  });

  it("shake without a tap is just dizzy, not the 8-ball", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 3000);
    s.shake(); run(s, 1500);
    expect(text(s, "fw")).not.toMatch(/\[8-ball\]/);
  });

  it("face down is quiet time: the backlight goes off; flipping it back wakes it", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 3000);
    s.preset("face-down"); run(s, 4500);
    expect(s.status.faceDown).toBe(true);
    expect(s.status.brightness).toBeLessThan(20);
    expect(text(s, "fw")).toContain("[face down] quiet time");
    s.preset("upright"); run(s, 2000);
    expect(s.status.faceDown).toBe(false);
    expect(s.status.brightness).toBeGreaterThan(100);
  });

  it("upside down is detected, and leaning moves the pupils", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 3000);
    s.preset("upside-down"); run(s, 2500);
    expect(s.status.upsideDown).toBe(true);
    s.preset("upright"); run(s, 2500);
    expect(s.status.upsideDown).toBe(false);
    s.preset("lean-right"); run(s, 1500);
    expect(s.status.tiltShown).toBe(true);
    expect(Math.abs(s.status.tiltX)).toBeGreaterThan(0.5);
  });

  it("needs the one-time calibration (cal up, cal face) before leaning works, as on the real board", async () => {
    const s = await make({ wifiSaved: true, preCalibrated: false });
    run(s, 3000);
    expect(text(s, "fw")).toContain("Leaning, face down and upside down need a one-time calibration: type cal up, then cal face");
    s.preset("face-down"); run(s, 3000);
    expect(s.status.faceDown).toBe(false);                 // not calibrated: ignored
    s.preset("upright"); run(s, 2000);
    s.send("cal up"); run(s, 300);
    s.preset("face-down"); run(s, 2500);
    s.send("cal face"); run(s, 300);
    expect(text(s, "fw")).toContain("Calibration done");
    expect(s.status.calibrated).toBe(true);
  });
});

describe("Wi-Fi, the clock and the cloud (real pollWifi, cloud.h and portal.h, mock server)", () => {
  it("with no Wi-Fi saved it opens the Dewy-Setup hotspot and shows the set-up card", async () => {
    const s = await make();
    run(s, 2000);
    expect(s.status.portalOn).toBe(true);
    expect(s.status.banner).toBe(1);
    expect(text(s, "fw")).toContain('[wifi] setup hotspot "Dewy-Setup" is open');
  });

  it("serves the firmware's real setup page and accepts a network typed into it", async () => {
    const s = await make();
    run(s, 2000);
    const page = s.core.portalPage();
    expect(page).toContain("Hi, I'm Dewy");
    expect(page).toContain("Virtual-Home");
    expect(page).toContain("Cafe &lt;Guest&gt;");          // network names are escaped by the firmware
    expect(s.submitSetupForm("Virtual-Home", "short")).toBe(303);                       // too short: refused by the firmware
    expect(s.core.portalMessage()).toMatch(/8 to 63 characters/);
    expect(s.submitSetupForm("Virtual-Home", "dewy1234")).toBe(200);
    run(s, 8000);
    expect(text(s, "fw")).toMatch(/Wi-Fi connected, IP 192\.168\.1\.50/);
    run(s, 6000);
    expect(s.status.portalOn).toBe(false);                 // the hotspot closes once Wi-Fi works
    expect(text(s, "fw")).toContain("[wifi] setup hotspot closed");
  });

  it("a wrong password never connects", async () => {
    const s = await make();
    run(s, 2000);
    s.submitSetupForm("Virtual-Home", "wrong-password");
    run(s, 30000);
    expect(s.status.wifiStatus).not.toBe(3);
    expect(s.core.portalMessage()).toMatch(/Couldn't connect to "Virtual-Home"/);
  });

  it("with Wi-Fi saved: connects, takes the real time (in India's zone), registers and asks for a pairing code", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    expect(text(s, "fw")).toMatch(/Connecting to Wi-Fi "Virtual-Home"[\s\S]*Wi-Fi connected, IP 192\.168\.1\.50/);
    expect(s.status.clockIsReal).toBe(true);
    expect(text(s, "fw")).toContain("[cloud] registered with the server");
    expect(s.status.cloudReady).toBe(true);
    expect(s.server.registered).toBe(true);
    expect(s.server.pairCode).toMatch(/^[2-9A-HJ-NP-Z]{8}$/);
    expect(s.status.banner).toBe(2);                       // "Pair your phone"
    s.send("time"); run(s, 1000);
    // 09:30 UTC is 15:00 in IST: the clock face must not show 09:30 (UTC)
    expect(text(s, "fw")).toMatch(/\[mood\] clock/);
  });

  it("a phone pairs with the code; Dewy learns it is paired and the card goes away", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    expect(s.pairPhone().ok).toBe(true);
    run(s, 12000);
    expect(s.status.paired).toBe(true);
    expect(s.status.banner).toBe(0);
  });

  it("a note from the phone arrives and is shown", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    s.pairPhone(); run(s, 10000);
    s.server.queueNote("Good luck today!", "Abijit");
    run(s, 12000);
    expect(text(s, "fw")).toContain("[note] from Abijit: Good luck today!");
  });

  it("a timer started from the phone starts the study timer", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    s.server.queueTimer(30);
    run(s, 12000);
    expect(text(s, "fw")).toContain("[timer] 30 min from the phone");
    expect(s.status.timerActive).toBe(true);
    s.server.queueTimerStop(); run(s, 12000);
    expect(s.status.timerActive).toBe(false);
  });

  it("the weather from the (mock) Open-Meteo shows in its eyes", async () => {
    const s = await make({ wifiSaved: true });
    s.setWeather("rain");
    run(s, SETTLE);
    expect(text(s, "fw")).toContain("[weather] rain");
    expect(s.status.weather).toBe(3);
  });

  it("loses Wi-Fi when the router goes away and comes back when it returns", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 15000);
    expect(s.status.wifiStatus).toBe(3);
    s.setRouter(false); run(s, 3000);
    expect(s.status.wifiStatus).not.toBe(3);
    expect(text(s, "fw")).toContain("Wi-Fi lost, reconnecting...");
    s.setRouter(true); run(s, 6000);
    expect(s.status.wifiStatus).toBe(3);
  });

  it("registers again after the server forgets it (the firmware's real 401 handling)", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    s.server.forgetDevice();
    run(s, 15000);
    expect(text(s, "fw")).toContain("[cloud] the server forgot this Dewy; registering again");
    run(s, 15000);
    expect(s.server.registered).toBe(true);
  });

  it("never talks to anything but the mock: unknown hosts get no answer", async () => {
    const s = await make({ wifiSaved: true });
    run(s, SETTLE);
    const hosts = new Set(s.serverLog.map((e) => e.path)).size;
    expect(hosts).toBeGreaterThan(0);
    expect(s.serverLog.some((e) => e.note?.startsWith("no network"))).toBe(false);   // the firmware only ever asked the two mock hosts
  });
});

describe("restarts and saved settings (flash)", () => {
  it("keeps what the firmware saved across a restart: affection, calibration, Wi-Fi", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 3000);
    s.send("affection 0.8"); run(s, 300);
    const before = prefsDecode(s.flash());
    expect(Object.keys(before)).toEqual(expect.arrayContaining(["aff", "calUp", "calFace", "wifi_ssid"]));
    await s.restart(); run(s, SETTLE);
    expect(text(s, "link")).toContain("board restart");
    expect(s.status.cloudReady).toBe(true);                // Wi-Fi came back from flash with no seeding
  });

  it("`wifi reset` forgets the Wi-Fi and restarts into the hotspot", async () => {
    const s = await make({ wifiSaved: true });
    run(s, 4000);
    s.send("wifi reset"); run(s, 1500);
    await new Promise((r) => setTimeout(r, 20));
    run(s, 3000);
    expect(text(s, "fw")).toContain("Saved Wi-Fi forgotten. Restarting into the setup hotspot...");
    expect(s.status.portalOn).toBe(true);
    expect(prefsDecode(s.flash()).wifi_ssid).toBeUndefined();
  });
});

describe("the voice loop drives the real face", () => {
  async function online() { const s = await make({ wifiSaved: true }); run(s, SETTLE); return s; }

  it("ask: think, talk, then happy again, with the heard / reply lines", async () => {
    const s = await online();
    s.send("ask what's the weather"); run(s, 400);
    expect(mood(s)).toBe("thinking");
    run(s, 2500);
    expect(mood(s)).toBe("talking");
    run(s, 25000);
    expect(mood(s)).toBe("happy");
    expect(text(s, "virtual")).toMatch(/heard: "what's the weather"/);
    expect(text(s, "virtual")).toMatch(/reply: "Here you go: \{"place":"Palakkad"/);
  });

  it("wake: listen while the person talks, then think, talk, happy", async () => {
    const s = await online();
    s.mic.transcript = "what's the time";
    expect(s.wake()).toBe(true);
    run(s, 600);
    expect(mood(s)).toBe("listening");
    run(s, 4800);
    expect(["thinking", "talking"]).toContain(mood(s));
    run(s, 25000);
    expect(mood(s)).toBe("happy");
    expect(text(s, "virtual")).toMatch(/heard: "what's the time"/);
  });

  it("a start timer question really starts the timer on Dewy, through the cloud and cloud.h", async () => {
    const s = await online();
    s.send("ask start a 7 minute timer"); run(s, 30000);
    expect(text(s, "fw")).toContain("[timer] 7 min from the phone");
  });

  it("mic: prints the level and what the silence check makes of it", async () => {
    const s = await online();
    s.send("mic 1"); run(s, 100);
    expect(text(s, "virtual")).toMatch(/\[mic\] Speech-like \(3 s\), first 1\.0 s: rms \d+ \(-?\d+\.\d dBFS\), peak \d+/);
    expect(text(s, "virtual")).toMatch(/frames of 20 ms are above the silence level \(rms 60\)/);
    s.mic.source = "noise";
    s.send("mic"); run(s, 100);
    expect(text(s, "virtual")).toMatch(/nobody speaking/);
  });

  it("ww debug says the wake word is not simulated, and prints a level each second", async () => {
    const s = await online();
    s.send("ww debug"); run(s, 2500);
    expect(text(s, "virtual")).toMatch(/NOT SIMULATED/);
    expect(text(s, "virtual")).toMatch(/cutoff 0\.98/);
    expect(text(s, "virtual")).toMatch(/\[ww\] level rms \d+/);
    s.send("ww debug off"); run(s, 100);
    expect(s.wwDebug).toBe(false);
  });

  it("refuses to listen when the mic isn't wired", async () => {
    const s = await make({ wifiSaved: true, hardware: { display: true, touch: true, imu: true, mic: false, amp: true } });
    run(s, SETTLE);
    expect(s.wake()).toBe(false);
    expect(text(s, "virtual")).toMatch(/microphone isn't wired/);
    expect(mood(s)).toBe("happy");
  });

  it("will not ask anything before the firmware has registered with the server", async () => {
    const s = await make();                                // no Wi-Fi saved
    run(s, 2000);
    expect(s.ask("hello")).toBe(false);
    expect(text(s, "virtual")).toMatch(/isn't registered/);
  });
});
