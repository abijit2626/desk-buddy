// The virtual motion sensor's physics: what an MPU6050 on Dewy's board would measure.
//
// Axes as mounted in the simulator (the same for the one-time "cal up" / "cal face" calibration the firmware asks for):
//   x = Dewy's right, y = up the screen, z = out of the screen. The sensor reports the force that holds it up, so
//   standing upright reads (0, +1, 0) g and lying on its face reads (0, 0, -1) g.
// APPROXIMATE: no sensor noise, no drift, no temperature. Shakes and pick-ups are made-up motions that exceed
// motion.h's thresholds the way a hand would; motion.h's detection of them is real.
export type Vec3 = [number, number, number];

export const PRESETS = {
  upright: { roll: 0, pitch: 0, label: "Upright" },
  "lean-left": { roll: -25, pitch: 0, label: "Lean left" },
  "lean-right": { roll: 25, pitch: 0, label: "Lean right" },
  "lean-back": { roll: 0, pitch: 25, label: "Tip back" },
  "lean-forward": { roll: 0, pitch: -25, label: "Tip forward" },
  "face-down": { roll: 0, pitch: -90, label: "On its face" },
  "face-up": { roll: 0, pitch: 90, label: "On its back" },
  "upside-down": { roll: 180, pitch: 0, label: "Upside down" },
} as const;
export type PresetName = keyof typeof PRESETS;

const rad = (d: number) => (d * Math.PI) / 180;
/** Gravity as the sensor sees it, for a roll (left/right lean) and pitch (back/forward tip) in degrees. */
export function gravity(rollDeg: number, pitchDeg: number): Vec3 {
  const r = rad(rollDeg), p = rad(pitchDeg);
  return [Math.cos(p) * Math.sin(r), Math.cos(p) * Math.cos(r), Math.sin(p)];
}

/** how fast a hand can turn Dewy, in degrees per second (a flip takes about half a second) */
export const TURN_RATE = 400;

export class Imu {
  /** where the sensor is now, in degrees (it turns towards the target at TURN_RATE, like a hand would, instead of teleporting) */
  roll = 0; pitch = 0;
  targetRoll = 0; targetPitch = 0;
  private shakeLeft = 0; private liftLeft = 0; private lift = 0;
  private flip = 1; private since = 0;
  private seed = 7;
  private rnd() { this.seed ^= this.seed << 13; this.seed >>>= 0; this.seed ^= this.seed >> 17; this.seed ^= this.seed << 5; this.seed >>>= 0; return this.seed / 4294967296; }

  /** turn towards these angles. `snap` jumps there at once (only for setting up a start position). */
  setAngles(roll: number, pitch: number, snap = false) {
    this.targetRoll = roll; this.targetPitch = pitch;
    if (snap) { this.roll = roll; this.pitch = pitch; }
  }
  preset(name: PresetName) { this.setAngles(PRESETS[name].roll, PRESETS[name].pitch); }
  /** a vigorous shake for about 1.2 s */
  shake(ms = 1200) { this.shakeLeft = ms; }
  /** lifting it off the desk: a quick change of direction and strength for about 350 ms */
  pickUp(ms = 350) { this.liftLeft = ms; this.lift = ms; }
  get shaking() { return this.shakeLeft > 0; }

  /** advances time and returns the acceleration to report, in g */
  step(dtMs: number): Vec3 {
    // An instant 180 degree flip would never be seen by the firmware's low-pass filter (a real flip passes through the
    // sideways positions on the way), so the sensor turns at a human speed.
    const maxTurn = (TURN_RATE * dtMs) / 1000;
    this.roll += Math.max(-maxTurn, Math.min(maxTurn, this.targetRoll - this.roll));
    this.pitch += Math.max(-maxTurn, Math.min(maxTurn, this.targetPitch - this.pitch));
    let [x, y, z] = gravity(this.roll, this.pitch);
    if (this.liftLeft > 0) {
      const k = Math.sin((Math.PI * (this.lift - this.liftLeft)) / this.lift);        // 0 -> 1 -> 0
      const tilted = gravity(this.roll + 35, this.pitch + 10);
      x = x + (tilted[0] * 1.35 - x) * k; y = y + (tilted[1] * 1.35 - y) * k; z = z + (tilted[2] * 1.35 - z) * k;
      this.liftLeft = Math.max(0, this.liftLeft - dtMs);
    }
    if (this.shakeLeft > 0) {
      this.since += dtMs;
      if (this.since >= 15) { this.since = 0; this.flip = -this.flip; }
      const amp = 1.4 + this.rnd() * 0.8;
      x += this.flip * amp * 0.7; y += this.flip * amp * 0.5; z += this.flip * amp * 0.4;
      this.shakeLeft = Math.max(0, this.shakeLeft - dtMs);
    }
    return [x, y, z];
  }
}
