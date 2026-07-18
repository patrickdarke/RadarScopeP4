// ============================================================
//  RadarScope sender — XIAO ESP32-S3 + RD-03D 24GHz mmWave radar
//
//  Untethered radar node. Based on Stevee87's rd03d_xiao_s3_transmitter
//  (same RD-03D protocol, clustering, and UDP format), simplified for a
//  minimal build and extended with battery telemetry:
//
//   - LiPo solders straight to the XIAO's BAT pads (built-in charger;
//     no TP4056). Charge via the XIAO's USB-C.
//   - Battery voltage read through a 220k/220k divider into A2 (GPIO3);
//     millivolts + percent are appended to every UDP packet. The stock
//     GIGA receiver ignores the extra bytes; RadarScope P4 shows them.
//   - Wi-Fi modem sleep + throttled send rate (kSendHz) for runtime.
//   - User LED heartbeat: solid = connecting, brief flash = packet sent.
//
//  WIRING (see docs/wiring.svg):
//    RD-03D 5V/VCC -> 3V3   (test this first; if detection is flaky at
//                            range, power the radar from a 5 V source)
//    RD-03D GND    -> GND
//    RD-03D OT1    -> D0 (GPIO1, UART1 RX)
//    RD-03D RX     -> D1 (GPIO2, UART1 TX)
//    RD-03D OT2    -> not connected
//    BAT+ -> 220k -> A2 (GPIO3) -> 220k -> GND   (battery sense)
//    LiPo + inline switch -> BAT+/BAT- pads (underside)
//
//  Board: Seeed XIAO ESP32S3  (FQBN esp32:esp32:XIAO_ESP32S3)
// ============================================================
#include <WiFi.h>
#include <WiFiUdp.h>

// ---- Pins ---------------------------------------------------
#define RADAR_RX_PIN 1   // D0: radar OT1 -> us
#define RADAR_TX_PIN 2   // D1: us -> radar RX
#define BAT_ADC_PIN  3   // A2/D2: middle of the 220k/220k divider
#define LED_PIN      21  // XIAO user LED, active LOW

// ---- Network (must match the display) -----------------------
static const char*    kSsid    = "RadarNet";
static const char*    kPass    = "radar12345";
static const uint16_t kUdpPort = 4210;

// ---- Behavior ----------------------------------------------
static const uint8_t  kSendHz       = 5;    // UDP rate (radar still parsed at full rate)
static const uint32_t kBatPeriodMs  = 5000; // battery sample cadence
static const float    kBatDivider   = 2.0f; // 220k/220k -> Vbat = 2 * Vadc

// ---- RD-03D protocol (verbatim from upstream) ---------------
static const uint8_t CMD_ENABLE[14] = {0xFD,0xFC,0xFB,0xFA,0x04,0x00,0xFF,0x00,0x01,0x00,0x04,0x03,0x02,0x01};
static const uint8_t CMD_MULTI[12]  = {0xFD,0xFC,0xFB,0xFA,0x02,0x00,0x90,0x00,0x04,0x03,0x02,0x01};
static const uint8_t CMD_END[12]    = {0xFD,0xFC,0xFB,0xFA,0x02,0x00,0xFE,0x00,0x04,0x03,0x02,0x01};
static const uint8_t HDR[4]         = {0xAA,0xFF,0x03,0x00};

#define MAX_TARGETS 3

struct Target {
  int16_t x;
  int16_t y;
  int16_t spd;
  uint8_t valid;
};

// First 32 bytes are byte-identical to the stock packet; the battery tail is
// new. Old receivers read only sizeof(their struct) and never see it.
struct __attribute__((packed)) UdpPacket {
  uint32_t magic;
  uint32_t frameCount;
  Target   targets[MAX_TARGETS];
  uint16_t batMv;    // battery millivolts (0 = unknown)
  uint8_t  batPct;   // 0..100 (255 = unknown)
  uint8_t  version;  // packet tail version, 1
};

HardwareSerial RadarSerial(1);
WiFiUDP        udp;
UdpPacket      pkt;
IPAddress      hostIP;
uint32_t       frameCount = 0;
uint32_t       badCount   = 0;

// ---- Battery ------------------------------------------------
// Rough LiPo open-circuit curve; good enough for a status readout.
static uint8_t lipoPercent(uint16_t mv) {
  struct P { uint16_t mv; uint8_t pct; };
  static const P curve[] = {{4200, 100}, {4000, 85}, {3850, 65}, {3700, 40},
                            {3550, 15},  {3400, 5},  {3300, 0}};
  if (mv >= curve[0].mv) return 100;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv >= curve[i].mv) {
      const P &a = curve[i], &b = curve[i - 1];
      return a.pct + (uint8_t)((uint32_t)(mv - a.mv) * (b.pct - a.pct) / (b.mv - a.mv));
    }
  }
  return 0;
}

static void sampleBattery() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  uint16_t mv = (uint16_t)(sum / 8 * kBatDivider);
  pkt.batMv  = mv;
  pkt.batPct = lipoPercent(mv);
}

// ---- Radar decode (verbatim math from upstream) -------------
static int16_t decodeVal(uint8_t lo, uint8_t hi) {
  uint16_t raw = (uint16_t)lo | ((uint16_t)hi << 8);
  if (raw >= 0x8000) return  (int16_t)(raw - 0x8000);
  else               return -(int16_t)(raw);
}

