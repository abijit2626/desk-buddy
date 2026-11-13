// Desk Buddy face engine + renderer.
// Plain C++ (no Arduino calls), so the same file runs on the ESP32-S3 and on a PC for testing.
// It draws a 320 x 240 face into a framebuffer of byte-swapped RGB565 pixels, which is the
// order the ILI9341 wants over SPI, so the buffer can be pushed straight to the screen.
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace face {

static const int W = 320, H = 240;
static const float PI_F = 3.14159265f;

// The Arduino build optimises for size; the drawing loops need speed.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------- colour
struct RGB { float r, g, b; };
inline RGB hex(uint32_t h) { return { float((h >> 16) & 255), float((h >> 8) & 255), float(h & 255) }; }
inline RGB mix(RGB a, RGB b, float t) { return { lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t) }; }
inline uint16_t pack(RGB c) {
  // multiply by a constant: "x * 31.f / 255.f" is a real division per channel, and the ESP32-S3 divides in software
  int r = int(clampf(c.r, 0, 255) * (31.f / 255.f) + 0.5f);
  int g = int(clampf(c.g, 0, 255) * (63.f / 255.f) + 0.5f);
  int b = int(clampf(c.b, 0, 255) * (31.f / 255.f) + 0.5f);
  uint16_t v = uint16_t((r << 11) | (g << 5) | b);
  return uint16_t((v >> 8) | (v << 8));
}
inline RGB unpack(uint16_t s) {
  uint16_t v = uint16_t((s >> 8) | (s << 8));
  return { float((v >> 11) & 31) * (255.f / 31.f), float((v >> 5) & 63) * (255.f / 63.f), float(v & 31) * (255.f / 31.f) };
}

// Eye colour sets. Odd-eyed "socks" sets give each eye its own pupil colour.
// weight = how likely it is on each wake-up, per thousand (they add up to 1000, so 29 = 2.9%).
struct Palette {
  const char* name;
  uint32_t eye, pupilL, pupilR, blush, accent;
  uint16_t weight;
};
static const Palette PALETTES[] = {
  { "Bubblegum",    0xF5779F, 0x57001F, 0x57001F, 0x874157, 0xFF9DCB,  29 },
  { "Mint",         0x6EE7B7, 0x00402C, 0x00402C, 0x3D7F65, 0x92FFE6,  29 },
  { "Sky",          0x7CC4FF, 0x00264D, 0x00264D, 0x446C8C, 0xA3F5FF,  29 },
  { "Lemon",        0xFFE45E, 0x4A3A00, 0x4A3A00, 0x8C7D34, 0xFFFF80,  29 },
  { "Lavender",     0xC4A1FF, 0x2B0A5C, 0x2B0A5C, 0x6C598C, 0xF5CDFF,  29 },
  { "Peach",        0xFFB27A, 0x5A2200, 0x5A2200, 0x8C6243, 0xFFE1A0,  29 },
  { "Coral",        0xFF6B6B, 0x5A0010, 0x5A0010, 0x8C3B3B, 0xFF8F8F,  29 },
  { "Ice",          0xE8EEF7, 0x1A2A4A, 0x1A2A4A, 0x808388, 0xFFFFFF,  29 },
  { "Cherry Lime",  0xB0063C, 0x1FD11F, 0x1FD11F, 0x610321, 0x1FD11F,  29 },
  { "Rose Teal",    0xA80838, 0x12B488, 0x12B488, 0x5C041F, 0x12B488,  29 },
  { "Wine",         0x933A4B, 0xE3E6B0, 0xE3E6B0, 0x512029, 0xE3E6B0,  29 },
  { "Slate",        0x6E6E6E, 0x12C24E, 0x12C24E, 0x3D3D3D, 0x12C24E,  29 },
  { "Pearl",        0xC4C4C4, 0xC8320A, 0xC8320A, 0x6C6C6C, 0xC8320A,  29 },
  { "Royal",        0xD9161A, 0x2A12C8, 0x2A12C8, 0x770C0E, 0xFF2D32,  29 },
  { "Cocoa",        0x7A3A06, 0x06B8C4, 0x06B8C4, 0x432003, 0x06B8C4,  29 },
  { "Moss",         0x4F8A54, 0xD9AE24, 0xD9AE24, 0x2B4C2E, 0xD9AE24,  29 },
  { "Lagoon",       0x08A3A8, 0x6B24D6, 0x6B24D6, 0x045A5C, 0x6B24D6,  29 },
  { "Rosewood",     0xA25C5C, 0x6BBACB, 0x6BBACB, 0x593333, 0x6BBACB,  29 },
  { "Honey",        0xA5733F, 0xD3D166, 0xD3D166, 0x5B3F23, 0xD3D166,  29 },
  { "Periwinkle",   0x8D93CC, 0x6E6309, 0x6E6309, 0x4E5170, 0x6E6309,  29 },
  { "Orchid",       0xC463CC, 0x4A3F06, 0x4A3F06, 0x6C3670, 0x4A3F06,  29 },
  { "Blossom",      0xF2AFBB, 0x078993, 0x078993, 0x856067, 0x078993,  29 },
  { "Brick",        0xB85656, 0x3D3A0A, 0x3D3A0A, 0x652F2F, 0xE87777,  29 },
  { "Terracotta",   0xB5654F, 0x72107A, 0x72107A, 0x64382B, 0xE4886F,  29 },
  { "Mauve",        0xCC96C8, 0x13841E, 0x13841E, 0x70526E, 0x13841E,  29 },
  { "Fern",         0x16893A, 0x5CD46A, 0x5CD46A, 0x0C4B20, 0x5CD46A,  29 },
  { "Jade",         0x46917F, 0x15403A, 0x15403A, 0x265046, 0x64BBA6,  29 },
  { "Berry",        0x9A1468, 0xEE8BB8, 0xEE8BB8, 0x550B39, 0xEE8BB8,  29 },
  { "Classic",      0xFFFFFF, 0x000000, 0x000000, 0x8C8C8C, 0xFFFFFF,  29 },
  { "Kite",         0x80B5F0, 0xC1078A, 0xC1078A, 0x466484, 0xA7E4FF,  19 },
  { "Socks",        0xB967A8, 0x74D7E8, 0x8E1818, 0x66395C, 0x74D7E8,  15 },
  { "Cherry Frost", 0xA6060B, 0xB4D0D0, 0x57B80A, 0x5B0306, 0x57B80A,  10 },
  { "Parrot",       0x2FCCD2, 0x087A08, 0xD01020, 0x1A7074, 0x4AFFFF,  10 },
  { "Yin Yang",     0x6A6A6A, 0x050505, 0xDCDCDC, 0x3A3A3A, 0xDCDCDC,   9 },
  { "Panda",        0x646464, 0xDCC8C8, 0x050505, 0x373737, 0xDCC8C8,   8 },
  { "Lime",         0xB3C93A, 0x1D6B2C, 0x1477D6, 0x626F20, 0x1477D6,   8 },
  { "Khaki",        0xC9C09A, 0x1D7538, 0x2826B8, 0x6F6A55, 0xFBF1C5,   8 },
  { "Odd Mint",     0x6EE7B7, 0x00402C, 0x5A0010, 0x3D7F65, 0x92FFE6,   8 },
  { "Galaxy",       0xC4A1FF, 0x00264D, 0xFFE45E, 0x6C598C, 0xFFE45E,   8 },
  { "Sunset",       0xFFB27A, 0x5A0010, 0x2B0A5C, 0x8C6243, 0xFFE1A0,   8 },
  { "Glacier",      0xE8EEF7, 0x2A5C94, 0x8C2F55, 0x808388, 0x2A5C94,   8 },
  { "Pebble",       0xB5B3B6, 0xC04A20, 0x4057C4, 0x646264, 0xC04A20,   8 },
  { "Sage",         0xC2CFBF, 0x44108A, 0xC07808, 0x6B7269, 0xC07808,   8 },
  { "Mocha",        0x6B3C31, 0xB05CA6, 0x8A82B4, 0x3B211B, 0xB05CA6,   8 },
  { "Bumblebee",    0xE0B41E, 0x101010, 0xC0085E, 0x7B6310, 0xFFE336,   8 },
  { "Dusk",         0x7A7E90, 0x8A1A10, 0x7A1488, 0x43454F, 0xA0A5BA,   8 },
};
static const int NUM_PALETTES = sizeof(PALETTES) / sizeof(PALETTES[0]);

// ---------------------------------------------------------------- canvas and primitives
struct Seg { float x0, y0, x1, y1; };

