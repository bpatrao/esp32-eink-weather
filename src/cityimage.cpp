#include "cityimage.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>

static const char* NS = "cityimg";
static const char* KEY_VERSION = "v3|";  // bump to invalidate cached photos
static const char* USER_AGENT = "T5-Weather-Display/1.0 (ESP32 e-paper hobby project)";
static constexpr size_t MAX_JPEG = 160 * 1024;

bool cityImageLoad(const String& key, uint8_t* bits, bool& known) {
  Preferences p;
  known = false;
  if (!p.begin(NS, true)) return false;
  known = p.getString("key", "") == KEY_VERSION + key;
  bool ok = known && p.getBytesLength("img") == IMG_BYTES && p.getBytes("img", bits, IMG_BYTES) == IMG_BYTES;
  p.end();
  return ok;
}

void cityImageStore(const String& key, const uint8_t* bits) {
  Preferences p;
  p.begin(NS, false);
  if (bits) p.putBytes("img", bits, IMG_BYTES);
  else p.remove("img");
  p.putString("key", KEY_VERSION + key);
  p.end();
}

// ---------- download ----------

static void beginHttp(HTTPClient& http, WiFiClientSecure& client, const String& url) {
  client.setInsecure();
  http.setTimeout(15000);
  http.useHTTP10(true);
  http.setUserAgent(USER_AGENT);  // required by Wikimedia's API policy
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, url);
}

