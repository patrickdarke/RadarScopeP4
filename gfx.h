#pragma once
#include <cstdint>

// Software-rendering primitives for RGB565 framebuffers: a plain raster layer
// (pixels, lines, rects, circles, 5x7 bitmap text) plus an anti-aliased layer
// (alpha blending, SDF discs/rings/rounded-rects, baked AA fonts) used by the
// radar scope renderer.
namespace gfx {

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// A drawable RGB565 surface. `fb` is row-major, `w`*`h` pixels.
struct Canvas {
  uint16_t* fb;
  int w;
  int h;
};

void px(const Canvas& c, int x, int y, uint16_t col);
void fill(const Canvas& c, uint16_t col);
void hline(const Canvas& c, int x0, int x1, int y, uint16_t col);
void line(const Canvas& c, int x0, int y0, int x1, int y1, uint16_t col);
void thickLine(const Canvas& c, int x0, int y0, int x1, int y1, int t, uint16_t col);
void rect(const Canvas& c, int x0, int y0, int x1, int y1, uint16_t col);
void fillRect(const Canvas& c, int x0, int y0, int x1, int y1, uint16_t col);
void circle(const Canvas& c, int cx, int cy, int r, uint16_t col);
void circleT(const Canvas& c, int cx, int cy, int r, int t, uint16_t col);
void fillCircle(const Canvas& c, int cx, int cy, int r, uint16_t col);
void fillTri(const Canvas& c, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col);

// Filled radial band from angle a0 to a1 (degrees, 0 = up/north, clockwise),
// spanning radii [r - thickness, r].
void arcBand(const Canvas& c, int cx, int cy, int r, int thickness, double a0Deg, double a1Deg,
             uint16_t col);

// 5x7 bitmap text (scale = integer pixel multiplier).
void drawChar(const Canvas& c, int x, int y, char ch, int s, uint16_t col);
void drawText(const Canvas& c, int x, int y, const char* str, int s, uint16_t col);
void drawTextCentered(const Canvas& c, int cx, int y, const char* str, int s, uint16_t col);
int textW(const char* str, int s);

// Blit a `sw`x`sh` source buffer onto `dst` with its top-left at (ox,oy).
// If `circleMask`, only the inscribed circle of the source square is copied.
void blit(const Canvas& dst, int ox, int oy, const uint16_t* src, int sw, int sh, bool circleMask);

// ---- Anti-aliased layer (RadarScope polish pass) ---------------------------

// Blend `col` over the destination pixel with alpha 0..255.
void blendPx(const Canvas& c, int x, int y, uint16_t col, uint8_t a);

// Solid disc / ring (thickness t) with ~1px analytic edge AA.
void aaDisc(const Canvas& c, float cx, float cy, float r, uint16_t col);
void aaRing(const Canvas& c, float cx, float cy, float r, float t, uint16_t col);
// Soft radial glow: alpha = maxA * (1 - d/r)^2.
void glowDisc(const Canvas& c, float cx, float cy, float r, uint16_t col, uint8_t maxA);

// Rounded rect: translucent fill (alpha 0..255) and a 1px AA border.
void aaRoundRect(const Canvas& c, int x0, int y0, int x1, int y1, float rad,
                 uint16_t fillCol, uint8_t fillA, uint16_t borderCol);

// Anti-aliased monospaced text from a baked alpha-cell font (see font_aa.h).
// Glyphs are fixed cellW x cellH cells of 8-bit alpha, ASCII 32-126; y = top.
struct AAFontDesc {
  const uint8_t* bits;
  int cellW;
  int cellH;
};
void drawTextAA(const Canvas& c, int x, int y, const char* str, const AAFontDesc& f, uint16_t col);
void drawTextAACentered(const Canvas& c, int cx, int y, const char* str, const AAFontDesc& f,
                        uint16_t col);
int textWAA(const char* str, const AAFontDesc& f);

}  // namespace gfx
