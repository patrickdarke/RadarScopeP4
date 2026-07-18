#include "pinger.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <atomic>
#include <cmath>

#include "board_config.h"
#include "bsp_stc8h1kxx.h"

// Upstream buzzer tuning (rd03d_giga_receiver.ino)
static const float    kDistMin     = 300.0f;   // mm -> fastest/highest ping
static const float    kDistMax     = 8000.0f;  // mm -> slowest/lowest ping
static const uint16_t kIntervalMin = 120;      // ms between pings when close
static const uint16_t kIntervalMax = 900;      // ms between pings when far
static const uint16_t kFreqMin     = 700;      // Hz far away
static const uint16_t kFreqMax     = 1800;     // Hz up close
static const uint32_t kBeepMs      = 70;

static const int   kSampleRate = 16000;
static const float kGain       = 0.35f;  // software volume (no codec)

static I2SClass          i2s;
static std::atomic<int>  g_distMm{-1};   // -1 = no target
static std::atomic<bool> g_muted{false};

void pingerSetClosest(float distMm) { g_distMm.store(distMm < 0 ? -1 : (int)distMm); }
void pingerSetMuted(bool muted)     { g_muted.store(muted); }

// Write `ms` of a sine ping (3 ms attack/release ramps) as 16-bit stereo.
static void writeBeep(uint16_t freq, uint32_t ms) {
  const int total = kSampleRate * (int)ms / 1000;
  const int ramp  = kSampleRate * 3 / 1000;
  static int16_t buf[256];  // 64 stereo frames per chunk
  int written = 0;
  float phase = 0.0f, dphi = 2.0f * (float)M_PI * freq / kSampleRate;
  while (written < total) {
    int frames = min(64, total - written);
    for (int i = 0; i < frames; i++) {
      int n = written + i;
      float env = 1.0f;
      if (n < ramp)              env = (float)n / ramp;
      else if (total - n < ramp) env = (float)(total - n) / ramp;
      int16_t s = (int16_t)(sinf(phase) * 32767.0f * kGain * env);
      phase += dphi;
      buf[2 * i]     = s;
      buf[2 * i + 1] = s;
    }
    i2s.write((uint8_t*)buf, frames * 4);
    written += frames;
  }
}

static void writeSilence(uint32_t ms) {
  static int16_t zeros[256] = {0};
  int total = kSampleRate * (int)ms / 1000;
  while (total > 0) {
    int frames = min(64, total);
    i2s.write((uint8_t*)zeros, frames * 4);
    total -= frames;
  }
}

// The i2s.write() calls block on the DMA queue, so this task self-paces at
// real-time audio speed; keeping the pipe fed with silence between pings
// avoids underrun pops.
static void pingerTask(void*) {
  uint32_t lastBeepMs = 0;
  for (;;) {
    int  dist  = g_distMm.load();
    bool muted = g_muted.load();
    if (muted || dist < 0) {
      writeSilence(20);
      continue;
    }
    float d = constrain((float)dist, kDistMin, kDistMax);
    float t = (d - kDistMin) / (kDistMax - kDistMin);  // 0 = close, 1 = far
    uint16_t interval = kIntervalMin + (uint16_t)(t * (kIntervalMax - kIntervalMin));
    uint16_t freq     = kFreqMax     - (uint16_t)(t * (kFreqMax - kFreqMin));
    if (millis() - lastBeepMs >= interval) {
      writeBeep(freq, kBeepMs);
      lastBeepMs = millis();
    } else {
      writeSilence(10);
    }
  }
}

bool pingerInit() {
  stc8_gpio_set_level(STC8_GPIO_OUT_AUDIO_SD, AUDIO_POWER_ENABLE);
  i2s.setPins(AUDIO_GPIO_BCLK, AUDIO_GPIO_LRCLK, AUDIO_GPIO_SDATA);
  if (!i2s.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    return false;
  }
  return xTaskCreatePinnedToCore(pingerTask, "pinger", 4096, nullptr, 1, nullptr, 0) == pdPASS;
}
