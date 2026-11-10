// Temporal dithering controls (core: wled00/temporal_dither.{h,cpp}).
//
// Settings page: Config -> Usermods -> "Temporal dither" (saved; used at boot).
// Live control (not saved), e.g. from Signinator / tree-show:
//   POST /json/state {"dither": {"on": true, "bri16": 1800, "g": 2.8, "wb": [255,176,240], "dark": true, "fps": 120}}
//   bri16: 0-65535 brightness used while dithering (0 = follow WLED's brightness slider)
//   fps: re-sends per second in realtime mode: "auto" (or 255, default) = as fast as the LEDs allow with a
//        little room for new frames (measured: one LED refresh takes ledMs), 1-250 = fixed, 0 = only new frames
// /json/info has "dither": {fw, on, bri16, g, wb, dark, fps, rate} so senders can check and re-send after a reboot.
#include "wled.h"
#include "temporal_dither.h"

#define USERMOD_ID_TEMPORAL_DITHER 200
#define TDITHER_FW "dither2"
#define TDITHER_AUTO 255          // fps value meaning "as fast as the LEDs allow"

class TemporalDitherUsermod : public Usermod {
  unsigned long lastRefresh = 0, rateAt = 0;
  unsigned long sentAtUs = 0;     // when our last refresh started (0 = not timing one)
  float ledUs = 0;                // measured time for one LED refresh (average)
  uint32_t rateFrames = 0;
  float rate = 0;
  // saved settings (boot defaults)
  bool sOn = false, sDark = true;
  float sGamma = 2.8f;
  int sWb[3] = {255, 255, 255};
  int sFps = TDITHER_AUTO;
  static const char _name[];

  void applySaved() {
    TDither::cfg.on = sOn;
    TDither::cfg.dark = sDark;
    TDither::cfg.gamma = constrain(sGamma, 1.0f, 3.5f);
    for (int i = 0; i < 3; i++) TDither::cfg.wb[i] = constrain(sWb[i], 0, 255);
    TDither::cfg.fps = sFps == TDITHER_AUTO ? TDITHER_AUTO : constrain(sFps, 0, 250);
    TDither::cfg.bri16 = 0;
  }

  void fill(JsonObject o) {
    const TDither::Config &c = TDither::cfg;
    o["on"] = c.on;
    o["bri16"] = c.bri16;
    o["g"] = c.gamma;
    JsonArray wb = o.createNestedArray("wb");
    for (int i = 0; i < 3; i++) wb.add(c.wb[i]);
    o["dark"] = c.dark;
    if (c.fps == TDITHER_AUTO) o["fps"] = "auto"; else o["fps"] = c.fps;
  }

 public:
  void setup() override { applySaved(); }

  void loop() override {
    unsigned long now = millis();
    if (now - rateAt >= 1000) {          // dithered refreshes per second, for /json/info
      rate = (TDither::frames - rateFrames) * 1000.0f / (now - rateAt);
      rateFrames = TDither::frames;
      rateAt = now;
    }
    // realtime mode only shows new frames when they arrive: keep refreshing the last one so the dither runs
    const TDither::Config &c = TDither::cfg;
    bool busy = strip.isUpdating();
    if (sentAtUs && !busy) {            // our refresh has gone out to the LEDs: how long did it take?
      float us = micros() - sentAtUs;
      ledUs = ledUs > 0 ? ledUs * 0.9f + us * 0.1f : us;
      sentAtUs = 0;
    }
    if (!c.on || !c.fps || realtimeMode == REALTIME_MODE_INACTIVE || realtimeOverride || busy) return;
    unsigned long periodUs;
    if (c.fps == TDITHER_AUTO) periodUs = ledUs > 0 ? max(4000UL, (unsigned long)(ledUs * 1.1f) + 500UL) : 0;  // ~90% of the LEDs' maximum
    else periodUs = 1000000UL / c.fps;
    unsigned long nowUs = micros();
    if (nowUs - lastRefresh < periodUs) return;
    lastRefresh = nowUs;
    sentAtUs = nowUs;
    strip.show();
  }

