#include "pinger.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <atomic>
#include <cmath>

#include "board_config.h"
#include "bsp_stc8h1kxx.h"
#include "audio_clips.h"

// Distance mapping (upstream rd03d_giga_receiver.ino tempo curve, plus a
// playback-rate curve: the contact ping is pitched UP as the target closes).
static const float    kDistMin     = 300.0f;   // mm -> fastest/highest ping
static const float    kDistMax     = 8000.0f;  // mm -> slowest/lowest ping
// Curves matched to the Aliens tracker scene (measured from reference video):
// ping tone 1675 Hz far -> 2150 Hz close, interval 1.25 s far -> 150 ms close.
// The synthesized contact clip sits at 2075 Hz, so rate = target/2075.
static const uint16_t kIntervalMin = 150;      // ms between pings when close
static const uint16_t kIntervalMax = 1250;     // ms between pings when far
static const float    kRateFar     = 0.807f;   // contact rate at 8 m  (1675 Hz)
static const float    kRateClose   = 1.036f;   // contact rate up close (2150 Hz)
static const uint32_t kScanBlipMs  = 1200;     // idle "scanning" blip cadence

static const int   kSampleRate  = 16000;
static const float kGain        = 0.175f;  // software volume (no codec)
static const int   kFadeSamples = 80;      // 5 ms anti-click fade on cut

static I2SClass          i2s;
static std::atomic<int>  g_distMm{-1};   // -1 = no target
static std::atomic<bool> g_muted{false};

void pingerSetClosest(float distMm) { g_distMm.store(distMm < 0 ? -1 : (int)distMm); }
void pingerSetMuted(bool muted)     { g_muted.store(muted); }

// Two-voice sample player: retriggering a clip starts it on the free voice
// while the old one fades out over kFadeSamples, so rapid close-range
// retriggers can cut the ping's tail without clicks. pos/step are 16.16
// fixed point; step != 1.0 resamples the clip, which is what shifts pitch.
struct Voice {
  const int16_t* data = nullptr;
  int      len  = 0;
  uint32_t pos  = 0;   // 16.16
  uint32_t step = 0;   // 16.16
  int      fade = -1;  // -1 = none, else samples of fade-out left
  bool     active = false;
};
static Voice g_voices[2];

static void triggerClip(const int16_t* data, int len, float rate) {
  int slot = 0;
  for (int i = 0; i < 2; i++) if (!g_voices[i].active) slot = i;
  for (int i = 0; i < 2; i++)
    if (g_voices[i].active && i != slot && g_voices[i].fade < 0)
      g_voices[i].fade = kFadeSamples;
  Voice& v = g_voices[slot];
  v.data = data; v.len = len; v.pos = 0;
  v.step = (uint32_t)(rate * 65536.0f);
  v.fade = -1; v.active = true;
}

static void stopAllVoices() {
  for (int i = 0; i < 2; i++)
    if (g_voices[i].active && g_voices[i].fade < 0) g_voices[i].fade = kFadeSamples;
}

// Render one 10 ms chunk (mix voices -> 16-bit stereo) and push it to I2S.
// i2s.write() blocks on the DMA queue, so the task self-paces at audio speed.
static void renderChunk() {
  static int16_t buf[320];  // 160 stereo frames = 10 ms
  for (int f = 0; f < 160; f++) {
    int32_t acc = 0;
    for (int vi = 0; vi < 2; vi++) {
      Voice& v = g_voices[vi];
      if (!v.active) continue;
      uint32_t idx = v.pos >> 16;
      if ((int)idx >= v.len - 1) { v.active = false; continue; }
      int32_t frac = v.pos & 0xFFFF;
      int32_t s = (v.data[idx] * (65536 - frac) + v.data[idx + 1] * frac) >> 16;
      if (v.fade >= 0) {
        s = s * v.fade / kFadeSamples;
        if (--v.fade <= 0) { v.active = false; }
      }
      acc += s;
      v.pos += v.step;
    }
    acc = (int32_t)(acc * kGain);
    if (acc >  32767) acc =  32767;
    if (acc < -32768) acc = -32768;
    buf[2 * f]     = (int16_t)acc;
    buf[2 * f + 1] = (int16_t)acc;
  }
  i2s.write((uint8_t*)buf, sizeof(buf));
}

static void pingerTask(void*) {
  uint32_t nextBlipAt = 0, nextPingAt = 0;
  bool wasMuted = false;
  for (;;) {
    int  dist  = g_distMm.load();
    bool muted = g_muted.load();
    uint32_t now = millis();
    if (muted) {
      if (!wasMuted) stopAllVoices();
    } else if (dist < 0) {
      // scanning, nothing detected: idle blip
      if (now >= nextBlipAt) {
        triggerClip(kClipBlip, kClipBlipLen, 1.0f);  // synthesized at 1675 Hz
        nextBlipAt = now + kScanBlipMs;
      }
      nextPingAt = 0;  // first contact fires instantly
    } else {
      // target detected: contact ping, faster AND higher-pitched as it closes
      float d = constrain((float)dist, kDistMin, kDistMax);
      float t = (d - kDistMin) / (kDistMax - kDistMin);  // 0 = close, 1 = far
      uint16_t interval = kIntervalMin + (uint16_t)(t * (kIntervalMax - kIntervalMin));
      // Pitch front-loads like the film: it tracks ~the square of the
      // approach progress (t^2), so most of the sweep happens early and
      // the tone saturates near the top for the close-in rapid fire.
      float    rate     = kRateClose - (t * t) * (kRateClose - kRateFar);
      if (now >= nextPingAt) {
        triggerClip(kClipContact, kClipContactLen, rate);
        nextPingAt = now + interval;
      }
      nextBlipAt = now + 600;  // don't blip right on the heels of a ping
    }
    wasMuted = muted;
    renderChunk();
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
