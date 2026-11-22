#include "config.h"
#include <Preferences.h>

static const char* NS = "weather";

void config::load(Settings& s) {
  Preferences p;
  p.begin(NS, true);
  s.ssid = p.getString("ssid", "");
  s.pass = p.getString("pass", "");
  s.location = p.getString("loc", "");
  s.place = p.getString("place", "");
  s.lat = p.getFloat("lat", NAN);
  s.lon = p.getFloat("lon", NAN);
  s.imperial = p.getBool("imperial", false);
  s.refreshMin = p.getUShort("refresh", 30);
  s.verified = p.getBool("verified", false);
  s.needsSetup = p.getBool("setup", true);
  s.lastError = p.getString("err", "");
  p.end();
  if (s.ssid.isEmpty()) s.needsSetup = true;
}

void config::save(const Settings& s) {
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", s.ssid);
  p.putString("pass", s.pass);
  p.putString("loc", s.location);
  p.putString("place", s.place);
  p.putFloat("lat", s.lat);
  p.putFloat("lon", s.lon);
  p.putBool("imperial", s.imperial);
  p.putUShort("refresh", s.refreshMin);
  p.putBool("verified", s.verified);
  p.putBool("setup", s.needsSetup);
  p.putString("err", s.lastError);
  p.end();
}
