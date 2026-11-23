#pragma once
#include <Arduino.h>
#include "config.h"
#include "weather.h"

enum View : uint8_t {
  VIEW_HOME,
  VIEW_WEEK,
  VIEW_HOURLY,
  VIEW_RAIN,
  VIEW_AIR,
  VIEW_SUN,
  VIEW_CITY,
  VIEW_INFO,
  VIEW_COUNT
};

struct ViewContext {
  const Settings& s;
  const WeatherData& w;
  int batteryMv;
  int batteryPct;
  const uint8_t* cityBits;  // nullptr if no photo
};

void uiInit();
// QR code to join the setup access point + instructions (+ optional error).
// homeIp: the board's address on the home Wi-Fi for OTA updates ("" if not joined).
void uiSetupScreen(const String& apSsid, const String& apPass, const String& error, const String& homeIp = "");
// Simple full-screen status message.
void uiMessage(const String& title, const String& l1, const String& l2 = "", const String& l3 = "");
// Draws one view. fast = partial refresh (quick, no flashing; only valid
// after a full refresh in the same wake cycle).
void uiView(View v, const ViewContext& c, bool fast);
#ifdef FRAME_DUMP
void uiDumpView(View v, const ViewContext& c);  // preview build only
#endif
// Put the panel controller into its lowest-power state (image stays visible).
void uiSleep();
