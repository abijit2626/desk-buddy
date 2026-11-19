"""Dewy carrier board: every position and connection in one place.
Coordinates are the CASE's body coordinates in mm, seen from the BACK of the case (the side the lid closes):
+x to the right, +y up, the screen is behind the board. Board back face (KiCad F.Cu) carries the parts.
"""
G = 0.635                      # routing grid (a quarter of 2.54)

# ---- things to confirm on the real parts before ordering -------------------------------------------------
ESP_ROW_SPACINGS = (22.86, 25.4)   # both hole rows are on the board, so either dev board width fits
J1_VCC_ROW_OUTER = False       # True: the VCC/GND/LEDA row is the one nearer the screen board's edge
J1_VCC_AT_TOP    = False       # True: the VCC end of the header is at the top when the screen is upright
J1_OUTER_X       = -46.19      # where the header's outer row sits in the case (screen pushed into its fence)
J1_CENTRE_Y      = 2.0         # header centred on the screen board's height

# ---- board outline -----------------------------------------------------------------------------------------
X0, X1, Y0, Y1 = -50.0, 49.695, -27.0, 39.04
CORNER_R = 3.0            # bottom corners
CORNER_R_TOP = 6.0        # top corners, rounder so the board slides past the lid posts
def snap(v, o): return o + round((v - o) / G) * G
def sx(v): return snap(v, X0)
def sy(v): return snap(v, Y0)

# ---- screen header J1: 20 columns, each a pair (VCC row, BK_LIGHT row) ----------------------------------------
J1_COLS = [("VCC", "BK_LIGHT"), ("GND", "IM2"), ("LEDA", "IM1"), ("T_CLK", "IM0"), ("T_CS", "DB15"), ("T_DIN", "DB14"),
           ("T_DOUT", "DB13"), ("T_IRQ", "DB12"), ("DB7", "DB11"), ("DB6", "DB10"), ("DB5", "DB9"), ("DB4", "DB8"),
           ("DB3", "LCD_RST"), ("DB2", "LCD_MISO"), ("DB1", "LCD_MOSI"), ("DB0", "LCD_RD"), ("SD_CS", "LCD_WR"),
           ("SD_MOSI", "LCD_RS"), ("SD_CLK", "LCD_CS"), ("SD_MISO", "NC")]
# screen pin -> net (None = not connected)
SCREEN_NET = {"VCC": "+5V", "LEDA": "+5V", "GND": "GND", "BK_LIGHT": "BL_GPIO7", "IM2": "GND", "IM1": "GND", "IM0": "+3V3",
              "LCD_CS": "GND", "LCD_RD": "+3V3", "LCD_RST": "LCD_RST_GPIO8", "LCD_RS": "LCD_RS_GPIO42", "LCD_WR": "LCD_WR_GPIO41",
              "DB8": "D0_GPIO9", "DB9": "D1_GPIO10", "DB10": "D2_GPIO11", "DB11": "D3_GPIO12", "DB12": "D4_GPIO13",
              "DB13": "D5_GPIO14", "DB14": "D6_GPIO39", "DB15": "D7_GPIO40",
              "T_CLK": "T_CLK_GPIO2", "T_CS": "T_CS_GPIO1", "T_DIN": "T_DIN_GPIO6", "T_DOUT": "T_DOUT_GPIO47"}