struct Canvas {
  uint16_t* fb = nullptr;      // W*H byte-swapped RGB565, indexed fb[y * W + x] (for a strip, only its rows exist)
  float tx = 0, ty = 0;        // whole-face offset (shake, breathing)
  int bandY0 = 0, bandY1 = H;  // the rows this pass draws: the sketch draws the frame in strips, one render per strip
  int cx0 = 0, cy0 = 0, cx1 = W, cy1 = H;   // clip rectangle
  void clear() { memset(fb + bandY0 * W, 0, size_t(bandY1 - bandY0) * W * 2); }
  void resetClip() { cx0 = 0; cy0 = bandY0; cx1 = W; cy1 = bandY1; }
  void clipTo(int x0, int y0, int x1, int y1) {   // a smaller clip, never outside the strip
    cx0 = x0 < 0 ? 0 : x0; cy0 = y0 < bandY0 ? bandY0 : y0; cx1 = x1 > W ? W : x1; cy1 = y1 > bandY1 ? bandY1 : y1;
  }
  inline void put(int x, int y, RGB c, float a) {
    if (a <= 0.004f || x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    uint16_t* p = fb + y * W + x;
    if (a >= 0.996f) { *p = pack(c); return; }
    *p = pack(mix(unpack(*p), c, a));
  }
  void bounds(float x0, float y0, float x1, float y1, int& ix0, int& iy0, int& ix1, int& iy1) {
    ix0 = int(floorf(x0)); iy0 = int(floorf(y0)); ix1 = int(ceilf(x1)); iy1 = int(ceilf(y1));
    if (ix0 < cx0) ix0 = cx0;
    if (iy0 < cy0) iy0 = cy0;
    if (ix1 > cx1) ix1 = cx1;
    if (iy1 > cy1) iy1 = cy1;
  }
};

// Edge coverage of a shape given F (negative inside) and its gradient squared g2: clamp(0.5 - F / sqrt(g2), 0, 1).
// Well inside or well outside needs no square root or division, which the ESP32-S3 has no hardware for:
// only the pixels on the edge pay for them. This is what makes the face fast enough to be smooth.
inline float edgeCov(float F, float g2) {
  float q = 0.25f * g2, F2 = F * F;
  if (F2 >= q) return F < 0 ? 1.f : (g2 > 1e-12f ? 0.f : 1.f);
  float g = sqrtf(g2);
  return g > 1e-6f ? clampf(0.5f - F / g, 0, 1) : 1.f;
}

// stroke a set of segments as one shape (round caps and joins, anti-aliased, no double-blending)
inline void strokeSegs(Canvas& cv, const Seg* s, int n, float width, RGB c, float alpha) {
  if (alpha < 0.01f || n <= 0) return;
  if (n > 160) n = 160;
  float r = width * 0.5f, minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  static float sdx[160], sdy[160], sinv[160], slo[160], shi[160];   // per segment, worked out once (only loop() draws)
  for (int i = 0; i < n; i++) {
    minx = fminf(minx, fminf(s[i].x0, s[i].x1)); maxx = fmaxf(maxx, fmaxf(s[i].x0, s[i].x1));
    miny = fminf(miny, fminf(s[i].y0, s[i].y1)); maxy = fmaxf(maxy, fmaxf(s[i].y0, s[i].y1));
    float dx = s[i].x1 - s[i].x0, dy = s[i].y1 - s[i].y0, l2 = dx * dx + dy * dy;
    sdx[i] = dx; sdy[i] = dy; sinv[i] = l2 > 1e-6f ? 1.f / l2 : 0.f;
    slo[i] = fminf(s[i].y0, s[i].y1) - r - 1; shi[i] = fmaxf(s[i].y0, s[i].y1) + r + 1;
  }
  int x0, y0, x1, y1;
  cv.bounds(minx + cv.tx - r - 1, miny + cv.ty - r - 1, maxx + cv.tx + r + 1, maxy + cv.ty + r + 1, x0, y0, x1, y1);
  const float reach = (r + 0.5f) * (r + 0.5f), solid = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : -1.f;
  static uint8_t act[160];
  for (int y = y0; y < y1; y++) {
    float py = y + 0.5f - cv.ty;
    int na = 0;
    for (int i = 0; i < n; i++) if (py >= slo[i] && py <= shi[i]) act[na++] = uint8_t(i);   // segments near this row
    if (!na) continue;
    for (int x = x0; x < x1; x++) {
      float px = x + 0.5f - cv.tx, best = 1e9f;
      for (int k = 0; k < na; k++) {
        int i = act[k];
        float t = clampf(((px - s[i].x0) * sdx[i] + (py - s[i].y0) * sdy[i]) * sinv[i], 0, 1);
        float ex = s[i].x0 + sdx[i] * t - px, ey = s[i].y0 + sdy[i] * t - py, d2 = ex * ex + ey * ey;
        if (d2 < best) best = d2;
      }
      if (best >= reach) continue;                                   // outside: no square root needed
      float cov = best <= solid ? 1.f : clampf(r + 0.5f - sqrtf(best), 0, 1);
      if (cov > 0) cv.put(x, y, c, cov * alpha);
    }
  }
}

// builds segment lists; quadratic curves are flattened
struct Path {
  Seg segs[160]; int n = 0; float lx = 0, ly = 0;
  // optional affine transform applied to every point: x' = a*x + b*y + e, y' = c*x + d*y + f
  float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  void setTransform(float tx, float ty, float rotRad, float sx = 1, float sy = 1) {
    float cs = cosf(rotRad), sn = sinf(rotRad);
    a = cs * sx; b = -sn * sy; c = sn * sx; d = cs * sy; e = tx; f = ty;
  }
  void tp(float x, float y, float& ox, float& oy) const { ox = a * x + b * y + e; oy = c * x + d * y + f; }
  void moveTo(float x, float y) { lx = x; ly = y; }
  void lineTo(float x, float y) {
    if (n < 160) { float ax, ay, bx, by; tp(lx, ly, ax, ay); tp(x, y, bx, by); segs[n++] = { ax, ay, bx, by }; }
    lx = x; ly = y;
  }
  void quadTo(float qx, float qy, float x, float y, int steps = 12) {
    float sx = lx, sy = ly;
    for (int i = 1; i <= steps; i++) {
      float t = float(i) / steps, u = 1 - t;
      lineTo(u * u * sx + 2 * u * t * qx + t * t * x, u * u * sy + 2 * u * t * qy + t * t * y);
    }
  }
  void dot(float x, float y) { moveTo(x, y); lineTo(x + 0.01f, y); }
};

// anti-aliased filled ellipse (+ optional outline ring of width lw in colour oc)
inline void fillEllipse(Canvas& cv, float cx, float cy, float rx, float ry, RGB c, float alpha) {
  if (alpha < 0.01f || rx <= 0 || ry <= 0) return;
  cx += cv.tx; cy += cv.ty;
  int x0, y0, x1, y1;
  cv.bounds(cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1, x0, y0, x1, y1);
  const float irx2 = 1 / (rx * rx), iry2 = 1 / (ry * ry);
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    float u = x + 0.5f - cx, v = y + 0.5f - cy;
    float F = u * u * irx2 + v * v * iry2 - 1;
    float gx = 2 * u * irx2, gy = 2 * v * iry2;
    float cov = edgeCov(F, gx * gx + gy * gy);
    if (cov > 0) cv.put(x, y, c, cov * alpha);
  }
}
inline void strokeEllipse(Canvas& cv, float cx, float cy, float rx, float ry, float lw, RGB c, float alpha) {
  if (alpha < 0.01f) return;
  cx += cv.tx; cy += cv.ty;
  int x0, y0, x1, y1;
  cv.bounds(cx - rx - lw, cy - ry - lw, cx + rx + lw, cy + ry + lw, x0, y0, x1, y1);
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    float u = x + 0.5f - cx, v = y + 0.5f - cy;
    float F = u * u / (rx * rx) + v * v / (ry * ry) - 1;
    float gx = 2 * u / (rx * rx), gy = 2 * v / (ry * ry), g = sqrtf(gx * gx + gy * gy);
    float dd = g > 1e-6f ? fabsf(F / g) : 100.f;
    float cov = clampf(lw * 0.5f + 0.5f - dd, 0, 1);
    if (cov > 0) cv.put(x, y, c, cov * alpha);
  }
}

// rounded rectangle via a signed distance field. r = corner radii {top-left, top-right, bottom-right, bottom-left}.
// fill colour fc (fa = its alpha), outline colour oc with width lw (oa = its alpha); optional rotation about the centre
inline void roundRect(Canvas& cv, float x, float y, float w, float h, const float r[4], RGB fc, float fa, RGB oc, float lw, float oa, float rotDeg = 0) {
  if (fa < 0.01f && oa < 0.01f) return;
  float cx = x + w / 2 + cv.tx, cy = y + h / 2 + cv.ty, hw = w / 2, hh = h / 2;
  float cs = cosf(rotDeg * PI_F / 180), sn = sinf(rotDeg * PI_F / 180), ext = sqrtf(hw * hw + hh * hh) + lw + 2;
  int x0, y0, x1, y1;
  cv.bounds(cx - ext, cy - ext, cx + ext, cy + ext, x0, y0, x1, y1);
  for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) {
    float px = xx + 0.5f - cx, py = yy + 0.5f - cy;
    float qx = px * cs + py * sn, qy = -px * sn + py * cs;           // into the box's frame
    float rad = qx < 0 ? (qy < 0 ? r[0] : r[3]) : (qy < 0 ? r[1] : r[2]);
    rad = fminf(rad, fminf(hw, hh));
    float dx = fabsf(qx) - (hw - rad), dy = fabsf(qy) - (hh - rad);
    float ox = fmaxf(dx, 0), oy = fmaxf(dy, 0);
    float dist = ((ox > 0 && oy > 0) ? sqrtf(ox * ox + oy * oy) : ox + oy) + fminf(fmaxf(dx, dy), 0) - rad;
    if (fa > 0.01f) { float cov = clampf(0.5f - dist, 0, 1); if (cov > 0) cv.put(xx, yy, fc, cov * fa); }
    if (oa > 0.01f && lw > 0) { float cov = clampf(lw * 0.5f + 0.5f - fabsf(dist), 0, 1); if (cov > 0) cv.put(xx, yy, oc, cov * oa); }
  }
}

// heart (signed distance by Inigo Quilez), point at the bottom
inline void fillHeart(Canvas& cv, float cx, float bottom, float size, RGB c, float alpha) {
  if (alpha < 0.01f) return;
  cx += cv.tx; bottom += cv.ty;
  int x0, y0, x1, y1;
  cv.bounds(cx - size * 0.7f, bottom - size * 1.15f, cx + size * 0.7f, bottom + 2, x0, y0, x1, y1);
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    float px = fabsf(x + 0.5f - cx) / size, py = (bottom - (y + 0.5f)) / size, dd;
    if (py + px > 1.0f) { float ax = px - 0.25f, ay = py - 0.75f; dd = sqrtf(ax * ax + ay * ay) - 0.35355339f; }
    else {
      float ax = px, ay = py - 1.0f, k = 0.5f * fmaxf(px + py, 0.0f), bx = px - k, by = py - k;
      dd = sqrtf(fminf(ax * ax + ay * ay, bx * bx + by * by)) * (px - py > 0 ? 1.f : -1.f);
    }
    float cov = clampf(0.5f - dd * size, 0, 1);
    if (cov > 0) cv.put(x, y, c, cov * alpha);
  }
}

// small stroke font (digits, colon, z, Z, !) on a 10 x 16 grid
inline void glyph(Path& p, char ch) {
  switch (ch) {
    case '0': p.moveTo(3, 0); p.lineTo(7, 0); p.quadTo(10, 0, 10, 4); p.lineTo(10, 12); p.quadTo(10, 16, 7, 16); p.lineTo(3, 16); p.quadTo(0, 16, 0, 12); p.lineTo(0, 4); p.quadTo(0, 0, 3, 0); break;
    case '1': p.moveTo(3, 3); p.lineTo(7, 0); p.lineTo(7, 16); break;
    case '2': p.moveTo(0, 4); p.quadTo(0, 0, 5, 0); p.quadTo(10, 0, 10, 4.5f); p.quadTo(10, 7, 6, 10); p.lineTo(0, 16); p.lineTo(10, 16); break;
    case '3': p.moveTo(0, 2.5f); p.quadTo(2, 0, 5, 0); p.quadTo(10, 0, 10, 4); p.quadTo(10, 8, 4, 8); p.moveTo(4, 8); p.quadTo(10, 8, 10, 12); p.quadTo(10, 16, 5, 16); p.quadTo(2, 16, 0, 13.5f); break;
    case '4': p.moveTo(7.5f, 16); p.lineTo(7.5f, 0); p.lineTo(0, 11); p.lineTo(10, 11); break;
    case '5': p.moveTo(9.5f, 0); p.lineTo(1, 0); p.lineTo(0.5f, 7); p.quadTo(2.5f, 6, 5, 6); p.quadTo(10, 6, 10, 11); p.quadTo(10, 16, 5, 16); p.quadTo(1.5f, 16, 0, 13.5f); break;
    case '6': p.moveTo(9, 1.5f); p.quadTo(7.5f, 0, 5.5f, 0); p.quadTo(0, 0, 0, 9); p.quadTo(0, 16, 5, 16); p.quadTo(10, 16, 10, 11); p.quadTo(10, 6.5f, 5, 6.5f); p.quadTo(1, 6.5f, 0, 10); break;
    case '7': p.moveTo(0, 0); p.lineTo(10, 0); p.lineTo(3.5f, 16); break;
    case '8': p.moveTo(5, 7.5f); p.quadTo(1, 7.5f, 1, 4); p.quadTo(1, 0, 5, 0); p.quadTo(9, 0, 9, 4); p.quadTo(9, 7.5f, 5, 7.5f); p.quadTo(0, 7.5f, 0, 12); p.quadTo(0, 16, 5, 16); p.quadTo(10, 16, 10, 12); p.quadTo(10, 7.5f, 5, 7.5f); break;
    case '9': p.moveTo(1, 14.5f); p.quadTo(2.5f, 16, 4.5f, 16); p.quadTo(10, 16, 10, 7); p.quadTo(10, 0, 5, 0); p.quadTo(0, 0, 0, 5); p.quadTo(0, 9.5f, 5, 9.5f); p.quadTo(9, 9.5f, 10, 6); break;
    case ':': p.dot(5, 4.5f); p.dot(5, 12); break;
    case 'z': p.moveTo(1, 6); p.lineTo(9, 6); p.lineTo(1, 16); p.lineTo(9, 16); break;
    case 'Z': p.moveTo(0, 0); p.lineTo(10, 0); p.lineTo(0, 16); p.lineTo(10, 16); break;
    case '!': p.moveTo(5, 0); p.lineTo(5, 10); p.dot(5, 15.5f); break;
    default: break;
  }
}
// draws text with its top-centre at (cx, top); height = glyph height in px
inline void drawText(Canvas& cv, const char* s, float cx, float top, float height, float stroke, RGB c, float alpha) {
  if (alpha < 0.01f) return;
  float k = height / 16.f, adv = 14.f, total = 0;
  for (const char* q = s; *q; q++) total += (*q == ':' ? 8.f : adv);
  total -= 4.f;
  float x = cx - total * k / 2;
  for (const char* q = s; *q; q++) {
    float shift = (*q == ':' ? -3.f : 0.f);   // centres the colon between its neighbours
    Path p; p.setTransform(x + shift * k, top, 0, k, k); glyph(p, *q);
    strokeSegs(cv, p.segs, p.n, stroke, c, alpha);
    x += (*q == ':' ? 8.f : adv) * k;
  }
}

