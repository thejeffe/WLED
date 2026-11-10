#include "wled.h"
#include "temporal_dither.h"

namespace TDither {

Config cfg;
uint32_t frames = 0;
uint16_t levels = 256;

static uint16_t lut[256];          // 8-bit input -> 16-bit linear output (gamma applied)
static float lutGamma = -1.0f;
static uint8_t *err = nullptr;     // per LED channel: fraction of a level carried to the next refresh
static size_t errLen = 0;
static uint32_t scale[3] = {0, 0, 0};   // per channel: brightness x white balance, 0..65535

void setGamma(float g) {
  if (g < 1.0f) g = 1.0f;
  if (g > 3.5f) g = 3.5f;
  if (fabsf(g - lutGamma) < 0.001f) return;
  for (int i = 0; i < 256; i++) lut[i] = (uint16_t)lroundf(65535.0f * powf(i / 255.0f, g));
  lutGamma = g;
}

bool beginFrame(size_t len, uint8_t stripBri) {
  if (!cfg.on || len == 0) return false;
  if (fabsf(cfg.gamma - lutGamma) >= 0.001f) setGamma(cfg.gamma);
  if (len * 3 != errLen) {
    free(err);
    err = (uint8_t *)malloc(len * 3);
    if (!err) { errLen = 0; return false; }
    errLen = len * 3;
    for (size_t k = 0; k < errLen; k++) err[k] = hw_random8();   // start out of step, so LEDs don't flip together
  }
  uint32_t b16 = stripBri == 0 ? 0 : (cfg.bri16 ? cfg.bri16 : stripBri * 257u);
  for (int c = 0; c < 3; c++) scale[c] = (b16 * cfg.wb[c] + 127) / 255;
  frames++;
  return true;
}

uint32_t apply(size_t i, uint32_t c) {
  if (i * 3 + 2 >= errLen) return 0;
  uint8_t in[3] = {(uint8_t)R(c), (uint8_t)G(c), (uint8_t)B(c)}, out[3];
  uint8_t *e = err + i * 3;
  for (int k = 0; k < 3; k++) {
    uint32_t v = ((uint32_t)lut[in[k]] * scale[k]) >> 16;   // 8.8 fixed point output level
    uint32_t level = v >> 8, frac = v & 0xFF;
    if (levels < 256) {                          // flicker limit: only k/levels of a step, so patterns repeat fast
      uint32_t k = (frac * levels + 128) >> 8;
      if (k >= levels) { if (level < 255) level++; frac = 0; }
      else frac = (k << 8) / levels;
    }
    if (level == 0 && !cfg.dark) { out[k] = frac >= 128 ? 1 : 0; continue; }
    uint32_t a = e[k] + frac;
    if (a >= 256 && level < 255) level++;
    e[k] = a & 0xFF;
    out[k] = level;
  }
  uint8_t w = W(c) ? (uint8_t)(((uint32_t)W(c) * scale[1]) >> 16) : 0;   // white channel: plain scaling
  return RGBW32(out[0], out[1], out[2], w);
}

}  // namespace TDither
