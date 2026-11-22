// Wi-Fi weather display for the LilyGO T5 V2.3 (2.13" e-paper).
//
// Buttons
//   - IO39, short press: next view (wakes the board if asleep). Further presses
//     within INTERACT_MS use the fast partial refresh.
//   - IO39, held 3 s: setup portal (access point + QR code, portal.cpp).
//   - RST: restarts -> Home view with freshly fetched data.
//
// Data
//   - Forecast + air quality are kept in RTC memory, so switching views
//     needs no Wi-Fi. They are re-fetched every `refreshMin` minutes.
//   - The city photo is downloaded once per location and stored in flash.
//
// Errors
//   - Wi-Fi fails with credentials that never worked (e.g. wrong password)
//       -> save the reason and restart into setup mode.
//   - Wi-Fi fails with credentials that worked before (router down)
//       -> keep the last data on screen and retry every RETRY_MIN minutes.
//   - Battery below BATTERY_EMPTY_MV -> "charge me" and sleep until the button.

#include <Arduino.h>
#include <WiFi.h>
#include "battery.h"
#include "cityimage.h"
#include "config.h"
#include "display_ui.h"
#include "portal.h"
#include "weather.h"

static constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
static constexpr uint16_t RETRY_MIN = 5;
static constexpr uint32_t SETUP_HOLD_MS = 3000;
static constexpr uint32_t INTERACT_MS = 20000;

// Survive deep sleep (re-initialised on power-on / reset)
RTC_DATA_ATTR static WeatherData cache;
RTC_DATA_ATTR static uint8_t view = VIEW_HOME;
RTC_DATA_ATTR static uint8_t failCount = 0;

static Settings settings;
static uint8_t cityBits[IMG_BYTES];
static bool haveCity = false;
static int batteryMv = 0, batteryPct = 0;
static volatile uint8_t lastDisconnectReason = 0;

// ---------- sleep / restart ----------

[[noreturn]] static void sleepSeconds(uint32_t seconds) {
  Serial.printf("Sleeping %u s\n", seconds);
  uiSleep();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
  esp_deep_sleep_start();
}

[[noreturn]] static void restartIntoSetup(const String& error) {
  Serial.printf("Back to setup: %s\n", error.c_str());
  settings.lastError = error;
  settings.needsSetup = true;
  config::save(settings);
  ESP.restart();
  for (;;) {}
}

// Waits while the button is held. true = held for `ms` (long press).
static bool buttonHeldFor(uint32_t ms) {
  uint32_t start = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - start >= ms) return true;
    delay(10);
  }
  delay(30);  // debounce release
  return false;
}

// ---------- Wi-Fi ----------

static bool isAuthFailure(uint8_t r) {
  return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_EXPIRE || r == WIFI_REASON_MIC_FAILURE;
}

static bool connectWifi(String& error) {
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastDisconnectReason = info.wifi_sta_disconnected.reason; },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());

  uint32_t start = millis();
  uint8_t authFails = 0, notFound = 0, seenReason = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(100);
    uint8_t r = lastDisconnectReason;
    if (r && r != seenReason) Serial.printf("Wi-Fi disconnect reason %u\n", r);
    seenReason = r;
    if (r) {
      lastDisconnectReason = 0;
      if (isAuthFailure(r) && ++authFails >= 2) break;  // give up early on a clear wrong password
      if (r == WIFI_REASON_NO_AP_FOUND && ++notFound >= 3) break;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }

  if (authFails) error = "Wrong password for \"" + settings.ssid + "\".";
  else if (notFound) error = "Network \"" + settings.ssid + "\" not found.";
  else error = "Could not connect to \"" + settings.ssid + "\".";
  return false;
}

// ---------- data refresh ----------

static String photoTitle() {
  int comma = settings.place.indexOf(',');
  String t = comma > 0 ? settings.place.substring(0, comma) : settings.place;
  t.trim();
  float a;
  if (t.isEmpty() || sscanf(t.c_str(), "%f", &a) == 1) return "";  // typed coordinates: no city name
  return t;
}

