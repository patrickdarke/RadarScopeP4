// Host-side preview: renders the exact on-device compose path (background +
// targets + info box + mute button) at 800x480 and writes a PPM.
//   c++ -std=c++17 -O2 -I.. preview.cpp ../radar_ui.cpp ../gfx.cpp -o /tmp/radar_prev
//   /tmp/radar_prev preview.ppm
#include <cstdio>
#include <cstring>

#include "gfx.h"
#include "radar_ui.h"

namespace ui = radar_ui;

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "preview.ppm";
  static uint16_t bg[ui::kScreenW * ui::kScreenH];
  static uint16_t fb[ui::kScreenW * ui::kScreenH];

  gfx::Canvas bgc{bg, ui::kScreenW, ui::kScreenH};
  ui::drawBackground(bgc);

  ui::Target targets[ui::kMaxTargets] = {
      {-1400.0f, 2600.0f, 42.0f, true},   // T1 up-left, 2.95 m
      {2200.0f, 4800.0f, 88.0f, true},    // T2 right, 5.28 m
      {300.0f, 900.0f, 12.0f, true},      // T3 close in
  };
  bool stale[ui::kMaxTargets] = {false, false, false};

  ui::Status st;
  st.senderAlive  = false;
  st.simMode      = true;
  st.muted        = false;
  st.frameCount   = 1234;
  st.targetCount  = 3;
  st.senderBatPct = 87;
  snprintf(st.apInfo, sizeof(st.apInfo), "AP: RadarNet");

  gfx::Canvas fbc{fb, ui::kScreenW, ui::kScreenH};
  ui::composeFrame(fbc, bg, targets, stale, st);

  FILE* f = fopen(out, "wb");
  fprintf(f, "P6\n%d %d\n255\n", ui::kScreenW, ui::kScreenH);
  for (int i = 0; i < ui::kScreenW * ui::kScreenH; i++) {
    uint16_t p = fb[i];
    unsigned char rgb[3] = {(unsigned char)(((p >> 11) & 0x1F) << 3),
                            (unsigned char)(((p >> 5) & 0x3F) << 2),
                            (unsigned char)((p & 0x1F) << 3)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("wrote %s\n", out);
  return 0;
}