  void addToJsonInfo(JsonObject &root) override {
    JsonObject d = root.createNestedObject("dither");
    d["fw"] = TDITHER_FW;
    fill(d);
    d["rate"] = roundf(rate * 10) / 10;
    d["ledMs"] = roundf(ledUs / 100.0f) / 10;                         // one LED refresh, measured
    d["ledMax"] = ledUs > 0 ? (int)(1000000.0f / ledUs) : 0;          // refreshes/s the LEDs can do
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray a = user.createNestedArray(F("Temporal dither"));
    if (TDither::cfg.on) { a.add(rate); a.add(F(" refreshes/s")); }
    else a.add(F("off"));
  }

  void addToJsonState(JsonObject &root) override { fill(root.createNestedObject("dither")); }

  void readFromJsonState(JsonObject &root) override {
    JsonObject d = root["dither"];
    if (d.isNull()) return;
    TDither::Config &c = TDither::cfg;
    if (d["on"].is<bool>()) c.on = d["on"];
    if (d["bri16"].is<int>()) c.bri16 = constrain(d["bri16"].as<int>(), 0, 65535);
    if (d["g"].is<float>()) c.gamma = constrain(d["g"].as<float>(), 1.0f, 3.5f);
    if (d["dark"].is<bool>()) c.dark = d["dark"];
    if (d["fps"].is<const char *>() && strcmp(d["fps"].as<const char *>(), "auto") == 0) c.fps = TDITHER_AUTO;
    else if (d["fps"].is<int>()) { int f = d["fps"].as<int>(); c.fps = f == TDITHER_AUTO ? TDITHER_AUTO : constrain(f, 0, 250); }
    JsonArray wb = d["wb"];
    if (!wb.isNull() && wb.size() == 3)
      for (int i = 0; i < 3; i++) c.wb[i] = constrain(wb[i].as<int>(), 0, 255);
    if (realtimeMode == REALTIME_MODE_INACTIVE) strip.trigger();
  }

  void addToConfig(JsonObject &root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top["enabled"] = sOn;
    top["gamma"] = sGamma;
    top["wbRed"] = sWb[0];
    top["wbGreen"] = sWb[1];
    top["wbBlue"] = sWb[2];
    top["ditherDarkest"] = sDark;
    top["refreshFps"] = sFps;
  }

  bool readFromConfig(JsonObject &root) override {
    JsonObject top = root[FPSTR(_name)];
    bool complete = !top.isNull();
    complete &= getJsonValue(top["enabled"], sOn, false);
    complete &= getJsonValue(top["gamma"], sGamma, 2.8f);
    complete &= getJsonValue(top["wbRed"], sWb[0], 255);
    complete &= getJsonValue(top["wbGreen"], sWb[1], 255);
    complete &= getJsonValue(top["wbBlue"], sWb[2], 255);
    complete &= getJsonValue(top["ditherDarkest"], sDark, true);
    complete &= getJsonValue(top["refreshFps"], sFps, TDITHER_AUTO);
    applySaved();                        // settings page saved: use the new values now
    return complete;
  }

  void appendConfigData() override {
    oappend(F("addInfo('Temporal dither:enabled',1,'Exact colors when dim. Needs a fast LED refresh (short strands per output).');"));
    oappend(F("addInfo('Temporal dither:gamma',1,'Applied before dimming while dithering (1 = none; WLED gamma is skipped).');"));
    oappend(F("addInfo('Temporal dither:wbRed',1,'White balance 0-255 per channel.');"));
    oappend(F("addInfo('Temporal dither:ditherDarkest',1,'Also dither below one level (smoother fades, may sparkle).');"));
    oappend(F("addInfo('Temporal dither:refreshFps',1,'Realtime (DDP etc.): re-sends per second so the dither keeps running. 255 = auto (as fast as the LEDs allow), 0 = off.');"));
  }

  uint16_t getId() override { return USERMOD_ID_TEMPORAL_DITHER; }
};

const char TemporalDitherUsermod::_name[] PROGMEM = "Temporal dither";

static TemporalDitherUsermod temporal_dither_usermod;
REGISTER_USERMOD(temporal_dither_usermod);