// Connects, fetches everything, disconnects. false + `error` on failure.
// Restarts into setup for errors only the user can fix.
static bool refreshData(String& error) {
  if (!settings.verified) uiMessage("Connecting...", "Wi-Fi: " + settings.ssid, "This takes a few seconds.");

  if (!connectWifi(error)) {
    if (!settings.verified) restartIntoSetup(error);
    return false;
  }
  if (!settings.verified) {
    settings.verified = true;
    config::save(settings);
  }

  if (!settings.hasLocation()) {
    switch (resolveLocation(settings)) {
      case GeoResult::Ok:
        Serial.printf("Location: %s (%.4f, %.4f)\n", settings.place.c_str(), settings.lat, settings.lon);
        config::save(settings);
        break;
      case GeoResult::NotFound:
        restartIntoSetup("Location \"" + settings.location + "\" not found. Try another spelling.");
      case GeoResult::NetworkError:
        error = "Could not look up location.";
        return false;
    }
  }

  WeatherData fresh = cache;  // keep old data if this fetch fails halfway
  if (!fetchWeather(settings, fresh)) {
    delay(2000);
    if (!fetchWeather(settings, fresh)) {
      error = "Weather service unreachable.";
      return false;
    }
  }
  if (!fetchAirQuality(settings, fresh)) Serial.println("Air quality unavailable");
  strlcpy(fresh.ip, WiFi.localIP().toString().c_str(), sizeof(fresh.ip));
  fresh.rssi = WiFi.RSSI();
  cache = fresh;
  Serial.printf("%s: %.1f, %s, AQI %d\n", settings.place.c_str(), cache.temp, weatherText(cache.code), cache.aqi);

  // City photo: once per location
  bool known;
  haveCity = cityImageLoad(settings.place, cityBits, known);
  if (!known) {
    String title = photoTitle();
    ImgResult r = title.length() ? cityImageFetch(title, cityBits) : ImgResult::NoImage;
    Serial.printf("City photo for '%s': %s\n", title.c_str(),
                  r == ImgResult::Ok ? "ok" : r == ImgResult::NoImage ? "none" : "network error");
    if (r == ImgResult::Ok) cityImageStore(settings.place, cityBits);
    else if (r == ImgResult::NoImage) cityImageStore(settings.place, nullptr);
    haveCity = r == ImgResult::Ok;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return true;
}

// ---------- drawing ----------

static void draw(bool fast) {
  ViewContext ctx{settings, cache, batteryMv, batteryPct, haveCity ? cityBits : nullptr};
  Serial.printf("View %u (%s refresh)\n", view, fast ? "fast" : "full");
  uiView((View)view, ctx, fast);
}

// Stay awake briefly after a press so further presses switch views quickly.
// Partial refreshes leave faint ghosts of earlier screens, so every
// FULL_EVERY-th switch is a full refresh, and the view left on screen gets a
// clean full refresh before sleeping.
static void interactiveWindow() {
  constexpr int FULL_EVERY = 4;
  int partials = 0;
  uint32_t deadline = millis() + INTERACT_MS;
  while ((int32_t)(deadline - millis()) > 0) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      if (buttonHeldFor(SETUP_HOLD_MS)) {
        settings.needsSetup = true;
        settings.lastError = "";
        config::save(settings);
        ESP.restart();
      }
      view = (view + 1) % VIEW_COUNT;
      bool full = partials + 1 >= FULL_EVERY;
      draw(!full);
      partials = full ? 0 : partials + 1;
      deadline = millis() + INTERACT_MS;
    }
    delay(10);
  }
  if (partials) draw(false);  // clean up ghosting before sleeping
}

static uint32_t secondsUntilRefresh() {
  int64_t age = (int64_t)time(nullptr) - cache.fetchedAt;
  int64_t left = (int64_t)settings.refreshMin * 60 - age;
  return (uint32_t)constrain(left, 60, (int64_t)settings.refreshMin * 60);
}

// ---------- main flow ----------

void setup() {
  Serial.begin(115200);
  delay(50);
  pinMode(BUTTON_PIN, INPUT);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool buttonWake = cause == ESP_SLEEP_WAKEUP_EXT0;
  bool timerWake = cause == ESP_SLEEP_WAKEUP_TIMER;
  Serial.printf("\nT5 weather: wake cause %d, view %u\n", cause, view);

  config::load(settings);
  uiInit();

  // Read before Wi-Fi starts: the radio's current draw sags the voltage.
  batteryMv = batteryMillivolts();
  batteryPct = batteryPercent(batteryMv);
  Serial.printf("Battery %d mV (%d%%)\n", batteryMv, batteryPct);
  if (batteryMv < BATTERY_EMPTY_MV) {
    uiMessage("Battery empty", "Please charge the display.", "It resumes after charging", "when you press the button.");
    uiSleep();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
    esp_deep_sleep_start();
  }

  // Button: long press = setup, short press (that woke us) = next view
  if (digitalRead(BUTTON_PIN) == LOW && buttonHeldFor(SETUP_HOLD_MS)) {
    Serial.println("Button held: setup requested");
    settings.needsSetup = true;
    settings.lastError = "";
    config::save(settings);
  } else if (buttonWake) {
    view = (view + 1) % VIEW_COUNT;
  }
  if (settings.needsSetup) runPortal(settings);

  // Refresh data on schedule, at power-on/reset, or if it's missing/stale
  bool stale = !cache.valid || timerWake || !settings.hasLocation() ||
               (int64_t)time(nullptr) - cache.fetchedAt >= (int64_t)settings.refreshMin * 60;
  bool ok = true;
  String error;
  if (stale) ok = refreshData(error);
  if (!stale) {
    bool known;
    haveCity = cityImageLoad(settings.place, cityBits, known);
  }

  if (ok) {
    failCount = 0;
#ifdef FRAME_DUMP
    ViewContext ctx{settings, cache, batteryMv, batteryPct, haveCity ? cityBits : nullptr};
    for (int v = 0; v < VIEW_COUNT; v++) uiDumpView((View)v, ctx);
#endif
    draw(false);
    if (buttonWake) interactiveWindow();
    sleepSeconds(secondsUntilRefresh());
  }

  // Temporary failure with settings that worked before
  failCount++;
  Serial.printf("%s (failure %u)\n", error.c_str(), failCount);
  if (cache.valid && buttonWake) {
    draw(false);  // older data is better than nothing
    interactiveWindow();
  } else if (!cache.valid || failCount == 3) {
    uiMessage("Weather offline", error, "Retrying every " + String(RETRY_MIN) + " min.", "Hold button 3 s for setup.");
  }
  sleepSeconds(RETRY_MIN * 60);
}

void loop() {}
