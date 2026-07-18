#pragma once
#include <cstdint>

#include "gfx.h"

// Ported 1:1 from Stevee87/Arduino-ESP32-Radarproject (rd03d_giga_receiver):
// same 800x480 layout, bottom-center origin, 120-degree fan, log-ish range
// scale to 8 m. The GIGA's incremental erase/redraw machinery is gone -- we
// recompose the whole frame from a pre-rendered background every tick.

namespace radar_ui {

constexpr int   kScreenW    = 800;
constexpr int   kScreenH    = 480;
constexpr int   kRadarCX    = 400;
constexpr int   kRadarCY    = 480;
constexpr int   kRadarR     = 440;
constexpr float kMaxDistMm  = 8000.0f;
constexpr float kSectorHalf = 60.0f;
constexpr int   kMaxTargets = 3;

// Mute button (top-right, under the info box)
constexpr int kBtnX = 660, kBtnY = 114, kBtnW = 136, kBtnH = 36;

struct Target {
  float x, y, spd;  // mm, mm, cm/s
  bool  valid;
};

struct Status {
  bool     senderAlive;   // packets seen within the last 3 s
  bool     simMode;       // synthetic targets (no sender yet)
  bool     muted;
  uint32_t frameCount;
  int      targetCount;   // valid && !stale
  int      senderBatPct;  // sender battery 0..100, -1 = unknown/stock sender
  char     apInfo[24];    // e.g. "AP: RadarNet"
};

// Range scale + projection (verbatim math from the upstream sketch).
float distToR(float d);
void  targetToPixel(float x, float y, int16_t& px, int16_t& py);

// Render the static scope (fan, rings, spokes, labels, info-box header) once.
void drawBackground(const gfx::Canvas& c);

// Compose one frame: memcpy(bg) + targets + info box + mute button.
void composeFrame(const gfx::Canvas& fb, const uint16_t* bg,
                  const Target targets[kMaxTargets], const bool stale[kMaxTargets],
                  const Status& st);

bool hitMuteButton(int x, int y);

// Baked AA fonts (font_aa.h lives in radar_ui.cpp so the ~55 KB of glyph data
// is emitted exactly once; the sketch reaches them through these).
const gfx::AAFontDesc& fontSmall();
const gfx::AAFontDesc& fontSmallBold();
const gfx::AAFontDesc& fontBig();

}  // namespace radar_ui
