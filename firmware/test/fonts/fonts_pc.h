// The two LovyanGFX fonts the buddy uses, loaded on a PC for testing.
#pragma once
#include <stdint.h>
#define PROGMEM
struct GFXglyph { uint32_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; };
struct GFXfont { uint8_t* bitmap; GFXglyph* glyph; uint16_t first, last; uint8_t yAdvance; };
#include "FreeSansBold18pt7b.h"
#include "FreeSansBold24pt7b.h"
static const face::Font PC_FONT18 = { FreeSansBold18pt7b.bitmap, (const face::Glyph*)FreeSansBold18pt7b.glyph, FreeSansBold18pt7b.first, FreeSansBold18pt7b.last, FreeSansBold18pt7b.yAdvance };
static const face::Font PC_FONT24 = { FreeSansBold24pt7b.bitmap, (const face::Glyph*)FreeSansBold24pt7b.glyph, FreeSansBold24pt7b.first, FreeSansBold24pt7b.last, FreeSansBold24pt7b.yAdvance };