// filled convex polygon (up to 8 points, either winding), anti-aliased. xy = x0, y0, x1, y1, ...
inline void fillConvex(Canvas& cv, const float* xy, int n, RGB c, float alpha) {
  if (alpha < 0.01f || n < 3 || n > 8) return;
  float nx[8], ny[8], d0[8], area = 0, minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1]; }
  float sgn = area > 0 ? 1.f : -1.f;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    float x0 = xy[2 * i] + cv.tx, y0 = xy[2 * i + 1] + cv.ty, ex = xy[2 * j] - xy[2 * i], ey = xy[2 * j + 1] - xy[2 * i + 1];
    float l = fmaxf(1e-6f, sqrtf(ex * ex + ey * ey));
    nx[i] = sgn * ey / l; ny[i] = -sgn * ex / l; d0[i] = nx[i] * x0 + ny[i] * y0;   // outward edge normals
    minx = fminf(minx, x0); maxx = fmaxf(maxx, x0); miny = fminf(miny, y0); maxy = fmaxf(maxy, y0);
  }
  int x0, y0, x1, y1;
  cv.bounds(minx - 1, miny - 1, maxx + 1, maxy + 1, x0, y0, x1, y1);
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    float px = x + 0.5f, py = y + 0.5f, d = -1e9f;
    for (int i = 0; i < n; i++) d = fmaxf(d, nx[i] * px + ny[i] * py - d0[i]);
    float cov = clampf(0.5f - d, 0, 1);
    if (cov > 0) cv.put(x, y, c, cov * alpha);
  }
}

// darken everything drawn so far (a = 0: no change, 1: black)
inline void dimAll(Canvas& cv, float a) {
  int k = int((1 - clampf(a, 0, 1)) * 256);
  for (int i = cv.bandY0 * W; i < cv.bandY1 * W; i++) {
    uint16_t v = uint16_t((cv.fb[i] >> 8) | (cv.fb[i] << 8));
    int r = (((v >> 11) & 31) * k) >> 8, g = (((v >> 5) & 63) * k) >> 8, b = ((v & 31) * k) >> 8;
    v = uint16_t((r << 11) | (g << 5) | b);
    cv.fb[i] = uint16_t((v >> 8) | (v << 8));
  }
}
// turn the picture upside down (used when the buddy itself is upside down, so it still reads right).
// Drawing in strips, the sketch renders the mirrored strip (rows H-y1..H-y0) and this turns it round for rows y0..y1.
inline void flip180(Canvas& cv) {
  for (int i = cv.bandY0 * W, j = cv.bandY1 * W - 1; i < j; i++, j--) { uint16_t s = cv.fb[i]; cv.fb[i] = cv.fb[j]; cv.fb[j] = s; }
}

// ---------------------------------------------------------------- words (real fonts)
// Same layout as the Adafruit / LovyanGFX "GFX" fonts, so the sketch can hand over LovyanGFX's own fonts.
// They are 1-bit, so each is drawn shrunk 2x or 3x with the pixels averaged: smooth edges like the rest of the face.
struct Glyph { uint32_t off; uint8_t w, h, adv; int8_t xo, yo; };
struct Font { const uint8_t* bits; const Glyph* glyphs; uint16_t first, last; uint8_t yAdv; };

inline const Glyph* glyphOf(const Font& f, char ch) {
  uint8_t c = uint8_t(ch);
  return (c < f.first || c > f.last) ? nullptr : &f.glyphs[c - f.first];
}
inline float textWidth(const Font& f, int ss, const char* s, int n = -1) {
  int w = 0;
  for (int i = 0; s[i] && (n < 0 || i < n); i++) { const Glyph* g = glyphOf(f, s[i]); if (g) w += g->adv; }
  return float(w) / ss;
}
inline float capHeight(const Font& f, int ss) { const Glyph* g = glyphOf(f, 'H'); return g ? -g->yo / float(ss) : f.yAdv * 0.7f / ss; }
inline int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// draws n characters of s (all when n < 0). align: 0 = starts at x, 1 = centred on x, 2 = ends at x.
// ymid = the middle of the capital letters.
inline void drawString(Canvas& cv, const Font& f, int ss, const char* s, float x, float ymid, int align, RGB c, float alpha, int n = -1) {
  if (alpha < 0.01f) return;
  float w = textWidth(f, ss, s, n);
  float left = (align == 1 ? x - w / 2 : (align == 2 ? x - w : x)) + cv.tx;
  float base = ymid + capHeight(f, ss) / 2 + cv.ty;
  int penX = int(lroundf(left * ss)), baseY = int(lroundf(base * ss));
  float inv = 1.f / float(ss * ss);
  for (int i = 0; s[i] && (n < 0 || i < n); i++) {
    const Glyph* g = glyphOf(f, s[i]);
    if (!g) continue;
    int gx = penX + g->xo, gy = baseY + g->yo;   // glyph's top-left corner in font pixels
    if (g->w && g->h) {
      int sx0 = floorDiv(gx, ss), sy0 = floorDiv(gy, ss), sx1 = floorDiv(gx + g->w - 1, ss), sy1 = floorDiv(gy + g->h - 1, ss);
      for (int sy = sy0; sy <= sy1; sy++) {
        if (sy < cv.cy0 || sy >= cv.cy1) continue;
        for (int sx = sx0; sx <= sx1; sx++) {
          if (sx < cv.cx0 || sx >= cv.cx1) continue;
          int cnt = 0;
          for (int v = 0; v < ss; v++) {
            int by = sy * ss + v - gy;
            if (by < 0 || by >= g->h) continue;
            for (int u = 0; u < ss; u++) {
              int bx = sx * ss + u - gx;
              if (bx < 0 || bx >= g->w) continue;
              uint32_t bit = uint32_t(by) * g->w + uint32_t(bx);
              if (f.bits[g->off + (bit >> 3)] & (0x80 >> (bit & 7))) cnt++;
            }
          }
          if (cnt) cv.put(sx, sy, c, alpha * cnt * inv);
        }
      }
    }
    penX += g->adv;
  }
}

// splits s into lines no wider than maxW (breaking at spaces; a very long word is cut).
// Fills starts/lens, returns the line count (at most maxLines). *more = true when text was left over.
inline int wrapText(const Font& f, int ss, const char* s, float maxW, int* starts, int* lens, int maxLines, bool* more = nullptr) {
  int n = 0, i = 0, len = int(strlen(s));
  while (n < maxLines) {
    while (i < len && s[i] == ' ') i++;
    if (i >= len) break;
    int st = i, fit = -1;
    for (int j = i; j <= len; j++) {
      if (j < len && s[j] != ' ') continue;
      if (textWidth(f, ss, s + st, j - st) <= maxW) fit = j; else break;
    }
    int end = fit;
    if (end < 0) {                                   // a single word wider than the line
      end = st + 1;
      while (end < len && s[end] != ' ' && textWidth(f, ss, s + st, end + 1 - st) <= maxW) end++;
    }
    starts[n] = st; lens[n] = end - st; n++;
    i = end;
  }
  while (i < len && s[i] == ' ') i++;
  if (more) *more = i < len;
  return n;
}

// phones send curly quotes, emoji and other UTF-8; keep only what the font can draw
inline void asciiFold(const char* in, char* out, int cap) {
  int o = 0;
  for (int i = 0; in[i] && o < cap - 1;) {
    uint8_t c = uint8_t(in[i]);
    if (c < 0x80) { out[o++] = (c < 32) ? ' ' : char(c); i++; continue; }
    uint32_t cp; int extra;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { i++; continue; }
    i++;
    for (int k = 0; k < extra && (uint8_t(in[i]) & 0xC0) == 0x80; k++, i++) cp = (cp << 6) | (uint8_t(in[i]) & 0x3F);
    const char* rep = nullptr;
    switch (cp) {
      case 0x2018: case 0x2019: case 0x2032: rep = "'"; break;
      case 0x201C: case 0x201D: rep = "\""; break;
      case 0x2026: rep = "..."; break;
      case 0x2013: case 0x2014: rep = "-"; break;
      case 0x00A0: rep = " "; break;
      case 0x2764: case 0x2665: case 0x1F496: case 0x1F497: case 0x1F495: case 0x1F49E: case 0x1F970: rep = "<3"; break;
      default: break;
    }
    for (const char* r = rep; r && *r && o < cap - 1; r++) out[o++] = *r;
  }
  out[o] = 0;
}

// ---------------------------------------------------------------- the face
enum Mood { HAPPY, PETTED, BORED, SLEEPY, ANNOYED, DIZZY, SURPRISED, LISTENING, THINKING, TALKING, ALARM, CLOCK, NUM_MOODS };
static const char* const MOOD_NAMES[NUM_MOODS] = { "happy", "petted", "bored", "asleep", "annoyed", "dizzy", "surprised", "listening", "thinking", "talking", "alarm", "clock" };

enum Weather { WEATHER_NONE, WEATHER_SUN, WEATHER_CLOUD, WEATHER_RAIN };
static const char* const WEATHER_NAMES[] = { "none", "sun", "cloud", "rain" };
enum Reminder { REMIND_WATER, REMIND_STRETCH };
enum TextSize { TEXT_SMALL, TEXT_MEDIUM, TEXT_LARGE };   // about 16, 18 and 24 px tall letters (em size)

static const char* const EIGHT_BALL[] = { "Yes!", "Definitely", "Without a doubt", "Signs say yes", "Maybe...", "Ask again later", "Not today", "No way" };
static const int NUM_EIGHT_BALL = sizeof(EIGHT_BALL) / sizeof(EIGHT_BALL[0]);