// Returns the lead image URL for a Wikipedia article ("" if none / not JPEG).
static ImgResult findImageUrl(const String& title, String& url) {
  String t = title;
  t.replace(' ', '_');
  WiFiClientSecure client;
  HTTPClient http;
  beginHttp(http, client, "https://en.wikipedia.org/api/rest_v1/page/summary/" + t);
  int code = http.GET();
  if (code == 404) { http.end(); return ImgResult::NoImage; }
  if (code != HTTP_CODE_OK) {
    Serial.printf("Wikipedia HTTP %d\n", code);
    http.end();
    return ImgResult::NetworkError;
  }
  JsonDocument filter;
  filter["thumbnail"]["source"] = true;
  filter["type"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return ImgResult::NetworkError;
  url = doc["thumbnail"]["source"] | "";
  int q = url.indexOf('?');  // drop tracking parameters (utm_source=...)
  if (q >= 0) url = url.substring(0, q);
  String lower = url;
  lower.toLowerCase();
  if (!lower.endsWith(".jpg") && !lower.endsWith(".jpeg")) {
    Serial.printf("No JPEG lead image for %s (%s)\n", title.c_str(), url.c_str());
    return ImgResult::NoImage;
  }
  return ImgResult::Ok;
}

static ImgResult download(const String& url, uint8_t*& buf, size_t& len) {
  WiFiClientSecure client;
  HTTPClient http;
  beginHttp(http, client, url);
  int code = http.GET();
  int size = http.getSize();
  if (code != HTTP_CODE_OK || size <= 0 || (size_t)size > MAX_JPEG) {
    Serial.printf("Image HTTP %d, size %d\n", code, size);
    http.end();
    return code == HTTP_CODE_OK ? ImgResult::NoImage : ImgResult::NetworkError;
  }
  buf = (uint8_t*)malloc(size);
  if (!buf) { http.end(); return ImgResult::NoImage; }
  WiFiClient* s = http.getStreamPtr();
  len = 0;
  uint32_t deadline = millis() + 20000;
  while (len < (size_t)size && millis() < deadline) {
    int n = s->read(buf + len, size - len);
    if (n > 0) len += n;
    else delay(5);
  }
  http.end();
  if (len != (size_t)size) {
    free(buf);
    buf = nullptr;
    return ImgResult::NetworkError;
  }
  return ImgResult::Ok;
}

// ---------- decode, crop & scale into a grayscale screen-sized buffer ----------

struct DecodeCtx {
  int cropX, cropY, cropW, cropH;  // in decoded (possibly downscaled) pixels
  uint8_t* gray;                   // IMG_W * IMG_H running averages
  uint8_t* count;
};
static DecodeCtx ctx;

static int onJpegBlock(JPEGDRAW* d) {
  const uint8_t* px = (const uint8_t*)d->pPixels;
  for (int j = 0; j < d->iHeight; j++) {
    int sy = d->y + j - ctx.cropY;
    if (sy < 0 || sy >= ctx.cropH) continue;
    int ty = sy * IMG_H / ctx.cropH;
    for (int i = 0; i < d->iWidth; i++) {
      int sx = d->x + i - ctx.cropX;
      if (sx < 0 || sx >= ctx.cropW) continue;
      int k = ty * IMG_W + sx * IMG_W / ctx.cropW;
      uint8_t n = ctx.count[k], v = px[j * d->iWidth + i];
      ctx.gray[k] = n ? (ctx.gray[k] * n + v) / (n + 1) : v;
      if (n < 255) ctx.count[k] = n + 1;
    }
  }
  return 1;
}

static bool decodeToGray(uint8_t* jpg, size_t len, uint8_t* gray) {
  JPEGDEC* jpeg = new JPEGDEC();
  bool ok = false;
  uint8_t* count = (uint8_t*)calloc(IMG_W * IMG_H, 1);
  if (count && jpeg->openRAM(jpg, len, onJpegBlock)) {
    int w = jpeg->getWidth(), h = jpeg->getHeight();
    // Centre crop to the screen's aspect ratio
    int cw = w, ch = w * IMG_H / IMG_W;
    if (ch > h) { ch = h; cw = h * IMG_W / IMG_H; }
    int cx = (w - cw) / 2, cy = (h - ch) / 2;
    // Let the decoder downscale by a power of two while staying >= screen size
    int scale = 1, opt = 0;
    if (cw >= IMG_W * 8) { scale = 8; opt = JPEG_SCALE_EIGHTH; }
    else if (cw >= IMG_W * 4) { scale = 4; opt = JPEG_SCALE_QUARTER; }
    else if (cw >= IMG_W * 2) { scale = 2; opt = JPEG_SCALE_HALF; }
    ctx = {cx / scale, cy / scale, max(1, cw / scale), max(1, ch / scale), gray, count};
    jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
    Serial.printf("JPEG %dx%d, crop %dx%d, scale 1/%d\n", w, h, cw, ch, scale);
    ok = jpeg->decode(0, 0, opt) == 1;
    if (!ok) Serial.printf("JPEG decode error %d\n", jpeg->getLastError());
    jpeg->close();
    // Upscaled images leave gaps: copy the nearest filled neighbour
    for (int k = 0; ok && k < IMG_W * IMG_H; k++)
      if (!count[k]) gray[k] = k % IMG_W ? gray[k - 1] : (k >= IMG_W ? gray[k - IMG_W] : 128);
  }
  free(count);
  delete jpeg;
  return ok;
}

// ---------- contrast stretch + sharpen + Atkinson dither ----------
// Atkinson (classic Mac) diffuses only 3/4 of the error: flatter blacks and
// whites and less speckle than Floyd-Steinberg, which suits e-paper photos.

static void ditherToBits(uint8_t* gray, uint8_t* bits) {
  uint32_t hist[256] = {};
  for (int k = 0; k < IMG_W * IMG_H; k++) hist[gray[k]]++;
  const uint32_t clip = IMG_W * IMG_H / 50;  // ignore darkest/brightest 2%
  int lo = 0, hi = 255;
  for (uint32_t acc = 0; lo < 255 && (acc += hist[lo]) < clip; lo++) {}
  for (uint32_t acc = 0; hi > 0 && (acc += hist[hi]) < clip; hi--) {}
  if (hi - lo < 32) { lo = max(0, lo - 16); hi = min(255, hi + 16); }
  for (int k = 0; k < IMG_W * IMG_H; k++) gray[k] = constrain((gray[k] - lo) * 255 / (hi - lo), 0, 255);

  // Light unsharp mask (cross-shaped blur), using two saved rows
  uint8_t prev[IMG_W], curRow[IMG_W];
  memcpy(prev, gray, IMG_W);
  for (int y = 1; y < IMG_H - 1; y++) {
    memcpy(curRow, gray + y * IMG_W, IMG_W);
    for (int x = 1; x < IMG_W - 1; x++) {
      int c = curRow[x];
      int blur = (curRow[x - 1] + curRow[x + 1] + prev[x] + gray[(y + 1) * IMG_W + x] + c) / 5;
      gray[y * IMG_W + x] = constrain(c + (c - blur), 0, 255);
    }
    memcpy(prev, curRow, IMG_W);
  }

  // Error rows: current, next, next+1 (x offset by 2 for left/right spill)
  static int16_t rowsBuf[3][IMG_W + 4];
  memset(rowsBuf, 0, sizeof(rowsBuf));
  int16_t *r0 = rowsBuf[0], *r1 = rowsBuf[1], *r2 = rowsBuf[2];
  memset(bits, 0, IMG_BYTES);
  for (int y = 0; y < IMG_H; y++) {
    for (int x = 0; x < IMG_W; x++) {
      int v = gray[y * IMG_W + x] + r0[x + 2];
      int out = v < 128 ? 0 : 255;
      if (!out) bits[y * IMG_STRIDE + x / 8] |= 0x80 >> (x % 8);
      int e = (v - out) / 8;
      r0[x + 3] += e;
      r0[x + 4] += e;
      r1[x + 1] += e;
      r1[x + 2] += e;
      r1[x + 3] += e;
      r2[x + 2] += e;
    }
    int16_t* t = r0;
    r0 = r1;
    r1 = r2;
    r2 = t;
    memset(r2, 0, sizeof(rowsBuf[0]));
  }
}

ImgResult cityImageFetch(const String& title, uint8_t* bits) {
  String url;
  ImgResult r = findImageUrl(title, url);
  if (r != ImgResult::Ok) return r;
  Serial.printf("City photo: %s\n", url.c_str());

  uint8_t* jpg = nullptr;
  size_t len = 0;
  r = download(url, jpg, len);
  if (r != ImgResult::Ok) return r;

  uint8_t* gray = (uint8_t*)malloc(IMG_W * IMG_H);
  bool ok = gray && decodeToGray(jpg, len, gray);
  free(jpg);
  if (ok) ditherToBits(gray, bits);
  free(gray);
  return ok ? ImgResult::Ok : ImgResult::NoImage;
}
