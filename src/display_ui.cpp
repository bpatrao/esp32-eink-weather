#include "display_ui.h"
#ifdef FRAME_DUMP
#define private public  // preview build: read the frame buffer
#endif
#include <GxEPD2_BW.h>
#ifdef FRAME_DUMP
#undef private
#endif
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "cityimage.h"
#include "qrcode.h"  // ESP-IDF's built-in QR generator

// T5 V2.3 display pins, GDEH0213B73 panel (confirmed with the driver test)
static GxEPD2_BW<GxEPD2_213_B73, GxEPD2_213_B73::HEIGHT> display(GxEPD2_213_B73(5, 17, 16, 4));

static constexpr uint16_t BLACK = GxEPD_BLACK;
static constexpr uint16_t WHITE = GxEPD_WHITE;
static constexpr int W = 250;
static constexpr int H = 122;

void uiInit() {
  display.init(0);  // 0 = no GxEPD2 debug output on Serial
  display.setRotation(1);  // landscape, 250 x 122
  display.setTextColor(BLACK);
  display.setTextWrap(false);
  display.cp437(true);
}

void uiSleep() { display.hibernate(); }

// ===================== text helpers =====================

static int16_t textWidth(const String& s) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

static void printRight(const String& s, int16_t rightX, int16_t baseline) {
  display.setCursor(rightX - textWidth(s), baseline);
  display.print(s);
}

static void printCentered(const String& s, int16_t cx, int16_t baseline) {
  display.setCursor(cx - textWidth(s) / 2, baseline);
  display.print(s);
}

// Shortens s with "..." until it fits in maxW pixels (current font).
static String fit(String s, int maxW) {
  if (textWidth(s) <= maxW) return s;
  while (s.length() > 1 && textWidth(s + "...") > maxW) s.remove(s.length() - 1);
  return s + "...";
}

static int degreeRadius() {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("8", 0, 0, &x1, &y1, &w, &h);
  return max(2, (int)h / 8);
}

// GFX fonts are ASCII-only, so draw the degree sign as a small ring.
static void printDegree(int16_t baseline) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("8", 0, baseline, &x1, &y1, &w, &h);  // y1 = top of digits
  int r = degreeRadius();
  int cx = display.getCursorX() + r + 1, cy = y1 + r;
  display.fillCircle(cx, cy, r, BLACK);
  if (r > 2) display.fillCircle(cx, cy, r - max(1, r / 2), WHITE);
  else display.drawPixel(cx, cy, WHITE);
  display.setCursor(cx + r + 2, baseline);
}

static String tempStr(float t) { return String((int)lroundf(t)); }

// Temperature + degree sign centred on cx (current font).
static void printTempCentered(float t, int cx, int baseline) {
  String s = tempStr(t);
  int w = textWidth(s) + degreeRadius() * 2 + 2;
  display.setCursor(cx - w / 2, baseline);
  display.print(s);
  printDegree(baseline);
}

// Classic 6x8 font: cursor is the top-left corner.
static void smallText(const String& s, int16_t x, int16_t top) {
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(x, top);
  display.print(s);
}

static void smallCentered(const String& s, int cx, int top) { smallText(s, cx - (int)s.length() * 3, top); }
static void smallRight(const String& s, int right, int top) { smallText(s, right - (int)s.length() * 6, top); }

// Greedy word-wrap for the 6x8 font, max `cols` characters per line.
static int wrapSmall(const String& text, int16_t x, int16_t top, int cols, int maxLines, int lineH = 10) {
  String rest = text, line;
  int lines = 0;
  while (rest.length() && lines < maxLines) {
    int cut = rest.length() <= (unsigned)cols ? rest.length() : rest.lastIndexOf(' ', cols);
    if (cut <= 0) cut = min((int)rest.length(), cols);
    line = rest.substring(0, cut);
    rest = rest.substring(cut);
    rest.trim();
    if (rest.length() && lines == maxLines - 1) line = line.substring(0, cols - 3) + "...";
    smallText(line, x, top + lines * lineH);
    lines++;
  }
  return lines;
}

// ===================== drawing primitives =====================

static void thickLine(int x0, int y0, int x1, int y1, int t) {
  for (int dx = 0; dx < t; dx++)
    for (int dy = 0; dy < t; dy++) display.drawLine(x0 + dx, y0 + dy, x1 + dx, y1 + dy, BLACK);
}

static void dottedHLine(int x0, int x1, int y, int gap = 3) {
  for (int x = x0; x <= x1; x += gap) display.drawPixel(x, y, BLACK);
}