// Merge two targets closer than 1 m (torso + swinging arm of one person).
static const float CLUSTER_DIST_MM = 1000.0f;

static void clusterTargets() {
  for (int i = 0; i < MAX_TARGETS; i++) {
    if (!pkt.targets[i].valid) continue;
    for (int j = i + 1; j < MAX_TARGETS; j++) {
      if (!pkt.targets[j].valid) continue;
      float dx = (float)pkt.targets[i].x - (float)pkt.targets[j].x;
      float dy = (float)pkt.targets[i].y - (float)pkt.targets[j].y;
      if (sqrtf(dx * dx + dy * dy) < CLUSTER_DIST_MM) {
        pkt.targets[i].x   = (int16_t)(((float)pkt.targets[i].x + (float)pkt.targets[j].x) / 2.0f);
        pkt.targets[i].y   = (int16_t)(((float)pkt.targets[i].y + (float)pkt.targets[j].y) / 2.0f);
        pkt.targets[i].spd = max(pkt.targets[i].spd, pkt.targets[j].spd);
        pkt.targets[j] = {0, 0, 0, 0};
      }
    }
  }
}

static void parseFrame(const uint8_t* pl) {
  for (int i = 0; i < MAX_TARGETS; i++) {
    const uint8_t* b = pl + i * 8;
    bool empty = true;
    for (int j = 0; j < 8; j++)
      if (b[j]) { empty = false; break; }
    if (empty) { pkt.targets[i] = {0, 0, 0, 0}; continue; }
    int16_t xi  = decodeVal(b[0], b[1]);
    int16_t yi  = decodeVal(b[2], b[3]);
    int16_t spi = decodeVal(b[4], b[5]);
    float dist  = sqrtf((float)xi * xi + (float)yi * yi);
    if (dist > 100.0f && dist <= 8000.0f) pkt.targets[i] = {xi, yi, spi, 1};
    else                                  pkt.targets[i] = {0, 0, 0, 0};
  }
  clusterTargets();
  frameCount++;
}

// ---- Frame reader ------------------------------------------
static uint8_t pl[26];
static uint8_t plIdx = 0, hdrIdx = 0;
static bool    inFrame = false;

static void readRadar() {
  while (RadarSerial.available()) {
    uint8_t c = RadarSerial.read();
    if (!inFrame) {
      if (c == HDR[hdrIdx]) {
        hdrIdx++;
        if (hdrIdx == 4) { hdrIdx = 0; inFrame = true; plIdx = 0; }
      } else {
        hdrIdx = (c == HDR[0]) ? 1 : 0;
      }
    } else {
      pl[plIdx++] = c;
      if (plIdx == 26) {
        inFrame = false; plIdx = 0;
        if (pl[24] == 0x55 && pl[25] == 0xCC) parseFrame(pl);
        else                                  badCount++;
      }
    }
  }
}

static void sendMultiTargetCmd() {
  while (RadarSerial.available()) RadarSerial.read();
  delay(50);
  RadarSerial.write(CMD_ENABLE, sizeof(CMD_ENABLE));
  RadarSerial.flush(); delay(200);
  while (RadarSerial.available()) RadarSerial.read();
  RadarSerial.write(CMD_MULTI, sizeof(CMD_MULTI));
  RadarSerial.flush(); delay(200);
  while (RadarSerial.available()) RadarSerial.read();
  RadarSerial.write(CMD_END, sizeof(CMD_END));
  RadarSerial.flush(); delay(200);
  while (RadarSerial.available()) RadarSerial.read();
  inFrame = false; plIdx = 0; hdrIdx = 0;
}

// ---- Wi-Fi --------------------------------------------------
static void connectWiFi() {
  digitalWrite(LED_PIN, LOW);  // solid while connecting
  WiFi.begin(kSsid, kPass);
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    tries++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    ESP.restart();
  }
  hostIP = WiFi.gatewayIP();  // the display hosts the AP
  WiFi.setSleep(true);        // modem sleep between sends (battery)
  digitalWrite(LED_PIN, HIGH);
  Serial.printf("WiFi up, sending to %s\n", hostIP.toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  pkt.magic   = 0xD03DA7A;
  pkt.batMv   = 0;
  pkt.batPct  = 255;
  pkt.version = 1;

  RadarSerial.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  delay(300);

  connectWiFi();
  udp.begin(kUdpPort);
  sendMultiTargetCmd();
  sampleBattery();
}

void loop() {
  static uint32_t lastSendMs = 0, lastBatMs = 0, lastCmdMs = 0, lastCheckMs = 0, ledOffMs = 0;
  uint32_t now = millis();

  readRadar();  // keep the parser drained at full radar rate

  if (now - lastSendMs >= 1000 / kSendHz) {
    lastSendMs = now;
    pkt.frameCount = frameCount;
    udp.beginPacket(hostIP, kUdpPort);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();
    digitalWrite(LED_PIN, LOW);  // blink on send
    ledOffMs = now + 15;
  }
  if (ledOffMs && now >= ledOffMs) { digitalWrite(LED_PIN, HIGH); ledOffMs = 0; }

  if (now - lastBatMs >= kBatPeriodMs) { lastBatMs = now; sampleBattery(); }

  if (now - lastCmdMs > 60000) { lastCmdMs = now; sendMultiTargetCmd(); }

  if (now - lastCheckMs > 5000) {
    lastCheckMs = now;
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
  }
}
