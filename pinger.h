#pragma once
#include <cstdint>

// Motion-tracker sounds on the CrowPanel's speaker (raw I2S 21/22/23, amp
// shutdown via the STC8H helper MCU). Plays synthesized clips (audio_clips.h,
// sine + envelopes measured from the Aliens tracker scene): a "scanning"
// blip + echo-pip while nothing is detected, and a contact swell-ping when a
// target is tracked -- the ping repeats faster AND is pitched higher as the
// target closes (1675 Hz / 1.25 s far -> 2150 Hz / 150 ms close).
//
// Call pingerInit() once after i2c_init() (the amp enable is an STC8 I2C
// write). Then feed it from the UI loop:
//   pingerSetClosest(distMm)  -- nearest valid target in mm, or -1 for none
//   pingerSetMuted(bool)

bool pingerInit();
void pingerSetClosest(float distMm);
void pingerSetMuted(bool muted);
