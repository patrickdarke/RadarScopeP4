// ============================================================
//  RadarScope P4 — mmWave motion radar display
//  ELECROW CrowPanel Advance 5.0" (ESP32-P4, 800x480 RGB, GT911 touch)
//
//  Port of Stevee87/Arduino-ESP32-Radarproject's Arduino GIGA receiver.
//  Drop-in compatible with the stock XIAO ESP32-S3 + RD-03D sender:
//  this unit hosts the "RadarNet" AP on its onboard ESP32-C6 and listens
//  for the same UDP packets on port 4210 (magic 0xD03DA7A).
//
//  Differences from the GIGA build:
//   - full-frame recompose from a pre-rendered background (no incremental
//     erase/redraw, no 10 s repaint) into a PSRAM framebuffer
//   - tracker pings on the onboard speaker (I2S) instead of a piezo pin
//   - SIM MODE: until a sender is heard, synthetic targets wander the scope
//     so display/touch/audio can be verified without the radar hardware
//
//  BOARD SETTINGS (Arduino IDE -> Tools):
//    Board:            ESP32P4 Dev Module
//    PSRAM:            Enabled
//    Flash Size:       16MB
//    Partition Scheme: Huge App
//    USB CDC On Boot:  Enabled
//
//  LIBRARIES: ESP32_Display_Panel 1.0.4 (+ ESP32_IO_Expander, esp-lib-utils).
//  No LVGL needed. Panel/touch config: esp_panel_board_custom_conf.h (ELECROW
//  Lesson07 values, verbatim). Backlight + amp enable go through the STC8H
//  helper MCU on I2C (bsp_stc8h1kxx).
// ============================================================
#include "board_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_heap_caps.h>
#include <esp_display_panel.hpp>

#include "bsp_i2c.h"
#include "bsp_stc8h1kxx.h"
#include "gfx.h"
#include "pinger.h"
#include "radar_ui.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;
namespace ui = radar_ui;

// ---- Network (must match the stock XIAO sender) -------------
static const char*    kApSsid  = "RadarNet";
static const char*    kApPass  = "radar12345";
static const uint16_t kUdpPort = 4210;
static const uint32_t kMagic   = 0xD03DA7A;

// ---- UDP packet (layout verbatim from upstream; 32 bytes) ---
struct Target {
  int16_t x;
  int16_t y;
  int16_t spd;
  uint8_t valid;
};
struct __attribute__((packed)) UdpPacket {
  uint32_t magic;
  uint32_t frameCount;
  Target   targets[ui::kMaxTargets];
};

// ---- State --------------------------------------------------
static Board*    board = nullptr;
static uint16_t* g_bg  = nullptr;  // pre-rendered scope background (PSRAM)
static uint16_t* g_fb  = nullptr;  // composed frame (PSRAM)
static WiFiUDP   udp;

static ui::Target targets[ui::kMaxTargets];
static bool       staleNow[ui::kMaxTargets];
static uint32_t   frameCount   = 0;
static uint32_t   lastPacketMs = 0;
static bool       senderSeen   = false;
static bool       muted        = false;

// Ghost filter (upstream): a target that hasn't moved > kMoveThreshMm for 2 s
// is hidden until it moves again (the RD-03D can't see truly static people, so
// a frozen "target" is clutter).
static const float    kMoveThreshMm  = 15.0f;
static const uint32_t kStillTimeout  = 2000;
static ui::Target prevRaw[ui::kMaxTargets];
static uint32_t   stillSinceMs[ui::kMaxTargets];

// SIM mode: active until the first real packet, and again if the sender goes
// quiet for 30 s. Synthetic walkers exercise the scope, pings, and touch.
static const uint32_t kSimAfterQuietMs = 30000;
static bool simActive = true;
struct SimWalker {
  float angDeg, distMm, velAng, velDist;
  bool  on;
};
static SimWalker sim[ui::kMaxTargets];

// ---- UDP receive --------------------------------------------
static void receiveUdp() {
  int sz = udp.parsePacket();
  if (sz < (int)sizeof(UdpPacket)) return;
  UdpPacket pkt;
  udp.read((uint8_t*)&pkt, sizeof(pkt));
  if (pkt.magic != kMagic) return;

  frameCount   = pkt.frameCount;
  lastPacketMs = millis();
  senderSeen   = true;
  simActive    = false;

  for (int i = 0; i < ui::kMaxTargets; i++) {
    if (pkt.targets[i].valid) {
      targets[i] = {(float)pkt.targets[i].x, (float)pkt.targets[i].y,
                    (float)abs(pkt.targets[i].spd), true};
    } else {
      targets[i].valid = false;
    }
  }
}

