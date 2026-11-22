#pragma once
#include <Arduino.h>

// Board pins (LilyGO T5 V2.3)
constexpr int BUTTON_PIN = 39;

// Everything the user sets in the setup portal, plus a little state,
// persisted in NVS (flash) so it survives deep sleep, resets and power loss.
struct Settings {
  String ssid;
  String pass;
  String location;          // what the user typed: "Lisbon", "Porto, PT" or "38.72,-9.14"
  String place;             // resolved name shown on screen, e.g. "Lisbon, PT"
  float lat = NAN;
  float lon = NAN;
  bool imperial = false;    // °F + mph instead of °C + km/h
  uint16_t refreshMin = 30;

  bool verified = false;    // these Wi-Fi credentials have connected at least once
  bool needsSetup = true;   // next boot goes into the setup portal
  String lastError;         // shown on the setup screen and page

  bool hasLocation() const { return !isnan(lat) && !isnan(lon); }
};

namespace config {
void load(Settings& s);
void save(const Settings& s);
}  // namespace config