# ---- ESP32-S3 dev board, lying on its side: antenna to the right, USB to the left ---------------------------
# top row = the dev board's left column (3V3 ... 5Vin, GND), bottom row = its right column (GND, TX ... GND, GND),
# both listed from the antenna end
ESP_TOP = ["3V3", "3V3", "RST", "4", "5", "6", "7", "15", "16", "17", "18", "8", "3", "46", "9", "10", "11", "12", "13", "14", "5V", "GND"]
ESP_BOT = ["GND", "TX", "RX", "1", "2", "42", "41", "40", "39", "38", "37", "36", "35", "0", "45", "48", "47", "21", "20", "19", "GND", "GND"]
ESP_X_FIRST = sx(44.0)          # antenna-end pin
ESP_Y_TOP = sy(29.7)
GPIO_NET = {"3V3": "+3V3", "5V": "+5V", "GND": "GND",
            "4": "SDA_GPIO4", "5": "SCL_GPIO5", "6": "T_DIN_GPIO6", "7": "BL_GPIO7", "15": "MIC_SCK_GPIO15", "16": "MIC_WS_GPIO16",
            "17": "MIC_SD_GPIO17", "18": "AMP_BCLK_GPIO18", "8": "LCD_RST_GPIO8", "9": "D0_GPIO9", "10": "D1_GPIO10", "11": "D2_GPIO11",
            "12": "D3_GPIO12", "13": "D4_GPIO13", "14": "D5_GPIO14", "1": "T_CS_GPIO1", "2": "T_CLK_GPIO2", "42": "LCD_RS_GPIO42",
            "41": "LCD_WR_GPIO41", "40": "D7_GPIO40", "39": "D6_GPIO39", "38": "AMP_DIN_GPIO38", "47": "T_DOUT_GPIO47", "21": "AMP_LRC_GPIO21"}

# ---- modules: (reference, label, first pin x, y, pin names left to right, nets) ---------------------------------
ROW_Y = sy(-6.0)
HEADERS = [
  dict(ref="U2", title="GY-87", x=sx(17.2), y=ROW_Y, pins=["DRDY", "INTA", "FSYNC", "SDA", "SCL", "GND", "3.3V", "VCC_IN"],
       nets=[None, None, None, "SDA_GPIO4", "SCL_GPIO5", "GND", None, "+3V3"], body=(-1.9, 1.3, 19.7, -15.0)),
  dict(ref="U3", title="MAX98357A", x=sx(-3.2), y=ROW_Y, pins=["VIN", "GND", "SD", "GAIN", "DIN", "BCLK", "LRC"],
       nets=["+5V", "GND", None, None, "AMP_DIN_GPIO38", "AMP_BCLK_GPIO18", "AMP_LRC_GPIO21"], body=(-1.3, 1.3, 16.5, -18.0)),
  dict(ref="J2", title="MIC", x=sx(-31.0), y=sy(35.2), pins=["L/R", "WS", "SCK", "SD", "VDD", "GND"],
       nets=["GND", "MIC_WS_GPIO16", "MIC_SCK_GPIO15", "MIC_SD_GPIO17", "+3V3", "GND"], body=None),
]
CAPS = [dict(ref="C1", x=sx(-13.9), y=sy(-14.0), label="220uF")]      # + pad at x, - pad 3.81 to the right
TERMINAL = dict(ref="J3", x=sx(-26.0), y=sy(-21.0), label="5V IN")    # +5V pad at x, GND pad 5.08 to the right
# M3 standoff holes, checked against a photo of the screen's back (screen 91 x 64 mm, header 2 mm from its edge):
# H1 and H3 rest on bare board; H2 is on the axis of the screen's own top-left corner hole, so one screw can go through both.
HOLES = [(sx(40.0), sy(-14.0)), (-45.31, 31.30), (sx(-26.0), sy(2.0))]
ANTENNA_KEEPOUT = (45.6, 2.0, 50.0, 32.0)   # no copper under the ESP32's antenna

def j1_pads():
    """[(name, net, x, y)] for the 40 screen pins"""
    out = []
    inner_x = J1_OUTER_X + 2.54
    for c, (a, b) in enumerate(J1_COLS):
        y = J1_CENTRE_Y + (9.5 - c) * 2.54 * (1 if J1_VCC_AT_TOP else -1)
        xa, xb = (J1_OUTER_X, inner_x) if J1_VCC_ROW_OUTER else (inner_x, J1_OUTER_X)
        out.append((a, SCREEN_NET.get(a), sx(xa), sy(y))); out.append((b, SCREEN_NET.get(b), sx(xb), sy(y)))
    return out
def esp_pads():
    out = []
    for i, n in enumerate(ESP_TOP): out.append((n, GPIO_NET.get(n), ESP_X_FIRST - i * 2.54, ESP_Y_TOP, "top"))
    for sp in ESP_ROW_SPACINGS:
        for i, n in enumerate(ESP_BOT): out.append((n, GPIO_NET.get(n), ESP_X_FIRST - i * 2.54, sy(ESP_Y_TOP - sp), "bot%.2f" % sp))
    return out