static void dottedVLine(int x, int y0, int y1, int gap = 3) {
  for (int y = y0; y <= y1; y += gap) display.drawPixel(x, y, BLACK);
}

// 50% checkerboard: reads as grey on a 1-bit display.
static void fillGrey(int x, int y, int w, int h) {
  for (int j = 0; j < h; j++)
    for (int i = (j + y + x) & 1; i < w; i += 2) display.drawPixel(x + i, y + j, BLACK);
}

// Row of `n` segments, the first `filled` black.
static void segmentBar(int x, int y, int w, int h, int n, int filled) {
  int seg = (w - (n - 1) * 2) / n;
  for (int i = 0; i < n; i++) {
    int sx = x + i * (seg + 2);
    if (i < filled) display.fillRect(sx, y, seg, h, BLACK);
    else display.drawRect(sx, y, seg, h, BLACK);
  }
}

// ===================== weather icons =====================

static void sun(int cx, int cy, int r, int t) {
  int core = r * 55 / 100;
  display.fillCircle(cx, cy, core, BLACK);
  display.fillCircle(cx, cy, core - t, WHITE);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    thickLine(cx + cosf(a) * (core + t + 2) - t / 2, cy + sinf(a) * (core + t + 2) - t / 2,
              cx + cosf(a) * r - t / 2, cy + sinf(a) * r - t / 2, t);
  }
}

static void moon(int cx, int cy, int r) {
  display.fillCircle(cx, cy, r, BLACK);
  display.fillCircle(cx + r / 2, cy - r / 3, r * 8 / 10, WHITE);
}

static void cloudShape(int cx, int cy, int w, uint16_t color, int inset) {
  int h = w * 6 / 10;
  int bottom = cy + h / 2, left = cx - w / 2;
  int c1x = left + h * 32 / 100, c1y = bottom - h * 32 / 100, r1 = h * 32 / 100;
  int c2x = cx - w * 5 / 100, c2y = bottom - h * 55 / 100, r2 = h * 45 / 100;
  int c3x = cx + w * 25 / 100, c3y = bottom - h * 38 / 100, r3 = h * 38 / 100;
  display.fillCircle(c1x, c1y, r1 - inset, color);
  display.fillCircle(c2x, c2y, r2 - inset, color);
  display.fillCircle(c3x, c3y, r3 - inset, color);
  int rectTop = bottom - h * 40 / 100;
  display.fillRect(c1x, rectTop, c3x - c1x, bottom - inset - rectTop + 1, color);
}

static void cloud(int cx, int cy, int w, int t) {
  cloudShape(cx, cy, w, BLACK, 0);
  cloudShape(cx, cy, w, WHITE, t);
}

enum Sky { CLEAR, PARTLY, CLOUDY, FOG, DRIZZLE, RAIN, SNOW, THUNDER };

static Sky skyFor(int c) {
  if (c <= 1) return CLEAR;
  if (c == 2) return PARTLY;
  if (c == 3) return CLOUDY;
  if (c == 45 || c == 48) return FOG;
  if (c >= 51 && c <= 57) return DRIZZLE;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return SNOW;
  if (c >= 95) return THUNDER;
  return CLOUDY;
}

