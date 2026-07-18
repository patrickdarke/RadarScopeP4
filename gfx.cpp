#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "font5x7.h"

namespace gfx {

void px(const Canvas& c, int x, int y, uint16_t col) {
  if (x >= 0 && x < c.w && y >= 0 && y < c.h) c.fb[y * c.w + x] = col;
}

void fill(const Canvas& c, uint16_t col) {
  for (int i = 0; i < c.w * c.h; i++) c.fb[i] = col;
}

void hline(const Canvas& c, int x0, int x1, int y, uint16_t col) {
  if (x0 > x1) std::swap(x0, x1);
  for (int x = x0; x <= x1; x++) px(c, x, y, col);
}

void line(const Canvas& c, int x0, int y0, int x1, int y1, uint16_t col) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    px(c, x0, y0, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void thickLine(const Canvas& c, int x0, int y0, int x1, int y1, int t, uint16_t col) {
  for (int i = 0; i < t; i++) {
    line(c, x0 + i, y0, x1 + i, y1, col);
    line(c, x0, y0 + i, x1, y1 + i, col);
  }
}

void rect(const Canvas& c, int x0, int y0, int x1, int y1, uint16_t col) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  hline(c, x0, x1, y0, col);
  hline(c, x0, x1, y1, col);
  for (int y = y0; y <= y1; y++) { px(c, x0, y, col); px(c, x1, y, col); }
}

void fillRect(const Canvas& c, int x0, int y0, int x1, int y1, uint16_t col) {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  for (int y = y0; y <= y1; y++) hline(c, x0, x1, y, col);
}

void circle(const Canvas& c, int cx, int cy, int r, uint16_t col) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    px(c, cx + x, cy + y, col); px(c, cx + y, cy + x, col);
    px(c, cx - y, cy + x, col); px(c, cx - x, cy + y, col);
    px(c, cx - x, cy - y, col); px(c, cx - y, cy - x, col);
    px(c, cx + y, cy - x, col); px(c, cx + x, cy - y, col);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

void circleT(const Canvas& c, int cx, int cy, int r, int t, uint16_t col) {
  for (int i = 0; i < t; i++) circle(c, cx, cy, r - i, col);
}

void fillCircle(const Canvas& c, int cx, int cy, int r, uint16_t col) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r) px(c, cx + x, cy + y, col);
}

void fillTri(const Canvas& c, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col) {
  int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
  int miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
  int d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
  if (d == 0) return;
  for (int y = miny; y <= maxy; y++)
    for (int x = minx; x <= maxx; x++) {
      double a = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / (double)d;
      double b = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / (double)d;
      double g = 1.0 - a - b;
      if (a >= 0 && b >= 0 && g >= 0) px(c, x, y, col);
    }
}

void arcBand(const Canvas& c, int cx, int cy, int r, int thickness, double a0Deg, double a1Deg,
             uint16_t col) {
  if (a1Deg < a0Deg) std::swap(a0Deg, a1Deg);
  // Step finely enough that the outer edge stays gap-free.
  double step = 0.6 / std::max(1, r);  // radians
  for (double a = a0Deg * M_PI / 180.0; a <= a1Deg * M_PI / 180.0; a += step) {
    double s = sin(a), co = cos(a);  // 0deg = up, clockwise
    for (int rr = r - thickness; rr <= r; rr++) {
      int x = cx + (int)lround(rr * s);
      int y = cy - (int)lround(rr * co);
      px(c, x, y, col);
    }
  }
}

void drawChar(const Canvas& c, int x, int y, char ch, int s, uint16_t col) {
  int idx = fontIndex(ch);
  for (int cc = 0; cc < 5; cc++) {
    uint8_t bits = FONT5X7[idx * 5 + cc];
    for (int row = 0; row < 7; row++)
      if (bits & (1 << row))
        for (int dy = 0; dy < s; dy++)
          for (int dx = 0; dx < s; dx++) px(c, x + cc * s + dx, y + row * s + dy, col);
  }
}

void drawText(const Canvas& c, int x, int y, const char* str, int s, uint16_t col) {
  for (const char* p = str; *p; p++) { drawChar(c, x, y, *p, s, col); x += 6 * s; }
}

int textW(const char* str, int s) { return (int)strlen(str) * 6 * s; }

void drawTextCentered(const Canvas& c, int cx, int y, const char* str, int s, uint16_t col) {
  drawText(c, cx - textW(str, s) / 2, y, str, s, col);
}

// ---- Anti-aliased layer ----------------------------------------------------