enum P {
  Lcx, Lcy, Lw, Lh, Lrot, Rcx, Rcy, Rw, Rh, Rrot, PUPIL_SZ, Lpx, Lpy, Rpx, Rpy, PO, SO, OPEN, LT, LA,
  ARCUP, ARCDOWN, BLUSH, HEART, ZS, WAVES, DOTS, BANG, RING, ATIME, CLOCKP,
  MSMILE, MFLAT, MRING, MFROWN, MWAVE, MTALK, MALARM, MSMALL, MSLANT, WOB, SHAKE, JIT, TALK, CAT, ACORN, NP
};
static const float BASE[NP] = {
  92, 110, 124, 150, -14, 228, 110, 124, 150, 14, 60, 8, 4, -8, 4, 1, 0, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
// spring (omega, zeta) for shape and position params; everything else eases exponentially
struct Spring { float om, ze; };
inline Spring springFor(int k) {
  switch (k) {
    case Lw: case Lh: case Rw: case Rh: return { 13, 0.5f };
    case Lpx: case Lpy: case Rpx: case Rpy: return { 8, 0.78f };
    case Lcx: case Lcy: case Rcx: case Rcy: case Lrot: case Rrot: return { 10, 0.85f };
    case PUPIL_SZ: return { 10, 0.7f };
    default: return { 0, 0 };
  }
}
struct KV { uint8_t k; float v; };
inline int moodPatch(Mood m, KV* out) {
  int n = 0;
  auto set = [&](int k, float v) { out[n++] = { uint8_t(k), v }; };
  switch (m) {
    case PETTED: set(OPEN, 0.05f); set(ARCUP, 1); set(BLUSH, 1); set(HEART, 1); set(MSMILE, 1); set(Lpy, 10); set(Rpy, 10); break;
    case BORED: set(LT, 0.46f); set(Lh, 138); set(Rh, 140); set(PUPIL_SZ, 54); set(Lpy, 16); set(Rpy, 16); set(MFLAT, 1); break;
    case SLEEPY: set(OPEN, 0.05f); set(ARCDOWN, 1); set(ZS, 1); set(MRING, 1); break;
    case ANNOYED: set(LT, 0.36f); set(LA, 22); set(Lh, 142); set(Rh, 142); set(PUPIL_SZ, 52); set(Lpx, 4); set(Rpx, -4); set(Lpy, 8); set(Rpy, 8); set(MFROWN, 1); break;
    case DIZZY: set(PO, 0); set(SO, 1); set(WOB, 1); set(MWAVE, 1); break;
    case SURPRISED: set(Lw, 136); set(Lh, 164); set(Rw, 136); set(Rh, 164); set(Lcy, 106); set(Rcy, 106); set(PUPIL_SZ, 30); set(Lpx, 0); set(Rpx, 0); set(Lpy, 0); set(Rpy, 0); set(BANG, 1); set(MRING, 1); break;
    case LISTENING: set(Lw, 128); set(Lh, 156); set(Rw, 128); set(Rh, 156); set(PUPIL_SZ, 68); set(Lpx, 4); set(Rpx, -4); set(Lpy, -4); set(Rpy, -4); set(WAVES, 1); set(MSMALL, 1); break;
    case THINKING: set(Lpx, 24); set(Rpx, 24); set(Lpy, -30); set(Rpy, -30); set(Lrot, -6); set(Rrot, 22); set(PUPIL_SZ, 54); set(DOTS, 1); set(MSLANT, 1); break;
    case TALKING: set(MTALK, 1); set(TALK, 1); break;
    case ALARM: set(Lw, 136); set(Lh, 162); set(Rw, 136); set(Rh, 162); set(Lcy, 108); set(Rcy, 108); set(Lrot, -18); set(Rrot, 18); set(PUPIL_SZ, 26); set(Lpx, 0); set(Rpx, 0); set(Lpy, 0); set(Rpy, 0); set(RING, 1); set(ATIME, 1); set(SHAKE, 1); set(JIT, 1); set(MALARM, 1); break;
    case CLOCK: set(Lcx, 138); set(Lcy, 34); set(Lw, 40); set(Lh, 46); set(Rcx, 182); set(Rcy, 34); set(Rw, 40); set(Rh, 46); set(Lrot, -10); set(Rrot, 10); set(PUPIL_SZ, 20); set(Lpx, 0); set(Rpx, 0); set(Lpy, 8); set(Rpy, 8); set(CLOCKP, 1); break;
    default: set(MSMILE, 1); break;
  }
  return n;
}

class Face {
public:
  // settings
  bool mouth = true;
  int catChancePct = 30;      // pupil shape on each wake-up: cat slits...
  int acornChancePct = 20;    // ...or tall acorn pupils; the rest are round
  const char* alarmText = "07:00";
  // optional hook to draw the date line under the clock (the ESP32 sketch uses a real font for it)
  void (*drawDate)(Canvas& cv, RGB colour, float alpha, void* user) = nullptr;
  void* drawDateUser = nullptr;
  // optional hook called whenever the mood changes
  void (*onMood)(Mood m, void* user) = nullptr;
  void* onMoodUser = nullptr;

  void begin(uint32_t seed) {
    rng = seed ? seed : 0x9E3779B9u;
    for (int k = 0; k < NP; k++) { cur[k] = BASE[k]; vel[k] = 0; }
    computeTargets(); for (int k = 0; k < NP; k++) cur[k] = tgt[k];
    palIdx = 0;
  }

  Mood mood() const { return moodNow; }
  bool isCat() const { return cat; }
  bool isAcorn() const { return acorn; }
  const char* shapeName() const { return cat ? "cat" : (acorn ? "acorn" : "round"); }
  const Palette& palette() const { return PALETTES[palIdx]; }
  float timeMs() const { return t; }

  // ---- sequences (a mood, how long it lasts, then the next one) ----
  struct Step { Mood m; uint32_t ms; };
  void play(const Step* steps, int n, int endMood = -1) {
    nq = 0;
    float at = 0;
    for (int i = 0; i < n; i++) {
      if (i == 0) go(steps[i].m); else push(t + at, steps[i].m);
      at += steps[i].ms;
    }
    if (endMood >= 0) push(t + at, Mood(endMood));
  }
  void play1(Mood m, uint32_t ms = 0, int endMood = -1) { Step s = { m, ms }; play(&s, 1, endMood); }
  bool busy() const { return nq > 0; }

  // ---- things that happen to it ----
  void shake()    { play1(DIZZY, 3000, HAPPY); }
  void pickUp()   { play1(SURPRISED, 1500, HAPPY); }
  void sayName()  { Step s[] = { { LISTENING, 3000 }, { THINKING, 2400 }, { TALKING, 3600 } }; play(s, 3, HAPPY); }
  void listen()   { play1(LISTENING); }
  void think()    { play1(THINKING); }
  void talk()     { play1(TALKING); }
  void petStart() { play1(PETTED); }
  void petEnd()   { play1(PETTED, 2500, HAPPY); }
  void pet()      { play1(PETTED, 2500, HAPPY); }
  void bored()    { play1(BORED); }
  void sleep()    { play1(SLEEPY); }
  void wake()     { if (moodNow == SLEEPY) play1(HAPPY); else { Step s[] = { { SLEEPY, 1800 }, { HAPPY, 0 } }; play(s, 2); } }
  void alarm()    { play1(ALARM, 8000, HAPPY); }
  void showTime(uint32_t ms = 5000) { play1(CLOCK, ms, HAPPY); }
  void reset()    { play1(HAPPY); }
  void snooze()   { play1(HAPPY); }
  void poke() {
    // forget pokes older than 3 s
    int k = 0; for (int i = 0; i < npk; i++) if (t - pokes[i] < 3000) pokes[k++] = pokes[i];
    npk = k; if (npk < 8) pokes[npk++] = t;
    if (moodNow == ALARM) { snooze(); return; }
    if (moodNow == SLEEPY) { play1(SURPRISED, 1500, HAPPY); return; }
    if (npk >= pokesToAnnoy) { npk = 0; play1(ANNOYED, 4000, HAPPY); return; }
    if (moodNow == ANNOYED) return;
    if (moodNow == BORED) play1(HAPPY);
    squintAt = t;
  }
  void setMood(Mood m) { play1(m); }
  void nextPalette() { pickPalette(); }
  void setPalette(int i) { if (i >= 0 && i < NUM_PALETTES) palIdx = i; }
  void setCat(bool on) { cat = on; if (on) acorn = false; }
  void setAcorn(bool on) { acorn = on; if (on) cat = false; }

  // =============================================================== extra features
  // Fonts for words on screen. The sketch passes LovyanGFX's FreeSansBold18pt7b and FreeSansBold24pt7b.
  // Without them everything still works, just without the words.
  const Font* font18 = nullptr;
  const Font* font24 = nullptr;
  const char* noteFrom = "Abijit";     // note cards say "From Abijit"
  bool weatherOnWake = true;            // show the weather for a few seconds after waking up

  // ---- eyes follow tilt. x: -1..1 (+ = pupils right), y: -1..1 (+ = pupils down). on = false: look around as usual
  void setTilt(float x, float y, bool on = true) {
    tiltX = clampf(x, -1, 1); tiltY = clampf(y, -1, 1);
    if (tiltOn && !on) nextGaze = t + 400;
    tiltOn = on;
  }
  bool tilted() const { return tiltOn; }

  // ---- lying on its face: asleep and quiet (the sketch turns the backlight off). Standing up wakes it.
  void setFaceDown(bool on) {
    if (on == faceDownOn) return;
    faceDownOn = on;
    if (on) play1(SLEEPY); else play1(HAPPY);
  }
  bool faceDown() const { return faceDownOn; }

  // ---- upside down: it complains, and draws itself the other way up so the words still read right
  void setUpsideDown(bool on) {
    if (on == upside) return;
    upside = on;
    if (on) play1(ANNOYED); else play1(HAPPY);
  }
  bool upsideDown() const { return upside; }

  // ---- magic 8-ball: a moment of dizziness, then an answer
  void eightBall() {
    int k = eightIdx;
    for (int tries = 0; tries < 6 && k == eightIdx; tries++) k = int(rnd() * NUM_EIGHT_BALL) % NUM_EIGHT_BALL;
    eightIdx = k;
    play1(DIZZY, 1300, HAPPY);
    eightAt = t + 1300;
  }
  const char* eightBallAnswer() const { return EIGHT_BALL[eightIdx]; }

  // ---- weather: setWeather stores today's weather, showWeather puts it on screen for a while
  void setWeather(Weather w) { weatherNow = w; if (w == WEATHER_NONE) weatherAt = -1; }
  Weather weather() const { return weatherNow; }
  void showWeather(uint32_t ms = 8000) { if (weatherNow != WEATHER_NONE) { weatherAt = t; weatherDur = float(ms); } }

  // ---- study timer
  void startTimer(uint32_t minutes) {
    if (minutes == 0) minutes = 1;
    timerOn = true; timerStart = t; timerDur = minutes * 60000.f; breakAt = -1;
    if (moodNow == SLEEPY || moodNow == BORED) play1(HAPPY);
  }
  void stopTimer() { timerOn = false; }
  bool timerActive() const { return timerOn; }
  uint32_t timerLeftMs() const { return timerActive() ? uint32_t(fmaxf(0, timerDur - (t - timerStart))) : 0; }

  // ---- water and stretch nudges. Returns false (and does nothing) while asleep or face down.
  bool remind(Reminder r) {
    if (moodNow == SLEEPY || faceDownOn || upside) return false;
    remindKind = r; remindAt = t;
    Step s[] = { { SURPRISED, 700 }, { TALKING, 2600 } };
    play(s, 2, HAPPY);
    return true;
  }

  // ---- notes. Kept until it is awake if it arrives while it sleeps or lies on its face.
  void showNote(const char* text, const char* from = nullptr) {
    asciiFold(text ? text : "", note, sizeof note);
    asciiFold(from && *from ? from : (noteFrom ? noteFrom : ""), noteBy, sizeof noteBy);
    if (moodNow == SLEEPY || faceDownOn) notePending = true; else startNote();
  }
  bool hasPendingNote() const { return notePending; }

  // ---- birthday party (own = the buddy's own birthday)
  void birthday(bool own = false) {
    bdayAt = t; bdayOwn = own;
    eightAt = -1; remindAt = -1; weatherAt = -1;
    for (int i = 0; i < NCONF; i++) {
      float x = rnd() * W, y = -10 - rnd() * 170, v = 45 + rnd() * 55, r = rnd() * 6.28f, s = 3 + rnd() * 2.5f;
      conf[i] = { x, y, v, r, s, uint8_t(i % 6) };
    }
    Step s[] = { { PETTED, 1400 }, { TALKING, 3000 } };
    play(s, 2, HAPPY);
  }

  // ---- grows a personality: 0 = brand new, 1 = best friends (the sketch saves it in flash)
  void setAffection(float a) { affection = clampf(a, 0, 1); }
  float affectionLevel() const { return affection; }

  // ---- bond moments: a short message with hearts rising (hello again, a new bond level, coming back after days)
  void bondMoment(const char* msg, int hearts, uint32_t ms = 5000) {
    if (faceDownOn || upside) return;
    asciiFold(msg ? msg : "", bondMsg, sizeof bondMsg);
    bondAt = t; bondDur = float(ms < 2000 ? 2000 : ms);
    bondHearts = hearts < 0 ? 0 : (hearts > MAX_BOND_HEARTS ? MAX_BOND_HEARTS : hearts);
    eightAt = -1; remindAt = -1; weatherAt = -1;
    play1(PETTED, uint32_t(bondDur * 0.6f), HAPPY);
  }
  // missing its person after a long time alone: a small grey heart droops now and then
  void setMissing(bool on) { missing = on; }
  bool isMissing() const { return missing; }
  int pokesToAnnoy = 5;     // quick pokes in 3 s before it gets annoyed (a close buddy is more patient, up to 8)
  bool resting = false;     // switched off in the app: it sleeps, and says so if you wake it

  // ---- battery percent (-1 = not measured). 20% or less: icon and tired eyes; 5% or less: "charge me"
  void setBattery(int pct) { battery = pct < 0 ? -1 : (pct > 100 ? 100 : pct); }
  int batteryPct() const { return battery; }

  // ---- update over Wi-Fi in progress: 0..1, or below 0 when there is none
  void setUpdateProgress(float p) {
    if (p >= 0 && updateP < 0) play1(THINKING);
    updateP = p;
  }

  // ---- a message card for setting up (Wi-Fi, pairing). It stays until cleared.
  void showBanner(const char* title, const char* line, const char* big) {
    asciiFold(title ? title : "", bTitle, sizeof bTitle);
    asciiFold(line ? line : "", bLine, sizeof bLine);
    asciiFold(big ? big : "", bBig, sizeof bBig);
    if (!bannerOn) bannerAt = t;
    bannerOn = true;
    if (moodNow == SLEEPY || moodNow == BORED) play1(HAPPY);
  }
  void clearBanner() { bannerOn = false; }
  bool bannerShown() const { return bannerOn; }

  // something is on screen that shouldn't be cut short by falling asleep
  bool overlayActive() const {
    return bannerOn || (eightAt >= 0 && t < eightAt + 4200) || (noteAt >= 0 && t < noteAt + NOTE_MS) || (bdayAt >= 0 && t < bdayAt + 8000) ||
           (remindAt >= 0 && t < remindAt + 6000) || (weatherAt >= 0 && t < weatherAt + weatherDur) || (breakAt >= 0 && t < breakAt + 5000) ||
           (bondAt >= 0 && t < bondAt + bondDur) || updateP >= 0;
  }

  // ---- advance time ----
  void update(float dtMs) {
    t += dtMs;
    if (t > 3.6e6f) maybeRebase();
    while (nq > 0 && q[0].at <= t) { Mood m = q[0].m; for (int i = 1; i < nq; i++) q[i - 1] = q[i]; nq--; go(m); }
    // things that happen once it's properly awake: a note that waited, or today's weather
    if (wakeExtras && !faceDownOn && (moodNow == HAPPY || moodNow == SURPRISED)) {
      wakeExtras = false;
      if (notePending) startNote();
      else if (weatherOnWake) showWeather(8000);
    }
    // study timer finished
    if (timerOn && t - timerStart >= timerDur) {
      timerOn = false;
      if (moodNow != SLEEPY && !faceDownOn) { breakAt = t; play1(PETTED, 2500, HAPPY); }
    }
    float dt = fminf(0.05f, dtMs / 1000.f);
    if (t >= nextBlink) {
      blinkAt = t; bool slow = moodNow == BORED; bool dbl = !slow && rnd() < 0.2f;
      nextBlink = t + (dbl ? 420 : between(slow ? 4000 : 2500, slow ? 7000 : 6000));
    }
    if (t >= nextGaze) {
      if (rnd() < 0.3f) { gx = 0; gy = 0; } else { gx = (rnd() * 2 - 1) * 22; gy = (rnd() * 2 - 1) * 14; }
      nextGaze = t + between(1200, 3800);
    }
    computeTargets();
    for (int k = 0; k < NP; k++) {
      Spring sp = springFor(k);
      if (sp.om > 0) {
        float v = vel[k] + (sp.om * sp.om * (tgt[k] - cur[k]) - 2 * sp.ze * sp.om * vel[k]) * dt;
        vel[k] = v; cur[k] += v * dt;
      } else {
        float tau = k == OPEN ? 0.12f : ((k == CAT || k == ACORN) ? 0.35f : 0.18f);
        cur[k] += (tgt[k] - cur[k]) * (1 - expf(-dt / tau));
      }
    }
  }

  // ---- draw one frame ----
  void render(Canvas& cv, const char* timeText) {
    cv.clear(); cv.resetClip();
    const float* c = cur;
    const Palette& pal = PALETTES[palIdx];
    RGB eye = hex(pal.eye), pupL = hex(pal.pupilL), pupR = hex(pal.pupilR), blushC = hex(pal.blush), acc = hex(pal.accent);
    float b = blinkAmt(), s = squintAmt();
    float breath = sinf(2 * PI_F * t / 4200);
    float lvl = fmaxf(0, fabsf(sinf(t / 95)) * (0.55f + 0.45f * sinf(t / 410)));
    float talkLvl = c[TALK] * lvl;
    float bounceMul = mouth ? 1 : 1 - 0.1f * talkLvl, bounceDy = mouth ? 0 : -3 * talkLvl;
    float wobble = c[WOB] * 9 * sinf(t / 520);
    float jx = c[JIT] * 2.2f * sinf(t / 37), jy = c[JIT] * 2.2f * cosf(t / 29);
    float dx = 0.8f * sinf(t / 730), dy = 0.6f * sinf(t / 910);
    cv.tx = c[SHAKE] * 5 * sinf(t / 16); cv.ty = 1.2f * breath;

    float blushA = c[BLUSH] * (0.75f + 0.25f * sinf(t / 190));
    blushA = fmaxf(blushA, affection * 0.55f);          // a fond buddy has rosy cheeks even at rest
    fillEllipse(cv, 48, 170, 24, 10, blushC, blushA);
    fillEllipse(cv, 272, 170, 24, 10, blushC, blushA);

    drawEye(cv, 0, -1, b, s, breath, bounceMul, bounceDy, wobble, jx, jy, dx, dy, eye, pupL);
    drawEye(cv, 1, 1, b, s, breath, bounceMul, bounceDy, wobble, jx, jy, dx, dy, eye, pupR);

    for (int side = 0; side < 2; side++) {
      float cx = side ? c[Rcx] : c[Lcx], closedY = (side ? c[Rcy] : c[Lcy]) + 0.22f * (side ? c[Rh] : c[Lh]);
      float rot = (side ? c[Rrot] : c[Lrot]) * 0.5f * PI_F / 180;
      if (c[ARCUP] > 0.01f) { Path p; p.setTransform(cx, closedY, rot); p.moveTo(-50, 8); p.quadTo(0, -36, 50, 8, 16); strokeSegs(cv, p.segs, p.n, 8, eye, c[ARCUP]); }
      if (c[ARCDOWN] > 0.01f) { float bob = 2 * sinf(t / 700); Path p; p.setTransform(cx, closedY + bob, rot); p.moveTo(-50, -6); p.quadTo(0, 22, 50, -6, 16); strokeSegs(cv, p.segs, p.n, 6, eye, c[ARCDOWN]); }
    }

    drawGlasses(cv);

    float hp = fmodf(t, 1500) / 1500;
    fillHeart(cv, 278 + 13, 14 - hp * 14 + 22, 21, acc, c[HEART] * (1 - hp));

    if (c[ZS] > 0.01f) {
      const float zd[3][3] = { { 238, 74, 16 }, { 256, 46, 22 }, { 276, 14, 30 } };
      for (int i = 0; i < 3; i++) {
        float p = fmodf(t + i * 800, 2400) / 2400, o = p < 0.15f ? p / 0.15f : 1 - (p - 0.15f) / 0.85f;
        float hgt = zd[i][2] * 0.72f;
        drawText(cv, i == 2 ? "Z" : "z", zd[i][0] + zd[i][2] * 0.3f, zd[i][1] - p * 22 + zd[i][2] * 0.12f, hgt, fmaxf(2.f, zd[i][2] * 0.13f), eye, c[ZS] * o);
      }
    }
    if (c[WAVES] > 0.01f) {
      for (int i = 0; i < 3; i++) {
        Path p; p.setTransform(296, 82, 0);
        if (i == 0) { p.moveTo(3, 22); p.quadTo(9, 32, 3, 42); }
        if (i == 1) { p.moveTo(9, 12); p.quadTo(19, 32, 9, 52); }
        if (i == 2) { p.moveTo(15, 4); p.quadTo(29, 32, 15, 60); }
        strokeSegs(cv, p.segs, p.n, 4, eye, c[WAVES] * (0.2f + 0.8f * fmaxf(0, sinf(t / 250 - i * 0.9f))));
      }
    }
    if (c[DOTS] > 0.01f) {
      int idx = int(t / 300) % 4;
      for (int i = 0; i < 3; i++) fillEllipse(cv, 252 + i * 18, 14, 6, 6, eye, c[DOTS] * (idx > i ? 1 : 0.2f));
    }
    if (c[BANG] > 0.01f) drawText(cv, "!", 306, 6, 24, 5, acc, c[BANG]);
    float ringA = c[RING] * (sinf(t / 90) > 0 ? 1 : 0.35f);
    if (ringA > 0.01f) {
      for (int sd = 0; sd < 2; sd++) for (int i = 0; i < 2; i++) {
        Path p; p.setTransform(sd ? 298.f : 0.f, 86, 0);
        if (!sd && !i) { p.moveTo(18, 6); p.quadTo(6, 28, 18, 50); }
        if (!sd && i)  { p.moveTo(10, 14); p.quadTo(2, 28, 10, 42); }
        if (sd && !i)  { p.moveTo(4, 6); p.quadTo(16, 28, 4, 50); }
        if (sd && i)   { p.moveTo(12, 14); p.quadTo(20, 28, 12, 42); }
        strokeSegs(cv, p.segs, p.n, 4, eye, ringA * (i ? 0.6f : 1));
      }
    }
    if (c[ATIME] > 0.01f) drawText(cv, alarmText, 160, 8, 13, 2.6f, acc, c[ATIME] * (0.5f + 0.5f * fabsf(sinf(t / 120))));
    if (c[CLOCKP] > 0.01f) {
      drawText(cv, timeText ? timeText : "--:--", 160, 84, 58, 8, eye, c[CLOCKP]);
      if (drawDate) drawDate(cv, eye, c[CLOCKP] * 0.75f, drawDateUser);
    }

    if (mouth && updateP < 0) {
      float kc = clampf(c[CAT], 0, 1);
      { Path p; p.setTransform(138, 196, 0); p.moveTo(4, 4); p.quadTo(22, 18, 40, 4); strokeSegs(cv, p.segs, p.n, 5, eye, c[MSMILE] * (1 - kc)); }
      { Path p; p.setTransform(140, 196, 0); p.moveTo(4, 4); p.quadTo(12, 13, 20, 5); p.quadTo(28, 13, 36, 4); strokeSegs(cv, p.segs, p.n, 4, eye, c[MSMILE] * kc); }
      { Path p; p.setTransform(140, 198, 0); p.moveTo(4, 12); p.quadTo(20, 0, 36, 12); strokeSegs(cv, p.segs, p.n, 5, eye, c[MFROWN]); }
      { Path p; p.setTransform(146, 198, 0); p.moveTo(3, 3); p.quadTo(14, 11, 25, 3); strokeSegs(cv, p.segs, p.n, 4, eye, c[MSMALL]); }
      const float r3[4] = { 3, 3, 3, 3 };
      roundRect(cv, 144, 202, 32, 5, r3, eye, c[MFLAT], eye, 0, 0);
      roundRect(cv, 146, 202, 28, 5, r3, eye, c[MSLANT], eye, 0, 0, -10);
      strokeEllipse(cv, 160, 205, 7, 8, 4, eye, c[MRING]);
      if (c[MWAVE] > 0.01f) {
        cv.clipTo(int(132 + cv.tx), int(194 + cv.ty), int(188 + cv.tx), int(214 + cv.ty));
        Path p; p.setTransform(132 - fmodf(t / 25, 36), 196, 0);
        p.moveTo(4, 8); p.quadTo(13, 1, 22, 8); p.quadTo(31, 15, 40, 8); p.quadTo(49, 1, 58, 8); p.quadTo(67, 15, 76, 8); p.quadTo(85, 1, 94, 8);
        strokeSegs(cv, p.segs, p.n, 4, eye, c[MWAVE]);
        cv.resetClip();
      }
      if (c[MTALK] > 0.01f) { float th = 8 + 16 * lvl; const float rt[4] = { 6, 6, 14, 14 }; roundRect(cv, 146, 194, 28, th - 4, rt, blushC, c[MTALK], eye, 4, c[MTALK]); }
      if (c[MALARM] > 0.01f) { fillEllipse(cv, 160, 204, 15, 12, blushC, c[MALARM]); strokeEllipse(cv, 160, 204, 15, 12, 4, eye, c[MALARM]); }
    }
    drawPartyHat(cv);
    cv.tx = 0; cv.ty = 0;
    drawOverlays(cv, eye, acc);
    if (upside) flip180(cv);
  }

private:
  float cur[NP], vel[NP], tgt[NP];
  Mood moodNow = HAPPY;
  bool cat = false, acorn = false;
  int palIdx = 0;
  float t = 0, gx = 0, gy = 0, blinkAt = -99999, squintAt = -99999, nextBlink = 1800, nextGaze = 1200;
  float pokes[8]; int npk = 0;
  struct Ev { float at; Mood m; };
  Ev q[8]; int nq = 0;
  uint32_t rng = 1;

  // ---- extra features (times are in ms on the same clock as t; -1 = not happening)
  float tiltX = 0, tiltY = 0;
  bool tiltOn = false, faceDownOn = false, upside = false, wakeExtras = false;
  float eightAt = -1; int eightIdx = 0;
  Weather weatherNow = WEATHER_NONE; float weatherAt = -1, weatherDur = 0;
  bool timerOn = false; float timerStart = 0, timerDur = 0, breakAt = -1;
  Reminder remindKind = REMIND_WATER; float remindAt = -1;
  static const int NOTE_MS = 10000;
  char note[128] = "", noteBy[24] = ""; float noteAt = -1; bool notePending = false;
  float bdayAt = -1; bool bdayOwn = false;
  struct Bit { float x, y, v, r, s; uint8_t col; };
  static const int NCONF = 40;
  Bit conf[NCONF];
  float affection = 0; int battery = -1; float updateP = -1;
  static const int MAX_BOND_HEARTS = 8;
  char bondMsg[40] = ""; float bondAt = -1, bondDur = 0; int bondHearts = 0; bool missing = false;
  bool bannerOn = false; float bannerAt = 0; char bTitle[40] = "", bLine[72] = "", bBig[24] = "";

  static float fade(float e, float dur, float in, float out) {
    if (e < 0 || e > dur) return 0;
    return fminf(1, e / in) * fminf(1, (dur - e) / out);
  }
  bool weatherShowing() const { return weatherAt >= 0 && t < weatherAt + weatherDur; }
  bool noteShowing() const { return noteAt >= 0 && t >= noteAt + 1200 && t < noteAt + NOTE_MS; }
  void startNote() {
    notePending = false; noteAt = t;
    eightAt = -1; remindAt = -1; weatherAt = -1;
    play1(PETTED, 1500, HAPPY);
  }

  // A float clock in ms loses precision after many hours of running, and the animation would get jerky.
  // So after an hour, every stored time is shifted back together, at a moment the eyes are closed.
  void maybeRebase() {
    if (!(moodNow == SLEEPY || blinkAmt() > 0.95f || t > 8.0e6f)) return;
    const float R = 3.0e6f;
    t -= R; blinkAt -= R; squintAt -= R; nextBlink -= R; nextGaze -= R; timerStart -= R;
    for (int i = 0; i < npk; i++) pokes[i] -= R;
    for (int i = 0; i < nq; i++) q[i].at -= R;
    float* ev[] = { &eightAt, &weatherAt, &breakAt, &remindAt, &noteAt, &bdayAt, &bondAt };
    for (float* e : ev) if (*e >= 0) *e = fmaxf(-1, *e - R);   // anything that old has finished anyway
  }

  // words on screen in one of three sizes
  bool fontFor(int size, const Font*& f, int& ss) const {
    const Font* a = font18 ? font18 : font24;
    const Font* b = font24 ? font24 : font18;
    if (!a) return false;
    if (size == TEXT_SMALL) { f = b; ss = 3; } else if (size == TEXT_MEDIUM) { f = a; ss = 2; } else { f = b; ss = 2; }
    return true;
  }
  void say(Canvas& cv, int size, const char* s, float x, float ymid, int align, RGB c, float a, int n = -1) const {
    const Font* f; int ss;
    if (fontFor(size, f, ss)) drawString(cv, *f, ss, s, x, ymid, align, c, a, n);
  }
  float sayWidth(int size, const char* s) const { const Font* f; int ss; return fontFor(size, f, ss) ? textWidth(*f, ss, s) : 0; }

  // sunglasses for sunny weather (drawn with the face, so they move with it)
  void drawGlasses(Canvas& cv) {
    if (weatherNow != WEATHER_SUN || moodNow == SLEEPY || moodNow == CLOCK) return;
    float a = weatherAt >= 0 ? fade(t - weatherAt, weatherDur, 400, 600) : 0;
    a *= clampf((cur[OPEN] - 0.3f) / 0.5f, 0, 1);
    if (a < 0.01f) return;
    const RGB lens = hex(0x111111), frame = hex(0x4A4A4A), shine = hex(0xFFFFFF);
    const float rl[4] = { 14, 14, 20, 20 }, rs[4] = { 4, 4, 4, 4 };
    for (int s = 0; s < 2; s++) {
      float w = s ? cur[Rw] : cur[Lw], h = s ? cur[Rh] : cur[Lh], cx = s ? cur[Rcx] : cur[Lcx], cy = s ? cur[Rcy] : cur[Lcy];
      float rot = (s ? cur[Rrot] : cur[Lrot]) * 0.35f;
      roundRect(cv, cx - w * 0.54f, cy - h * 0.2f, w * 1.08f, h * 0.42f, rl, lens, 0.94f * a, frame, 3, a, rot);
      roundRect(cv, cx - w * 0.4f, cy - h * 0.13f, w * 0.25f, h * 0.07f, rs, shine, 0.35f * a, shine, 0, 0, rot);
    }
  }
  // party hat on birthdays
  void drawPartyHat(Canvas& cv) {
    if (bdayAt < 0) return;
    float a = fade(t - bdayAt, 8000, 300, 500);
    if (a < 0.01f) return;
    float b = 2 * sinf(t / 260);
    float tri[6] = { 160, 4 + b, 179, 42 + b, 141, 42 + b };
    float band[8] = { 149, 26 + b, 171, 26 + b, 173, 30 + b, 147, 30 + b };
    fillConvex(cv, tri, 3, hex(0x4DA8FF), a);
    fillConvex(cv, band, 4, hex(0xFF5C9E), a);
    fillEllipse(cv, 160, 4 + b, 5.5f, 5.5f, hex(0xFFD21F), a);
  }

  // everything that sits on top of the face: weather, timer, reminders, notes, party, 8-ball, battery, updates
  void drawOverlays(Canvas& cv, RGB eye, RGB acc) {
    const RGB white = hex(0xFFFFFF);
    const float r4[4] = { 4, 4, 4, 4 };
    bool awake = moodNow != SLEEPY;

    // weather
    if (awake && weatherNow != WEATHER_NONE && weatherAt >= 0) {
      float a = fade(t - weatherAt, weatherDur, 400, 600);
      if (a > 0.01f && weatherNow == WEATHER_SUN) {
        const RGB y = hex(0xFFD21F); const float sx = 292, sy = 24;
        Seg rays[8];
        for (int i = 0; i < 8; i++) { float an = i * PI_F / 4 + t / 2000; rays[i] = { sx + cosf(an) * 13, sy + sinf(an) * 13, sx + cosf(an) * 18, sy + sinf(an) * 18 }; }
        fillEllipse(cv, sx, sy, 9, 9, y, a);
        strokeSegs(cv, rays, 8, 2.5f, y, a);
      } else if (a > 0.01f) {
        bool rain = weatherNow == WEATHER_RAIN;
        if (rain) {
          const RGB rc = hex(0x7CC4FF);
          for (int k = 0; k < 26; k++) {
            float x = fmodf(k * 37 + t * 0.05f, 330), y = fmodf(k * 53 + t / 4, 260) - 10;
            Seg s = { x, y, x - 3, y + 9 };
            strokeSegs(cv, &s, 1, 2, rc, 0.6f * a);
          }
        }
        const RGB cc = hex(rain ? 0x9AA6B8 : 0xE6ECF3); float cx0 = 280 + sinf(t / 900) * 4;
        const float r6[4] = { 6, 6, 6, 6 };
        fillEllipse(cv, cx0 - 14, 22, 10, 10, cc, a); fillEllipse(cv, cx0, 16, 13, 13, cc, a); fillEllipse(cv, cx0 + 14, 22, 10, 10, cc, a);
        roundRect(cv, cx0 - 24, 20, 48, 12, r6, cc, a, cc, 0, 0);
      }
    }

    // study timer: bar across the top, minutes left at the bottom
    if (timerOn && awake) {
      float pr = clampf((t - timerStart) / timerDur, 0, 1);
      roundRect(cv, 60, 8, 200, 8, r4, white, 0.15f, white, 0, 0);
      if (pr > 0.005f) roundRect(cv, 60, 8, fmaxf(8, 200 * pr), 8, r4, acc, 1, acc, 0, 0);
      char buf[24]; snprintf(buf, sizeof buf, "%u min left", unsigned((timerLeftMs() + 59999) / 60000));
      say(cv, TEXT_SMALL, buf, 160, 228, 1, eye, 0.8f);
    }
    if (breakAt >= 0) say(cv, TEXT_LARGE, "Break time!", 160, 226, 1, acc, fade(t - breakAt, 5000, 200, 500));

    // water / stretch nudge: a pill at the top with a little icon
    if (remindAt >= 0) {
      float a = fade(t - remindAt, 6000, 150, 500);
      if (a > 0.01f) {
        const char* msg = remindKind == REMIND_WATER ? "Sip some water" : "Stretch break!";
        float pw = sayWidth(TEXT_SMALL, msg) + 46, px = 160 - pw / 2, ix = px + 18;
        const float r14[4] = { 14, 14, 14, 14 };
        roundRect(cv, px, 3, pw, 28, r14, white, 0.14f * a, white, 0, 0);
        if (remindKind == REMIND_WATER) {
          const RGB wc = hex(0x7CC4FF);
          float tri[6] = { ix, 7.5f, ix + 5.4f, 17.5f, ix - 5.4f, 17.5f };
          fillConvex(cv, tri, 3, wc, a);
          fillEllipse(cv, ix, 19.5f, 6, 6, wc, a);
        } else {
          Seg fig[5] = { { ix, 13, ix, 21 }, { ix, 14.5f, ix - 5.5f, 7 }, { ix, 14.5f, ix + 5.5f, 7 }, { ix, 21, ix - 4, 27 }, { ix, 21, ix + 4, 27 } };
          strokeSegs(cv, fig, 5, 2.4f, eye, a);
          fillEllipse(cv, ix, 9, 2.8f, 2.8f, eye, a);
        }
        say(cv, TEXT_SMALL, msg, px + 32, 17, 0, eye, a);
      }
    }

    // grows a personality: once it's fond of you, a little heart floats up now and then, more often the closer you are
    bool bondShowing = bondAt >= 0 && t < bondAt + bondDur;
    if (affection >= 0.35f && awake && !bondShowing) {
      float period = 4500 - 2000 * clampf((affection - 0.35f) / 0.65f, 0, 1);
      float ph = fmodf(t, period);
      if (ph < 1300) { float k = ph / 1300; fillHeart(cv, 26, 48 - k * 14, 15, acc, (1 - k) * clampf((affection - 0.25f) / 0.6f, 0, 1)); }
    }

    // missing you: a small grey heart droops by its cheek every so often, with a quiet "miss you..."
    if (missing && awake && !bondShowing && !overlayActive()) {
      float ph = fmodf(t, 9000);
      if (ph < 3000) {
        float a = fade(ph, 3000, 500, 800);
        const RGB grey = hex(0x8A8F99);
        fillHeart(cv, 294, 40 + ph / 3000 * 10, 13, grey, 0.8f * a);
        say(cv, TEXT_SMALL, "miss you...", 160, 228, 1, grey, 0.75f * a);
      }
    }

    // bond moment: hearts rise across the screen, with a message along the bottom
    if (bondShowing) {
      float e = t - bondAt, a = fade(e, bondDur, 250, 600);
      for (int i = 0; i < bondHearts; i++) {
        float start = i * 260.f, k = (e - start) / 2200.f;
        if (k < 0 || k > 1) continue;
        float x = 40 + (i * 37) % 240 + sinf((e + i * 500) / 300) * 6;
        fillHeart(cv, x, 230 - k * 190, 12 + (i % 3) * 4, acc, (1 - k) * a);
      }
      if (bondMsg[0]) say(cv, TEXT_MEDIUM, bondMsg, 160, 226, 1, acc, a);
    }

    // switched off in the app, woken by a touch: say so quietly
    if (resting && awake && !bondShowing) say(cv, TEXT_SMALL, "resting - turn me on in the app", 160, 228, 1, eye, 0.6f);

    // battery
    if (battery >= 0 && battery <= 20) {
      const RGB g = hex(0xBBBBBB), red = hex(0xFF4D4D);
      const float r3[4] = { 3, 3, 3, 3 }, r2[4] = { 2, 2, 2, 2 };
      float blink = battery <= 5 ? 0.4f + 0.6f * fabsf(sinf(t / 300)) : 1;
      roundRect(cv, 12, 10, 30, 15, r3, g, 0, g, 2, 1);
      roundRect(cv, 42, 14, 3, 7, r2, g, 1, g, 0, 0);
      roundRect(cv, 14.5f, 12.5f, fmaxf(2.5f, 25 * battery / 100.f), 10, r2, red, blink, red, 0, 0);
      if (battery <= 5) say(cv, TEXT_SMALL, "charge me", 160, 228, 1, red, 0.5f + 0.5f * fabsf(sinf(t / 300)));
    }

    // note card
    if (noteAt >= 0 && t >= noteAt + 1200) {
      float e = t - noteAt - 1200, a = fade(e, NOTE_MS - 1200, 250, 400);
      if (a > 0.01f) {
        const float r14[4] = { 14, 14, 14, 14 };
        const RGB card = hex(0xFFF4F8), ink = hex(0x2A1520);
        float cy = 136 + (1 - clampf(e / 250, 0, 1)) * 16;
        roundRect(cv, 20, cy, 280, 98, r14, card, 0.95f * a, card, 0, 0);
        char from[40]; snprintf(from, sizeof from, "From %s", noteBy);
        say(cv, TEXT_SMALL, from, 38, cy + 16, 0, hex(0xC8106B), a);
        const Font* f; int ss;
        if (fontFor(TEXT_MEDIUM, f, ss)) {
          int st[3], ln[3]; bool more = false;
          int n = wrapText(*f, ss, note, 244, st, ln, 3, &more);
          for (int i = 0; i < n; i++) {
            float ly = cy + 39 + i * 22;
            if (i == n - 1 && more) {                       // too long: end the last line with "..."
              char tmp[136]; int L = ln[i];
              for (;;) { memcpy(tmp, note + st[i], L); memcpy(tmp + L, "...", 4); if (L == 0 || textWidth(*f, ss, tmp) <= 244) break; L--; }
              drawString(cv, *f, ss, tmp, 38, ly, 0, ink, a);
            } else drawString(cv, *f, ss, note + st[i], 38, ly, 0, ink, a, ln[i]);
          }
        }
      }
    }

    // birthday: confetti and a banner (the hat is drawn with the face)
    if (bdayAt >= 0) {
      float e = t - bdayAt, a = fade(e, 8000, 300, 500);
      if (a > 0.01f) {
        static const uint32_t CONFETTI[6] = { 0xFF5C9E, 0xFFD21F, 0x4DA8FF, 0x6EE7B7, 0xC4A1FF, 0xFF9F43 };
        for (int i = 0; i < NCONF; i++) {
          const Bit& b = conf[i];
          float y = b.y + b.v * e / 1000;
          while (y > 250) y -= 270;
          float x = b.x + sinf(e / 300 + b.r) * 6, rot = b.r + e / 400, cs = cosf(rot), sn = sinf(rot), hx = b.s / 2, hy = b.s;
          const float L[4][2] = { { -hx, -hy }, { hx, -hy }, { hx, hy }, { -hx, hy } };
          float pts[8];
          for (int k = 0; k < 4; k++) { pts[2 * k] = x + L[k][0] * cs - L[k][1] * sn; pts[2 * k + 1] = y + L[k][0] * sn + L[k][1] * cs; }
          fillConvex(cv, pts, 4, hex(CONFETTI[b.col % 6]), a);
        }
        say(cv, TEXT_LARGE, bdayOwn ? "It's my birthday!" : "Happy birthday!", 160, 226, 1, hex(0xFFD21F), a);
      }
    }

    // set-up card (Wi-Fi, pairing code)
    if (bannerOn) {
      float a = clampf((t - bannerAt) / 250, 0, 1);
      const float r14[4] = { 14, 14, 14, 14 };
      const RGB card = hex(0xFFF4F8), ink = hex(0x2A1520);
      const float cy = 128;
      roundRect(cv, 20, cy, 280, 106, r14, card, 0.96f * a, card, 0, 0);
      say(cv, TEXT_SMALL, bTitle, 160, cy + 16, 1, hex(0xC8106B), a);
      const Font* f; int ss;
      if (fontFor(TEXT_MEDIUM, f, ss)) {
        int st[2], ln[2]; int n = wrapText(*f, ss, bLine, 252, st, ln, 2);
        for (int i = 0; i < n; i++) drawString(cv, *f, ss, bLine + st[i], 160, cy + 39 + i * 21 - (n - 1) * 4, 1, ink, a, ln[i]);
      }
      say(cv, TEXT_LARGE, bBig, 160, cy + 86, 1, ink, a);
    }

    // magic 8-ball answer: the face goes dark behind a blue triangle
    if (eightAt >= 0 && t >= eightAt) {
      float a = fade(t - eightAt, 4200, 700, 700);
      if (a > 0.01f) {
        dimAll(cv, 0.85f * a);
        float tri[6] = { 160, 30, 262, 200, 58, 200 };
        fillConvex(cv, tri, 3, hex(0x1F3FBF), a);
        const Font* f; int ss;
        if (fontFor(TEXT_MEDIUM, f, ss)) {
          const char* ans = EIGHT_BALL[eightIdx];
          int st[3], ln[3]; int n = wrapText(*f, ss, ans, 118, st, ln, 3);
          for (int i = 0; i < n; i++) drawString(cv, *f, ss, ans + st[i], 160, 150 - (n - 1) * 11 + i * 22, 1, hex(0xE8EEFF), a, ln[i]);
        }
      }
    }

    // update over Wi-Fi
    if (updateP >= 0) {
      say(cv, TEXT_SMALL, updateP >= 1 ? "New trick learned!" : "Learning a new trick...", 160, 206, 1, eye, 0.9f);
      roundRect(cv, 70, 222, 180, 8, r4, white, 0.15f, white, 0, 0);
      roundRect(cv, 70, 222, fmaxf(8, 180 * clampf(updateP, 0, 1)), 8, r4, acc, 1, acc, 0, 0);
    }

    // upside down: complain (the whole picture is flipped after this, so it reads the right way up)
    if (upside && moodNow == ANNOYED) say(cv, TEXT_LARGE, "Flip me back!", 160, 226, 1, eye, 0.6f + 0.4f * fabsf(sinf(t / 250)));
  }

  float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) * (1.0f / 16777216.0f); }
  float between(float a, float b) { return a + rnd() * (b - a); }
  void push(float at, Mood m) { if (nq < 8) q[nq++] = { at, m }; }

  void pickPalette() {
    int total = 0; for (int i = 0; i < NUM_PALETTES; i++) total += PALETTES[i].weight;
    int i = palIdx;
    for (int tries = 0; tries < 8 && i == palIdx; tries++) {
      float r = rnd() * total;
      for (i = 0; i < NUM_PALETTES - 1; i++) { r -= PALETTES[i].weight; if (r < 0) break; }
    }
    palIdx = i;
  }
  void go(Mood m) {
    bool c2 = cat, a2 = acorn;
    if (m == SLEEPY) {
      c2 = false; a2 = false;
      wakeExtras = false; eightAt = -1; remindAt = -1; weatherAt = -1; breakAt = -1; bdayAt = -1;
      if (noteAt >= 0 && t < noteAt + NOTE_MS) notePending = true;   // cut short: show it again on waking
      noteAt = -1;
    }
    else if (moodNow == SLEEPY) {        // waking up: roll a new pupil shape and colour set
      float r = rnd() * 100;
      c2 = r < catChancePct;
      a2 = !c2 && r < catChancePct + acornChancePct;
      pickPalette();
      wakeExtras = true;
    }
    if (m == HAPPY) { gx = 0; gy = 0; }
    bool changed = m != moodNow;
    moodNow = m; cat = c2; acorn = a2;
    if (changed && onMood) onMood(m, onMoodUser);
  }
  void computeTargets() {
    for (int k = 0; k < NP; k++) tgt[k] = BASE[k];
    KV kv[24]; int n = moodPatch(moodNow, kv);
    for (int i = 0; i < n; i++) tgt[kv[i].k] = kv[i].v;
    tgt[CAT] = cat ? 1 : 0;
    tgt[ACORN] = acorn ? 1 : 0;
    float w = 0, ggx = gx, ggy = gy;
    if (moodNow == HAPPY || moodNow == TALKING) w = 1;
    if (moodNow == LISTENING) w = 0.35f;
    if (moodNow == BORED) { w = 1; ggx = 6 + fabsf(gx) * 0.5f; ggy = 4; }
    // where to look: tilt beats reading a note or studying, which beat looking around
    bool canLook = moodNow == HAPPY || moodNow == TALKING || moodNow == BORED;
    if (canLook && tiltOn) { w = 1; ggx = tiltX * 26; ggy = tiltY * 16; }
    else if (moodNow == HAPPY && (noteShowing() || bannerOn)) { w = 1; ggx = 0; ggy = 16; }
    else if (moodNow == HAPPY && timerOn) { w = 1; ggx = 0; ggy = 14; }
    if (w > 0) {
      tgt[Lpx] += ggx * w; tgt[Rpx] += ggx * w; tgt[Lpy] += ggy * w; tgt[Rpy] += ggy * w;
      tgt[Lcx] += ggx * 0.15f * w; tgt[Rcx] += ggx * 0.15f * w;
      tgt[Rw] += ggx * 0.35f * w; tgt[Rh] += ggx * 0.3f * w; tgt[Lw] -= ggx * 0.35f * w; tgt[Lh] -= ggx * 0.3f * w;
    }
    // a glum, tired look for rainy weather and a low battery
    if (moodNow == HAPPY && ((weatherNow == WEATHER_RAIN && weatherShowing()) || (battery >= 0 && battery <= 20))) {
      tgt[LT] = 0.3f; tgt[MSMILE] = 0; tgt[MFLAT] = 1; tgt[Lh] -= 8; tgt[Rh] -= 8; tgt[Lpy] += 6; tgt[Rpy] += 6;
    }
  }
  float blinkAmt() const {
    float d = t - blinkAt; bool slow = moodNow == BORED;
    float cl = slow ? 140 : 70, ho = slow ? 90 : 35, op = slow ? 320 : 160;
    if (d < 0) return 0;
    if (d < cl) { float x = d / cl; return x * x; }
    if (d < cl + ho) return 1;
    if (d < cl + ho + op) { float y = (d - cl - ho) / op; return (1 - y) * (1 - y) * (1 - y); }
    return 0;
  }
  float squintAmt() const {
    float d = t - squintAt;
    if (d < 0) return 0;
    if (d < 60) return d / 60;
    if (d < 180) return 1;
    if (d < 420) { float x = (d - 180) / 240; return (1 - x) * (1 - x); }
    return 0;
  }

  // one egg-shaped eye: pupil, cat slit, spiral and lids, all anti-aliased
  void drawEye(Canvas& cv, int side, float sg, float b, float s, float breath, float bounceMul, float bounceDy,
               float wobble, float jx, float jy, float dx, float dy, RGB eyeC, RGB pupC) {
    const float* c = cur;
    float w = side ? c[Rw] : c[Lw], h = (side ? c[Rh] : c[Lh]) * (1 + 0.008f * breath);
    float cx = side ? c[Rcx] : c[Lcx], cy = (side ? c[Rcy] : c[Lcy]) + bounceDy;
    float sy = fmaxf(0.03f, c[OPEN] * (1 - 0.94f * b) * (1 - 0.35f * s) * bounceMul);
    float rot = ((side ? c[Rrot] : c[Lrot]) + wobble * sg) * PI_F / 180;
    float eyeOp = clampf((c[OPEN] - 0.06f) / 0.3f, 0, 1);
    if (eyeOp < 0.01f) return;
    float a = w / 2, ym = 0.06f * h, bt = 0.56f * h, bb = 0.44f * h, piv = 0.22f * h;
    float ps = c[PUPIL_SZ], kc = clampf(c[CAT], 0, 1), ka = clampf(c[ACORN], 0, 1);
    // pupil shape: round (ps x ps), cat slit, or acorn (a tall oval reaching past the bottom of the eye,
    // leaving a crescent of eye colour around it). Acorn size still follows the mood through ps.
    float sz = ps / 60.f;
    float acW = 0.46f * w * sz, acH = h * (0.72f + 0.28f * sz);
    float pw = ps + (fmaxf(6, ps * 0.42f) - ps) * kc + (acW - ps) * ka;
    float ph = ps + (0.66f * h - ps) * kc + (acH - ps) * ka;
    float px = (side ? c[Rpx] : c[Lpx]) + jx + dx, py = (side ? c[Rpy] : c[Lpy]) + jy + dy + 4 * b;
    py += ka * 0.18f * h;   // the acorn sits low, so its bottom is hidden by the bottom of the eye
    float pra = pw / 2, prb = ph / 2;
    bool pupil = c[PO] > 0.01f;
    float lt = fmaxf(0, c[LT]); bool lid = lt > 0.005f;
    float lidY0 = -h / 2 + lt * h, lth = c[LA] * -sg * PI_F / 180, lc = cosf(lth), ls = sinf(lth);
    float cr = cosf(rot), sr = sinf(rot);
    float pivX = cx + cv.tx, pivY = cy + piv + cv.ty;
    // bounding box of the eye on screen
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    const float us[2] = { -a, a }, vs[2] = { ym - bt, ym + bb };
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {
      float lx = us[i], ly = (vs[j] - piv) * sy;
      float X = pivX + lx * cr - ly * sr, Y = pivY + lx * sr + ly * cr;
      minx = fminf(minx, X); maxx = fmaxf(maxx, X); miny = fminf(miny, Y); maxy = fmaxf(maxy, Y);
    }
    int x0, y0, x1, y1;
    cv.bounds(minx - 2, miny - 2, maxx + 2, maxy + 2, x0, y0, x1, y1);
    const RGB black = { 0, 0, 0 };
    float ia2 = 1 / (a * a), ibt2 = 1 / (bt * bt), ibb2 = 1 / (bb * bb), ipa2 = 1 / (pra * pra), ipb2 = 1 / (prb * prb), isy = 1 / sy;
    // Most pixels of an eye are plain eye colour or plain pupil colour. Each row works out its solid spans once (the
    // shapes are convex, so a binary search finds their ends) and fills them with ready-made colours; only the few
    // edge pixels run the full maths. Rows where that shortcut can't be trusted fall back to the full maths throughout.
    const bool opaque = eyeOp >= 0.996f;
    const float po = c[PO];
    const uint16_t pkEye = pack(eyeC), pkPup = pack(mix(eyeC, pupC, po));
    const float kx = -sr * isy;                                   // how fast v changes along a row
    const float pM = fmaxf(ipa2, ipb2 * isy * isy), pS = (sqrtf(pM) + sqrtf(pM + 4)) * 0.5f, pFar = pS * pS * 1.02f + 0.01f;
    const float pDen = cr * cr * ipa2 + kx * kx * ipb2;
    int hintL = -1, hintR = -1;                                    // last row's solid span: this row's is close by
    for (int y = y0; y < y1; y++) {
      float Y = y + 0.5f - pivY;
      auto outer = [&](int x) -> float {
        float X = x + 0.5f - pivX;
        float x1r = X * cr + Y * sr, y1r = -X * sr + Y * cr;
        float u = x1r, v = y1r * isy + piv, vv = v - ym;
        float ib2 = vv < 0 ? ibt2 : ibb2;
        float F = u * u * ia2 + vv * vv * ib2 - 1;
        float gxx = 2 * u * ia2, gyy = 2 * vv * ib2 * isy;
        return edgeCov(F, gxx * gxx + gyy * gyy);
      };
      auto pup = [&](int x) -> float {
        float X = x + 0.5f - pivX;
        float x1r = X * cr + Y * sr, y1r = -X * sr + Y * cr;
        float u = x1r, v = y1r * isy + piv;
        float pu = u - px, pv = v - py;
        float Fp = pu * pu * ipa2 + pv * pv * ipb2 - 1;
        float gpx = 2 * pu * ipa2, gpy = 2 * pv * ipb2 * isy;
        return edgeCov(Fp, gpx * gpx + gpy * gpy);
      };
      // ---- the eye's own outline on this row: solid span [sL, sR] inside the drawn span [eL, eR]
      int sL = 1, sR = 0, eL = x0, eR = x1 - 1;
      if (eL > eR) continue;
      {
        float uY = Y * sr, vvY = Y * cr * isy + piv - ym, Xm;
        float Xt = -(uY * cr * ia2 + vvY * kx * ibt2) / (cr * cr * ia2 + kx * kx * ibt2);
        if (vvY + kx * Xt < 0) Xm = Xt;
        else {
          float Xb = -(uY * cr * ia2 + vvY * kx * ibb2) / (cr * cr * ia2 + kx * kx * ibb2);
          Xm = (vvY + kx * Xb >= 0 || fabsf(kx) < 1e-9f) ? Xb : -vvY / kx;
        }
        float fm = Xm + pivX - 0.5f;
        int xm = fm < float(x0) ? x0 : (fm > float(x1 - 1) ? x1 - 1 : int(fm + 0.5f));
        if (outer(xm) == 1.f) {
          // left end of the solid span: somewhere in [lo, hi], hi is solid. Start near last row's end and step outwards.
          int lo = x0, hi = xm;
          if (hintL >= 0) {
            int h = hintL < x0 ? x0 : (hintL > xm ? xm : hintL);
            if (outer(h) == 1.f) { hi = h; for (int st = 1, q = h - 1; q >= x0; st <<= 1, q -= st) { if (outer(q) == 1.f) hi = q; else { lo = q + 1; break; } } }
            else { lo = h + 1; for (int st = 1, q = h + 1; q < xm; st <<= 1, q += st) { if (outer(q) == 1.f) { hi = q; break; } lo = q + 1; } }
          }
          while (lo < hi) { int mid = (lo + hi) >> 1; if (outer(mid) == 1.f) hi = mid; else lo = mid + 1; }
          sL = lo; lo = xm; hi = x1 - 1;
          if (hintR >= 0) {
            int h = hintR > x1 - 1 ? x1 - 1 : (hintR < xm ? xm : hintR);
            if (outer(h) == 1.f) { lo = h; for (int st = 1, q = h + 1; q <= x1 - 1; st <<= 1, q += st) { if (outer(q) == 1.f) lo = q; else { hi = q - 1; break; } } }
            else { hi = h - 1; for (int st = 1, q = h - 1; q > xm; st <<= 1, q -= st) { if (outer(q) == 1.f) { lo = q; break; } hi = q - 1; } }
          }
          while (lo < hi) { int mid = (lo + hi + 1) >> 1; if (outer(mid) == 1.f) lo = mid; else hi = mid - 1; }
          sR = lo; hintL = sL; hintR = sR;
          eL = sL; for (int x = sL - 1, z = 0; x >= x0 && z < 3; x--) { if (outer(x) > 0) { eL = x; z = 0; } else z++; }
          eR = sR; for (int x = sR + 1, z = 0; x < x1 && z < 3; x++) { if (outer(x) > 0) { eR = x; z = 0; } else z++; }
        }
      }
      // ---- the pupil on this row: 0 = none, 1 = spans (solid [psL, psR] inside [pL, pR]), 2 = work out every pixel
      int pmode = 0, pL = 0, pR = -1, psL = 1, psR = 0;
      if (pupil) {
        float pu0 = Y * sr - px, pv0 = Y * cr * isy + piv - py;
        float Xp = pDen > 1e-12f ? -(pu0 * cr * ipa2 + pv0 * kx * ipb2) / pDen : 0;
        float pum = pu0 + cr * Xp, pvm = pv0 + kx * Xp;
        if (!(pum * pum * ipa2 + pvm * pvm * ipb2 >= pFar)) {      // not clearly off this row
          float fm = Xp + pivX - 0.5f;
          int pm = fm < float(eL) ? eL : (fm > float(eR) ? eR : int(fm + 0.5f));
          if (pup(pm) == 1.f) {
            pmode = 1;
            int lo = eL, hi = pm;
            while (lo < hi) { int mid = (lo + hi) >> 1; if (pup(mid) == 1.f) hi = mid; else lo = mid + 1; }
            psL = lo; lo = pm; hi = eR;
            while (lo < hi) { int mid = (lo + hi + 1) >> 1; if (pup(mid) == 1.f) lo = mid; else hi = mid - 1; }
            psR = lo;
            pL = psL; for (int x = psL - 1, z = 0; x >= eL && z < 3; x--) { if (pup(x) > 0) { pL = x; z = 0; } else z++; }
            pR = psR; for (int x = psR + 1, z = 0; x <= eR && z < 3; x++) { if (pup(x) > 0) { pR = x; z = 0; } else z++; }
          } else pmode = 2;
        }
      }
      uint16_t* row = cv.fb + y * W;
      auto pixel = [&](int x) {                                    // the full maths for one pixel
        float cov = (x >= sL && x <= sR) ? 1.f : outer(x);
        if (cov <= 0) return;
        float cp = 0;
        if (pmode == 1) { if (x >= pL && x <= pR) cp = (x >= psL && x <= psR) ? po : pup(x) * po; }
        else if (pmode == 2) cp = pup(x) * po;
        float cl = 0;
        if (lid) {
          float X = x + 0.5f - pivX;
          float x1r = X * cr + Y * sr, y1r = -X * sr + Y * cr;
          float u = x1r, v = y1r * isy + piv;
          float qy = v - lidY0, qyr = -u * ls + qy * lc;
          cl = clampf(0.5f - qyr * sy, 0, 1);
        }
        if (opaque && cov == 1.f && cl <= 0) {
          if (cp <= 0) { row[x] = pkEye; return; }
          if (cp == po) { row[x] = pkPup; return; }
        }
        RGB col = eyeC;
        if (cp > 0) col = mix(col, pupC, cp);
        if (cl > 0) col = mix(col, black, cl);
        cv.put(x, y, col, cov * eyeOp);
      };
      if (!opaque || lid || pmode == 2 || sL > sR) { for (int x = eL; x <= eR; x++) pixel(x); continue; }
      // the usual case: edge pixels one by one, everything between them in plain runs
      auto run = [&](int a0, int b0, uint16_t pk) { for (int x = a0; x <= b0; x++) row[x] = pk; };
      for (int x = eL; x < sL; x++) pixel(x);
      if (pmode == 0 || pR < sL || pL > sR) run(sL, sR, pkEye);
      else {
        int a1 = pL > sL ? pL : sL, b1 = pR < sR ? pR : sR;          // the pupil's part of the solid span
        int a2 = psL > a1 ? psL : a1, b2 = psR < b1 ? psR : b1;      // and its solid middle
        run(sL, a1 - 1, pkEye);
        if (a2 > b2) { for (int x = a1; x <= b1; x++) pixel(x); }
        else { for (int x = a1; x < a2; x++) pixel(x); run(a2, b2, pkPup); for (int x = b2 + 1; x <= b1; x++) pixel(x); }
        run(b1 + 1, sR, pkEye);
      }
      for (int x = sR + 1; x <= eR; x++) pixel(x);
    }
    // dizzy spiral
    if (c[SO] > 0.01f) {
      float spin = fmodf(t / 600 * 360, 360) * sg * PI_F / 180, k = w / 80, cs = cosf(spin), sn = sinf(spin);
      Path p; p.setTransform(0, 0, 0);
      bool first = true;
      // half-turn arcs: r, centre x (spiral units, centre y = 32), start angle (rad, y-down)
      const float arcs[6][2] = { { 3, 35 }, { 6, 32 }, { 9, 35 }, { 12, 32 }, { 15, 35 }, { 18, 32 } };
      for (int i = 0; i < 6; i++) {
        float r = arcs[i][0], acx = arcs[i][1];
        float a0 = (i % 2 == 0) ? PI_F : 0.f;           // even arcs go over the top, odd ones under
        for (int j = 0; j <= 10; j++) {
          float ang = a0 + PI_F * j / 10.f;
          float sx = acx + r * cosf(ang), syy = 32 + r * sinf(ang);
          // spiral -> eye-local (rotate by spin, scale k), then eye-local -> screen
          float lx0 = (sx - 32) * k, ly0 = (syy - 32) * k;
          float u = lx0 * cs - ly0 * sn, v = lx0 * sn + ly0 * cs;
          float lx = u, ly = (v - piv) * sy;
          float X = cx + lx * cr - ly * sr, Y = cy + piv + lx * sr + ly * cr;
          if (first) { p.moveTo(X, Y); first = false; } else p.lineTo(X, Y);
        }
      }
      strokeSegs(cv, p.segs, p.n, 5 * k, pupC, eyeOp * c[SO]);
    }
  }
};

}  // namespace face