// Draws the icon for WMO code `code` inside the s x s box at (x, y).
static void drawIcon(int code, bool isDay, int x, int y, int s) {
  int t = max(1, s / 12);
  int cx = x + s / 2;
  Sky sky = skyFor(code);

  switch (sky) {
    case CLEAR:
      if (isDay) sun(cx, y + s / 2, s * 46 / 100, t);
      else moon(cx, y + s / 2, s * 38 / 100);
      return;
    case PARTLY:
      if (isDay) sun(x + s * 36 / 100, y + s * 36 / 100, s * 32 / 100, t);
      else moon(x + s * 36 / 100, y + s * 34 / 100, s * 26 / 100);
      cloud(x + s * 56 / 100, y + s * 64 / 100, s * 80 / 100, t);
      return;
    case CLOUDY:
      cloud(cx, y + s / 2, s * 95 / 100, t);
      return;
    case FOG:
      for (int i = 0; i < 4; i++) {
        int ly = y + s * (25 + i * 16) / 100;
        int inset = (i % 2) ? s / 10 : 0;
        thickLine(x + s / 10 + inset, ly, x + s * 9 / 10 - (s / 10 - inset), ly, max(2, t));
      }
      return;
    default:
      break;
  }

  // Precipitation: cloud on top, symbols underneath
  cloud(cx, y + s * 36 / 100, s * 92 / 100, t);
  int y0 = y + s * 70 / 100;
  int step = s / 4;
  for (int i = -1; i <= 1; i++) {
    int px = cx + i * step;
    switch (sky) {
      case RAIN:
        thickLine(px + s / 16, y0, px - s / 16, y0 + s * 22 / 100, max(1, t - 1) + 1);
        break;
      case DRIZZLE:
        display.fillCircle(px, y0 + s / 20, max(1, s / 22), BLACK);
        display.fillCircle(px - s / 16, y0 + s * 18 / 100, max(1, s / 22), BLACK);
        break;
      case SNOW: {
        int l = max(2, s / 12), sy = y0 + s / 10;
        display.drawLine(px - l, sy, px + l, sy, BLACK);
        display.drawLine(px - l / 2, sy - l, px + l / 2, sy + l, BLACK);
        display.drawLine(px + l / 2, sy - l, px - l / 2, sy + l, BLACK);
        break;
      }
      case THUNDER:
        if (i == 0) {
          int u = max(1, s / 24);
          display.fillTriangle(cx + 2 * u, y0 - 2 * u, cx - 5 * u, y0 + 6 * u, cx + 1 * u, y0 + 6 * u, BLACK);
          display.fillTriangle(cx - 1 * u, y0 + 4 * u, cx + 5 * u, y0 + 4 * u, cx - 3 * u, y0 + 13 * u, BLACK);
        }
        break;
      default:
        break;
    }
  }
}

// ===================== QR code / setup / messages =====================

static constexpr int QR_MAX = 41;  // version 6
static uint8_t qrSize = 0;
static bool qrModules[QR_MAX * QR_MAX];

static void qrCapture(esp_qrcode_handle_t q) {
  qrSize = esp_qrcode_get_size(q);
  if (qrSize > QR_MAX) { qrSize = 0; return; }
  for (int y = 0; y < qrSize; y++)
    for (int x = 0; x < qrSize; x++) qrModules[y * qrSize + x] = esp_qrcode_get_module(q, x, y);
}

static bool makeQr(const String& text) {
  esp_qrcode_config_t cfg = {};
  cfg.display_func = qrCapture;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  qrSize = 0;
  return esp_qrcode_generate(&cfg, text.c_str()) == ESP_OK && qrSize > 0;
}

void uiSetupScreen(const String& apSsid, const String& apPass, const String& error, const String& homeIp) {
  // Standard Wi-Fi QR format understood by iOS and Android cameras
  bool haveQr = makeQr("WIFI:T:WPA;S:" + apSsid + ";P:" + apPass + ";;");
  int scale = haveQr ? min(3, 110 / qrSize) : 0;
  int qrPx = qrSize * scale;
  int qx = 6, qy = (H - qrPx) / 2;
  int tx = haveQr ? qx + qrPx + 8 : 6;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(WHITE);
    for (int y = 0; y < qrSize && haveQr; y++)
      for (int x = 0; x < qrSize; x++)
        if (qrModules[y * qrSize + x]) display.fillRect(qx + x * scale, qy + y * scale, scale, scale, BLACK);

    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(tx, 16);
    display.print("Weather setup");

    smallText("Scan the code with your", tx, 24);
    smallText("phone camera, join the", tx, 34);
    smallText("Wi-Fi and follow the page", tx, 44);
    smallText("Wi-Fi: " + apSsid, tx, 58);
    smallText("Pass:  " + apPass, tx, 68);
    smallText("Page:  192.168.4.1", tx, 78);
    if (homeIp.length()) smallText("OTA:   " + homeIp, tx, 88);

    if (error.length()) {
      int top = homeIp.length() ? 99 : 89;
      display.fillRect(tx - 3, top, W - tx + 3, H - top, BLACK);
      display.setTextColor(WHITE);
      wrapSmall(error, tx, top + 3, (W - tx) / 6, (H - top - 3) / 10);
      display.setTextColor(BLACK);
    }
  } while (display.nextPage());
}

void uiMessage(const String& title, const String& l1, const String& l2, const String& l3) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(WHITE);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(8, 30);
    display.print(title);
    display.drawLine(8, 38, W - 8, 38, BLACK);
    display.setFont(&FreeSans9pt7b);
    const String* lines[] = {&l1, &l2, &l3};
    for (int i = 0; i < 3; i++) {
      display.setCursor(8, 60 + i * 20);
      display.print(*lines[i]);
    }
  } while (display.nextPage());
}

// ===================== shared view parts =====================