// ---- SIM mode -----------------------------------------------
static float frnd(float lo, float hi) {
  return lo + (hi - lo) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}

static void simInit() {
  for (int i = 0; i < ui::kMaxTargets; i++) {
    sim[i].on = (i < 2);  // two walkers
    sim[i].angDeg  = frnd(-45, 45);
    sim[i].distMm  = frnd(1500, 6000);
    sim[i].velAng  = frnd(-6, 6);       // deg/s
    sim[i].velDist = frnd(-400, 400);   // mm/s
  }
}

static void simUpdate(float dt) {
  for (int i = 0; i < ui::kMaxTargets; i++) {
    if (!sim[i].on) { targets[i].valid = false; continue; }
    SimWalker& w = sim[i];
    w.velAng  = constrain(w.velAng  + frnd(-8, 8) * dt,   -12.0f,  12.0f);
    w.velDist = constrain(w.velDist + frnd(-600, 600) * dt, -700.0f, 700.0f);
    w.angDeg += w.velAng * dt;
    w.distMm += w.velDist * dt;
    if (w.angDeg < -55 || w.angDeg > 55)    { w.velAng  = -w.velAng;  w.angDeg = constrain(w.angDeg, -55.0f, 55.0f); }
    if (w.distMm < 600 || w.distMm > 7500)  { w.velDist = -w.velDist; w.distMm = constrain(w.distMm, 600.0f, 7500.0f); }
    float rad = w.angDeg * (float)M_PI / 180.0f;
    targets[i].x     = w.distMm * sinf(rad);
    targets[i].y     = w.distMm * cosf(rad);
    // Radial+angular speed in cm/s, matching the sender's units
    float tang = fabsf(w.velAng) * (float)M_PI / 180.0f * w.distMm;
    targets[i].spd   = sqrtf(w.velDist * w.velDist + tang * tang) / 10.0f;
    targets[i].valid = true;
  }
  frameCount++;
}

// ---- Ghost/stale filter (upstream logic) --------------------
static void updateStale() {
  uint32_t now = millis();
  for (int i = 0; i < ui::kMaxTargets; i++) {
    staleNow[i] = false;
    if (!targets[i].valid) {
      stillSinceMs[i]  = 0;
      prevRaw[i].valid = false;
      continue;
    }
    bool movedRaw = !prevRaw[i].valid ||
                    fabsf(targets[i].x - prevRaw[i].x) > kMoveThreshMm ||
                    fabsf(targets[i].y - prevRaw[i].y) > kMoveThreshMm;
    if (movedRaw) stillSinceMs[i] = now;
    prevRaw[i]  = targets[i];
    staleNow[i] = (now - stillSinceMs[i]) > kStillTimeout;
  }
}

static float closestTargetDistMm() {
  float best = -1.0f;
  for (int i = 0; i < ui::kMaxTargets; i++) {
    if (!targets[i].valid || staleNow[i]) continue;
    float d = sqrtf(targets[i].x * targets[i].x + targets[i].y * targets[i].y);
    if (best < 0 || d < best) best = d;
  }
  return best;
}

// ---- Touch (mute button, edge-triggered like upstream) ------
static bool     touchDownLast = false;
static uint32_t lastToggleMs  = 0;

static void checkTouch() {
  auto* touch = board->getTouch();
  if (!touch) return;
  bool onButton = false;
  if (touch->readRawData(-1, 0, 0)) {
    TouchPoint pts[5];
    int n = touch->getPoints(pts, 5);
    for (int i = 0; i < n; i++) {
      if (ui::hitMuteButton(pts[i].x, pts[i].y)) { onButton = true; break; }
    }
  }
  if (onButton && !touchDownLast && (millis() - lastToggleMs > 300)) {
    muted = !muted;
    lastToggleMs = millis();
  }
  touchDownLast = onButton;
}

// ---- Present ------------------------------------------------
static void present() {
  board->getLCD()->drawBitmap(0, 0, ui::kScreenW, ui::kScreenH, (const uint8_t*)g_fb);
}

