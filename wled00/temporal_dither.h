#pragma once
/*
 * Temporal dithering (Jeff's WLED fork, for Signinator / Treeinator).
 *
 * Without it, WLED applies gamma (8 bit) and then brightness ("video" scaling, 8 bit), so at low brightness
 * weak colour channels round to 0 or 1 and hues drift. With it on, each pixel is worked out with 16-bit precision
 * (gamma -> white balance -> brightness) and the fraction of a level left over is carried to the next refresh,
 * so over a few refreshes every LED averages its exact colour.
 *
 * Needs a fast LED refresh (short strands per output, e.g. 256 LEDs per pin -> ~120 fps); at ~30 fps the
 * darkest colours shimmer. In realtime mode (DDP etc.) the usermod re-sends the last frame at `fps` so the
 * dithering keeps running between incoming frames.
 *
 * Brightness while dithering: `bri16` (0-65535) if set, otherwise WLED's own brightness. It is applied here,
 * so buses run at full brightness for the dithered frame.
 *
 * Settings and live control: usermods/temporal_dither (Usermod settings page, and "dither" in /json/state).
 */
#include <stdint.h>
#include <stddef.h>

namespace TDither {

struct Config {
  bool on = false;
  uint16_t bri16 = 0;              // 0 = follow WLED's brightness
  float gamma = 2.8f;              // applied before dimming (1 = none). WLED's own gamma is skipped while on.
  uint8_t wb[3] = {255, 255, 255}; // white balance per channel (R, G, B)
  bool dark = true;                // also dither below one level (smoother fades, may sparkle when very dim)
  uint8_t fps = 255;               // realtime mode: re-sends per second (255 = auto: as fast as the LEDs allow, 0 = only new frames)
};

extern Config cfg;
extern uint32_t frames;            // dithered frames shown (for /json/info)

void setGamma(float g);            // rebuild the 16-bit gamma table
bool beginFrame(size_t len, uint8_t stripBri);   // true when this frame is dithered
uint32_t apply(size_t i, uint32_t c);            // pixel i (0..len-1) of the frame -> output colour

}  // namespace TDither