void blendPx(const Canvas& c, int x, int y, uint16_t col, uint8_t a) {
  if (x < 0 || y < 0 || x >= c.w || y >= c.h || a == 0) return;
  uint16_t* p = &c.fb[y * c.w + x];
  if (a == 255) { *p = col; return; }
  uint16_t d = *p;
  // Blend per channel in 565 space.
  int dr = (d >> 11) & 0x1F, dg = (d >> 5) & 0x3F, db = d & 0x1F;
  int sr = (col >> 11) & 0x1F, sg = (col >> 5) & 0x3F, sb = col & 0x1F;
  int r = dr + ((sr - dr) * a >> 8);
  int g = dg + ((sg - dg) * a >> 8);
  int b = db + ((sb - db) * a >> 8);
  *p = (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint8_t covToA(float cov) {
  if (cov <= 0.0f) return 0;
  if (cov >= 1.0f) return 255;
  return (uint8_t)(cov * 255.0f + 0.5f);
}

void aaDisc(const Canvas& c, float cx, float cy, float r, uint16_t col) {
  int x0 = (int)std::floor(cx - r - 1), x1 = (int)std::ceil(cx + r + 1);
  int y0 = (int)std::floor(cy - r - 1), y1 = (int)std::ceil(cy + r + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      blendPx(c, x, y, col, covToA(r + 0.5f - d));
    }
}

void aaRing(const Canvas& c, float cx, float cy, float r, float t, uint16_t col) {
  float half = t * 0.5f;
  int x0 = (int)std::floor(cx - r - half - 1), x1 = (int)std::ceil(cx + r + half + 1);
  int y0 = (int)std::floor(cy - r - half - 1), y1 = (int)std::ceil(cy + r + half + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      blendPx(c, x, y, col, covToA(half + 0.5f - std::fabs(d - r)));
    }
}

void glowDisc(const Canvas& c, float cx, float cy, float r, uint16_t col, uint8_t maxA) {
  int x0 = (int)std::floor(cx - r), x1 = (int)std::ceil(cx + r);
  int y0 = (int)std::floor(cy - r), y1 = (int)std::ceil(cy + r);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      if (d >= r) continue;
      float k = 1.0f - d / r;
      blendPx(c, x, y, col, (uint8_t)(maxA * k * k));
    }
}

void aaRoundRect(const Canvas& c, int x0, int y0, int x1, int y1, float rad,
                 uint16_t fillCol, uint8_t fillA, uint16_t borderCol) {
  float icx0 = x0 + rad, icy0 = y0 + rad, icx1 = x1 - rad, icy1 = y1 - rad;
  for (int y = y0 - 1; y <= y1 + 1; y++)
    for (int x = x0 - 1; x <= x1 + 1; x++) {
      // Signed distance to the rounded-rect outline (negative = inside).
      float qx = std::max({icx0 - x, 0.0f, x - icx1});
      float qy = std::max({icy0 - y, 0.0f, y - icy1});
      float d = std::sqrt(qx * qx + qy * qy) - rad;
      float fillCov   = covToA(0.5f - d) / 255.0f;
      float borderCov = covToA(1.0f - std::fabs(d)) / 255.0f;
      if (fillCov > 0.0f && fillA > 0)
        blendPx(c, x, y, fillCol, (uint8_t)(fillCov * fillA));
      if (borderCov > 0.0f)
        blendPx(c, x, y, borderCol, covToA(borderCov));
    }
}

void drawTextAA(const Canvas& c, int x, int y, const char* str, const AAFontDesc& f, uint16_t col) {
  const int cell = f.cellW * f.cellH;
  for (const char* s = str; *s; s++, x += f.cellW - 1) {
    unsigned ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* g = f.bits + (ch - 32) * cell;
    for (int gy = 0; gy < f.cellH; gy++)
      for (int gx = 0; gx < f.cellW; gx++) {
        uint8_t a = g[gy * f.cellW + gx];
        if (a) blendPx(c, x + gx, y + gy, col, a);
      }
  }
}

int textWAA(const char* str, const AAFontDesc& f) {
  int n = 0;
  while (str[n]) n++;
  return n > 0 ? n * (f.cellW - 1) : 0;
}

void drawTextAACentered(const Canvas& c, int cx, int y, const char* str, const AAFontDesc& f,
                        uint16_t col) {
  drawTextAA(c, cx - textWAA(str, f) / 2, y, str, f, col);
}

void blit(const Canvas& dst, int ox, int oy, const uint16_t* src, int sw, int sh, bool circleMask) {
  const int cx = sw / 2, cy = sh / 2;
  const long r = std::min(sw, sh) / 2;
  const long r2 = r * r;
  for (int y = 0; y < sh; y++)
    for (int x = 0; x < sw; x++) {
      if (circleMask) {
        long dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > r2) continue;
      }
      px(dst, ox + x, oy + y, src[y * sw + x]);
    }
}

}  // namespace gfx
