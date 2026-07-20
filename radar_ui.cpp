#include "radar_ui.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "font_aa.h"

namespace radar_ui {

const gfx::AAFontDesc& fontSmall()     { return kFontSmall; }
const gfx::AAFontDesc& fontSmallBold() { return kFontSmallBold; }
const gfx::AAFontDesc& fontBig()       { return kFontBig; }

// Palette (RGB565). Same green-CRT spirit as upstream, tuned for the AA pass.
constexpr uint16_t C_GREEN  = 0x07E0;
constexpr uint16_t C_RED    = 0xF800;
constexpr uint16_t C_AMBER  = 0xFD20;
constexpr uint16_t C_CYAN   = 0x07FF;
constexpr uint16_t C_WHITE  = 0xFFFF;
static const uint16_t C_TEXT_DIM  = gfx::rgb(70, 160, 90);    // panel body text
static const uint16_t C_TEXT_HDR  = gfx::rgb(110, 230, 140);  // panel header
static const uint16_t C_LABEL     = gfx::rgb(50, 120, 60);    // scope labels
static const uint16_t C_LABEL_HI  = gfx::rgb(90, 220, 110);   // major scope labels
static const uint16_t C_PANEL_FIL = gfx::rgb(3, 18, 6);
static const uint16_t C_PANEL_BRD = gfx::rgb(30, 110, 45);

static const uint16_t kDotCols[kMaxTargets] = {C_RED, C_AMBER, C_CYAN};

// Info panel (top-right, translucent over the fan tip)
constexpr int kPanX0 = 656, kPanY0 = 4, kPanX1 = 796, kPanY1 = 106;
constexpr int kPanTx = kPanX0 + 10;  // text left edge
constexpr int kLineH = 16;

float distToR(float d) {
  if (d <= 0)       return 0;
  if (d <= 2000.0f) return (d / 2000.0f)                           * 0.50f * kRadarR;
  if (d <= 4000.0f) return (0.50f + (d - 2000.0f) / 2000.0f * 0.25f) * kRadarR;
  if (d <= 6000.0f) return (0.75f + (d - 4000.0f) / 2000.0f * 0.15f) * kRadarR;
  return              (0.90f + (d - 6000.0f) / 2000.0f * 0.10f)      * kRadarR;
}

void targetToPixel(float x, float y, int16_t& px, int16_t& py) {
  float dist  = sqrtf(x * x + y * y);
  float angle = atan2f(x, y) * 180.0f / (float)M_PI;
  float r     = distToR(dist);
  float rad   = (angle - 90.0f) * (float)M_PI / 180.0f;
  px = kRadarCX + (int16_t)(r * cosf(rad));
  py = kRadarCY + (int16_t)(r * sinf(rad));
}

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Analytic scope render: one pass over the framebuffer computing edge-accurate
// coverage for the fan, range rings, and bearing spokes -- no stipple, ~1px AA
// everywhere. Runs once at boot (sqrtf+atan2f per pixel, ~none of it per-frame).
void drawBackground(const gfx::Canvas& c) {
  constexpr float kDeg2Rad = (float)M_PI / 180.0f;
  const float ringR[9] = {distToR(500),  distToR(1000), distToR(2000),
                          distToR(3000), distToR(4000), distToR(5000),
                          distToR(6000), distToR(7000), distToR(8000)};
  const bool  ringMajor[9] = {false, true, false, false, true, false, false, false, true};
  const int   spokes[5]    = {-60, -30, 0, 30, 60};

  // Feature colors (fan base is a per-pixel gradient rgb(9,46,13) -> rgb(3,20,6))
  const uint16_t colRingMin = gfx::rgb(18, 95, 28);
  const uint16_t colRingMaj = gfx::rgb(40, 185, 60);
  const uint16_t colSpoke   = gfx::rgb(14, 80, 22);
  const uint16_t colSpoke0  = gfx::rgb(28, 135, 42);
  const uint16_t colEdge    = gfx::rgb(48, 210, 72);

  gfx::fill(c, 0x0000);
  for (int y = 0; y < c.h; y++) {
    float dy = (float)(kRadarCY - y);  // >= 0 above the origin
    for (int x = 0; x < c.w; x++) {
      float dx = (float)(x - kRadarCX);
      float r  = sqrtf(dx * dx + dy * dy);
      float th = atan2f(dx, dy) / kDeg2Rad;  // deg, 0 = up, +right

      // Fan body coverage: inside |th| <= 60 and r <= kRadarR
      float angInPx = (kSectorHalf - fabsf(th)) * kDeg2Rad * fmaxf(r, 1.0f);
      float fanCov  = clamp01(fminf(kRadarR + 0.5f - r, angInPx + 0.5f));
      if (fanCov <= 0.0f) continue;

      float t = clamp01(r / (float)kRadarR);
      uint16_t base = gfx::rgb((uint8_t)(9.0f - 6.0f * t + 0.5f),
                               (uint8_t)(46.0f - 26.0f * t + 0.5f),
                               (uint8_t)(13.0f - 7.0f * t + 0.5f));
      gfx::blendPx(c, x, y, base, (uint8_t)(fanCov * 255.0f));

      // Range rings
      for (int i = 0; i < 9; i++) {
        float w = ringMajor[i] ? 0.9f : 0.55f;
        float cov = clamp01(w + 0.5f - fabsf(r - ringR[i])) * fanCov;
        if (cov > 0.0f)
          gfx::blendPx(c, x, y, ringMajor[i] ? colRingMaj : colRingMin,
                       (uint8_t)(cov * (ringMajor[i] ? 235.0f : 200.0f)));
      }

      // Bearing spokes + bright fan edges
      for (int i = 0; i < 5; i++) {
        float dpx = fabsf(th - (float)spokes[i]) * kDeg2Rad * r;
        bool edge = (i == 0 || i == 4);
        float w = edge ? 0.9f : (spokes[i] == 0 ? 0.7f : 0.55f);
        float cov = clamp01(w + 0.5f - dpx) * (edge ? clamp01(kRadarR + 0.5f - r) : fanCov);
        if (cov > 0.0f) {
          uint16_t col = edge ? colEdge : (spokes[i] == 0 ? colSpoke0 : colSpoke);
          gfx::blendPx(c, x, y, col, (uint8_t)(cov * 220.0f));
        }
      }
    }
  }

  // Origin marker
  gfx::glowDisc(c, kRadarCX, kRadarCY, 18, C_GREEN, 90);
  gfx::aaDisc(c, kRadarCX, kRadarCY, 5.0f, C_GREEN);
  gfx::aaRing(c, kRadarCX, kRadarCY, 9.5f, 1.4f, gfx::rgb(30, 140, 45));

  // Range labels along the +60-degree edge
  const float rd[9] = {500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};
  for (int i = 0; i < 9; i++) {
    char buf[8];
    if (rd[i] < 1000) snprintf(buf, sizeof(buf), "0.5m");
    else              snprintf(buf, sizeof(buf), "%dm", (int)(rd[i] / 1000.0f));
    float rad = (kSectorHalf - 90.0f) * kDeg2Rad;
    int lx = kRadarCX + (int)((ringR[i] + 6) * cosf(rad)) + 3;
    int ly = kRadarCY + (int)((ringR[i] + 6) * sinf(rad)) - 8;
    gfx::drawTextAA(c, lx, ly, buf, kFontSmall, ringMajor[i] ? C_LABEL_HI : C_LABEL);
  }

  // Bearing labels at the spoke tips
  const int degs[5] = {-60, -30, 0, 30, 60};
  for (int i = 0; i < 5; i++) {
    float rad = (degs[i] - 90.0f) * kDeg2Rad;
    char buf[5];
    snprintf(buf, sizeof(buf), "%d", degs[i]);
    int lx = kRadarCX + (int)((kRadarR + 16) * cosf(rad));
    int ly = kRadarCY + (int)((kRadarR + 16) * sinf(rad)) - 8;
    int halfW = gfx::textWAA(buf, kFontSmall) / 2;
    if (lx < halfW + 2) lx = halfW + 2;                    // keep the +/-60 tips on-screen
    if (lx > c.w - halfW - 2) lx = c.w - halfW - 2;
    if (ly >= 0)
      gfx::drawTextAACentered(c, lx, ly, buf, degs[i] == 0 ? kFontSmallBold : kFontSmall,
                              degs[i] == 0 ? C_LABEL_HI : C_LABEL);
  }

  // Info panel shell + static header (dynamic lines land here each frame)
  gfx::aaRoundRect(c, kPanX0, kPanY0, kPanX1, kPanY1, 8.0f, C_PANEL_FIL, 225, C_PANEL_BRD);
  gfx::drawTextAA(c, kPanTx, kPanY0 + 6, "RD-03D 24GHz", kFontSmallBold, C_TEXT_HDR);
  gfx::drawTextAA(c, kPanTx, kPanY0 + 6 + kLineH, "3T 8m 120deg", kFontSmall, C_TEXT_DIM);
}

static void drawMuteButton(const gfx::Canvas& c, bool muted) {
  uint16_t border = muted ? C_RED : gfx::rgb(60, 210, 90);
  uint16_t fill   = muted ? gfx::rgb(60, 10, 10) : gfx::rgb(8, 42, 14);
  gfx::aaRoundRect(c, kBtnX, kBtnY, kBtnX + kBtnW - 1, kBtnY + kBtnH - 1, 8.0f, fill, 235, border);
  gfx::drawTextAACentered(c, kBtnX + kBtnW / 2, kBtnY + (kBtnH - 16) / 2 + 1,
                          muted ? "MUTED" : "SOUND ON", kFontSmallBold, border);
}

bool hitMuteButton(int x, int y) {
  return x >= kBtnX && x <= kBtnX + kBtnW && y >= kBtnY && y <= kBtnY + kBtnH;
}

static void textShadowed(const gfx::Canvas& c, int x, int y, const char* s,
                         const gfx::AAFontDesc& f, uint16_t col) {
  gfx::drawTextAA(c, x + 1, y + 1, s, f, 0x0000);
  gfx::drawTextAA(c, x, y, s, f, col);
}

// Radiating pulse: an AA ring band expanding from the origin to the rim,
// clipped to the fan sector, fading as it travels. Spans are computed per
// row from the ring geometry so only band pixels are touched (~8 k worst
// case), cheap enough for every frame.
static void drawPulse(const gfx::Canvas& fb, float phase, bool contact) {
  if (phase < 0.0f || phase >= 1.0f) return;
  const float r    = 24.0f + phase * (kRadarR - 24.0f);
  const float hw   = 3.0f + 5.0f * phase;              // band half-width grows
  const float aTop = contact ? 165.0f : 115.0f;
  const float aAmt = aTop * (1.0f - 0.85f * phase);    // fade toward the rim
  const uint16_t col = contact ? gfx::rgb(140, 255, 160) : gfx::rgb(70, 210, 100);
  const float tanS = tanf(kSectorHalf * (float)M_PI / 180.0f);
  const float rin = r - hw, rout = r + hw + 1.0f;
  int y0 = kRadarCY - (int)rout - 1; if (y0 < 0) y0 = 0;
  for (int y = y0; y < kScreenH; y++) {
    float dy = (float)(kRadarCY - y);
    if (dy >= rout) continue;
    float xo = sqrtf(rout * rout - dy * dy);
    float xi = (dy >= rin || rin <= 0.0f) ? 0.0f : sqrtf(rin * rin - dy * dy);
    for (int side = 0; side < 2; side++) {
      int xa = side ? kRadarCX + (int)xi : kRadarCX - (int)xo;
      int xb = side ? kRadarCX + (int)xo : kRadarCX - (int)xi;
      if (xa < 0) xa = 0;
      if (xb > fb.w - 1) xb = fb.w - 1;
      for (int x = xa; x <= xb; x++) {
        float dx = (float)(x - kRadarCX);
        float dist = sqrtf(dx * dx + dy * dy);
        float cov = hw + 0.5f - fabsf(dist - r);         // band AA
        if (cov <= 0.0f) continue;
        if (cov > 1.0f) cov = 1.0f;
        float edge = 0.5f * (dy * tanS - fabsf(dx)) + 0.5f;  // sector AA
        if (edge <= 0.0f) continue;
        if (edge > 1.0f) edge = 1.0f;
        float rim = kRadarR + 0.5f - dist;               // stay inside the rim
        if (rim <= 0.0f) continue;
        if (rim > 1.0f) rim = 1.0f;
        gfx::blendPx(fb, x, y, col, (uint8_t)(aAmt * cov * edge * rim));
      }
    }
  }
}

void composeFrame(const gfx::Canvas& fb, const uint16_t* bg,
                  const Target targets[kMaxTargets], const bool stale[kMaxTargets],
                  const Status& st) {
  memcpy(fb.fb, bg, (size_t)fb.w * fb.h * 2);

  // Pulses radiate beneath the target markers
  for (int i = 0; i < kMaxPulses; i++)
    drawPulse(fb, st.pulsePhase[i], st.pulseContact[i]);

  // Targets: soft glow + AA ring + core, labels with a drop shadow
  for (int i = 0; i < kMaxTargets; i++) {
    if (!targets[i].valid || stale[i]) continue;
    int16_t tx, ty;
    targetToPixel(targets[i].x, targets[i].y, tx, ty);
    uint16_t col = kDotCols[i];
    gfx::glowDisc(fb, tx, ty, 24, col, 110);
    gfx::aaRing(fb, tx, ty, 14.0f, 1.8f, col);
    gfx::aaDisc(fb, tx, ty, 5.0f, col);
    float dist = sqrtf(targets[i].x * targets[i].x + targets[i].y * targets[i].y);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2fm", dist / 1000.0f);
    textShadowed(fb, tx + 20, ty - 16, buf, kFontSmallBold, C_WHITE);
    if (targets[i].spd > 2.0f) {
      snprintf(buf, sizeof(buf), "%dcm/s", (int)targets[i].spd);
      textShadowed(fb, tx + 20, ty, buf, kFontSmall, gfx::rgb(200, 210, 200));
    }
  }

  // Info panel dynamic lines (shell + header are in the background)
  char line[24];
  int y = kPanY0 + 6 + 2 * kLineH;
  gfx::drawTextAA(fb, kPanTx, y, st.apInfo, kFontSmall, C_TEXT_DIM);
  y += kLineH;
  if (st.simMode) {
    gfx::drawTextAA(fb, kPanTx, y, "SIM MODE", kFontSmallBold, C_AMBER);
  } else if (st.senderAlive && st.senderBatPct >= 0) {
    snprintf(line, sizeof(line), "RX OK  bat %d%%", st.senderBatPct);
    uint16_t col = st.senderBatPct <= 15 ? C_RED : (st.senderBatPct <= 35 ? C_AMBER : C_GREEN);
    gfx::drawTextAA(fb, kPanTx, y, line, kFontSmallBold, col);
  } else {
    snprintf(line, sizeof(line), "%s fr:%lu", st.senderAlive ? "RX OK" : "RX ---",
             (unsigned long)st.frameCount);
    gfx::drawTextAA(fb, kPanTx, y, line, kFontSmallBold, st.senderAlive ? C_GREEN : C_RED);
  }
  y += kLineH;
  snprintf(line, sizeof(line), "Targets: %d", st.targetCount);
  gfx::drawTextAA(fb, kPanTx, y, line, kFontSmall, st.targetCount > 0 ? C_TEXT_HDR : C_TEXT_DIM);
  y += kLineH;
  for (int i = 0; i < kMaxTargets; i++) {
    if (targets[i].valid && !stale[i]) {
      float d = sqrtf(targets[i].x * targets[i].x + targets[i].y * targets[i].y);
      snprintf(line, sizeof(line), "T%d %.2fm %dcm/s", i + 1, d / 1000.0f, (int)targets[i].spd);
      gfx::drawTextAA(fb, kPanTx, y, line, kFontSmall, kDotCols[i]);
      break;
    }
  }

  drawMuteButton(fb, st.muted);
}

}  // namespace radar_ui
