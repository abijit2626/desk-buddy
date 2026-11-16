"""Reads the finished .kicad_pcb and checks every screen/module pin lands on the GPIO the firmware uses."""
import re, sys, pcbnew
fw = open('../../firmware/dewy/config.h').read()
a = fw.index('#elif SCREEN_PARALLEL'); blk = fw[a:fw.index('#else', a)]   # the block for Dewy's own board
def pin(name, src=blk):
    m = re.search(r'#define\s+' + name + r'\s+(-?\d+)', src); return int(m.group(1))
vo = open('../../firmware/dewy/voice.h').read()
want = {  # (ref, pad function) -> GPIO number, or a rail
 ('J1', 'DB8'): pin('PIN_LCD_D0'), ('J1', 'DB9'): pin('PIN_LCD_D1'), ('J1', 'DB10'): pin('PIN_LCD_D2'), ('J1', 'DB11'): pin('PIN_LCD_D3'),
 ('J1', 'DB12'): pin('PIN_LCD_D4'), ('J1', 'DB13'): pin('PIN_LCD_D5'), ('J1', 'DB14'): pin('PIN_LCD_D6'), ('J1', 'DB15'): pin('PIN_LCD_D7'),
 ('J1', 'LCD_WR'): pin('PIN_LCD_WR'), ('J1', 'LCD_RS'): pin('PIN_LCD_RS'), ('J1', 'LCD_RST'): pin('PIN_TFT_RST'), ('J1', 'BK_LIGHT'): pin('PIN_TFT_BL'),
 ('J1', 'T_CLK'): pin('PIN_TOUCH_SCLK'), ('J1', 'T_DIN'): pin('PIN_TOUCH_MOSI'), ('J1', 'T_DOUT'): pin('PIN_TOUCH_MISO'), ('J1', 'T_CS'): pin('PIN_TOUCH_CS'),
 ('U2', 'SDA'): pin('PIN_I2C_SDA'), ('U2', 'SCL'): pin('PIN_I2C_SCL'),
 ('J2', 'SCK'): pin('PIN_MIC_SCK', vo), ('J2', 'WS'): pin('PIN_MIC_WS', vo), ('J2', 'SD'): pin('PIN_MIC_SD', vo),
 ('U3', 'BCLK'): pin('PIN_SPK_BCLK', vo), ('U3', 'LRC'): pin('PIN_SPK_LRC', vo), ('U3', 'DIN'): pin('PIN_SPK_DIN', vo),
 ('J1', 'VCC'): '5V', ('J1', 'LEDA'): '5V', ('J1', 'GND'): 'GND', ('J1', 'IM2'): 'GND', ('J1', 'IM1'): 'GND', ('J1', 'IM0'): '3V3',
 ('J1', 'LCD_CS'): 'GND', ('J1', 'LCD_RD'): '3V3', ('U2', 'VCC_IN'): '3V3', ('U2', 'GND'): 'GND', ('U3', 'VIN'): '5V', ('U3', 'GND'): 'GND',
 ('J2', 'VDD'): '3V3', ('J2', 'GND'): 'GND', ('J2', 'L/R'): 'GND', ('J3', '+5V'): '5V', ('J3', 'GND'): 'GND', ('C1', '+'): '5V', ('C1', '-'): 'GND',
}
b = pcbnew.LoadBoard('dewy_carrier.kicad_pcb')
pads = [(fp.GetReference(), p.GetPinFunction(), p.GetNetname()) for fp in b.GetFootprints() for p in fp.Pads()]
# which ESP32 pin names sit on each net (the ESP32 pad's function is its printed label)
esp = {}
for ref, fn, net in pads:
    if ref == 'U1' and net: esp.setdefault(net, set()).add(fn)
bad = 0
for (ref, fn), w in want.items():
    nets = [n for r, f, n in pads if r == ref and f == fn]
    if not nets: print('MISSING pad', ref, fn); bad += 1; continue
    for net in nets:
        labels = esp.get(net, set())
        ok = (str(w) in labels) if isinstance(w, int) else (w in labels)
        if not ok: print(f'WRONG {ref} {fn}: net {net} reaches ESP32 pins {sorted(labels)}, firmware wants {w}'); bad += 1
# nothing else on the ESP32 may be tied to a used net by mistake
used = {}
for ref, fn, net in pads:
    if ref == 'U1' and net and fn not in ('GND', '3V3', '5V'): used.setdefault(net, set()).add(fn)
for net, s in used.items():
    if len(s) > 1: print('SHORT: ESP32 pins', sorted(s), 'share net', net); bad += 1
unused_conn = [p for p in pads if p[0] == 'J1' and p[2] and p[1] in ('SD_CS', 'SD_MOSI', 'SD_CLK', 'SD_MISO', 'NC', 'T_IRQ', 'LCD_MISO', 'LCD_MOSI') + tuple('DB%d' % i for i in range(8))]
if unused_conn: print('these screen pins should be empty but are connected:', unused_conn); bad += 1
print(f'{len(want)} connections checked against config.h and voice.h: {"all correct" if not bad else str(bad) + " problems"}')
sys.exit(1 if bad else 0)
