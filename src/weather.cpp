#include "weather.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>

static String urlEncode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// "Sun, 22 Nov 2026 14:23:11 GMT" -> UTC epoch, 0 on failure
static time_t parseHttpDate(const String& s) {
  char mon[4] = {};
  int d, y, H, M, S;
  if (sscanf(s.c_str(), "%*[^,], %d %3s %d %d:%d:%d", &d, mon, &y, &H, &M, &S) != 6) return 0;
  const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* p = strstr(months, mon);
  if (!p) return 0;
  int m = (p - months) / 3 + 1;
  return daysFromCivil(y, m, d) * 86400 + H * 3600 + M * 60 + S;
}

// "2026-11-22T14:15" interpreted as an epoch in the same (local) zone
static time_t parseIsoLocal(const char* s) {
  int y, m, d, H, M;
  if (sscanf(s, "%d-%d-%dT%d:%d", &y, &m, &d, &H, &M) != 5) return 0;
  return daysFromCivil(y, m, d) * 86400 + H * 3600 + M * 60;
}

static void copyHHMM(char* dst, const char* iso) {
  size_t n = strlen(iso);
  if (n >= 16) {
    memcpy(dst, iso + 11, 5);
    dst[5] = 0;
  } else {
    strcpy(dst, "--:--");
  }
}

static bool httpGetJson(const String& url, JsonDocument& doc, time_t* serverUtc = nullptr) {
  WiFiClientSecure client;
  client.setInsecure();  // public, read-only data; skip certificate pinning
  HTTPClient http;
  http.setTimeout(10000);
  http.useHTTP10(true);
  if (!http.begin(client, url)) return false;
  const char* keys[] = {"Date"};
  http.collectHeaders(keys, 1);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP %d for %s\n", code, url.c_str());
    http.end();
    return false;
  }
  if (serverUtc) *serverUtc = parseHttpDate(http.header("Date"));
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    return false;
  }
  return true;
}

GeoResult resolveLocation(Settings& s) {
  String q = s.location;
  q.trim();

  // Coordinates typed directly, e.g. "38.72, -9.14"
  float a, b;
  if (sscanf(q.c_str(), "%f , %f", &a, &b) == 2 && fabsf(a) <= 90 && fabsf(b) <= 180) {
    s.lat = a;
    s.lon = b;
    s.place = q;
    return GeoResult::Ok;
  }

  // "City" or "City, Country/Region"
  String name = q, hint;
  int comma = q.indexOf(',');
  if (comma >= 0) {
    name = q.substring(0, comma);
    hint = q.substring(comma + 1);
    name.trim();
    hint.trim();
  }
  if (name.isEmpty()) return GeoResult::NotFound;

  JsonDocument doc;
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=10&language=en&format=json&name=" +
               urlEncode(name);
  if (!httpGetJson(url, doc)) return GeoResult::NetworkError;

  for (JsonObject r : doc["results"].as<JsonArray>()) {
    String country = r["country"] | "";
    String cc = r["country_code"] | "";
    String admin = r["admin1"] | "";
    if (hint.length() && !hint.equalsIgnoreCase(country) && !hint.equalsIgnoreCase(cc) &&
        !hint.equalsIgnoreCase(admin))
      continue;
    s.lat = r["latitude"];
    s.lon = r["longitude"];
    s.place = String(r["name"] | name.c_str()) + (cc.length() ? ", " + cc : "");
    return GeoResult::Ok;
  }
  return GeoResult::NotFound;
}

bool fetchWeather(const Settings& s, WeatherData& w) {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(s.lat, 4) +
               "&longitude=" + String(s.lon, 4) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m"
               "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=24"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
               "sunrise,sunset,daylight_duration,uv_index_max&forecast_days=7"
               "&minutely_15=precipitation&forecast_minutely_15=8"
               "&timezone=auto";
  if (s.imperial) url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";

  JsonDocument doc;
  time_t serverUtc = 0;
  if (!httpGetJson(url, doc, &serverUtc)) return false;

  JsonObject cur = doc["current"];
  if (cur.isNull()) return false;

  w.utcOffset = doc["utc_offset_seconds"] | 0;
  // System clock runs on local time; it keeps ticking through deep sleep.
  time_t local = serverUtc ? serverUtc + w.utcOffset : parseIsoLocal(cur["time"] | "");
  if (local) {
    timeval tv = {local, 0};
    settimeofday(&tv, nullptr);
  }
  w.fetchedAt = local;
  tm t;
  gmtime_r(&local, &t);
  snprintf(w.updated, sizeof(w.updated), "%02d:%02d", t.tm_hour, t.tm_min);

  w.temp = cur["temperature_2m"];
  w.feels = cur["apparent_temperature"];
  w.humidity = cur["relative_humidity_2m"];
  w.wind = cur["wind_speed_10m"];
  w.code = cur["weather_code"];
  w.isDay = (int)cur["is_day"] == 1;

  JsonObject daily = doc["daily"];
  for (int i = 0; i < DAYS; i++) {
    DayForecast& d = w.days[i];
    d.code = daily["weather_code"][i] | 0;
    d.tMax = daily["temperature_2m_max"][i] | 0.0f;
    d.tMin = daily["temperature_2m_min"][i] | 0.0f;
    d.rainPct = daily["precipitation_probability_max"][i] | -1;
    d.uvMax = daily["uv_index_max"][i] | 0.0f;
    d.daylightSec = daily["daylight_duration"][i] | 0.0f;
    copyHHMM(d.sunrise, daily["sunrise"][i] | "");
    copyHHMM(d.sunset, daily["sunset"][i] | "");
    time_t day = parseIsoLocal((String(daily["time"][i] | "1970-01-01") + "T00:00").c_str());
    d.weekday = (day / 86400 + 4) % 7;  // 1970-01-01 was a Thursday
  }

  JsonObject hourly = doc["hourly"];
  for (int i = 0; i < HOURS; i++) {
    HourForecast& h = w.hours[i];
    const char* ts = hourly["time"][i] | "";
    h.hour = strlen(ts) >= 13 ? atoi(ts + 11) : 0;
    h.temp = hourly["temperature_2m"][i] | 0.0f;
    h.rainPct = hourly["precipitation_probability"][i] | -1;
    h.code = hourly["weather_code"][i] | 0;
  }

  for (int i = 0; i < RAIN_SLOTS; i++) w.rain15[i] = doc["minutely_15"]["precipitation"][i] | 0.0f;

  w.valid = true;
  return true;
}

bool fetchAirQuality(const Settings& s, WeatherData& w) {
  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(s.lat, 4) +
               "&longitude=" + String(s.lon, 4) + "&current=european_aqi,us_aqi,pm2_5,pm10,uv_index&timezone=auto";
  JsonDocument doc;
  w.airValid = false;
  if (!httpGetJson(url, doc)) return false;
  JsonObject cur = doc["current"];
  if (cur.isNull()) return false;
  w.aqi = s.imperial ? (cur["us_aqi"] | -1) : (cur["european_aqi"] | -1);
  w.pm25 = cur["pm2_5"] | 0.0f;
  w.pm10 = cur["pm10"] | 0.0f;
  w.uvNow = cur["uv_index"] | 0.0f;
  w.airValid = w.aqi >= 0;
  return w.airValid;
}

const char* weatherText(int c) {
  switch (c) {
    case 0: return "Clear";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: return "Snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm + hail";
    default: return "Unknown";
  }
}