static const char* WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* WEEKDAYS2[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

// Battery outline with fill proportional to charge and the percentage written
// inside (black on the empty part, white on the filled part); `right` is the
// nub's right edge.
static constexpr int BAT_W = 24, BAT_H = 11;  // body; the nub adds 2 px
static void drawBattery(int pct, int right, int top) {
  pct = constrain(pct, 0, 100);
  int x = right - 2 - BAT_W;
  display.drawRect(x, top, BAT_W, BAT_H, BLACK);
  display.fillRect(x + BAT_W, top + 3, 2, BAT_H - 6, BLACK);  // nub
  const int inW = BAT_W - 4, inH = BAT_H - 4, inX = x + 2, inY = top + 2;
  int fill = inW * pct / 100;
  if (pct > 0 && fill == 0) fill = 1;
  display.fillRect(inX, inY, fill, inH, BLACK);

  // Render the number off-screen, then copy it in the colour that contrasts
  // with whatever is underneath each pixel.
  String label(pct);
  GFXcanvas1 canvas(inW, inH);
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setCursor((inW - ((int)label.length() * 6 - 1)) / 2, 0);
  canvas.print(label);
  for (int cy = 0; cy < inH; cy++)
    for (int cx = 0; cx < inW; cx++)
      if (canvas.getPixel(cx, cy)) display.drawPixel(inX + cx, inY + cy, cx < fill ? WHITE : BLACK);
}

// Small dots showing which view is active.
static void pageDots(View v, int right, int y) {
  for (int i = 0; i < VIEW_COUNT; i++) {
    int x = right - (VIEW_COUNT - 1 - i) * 5;
    if (i == v) display.fillCircle(x, y, 2, BLACK);
    else display.drawPixel(x, y, BLACK);
  }
}

// Title left, page dots + battery + update time right, rule underneath.
static void header(const ViewContext& c, View v, const String& title) {
  display.setFont(&FreeSans9pt7b);
  int timeW = textWidth(c.w.updated);
  printRight(c.w.updated, W - 4, 13);
  int batRight = W - 4 - timeW - 6;
  drawBattery(c.batteryPct, batRight, 3);
  int dotsRight = batRight - (BAT_W + 2) - 6;
  pageDots(v, dotsRight, 8);
  int dotsLeft = dotsRight - (VIEW_COUNT - 1) * 5;
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(4, 13);
  display.print(fit(title, dotsLeft - 10));
  display.drawLine(0, 17, W, 17, BLACK);
}

// ===================== views =====================

static void viewHome(const ViewContext& c) {
  const WeatherData& w = c.w;
  header(c, VIEW_HOME, c.s.place);

  drawIcon(w.code, w.isDay, 4, 21, 46);
  display.setFont(&FreeSansBold24pt7b);
  display.setCursor(56, 59);
  display.print(tempStr(w.temp));
  printDegree(59);

  display.setFont(&FreeSans9pt7b);
  display.setCursor(4, 79);
  display.print(weatherText(w.code));

  const char* windUnit = c.s.imperial ? " mph" : " km/h";
  struct { const char* label; String value; bool deg; } stats[] = {
      {"Feels", tempStr(w.feels), true},
      {"Humidity", String(w.humidity) + "%", false},
      {"Wind", String((int)lroundf(w.wind)) + windUnit, false},
  };
  for (int i = 0; i < 3; i++) {
    int baseline = 37 + i * 20;
    smallText(stats[i].label, 152, baseline - 8);
    display.setFont(&FreeSansBold9pt7b);
    int right = W - 4 - (stats[i].deg ? 7 : 0);
    printRight(stats[i].value, right, baseline);
    if (stats[i].deg) printDegree(baseline);
  }
  display.drawLine(0, 85, W, 85, BLACK);

  for (int i = 0; i < 3; i++) {
    const DayForecast& d = w.days[i];
    int x0 = i * (W / 3);
    if (i) display.drawLine(x0, 86, x0, H, BLACK);
    drawIcon(d.code, true, x0 + 4, 88, 24);
    if (d.rainPct > 0) smallText(String(d.rainPct) + "%", x0 + 2, 113);
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(x0 + 30, 101);
    display.print(i == 0 ? "Today" : WEEKDAYS[d.weekday]);
    display.setFont(&FreeSans9pt7b);
    display.setCursor(x0 + 30, 118);
    display.print(tempStr(d.tMax) + "/" + tempStr(d.tMin));
    printDegree(118);
  }
}

static void viewWeek(const ViewContext& c) {
  header(c, VIEW_WEEK, "7 days");
  const int colW = W / DAYS;  // 35
  const int off = (W - colW * DAYS) / 2;
  for (int i = 0; i < DAYS; i++) {
    const DayForecast& d = c.w.days[i];
    int x0 = off + i * colW, cx = x0 + colW / 2;
    if (i) dottedVLine(x0, 20, H - 2);
    display.setFont(&FreeSansBold9pt7b);
    if (i == 0) {  // highlight today
      display.fillRect(x0 + 1, 19, colW - 1, 18, BLACK);
      display.setTextColor(WHITE);
    }
    printCentered(WEEKDAYS2[d.weekday], cx, 33);
    display.setTextColor(BLACK);
    drawIcon(d.code, true, cx - 13, 40, 26);
    display.setFont(&FreeSansBold9pt7b);
    printTempCentered(d.tMax, cx, 85);
    display.setFont(&FreeSans9pt7b);
    printTempCentered(d.tMin, cx, 102);
    if (d.rainPct > 0) smallCentered(String(d.rainPct) + "%", cx, 110);
  }
}

static void viewHourly(const ViewContext& c) {
  header(c, VIEW_HOURLY, "Next 24 hours");
  const HourForecast* hr = c.w.hours;
  float tMin = hr[0].temp, tMax = hr[0].temp;
  for (int i = 1; i < HOURS; i++) {
    tMin = min(tMin, hr[i].temp);
    tMax = max(tMax, hr[i].temp);
  }
  if (tMax - tMin < 4) {
    float mid = (tMax + tMin) / 2;
    tMin = mid - 2;
    tMax = mid + 2;
  }

  const int x0 = 28, x1 = 244, top = 32, bottom = 72;
  auto xAt = [&](int i) { return x0 + i * (x1 - x0) / (HOURS - 1); };
  auto yAt = [&](float t) { return bottom - (int)lroundf((t - tMin) * (bottom - top) / (tMax - tMin)); };

  // Temperature axis
  dottedHLine(x0, x1, top);
  dottedHLine(x0, x1, bottom);
  smallRight(tempStr(tMax), x0 - 4, top - 4);
  smallRight(tempStr(tMin), x0 - 4, bottom - 4);

  // Temperature line, with values every 6 hours
  for (int i = 0; i < HOURS - 1; i++) thickLine(xAt(i), yAt(hr[i].temp), xAt(i + 1), yAt(hr[i + 1].temp), 2);
  for (int i = 0; i < HOURS; i += 6) {
    int x = xAt(i), y = yAt(hr[i].temp);
    display.fillCircle(x, y, 3, BLACK);
    String v = tempStr(hr[i].temp);
    display.fillRect(x - v.length() * 3 - 1, y - 13, v.length() * 6 + 2, 9, WHITE);
    smallCentered(v, x, y - 12);
  }

  // Rain probability bars
  const int barTop = 80, barBottom = 100;
  smallText("Rain", 2, 86);
  dottedHLine(x0, x1, barBottom);
  for (int i = 0; i < HOURS; i++) {
    int pct = max(0, (int)hr[i].rainPct);
    int h = pct * (barBottom - barTop) / 100;
    if (!h) continue;
    int x = xAt(i) - 3;
    fillGrey(x, barBottom - h, 7, h);
    display.drawFastHLine(x, barBottom - h, 7, BLACK);
  }

  // Hour labels
  for (int i = 0; i < HOURS; i += 3) {
    int x = xAt(i);
    display.drawFastVLine(x, barBottom + 1, i % 6 ? 2 : 4, BLACK);
    if (i % 6 == 0) smallCentered(i == 0 ? String("now") : String(hr[i].hour) + "h", x, 108);
  }
}

static void viewRain(const ViewContext& c) {
  header(c, VIEW_RAIN, "Rain next 2 h");
  const float* r = c.w.rain15;
  const float WET = 0.1f;  // mm per 15 min
  float total = 0, peak = 0;
  int firstWet = -1, firstDryAfterWet = -1;
  for (int i = 0; i < RAIN_SLOTS; i++) {
    total += r[i];
    peak = max(peak, r[i]);
    if (r[i] >= WET && firstWet < 0) firstWet = i;
    if (firstWet >= 0 && firstDryAfterWet < 0 && r[i] < WET) firstDryAfterWet = i;
  }

  String headline, detail;
  if (firstWet < 0) headline = "No rain for 2 hours";
  else if (firstWet == 0 && firstDryAfterWet < 0) headline = "Rain for the next 2 h";
  else if (firstWet == 0) headline = "Rain stops in ~" + String(firstDryAfterWet * 15) + " min";
  else headline = "Rain in ~" + String(firstWet * 15) + " min";

  if (firstWet >= 0) {
    float rate = peak * 4;  // mm/h
    const char* kind = rate < 2.5f ? "Light" : rate < 7.6f ? "Moderate" : "Heavy";
    detail = String(kind) + " rain, " + String(total, 1) + " mm total";
  } else {
    int chance = 0;
    for (int i = 0; i < 6; i++) chance = max(chance, (int)c.w.hours[i].rainPct);
    detail = "Next 6 h: up to " + String(chance) + "% chance";
  }
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(4, 36);
  display.print(fit(headline, W - 8));
  display.setFont(&FreeSans9pt7b);
  display.setCursor(4, 54);
  display.print(fit(detail, W - 8));

  // 15-minute bars
  const int x0 = 10, barW = 25, gap = 4, chartTop = 62, chartBottom = 102;
  float scale = max(peak, 0.5f);
  display.drawFastHLine(x0 - 2, chartBottom, RAIN_SLOTS * (barW + gap), BLACK);
  for (int i = 0; i < RAIN_SLOTS; i++) {
    int x = x0 + i * (barW + gap);
    int h = (int)lroundf(r[i] / scale * (chartBottom - chartTop));
    if (r[i] > 0 && h < 2) h = 2;
    if (h) {
      fillGrey(x, chartBottom - h, barW, h);
      display.drawRect(x, chartBottom - h, barW, h, BLACK);
    }
  }
  const char* labels[] = {"now", "+30m", "+1h", "+1h30"};
  for (int i = 0; i < 4; i++) smallText(labels[i], x0 + i * 2 * (barW + gap), 107);
  smallRight("2h", x0 + RAIN_SLOTS * (barW + gap) - 2, 107);
}

static void levelInfo(int value, const int* limits, const char* const* names, int n, int& level, const char*& name) {
  level = 0;
  while (level < n - 1 && value > limits[level]) level++;
  name = names[level];
}

static void viewAir(const ViewContext& c) {
  header(c, VIEW_AIR, "Air & UV");
  const WeatherData& w = c.w;
  const int mid = W / 2;
  display.drawFastVLine(mid, 22, H - 26, BLACK);

  // Air quality (European AQI for metric, US AQI for imperial)
  smallText(c.s.imperial ? "US AQI" : "EU AQI", 6, 24);
  if (w.airValid) {
    static const int euLim[] = {20, 40, 60, 80, 100, 9999};
    static const char* const euNames[] = {"Good", "Fair", "Moderate", "Poor", "Very poor", "Extremely poor"};
    static const int usLim[] = {50, 100, 150, 200, 300, 9999};
    static const char* const usNames[] = {"Good", "Moderate", "Unhealthy (SG)", "Unhealthy", "Very unhealthy", "Hazardous"};
    int level;
    const char* name;
    levelInfo(w.aqi, c.s.imperial ? usLim : euLim, c.s.imperial ? usNames : euNames, 6, level, name);
    display.setFont(&FreeSansBold24pt7b);
    display.setCursor(4, 70);
    display.print(w.aqi);
    display.setFont(&FreeSans9pt7b);
    display.setCursor(6, 88);
    display.print(fit(name, mid - 10));
    segmentBar(6, 94, mid - 14, 6, 6, level + 1);
    smallText("PM2.5 " + String((int)lroundf(w.pm25)) + "  PM10 " + String((int)lroundf(w.pm10)), 6, 106);
  } else {
    display.setFont(&FreeSans9pt7b);
    display.setCursor(6, 64);
    display.print("No data");
  }

  // UV index
  static const int uvLim[] = {2, 5, 7, 10, 99};
  static const char* const uvNames[] = {"Low", "Moderate", "High", "Very high", "Extreme"};
  float uv = w.airValid ? w.uvNow : w.days[0].uvMax;
  int level;
  const char* name;
  levelInfo((int)lroundf(uv), uvLim, uvNames, 5, level, name);
  int x = mid + 8;
  smallText(w.airValid ? "UV index now" : "UV index (max)", x, 24);
  display.setFont(&FreeSansBold24pt7b);
  display.setCursor(x - 2, 70);
  display.print((int)lroundf(uv));
  display.setFont(&FreeSans9pt7b);
  display.setCursor(x, 88);
  display.print(name);
  segmentBar(x, 94, W - x - 6, 6, 5, level + 1);
  smallText("Max today " + String((int)lroundf(w.days[0].uvMax)), x, 106);
}

// Moon phase 0..1 (0 = new, 0.5 = full) for a UTC epoch.
static float moonPhase(time_t utc) {
  double jd = utc / 86400.0 + 2440587.5;
  double p = fmod((jd - 2451550.1) / 29.530588853, 1.0);
  return p < 0 ? p + 1 : p;
}

static const char* moonName(float p) {
  if (p < 0.03f || p > 0.97f) return "New moon";
  if (p < 0.22f) return "Waxing crescent";
  if (p < 0.28f) return "First quarter";
  if (p < 0.47f) return "Waxing gibbous";
  if (p < 0.53f) return "Full moon";
  if (p < 0.72f) return "Waning gibbous";
  if (p < 0.78f) return "Last quarter";
  return "Waning crescent";
}

// Dark disc with the lit part drawn per row (mirrored in the south).
static void drawMoon(int cx, int cy, int r, float p, bool south) {
  display.fillCircle(cx, cy, r, BLACK);
  float k = cosf(2 * PI * p);
  for (int dy = -r + 1; dy < r; dy++) {
    float hw = sqrtf((float)r * r - dy * dy) - 1;
    float a, b;  // lit span, relative to centre (waxing = right side lit)
    if (p <= 0.5f) { a = k * hw; b = hw; }
    else { a = -hw; b = -k * hw; }
    if (south) { float t = a; a = -b; b = -t; }
    int xa = cx + (int)lroundf(a), xb = cx + (int)lroundf(b);
    if (xb > xa) display.drawFastHLine(xa, cy + dy, xb - xa, WHITE);
  }
  display.drawCircle(cx, cy, r, BLACK);
}

static int minutesOf(const char* hhmm) { return atoi(hhmm) * 60 + atoi(hhmm + 3); }

static void viewSun(const ViewContext& c) {
  header(c, VIEW_SUN, "Sun & Moon");
  const DayForecast& today = c.w.days[0];
  time_t now = time(nullptr);
  tm t;
  gmtime_r(&now, &t);  // system clock holds local time
  int nowMin = t.tm_hour * 60 + t.tm_min;

  // Sun arc: sunrise on the left horizon, sunset on the right
  const int cx = 62, cy = 82, r = 48;
  display.drawFastHLine(6, cy, 114, BLACK);
  for (int deg = 0; deg <= 180; deg += 6)
    display.drawPixel(cx + r * cosf(deg * PI / 180), cy - r * sinf(deg * PI / 180), BLACK);
  int rise = minutesOf(today.sunrise), set = minutesOf(today.sunset);
  if (set > rise && nowMin >= rise && nowMin <= set) {
    float f = (float)(nowMin - rise) / (set - rise);
    float a = PI * (1 - f);
    sun(cx + r * cosf(a), cy - r * sinf(a), 10, 2);
  } else {
    smallCentered("night", cx, cy - 16);
  }
  smallText(String((char)24) + String(today.sunrise), 4, cy + 5);
  smallRight(String((char)25) + String(today.sunset), 122, cy + 5);
  int len = today.daylightSec / 60, diff = (int)(c.w.days[1].daylightSec / 60) - len;
  String day = "Day " + String(len / 60) + "h" + (len % 60 < 10 ? "0" : "") + String(len % 60);
  smallText(day + " (" + (diff >= 0 ? "+" : "") + String(diff) + "m)", 4, cy + 18);

  // Moon
  display.drawFastVLine(128, 22, H - 26, BLACK);
  float p = moonPhase(now - c.w.utcOffset);
  int mx = 189;
  drawMoon(mx, 48, 22, p, c.s.lat < 0);
  display.setFont(&FreeSans9pt7b);
  if (textWidth(moonName(p)) <= W - 132) printCentered(moonName(p), mx, 88);
  else smallCentered(moonName(p), mx, 80);
  int lit = (int)lroundf((1 - cosf(2 * PI * p)) * 50);
  float toFull = fmodf(0.5f - p + 1, 1) * 29.53f, toNew = (1 - p) * 29.53f;
  smallCentered(String(lit) + "% lit", mx, 95);
  String next = toFull < toNew ? "Full in " + String((int)lroundf(toFull)) + " days"
                               : "New in " + String((int)lroundf(toNew)) + " days";
  smallCentered(next, mx, 107);
}

static void viewCity(const ViewContext& c) {
  const WeatherData& w = c.w;
  if (c.cityBits) {
    display.drawBitmap(0, 0, c.cityBits, IMG_W, IMG_H, BLACK);
  } else {
    smallCentered("No photo found for", W / 2, 24);
    smallCentered(c.s.place, W / 2, 34);
  }

  // Label box: place + temperature + icon
  display.setFont(&FreeSansBold9pt7b);
  String place = fit(c.s.place, 150);
  int placeW = textWidth(place);
  display.setFont(&FreeSansBold24pt7b);
  int tempW = textWidth(tempStr(w.temp)) + 12;
  int boxW = max(placeW, tempW + 38) + 14, boxX = 3, boxY = 62, boxH = 57;
  display.fillRoundRect(boxX, boxY, boxW, boxH, 6, WHITE);
  display.drawRoundRect(boxX, boxY, boxW, boxH, 6, BLACK);
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(boxX + 7, boxY + 17);
  display.print(place);
  display.setFont(&FreeSansBold24pt7b);
  display.setCursor(boxX + 5, boxY + 52);
  display.print(tempStr(w.temp));
  printDegree(boxY + 52);
  drawIcon(w.code, w.isDay, boxX + 7 + tempW + 4, boxY + 22, 30);
}

static void viewInfo(const ViewContext& c) {
  header(c, VIEW_INFO, "Settings");
  const Settings& s = c.s;
  const WeatherData& w = c.w;
  char coords[32];
  snprintf(coords, sizeof(coords), "%.2f,%.2f", s.lat, s.lon);
  String refresh = s.refreshMin < 60 ? String(s.refreshMin) + " min" : String(s.refreshMin / 60) + " h";
  // value in the normal font, extra detail after it in the small font
  struct { const char* label; String value; String extra; } rows[] = {
      {"Wi-Fi", s.ssid, ""},
      {"IP", w.ip, String(w.rssi) + " dBm"},  // short extra: the IP needs up to 135 px
      {"Location", s.place, coords},
      {"Units", String(s.imperial ? "Imperial" : "Metric") + ", every " + refresh, ""},
      {"Battery", String(c.batteryMv / 1000.0f, 2) + " V", String(c.batteryPct) + "%"},
  };
  for (int i = 0; i < 5; i++) {
    int baseline = 34 + i * 17;
    smallText(rows[i].label, 4, baseline - 9);
    int extraW = rows[i].extra.length() ? rows[i].extra.length() * 6 + 6 : 0;
    display.setFont(&FreeSans9pt7b);
    display.setCursor(58, baseline);
    display.print(fit(rows[i].value, W - 62 - extraW));
    if (extraW) smallText(rows[i].extra, display.getCursorX() + 6, baseline - 8);
  }
  display.fillRect(0, H - 14, W, 14, BLACK);
  display.setTextColor(WHITE);
  // Build stamp (compile time of this file), e.g. "fw Nov 23 16:20", to see
  // which firmware is installed after an OTA update.
  String stamp = String(__DATE__).substring(0, 6) + " " + String(__TIME__).substring(0, 5);
  stamp.replace("  ", " ");  // __DATE__ pads single-digit days
  smallCentered("Hold 3 s for setup - fw " + stamp, W / 2, H - 11);
  display.setTextColor(BLACK);
}

static void drawView(View v, const ViewContext& c) {
  display.fillScreen(WHITE);
  switch (v) {
    case VIEW_HOME: viewHome(c); break;
    case VIEW_WEEK: viewWeek(c); break;
    case VIEW_HOURLY: viewHourly(c); break;
    case VIEW_RAIN: viewRain(c); break;
    case VIEW_AIR: viewAir(c); break;
    case VIEW_SUN: viewSun(c); break;
    case VIEW_CITY: viewCity(c); break;
    case VIEW_INFO: viewInfo(c); break;
    default: break;
  }
}

void uiView(View v, const ViewContext& c, bool fast) {
  if (fast) display.setPartialWindow(0, 0, W, H);
  else display.setFullWindow();
  display.firstPage();
  do {
    drawView(v, c);
  } while (display.nextPage());
}

#ifdef FRAME_DUMP
// Renders a view into the buffer (no panel refresh) and prints it as hex.
// Native buffer: 16 bytes per row, 250 rows, bit set = white.
void uiDumpView(View v, const ViewContext& c) {
  display.setFullWindow();
  display.firstPage();
  drawView(v, c);
  Serial.printf("FRAME %d\n", v);
  for (size_t i = 0; i < sizeof(display._buffer); i++) {
    Serial.printf("%02X", display._buffer[i]);
    if (i % 64 == 63) Serial.println();
  }
  Serial.println("\nEND");
}
#endif
