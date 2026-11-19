# Dewy carrier board v0.1

A 2-layer 99.7 x 66 mm board that sits 11 mm behind the screen, inside the v2 case. Every module is
soldered to it, so there are no jumper wires. Only through-hole parts.

## Files
- `gerbers/` (zip: `dewy_carrier_v0.1_gerbers.zip`): upload this to JLCPCB, PCBWay, Lion Circuits or PCB Power.
  Settings: 2 layers, 1.6 mm, HASL, any colour. No assembly.
- `dewy_carrier.kicad_pcb`: the board, opens in KiCad 7 or newer.
- `design.py`: every position and connection; `build.py` places and routes the board; `kicad_out.py` writes the KiCad file
  and runs the design-rule check; `pincheck.py` checks each pin against `firmware/dewy/config.h` and `voice.h`.
  Rebuild: `python3 build.py && python3 kicad_out.py && python3 pincheck.py` (needs KiCad 7's Python module).

## Parts that go on it
| Ref | Part | Face |
|---|---|---|
| J1 | 2x20 female header, 2.54 mm (the screen plugs in here) | screen face |
| U1 | ESP32-S3-N16R8 dev board, through 2 x 22 male pins (holes for both 22.86 mm and 25.4 mm row spacing) | back face |
| U2 | GY-87, 8 pins | back face |
| U3 | MAX98357A, 7 pins; the speaker goes to its own screw terminal | back face |
| J2 | INMP441 mic: 6 short wires (the mic sits at the hole in the top of the case) | back face |
| J3 | 2-pin 5.08 mm screw terminal: 5 V in from the battery/charger board | back face |
| C1 | 220 uF electrolytic, 3.81 mm pitch (the long leg goes in the + hole) | back face |
| H1-H3 | M3 holes for spacers to the screen board | |

## Checked against the real screen (19 Nov)
The SmartElex board measures **91 x 64 mm** (the shop listing says 90 x 55, which is wrong). From the measurements and
a photo of its back (`screen_overlay.png`):
1. **Header direction**: VCC is on the inner row (nearer the middle of the screen board) and at the bottom end, seen
   from the back with the header on the left. `J1_VCC_ROW_OUTER = False`, `J1_VCC_AT_TOP = False`.
2. **Header position**: outer row 2 mm from the screen board's edge, header centred on its 64 mm side.
3. **Standoffs**: H1 and H3 rest on bare board on the screen's back (clear of the SD slot, the chips and the ribbon).
   H2 is on the axis of the screen's top-left corner hole, so one M3 screw can go through both boards.
   Use 11 mm spacers.
4. **The ESP32 board's row spacing**: both spacings are drilled; solder into the row that fits.

The case: its screen fence is 62 mm tall inside, the screen is 64 mm. Sand or file the fence before fitting.

## Power
J3 and the ESP32's 5Vin pin are on the same 5 V line. With the IN-OUT pads bridged, the laptop's USB also feeds
that line, so do not plug the laptop in while the battery board feeds J3. Either switch the battery off while flashing,
or remove the IN-OUT bridge once the battery board powers J3: then J3 powers everything and the laptop's USB
only powers the ESP32.

## Status
Design-rule check: 0 errors, 0 unconnected pads (only silkscreen warnings: the ESP32 outline overhangs the edge on purpose).
Header and standoff holes checked against the screen's measurements and back photo (see above).