static void splash(const char* line1, const char* line2) {
  gfx::Canvas c{g_fb, ui::kScreenW, ui::kScreenH};
  gfx::fill(c, 0x0000);
  gfx::glowDisc(c, 400, 215, 190, gfx::rgb(10, 60, 18), 255);
  gfx::drawTextAACentered(c, 400, 196, "mmWAVE RADAR RD-03D", ui::fontBig(), 0x07E0);
  if (line1) gfx::drawTextAACentered(c, 400, 240, line1, ui::fontSmall(), gfx::rgb(70, 160, 90));
  if (line2) gfx::drawTextAACentered(c, 400, 258, line2, ui::fontSmall(), gfx::rgb(70, 160, 90));
  present();
}

// ---- Setup / loop -------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n[RadarScopeP4] boot (CrowPanel Advance 5.0 P4)");

  // I2C first (GT911 + STC8H share the bus; panel conf skips host init),
  // backlight dark until the first real frame is up.
  i2c_init(I2C_GPIO_SCL, I2C_GPIO_SDA);
  stc8_set_pwm_duty(STC8_PWM_LCD_BL_EN, 0);

  board = new Board();
  assert(board->init());
  assert(board->begin());

  size_t fbBytes = (size_t)ui::kScreenW * ui::kScreenH * 2;
  g_bg = (uint16_t*)heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM);
  g_fb = (uint16_t*)heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM);
  assert(g_bg && g_fb);

  splash("starting Wi-Fi AP...", nullptr);
  stc8_set_pwm_duty(STC8_PWM_LCD_BL_EN, 100);

  // Wi-Fi AP on the onboard C6 (esp-hosted over SDIO)
  WiFi.setPins(WIFI_HOSTED_SDIO_PIN_CLK, WIFI_HOSTED_SDIO_PIN_CMD,
               WIFI_HOSTED_SDIO_PIN_D0, WIFI_HOSTED_SDIO_PIN_D1,
               WIFI_HOSTED_SDIO_PIN_D2, WIFI_HOSTED_SDIO_PIN_D3,
               WIFI_HOSTED_SDIO_PIN_RESET);
  bool apOk = WiFi.softAP(kApSsid, kApPass, 6);
  if (apOk) udp.begin(kUdpPort);
  char info[40];
  snprintf(info, sizeof(info), "AP: %s  %s", kApSsid,
           apOk ? WiFi.softAPIP().toString().c_str() : "FAILED");
  Serial.println(info);
  splash(info, "waiting for sender (SIM until then)");
  delay(1200);

  if (!pingerInit()) Serial.println("[pinger] I2S init failed (no sound)");

  gfx::Canvas bgc{g_bg, ui::kScreenW, ui::kScreenH};
  ui::drawBackground(bgc);

  for (int i = 0; i < ui::kMaxTargets; i++) {
    targets[i] = prevRaw[i] = {0, 0, 0, false};
    stillSinceMs[i] = 0;
  }
  simInit();
}

void loop() {
  static uint32_t lastFrameMs = 0;
  uint32_t now = millis();

  receiveUdp();

  // Fall back to SIM if the sender has been quiet for a while
  if (!simActive && (!senderSeen || now - lastPacketMs > kSimAfterQuietMs)) {
    simActive = true;
    simInit();
  }
  // Real sender lost (but recently alive): clear its targets, upstream-style
  if (!simActive && senderSeen && now - lastPacketMs > 3000) {
    for (int i = 0; i < ui::kMaxTargets; i++) targets[i].valid = false;
  }

  // ~25 fps compose/present
  if (now - lastFrameMs >= 40) {
    float dt = (now - lastFrameMs) / 1000.0f;
    lastFrameMs = now;

    if (simActive) simUpdate(dt);
    updateStale();
    checkTouch();
    pingerSetClosest(closestTargetDistMm());
    pingerSetMuted(muted);

    ui::Status st;
    st.senderAlive = senderSeen && (now - lastPacketMs) < 3000;
    st.simMode     = simActive;
    st.muted       = muted;
    st.frameCount  = frameCount;
    st.targetCount = 0;
    for (int i = 0; i < ui::kMaxTargets; i++)
      if (targets[i].valid && !staleNow[i]) st.targetCount++;
    snprintf(st.apInfo, sizeof(st.apInfo), "AP: %s", kApSsid);

    gfx::Canvas fbc{g_fb, ui::kScreenW, ui::kScreenH};
    ui::composeFrame(fbc, g_bg, targets, staleNow, st);
    present();
  }
  delay(2);
}
