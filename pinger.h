#pragma once

// Sonar-style tracker pings on the CrowPanel's speaker (raw I2S 21/22/23, amp
// shutdown via the STC8H helper MCU). Replaces the GIGA build's piezo tone():
// ping tempo and pitch scale with the distance of the nearest target, exactly
// like the upstream buzzer logic.
//
// Call pingerInit() once after i2c_init() (the amp enable is an STC8 I2C
// write). Then feed it from the UI loop:
//   pingerSetClosest(distMm)  -- nearest valid target in mm, or -1 for none
//   pingerSetMuted(bool)

bool pingerInit();
void pingerSetClosest(float distMm);
void pingerSetMuted(bool muted);
