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

## Check these on the real parts before you order
1. **Which way the screen header runs**: `J1_VCC_AT_TOP` and `J1_VCC_ROW_OUTER` in `design.py`. A photo of the screen's back
   settles it.
2. **Where the screen header sits** (`J1_OUTER_X`): assumed at the screen board's left edge seen from the back.
3. **The screen's mounting holes**: H1-H3 should line up with them so M3 spacers can bolt the two boards together.
4. **The ESP32 board's row spacing**: both spacings are drilled; solder into the row that fits.

## Power
J3 and the ESP32's 5Vin pin are on the same 5 V line. With the IN-OUT pads bridged, the laptop's USB also feeds
that line, so do not plug the laptop in while the battery board feeds J3. Either switch the battery off while flashing,
or remove the IN-OUT bridge once the battery board powers J3: then J3 powers everything and the laptop's USB
only powers the ESP32.

## Status
Design-rule check: 0 errors, 0 unconnected pads (only silkscreen warnings: the ESP32 outline overhangs the edge on purpose).
Not yet checked against the real screen header and mounting holes (see above).
