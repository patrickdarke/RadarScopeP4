# Parts list / BOM

Everything needed to build both ends of RadarScope P4. Amazon third-party
listings for commodity modules rotate frequently — if a direct link is dead,
use the search link next to it to find the current equivalent.

## Display unit

| Part | Source | ~Price | Notes |
|------|--------|--------|-------|
| **ELECROW CrowPanel Advance 5.0" ESP32-P4** | [ELECROW direct](https://www.elecrow.com/crowpanel-advanced-5inch-esp32-p4-hmi-ai-display-800x480-ips-touch-screen-with-wifi-6.html) | $45–55 | Not on Amazon at the time of writing. Must be the **ESP32-P4** Advance model — ELECROW's 5" **ESP32-S3** panels look nearly identical and will not run this firmware. |
| Speaker, 8 Ω 2 W, JST plug | [search: 8 ohm 2W speaker JST PH2.0](https://www.amazon.com/s?k=8+ohm+2w+speaker+JST+PH2.0) | ~$8 | Only if not bundled with the board; used for the sonar pings. |
| USB-C cable + 5 V/2 A adapter | [search](https://www.amazon.com/s?k=usb-c+cable+5v+2a+wall+adapter) | ~$10 | Power via the **UART0** USB-C port. |
| *(Optional)* 1S LiPo w/ JST-PH plug | [EEMB 3.7 V 3700 mAh](https://www.amazon.com/EEMB-3700mAh-Rechargeable-Connector-Certified/dp/B08215B4KK) | ~$15 | Only for an untethered display. **Verify plug polarity against the board silkscreen with a meter first** — pigtail polarity is not standardized. |

## Radar node (untethered sender)

| Part | Source | ~Price | Notes |
|------|--------|--------|-------|
| **Seeed Studio XIAO ESP32-S3** | [Amazon B0BYSB66S5](https://www.amazon.com/ESP32S3-2-4GHz-Dual-core-Supported-Efficiency-Interface/dp/B0BYSB66S5) · [pre-soldered B0DRNVH8MQ](https://www.amazon.com/Seeed-Studio-XIAO-ESP32S3-Pre-Soldered/dp/B0DRNVH8MQ) | $9–13 | Plain version; the "Sense" camera model isn't needed. Its built-in LiPo charger is why no TP4056 module appears in this list. |
| **Ai-Thinker RD-03D radar** | [Amazon B0D3CXVCFK](https://www.amazon.com/EC-Buying-Multi-Target-Trajectory-Localization/dp/B0D3CXVCFK) · [alt B0DS8CQDMS](https://www.amazon.com/Multi-Target-Trajectory-Positioning-photoelectric-Rd-03d/dp/B0DS8CQDMS) | $10–14 | Must be exactly **RD-03D** — the RD-03 and RD-03E use different protocols. Prefer a listing that includes the 1.27 mm cable. |
| 1S LiPo, protected, ~1500 mAh | [EEMB 1500 mAh B09DPNCLQZ](https://www.amazon.com/EEMB-Battery-1500mAh-Rechargeable-Connector/dp/B09DPNCLQZ) | ~$11 | Protected cell only. Leads solder to the XIAO's underside BAT pads (see `docs/wiring.svg`). |
| Mini slide switch | [SS12D00 10-pack B07Q8489MK](https://www.amazon.com/SS12D00-SS-12D00-Toggle-Switch-Interruptor/dp/B07Q8489MK) | ~$6 | Inline on BAT+. |
| 220 kΩ resistors ×2, 1/4 W | [search: resistor assortment kit](https://www.amazon.com/s?k=resistor+assortment+kit+1%2F4W) | ~$6 | Battery-sense divider to A2. |
| Thin hookup wire | [search: 30AWG silicone wire kit](https://www.amazon.com/s?k=30awg+silicone+wire+kit) | ~$8 | Radar ↔ XIAO wiring. |

## Fallback — only if the radar won't run on 3.3 V

Bench-test the RD-03D from the XIAO's 3V3 pin first (see README). Order these
only if tracking is unreliable at range on 3.3 V:

| Part | Source | ~Price | Notes |
|------|--------|--------|-------|
| 18650 battery shield V3 | [Amazon B0981CNR4N](https://www.amazon.com/Battery-Shield-Raspberry-Arduino-Output/dp/B0981CNR4N) · [diymore B0784FPF8J](https://www.amazon.com/Diymore-Battery-Shield-Raspberry-Arduino/dp/B0784FPF8J) | ~$9 | Charge + protection + 5 V boost in one module; replaces the bare LiPo option. |
| Protected 18650 cell | [search: 18650 protected button top](https://www.amazon.com/s?k=18650+protected+button+top+3000mah) | ~$11 | Name-brand cell (Samsung/LG/Panasonic core). Capacity claims above ~3500 mAh are fake. |

## Totals

- Primary build: **~$95–115** (display + node)
- Fallback adds ~$20 — wait for the 3.3 V test before buying

Assumed on hand: soldering iron, solder, heat shrink. The upstream project's
`radar_top/bottom_case` STLs (in
[Stevee87/Arduino-ESP32-Radarproject](https://github.com/Stevee87/Arduino-ESP32-Radarproject))
fit the XIAO + RD-03D node.

## Battery safety

Protected cells only, everywhere. Always verify JST connector polarity with a
meter before plugging anything in — red-wire-to-positive is **not** guaranteed
on vendor pigtails, and reversed polarity is instantly fatal to these boards.
