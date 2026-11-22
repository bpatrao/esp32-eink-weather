#pragma once
#include <Arduino.h>
#include "config.h"

constexpr int DAYS = 7;
constexpr int HOURS = 24;
constexpr int RAIN_SLOTS = 8;  // 15-minute slots = 2 hours

struct DayForecast {
  uint8_t code;
  int8_t weekday;  // 0 = Sunday
  int8_t rainPct;  // -1 = unknown
  float tMax, tMin, uvMax;
  char sunrise[6], sunset[6];  // "HH:MM"
  uint32_t daylightSec;
};

struct HourForecast {
  uint8_t hour;
  uint8_t code;
  int8_t rainPct;
  float temp;
};

// Plain data (no String) so it can live in RTC memory across deep sleep;
// switching views then needs no Wi-Fi.
struct WeatherData {
  bool valid;
  time_t fetchedAt;  // local time as epoch (system clock is set to local time)
  int32_t utcOffset;
  char updated[6];   // "HH:MM" of the fetch

  float temp, feels, wind;
  int8_t humidity;
  uint8_t code;
  bool isDay;

  DayForecast days[DAYS];
  HourForecast hours[HOURS];
  float rain15[RAIN_SLOTS];  // mm per 15 min, first = current slot

  bool airValid;
  int16_t aqi;  // European AQI (metric) or US AQI (imperial)
  float pm25, pm10, uvNow;

  char ip[16];
  int8_t rssi;
};

enum class GeoResult { Ok, NotFound, NetworkError };

// Turns s.location into s.lat/s.lon/s.place (Open-Meteo geocoding).
GeoResult resolveLocation(Settings& s);

// Forecast (current, hourly, 7 days, 15-minute rain). Also sets the system
// clock to local time from the server's Date header.
bool fetchWeather(const Settings& s, WeatherData& w);

// Air quality + current UV (non-essential; sets w.airValid).
bool fetchAirQuality(const Settings& s, WeatherData& w);

const char* weatherText(int code);
