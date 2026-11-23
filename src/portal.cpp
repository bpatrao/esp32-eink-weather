#include "portal.h"
#include <DNSServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <algorithm>
#include <vector>
#include "display_ui.h"

static constexpr uint32_t PORTAL_TIMEOUT_MS = 10 * 60 * 1000;  // since last page request / OTA activity
static constexpr uint32_t HOME_WIFI_TIMEOUT_MS = 10000;
static const IPAddress AP_IP(192, 168, 4, 1);
static const uint16_t REFRESH_OPTIONS[] = {15, 30, 60, 120, 180};

static WebServer server(80);
static DNSServer dns;
static Settings* cfg;
static std::vector<String> networks;
static uint32_t lastActivity = 0;
static uint32_t restartAt = 0;
static String otaPass;  // = setup Wi-Fi password; guards /update on the home network
static bool uploadOk = false;
static String uploadError;

static String esc(const String& in) {
  String s = in;
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

// The settings page and the web firmware upload are only served to devices on
// the password-protected setup network, not to the whole home network.
static bool fromAp() {
  if (server.client().localIP() == AP_IP) return true;
  server.send(403, "text/plain", "Join the display's setup Wi-Fi to use this page.");
  return false;
}

// Firmware upload: open on the setup network, and on the home network with
// HTTP basic auth (any user name, the setup password). Sends the auth
// challenge and returns false when not allowed.
static bool updateAllowed() {
  if (server.client().localIP() == AP_IP) return true;
  if (otaPass.length() && server.authenticate("ota", otaPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "T5 weather update", "Use the password shown on the display.");
  return false;
}

// Leave setup mode after an update if the saved settings are known to work,
// so the board restarts straight into the weather app.
static void finishSetupAfterUpdate() {
  if (cfg->verified) {
    cfg->needsSetup = false;
    config::save(*cfg);
  }
}

static void scanNetworks() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  std::vector<int> idx;
  for (int i = 0; i < n; i++) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });
  networks.clear();
  for (int i : idx) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty() || std::find(networks.begin(), networks.end(), ssid) != networks.end()) continue;
    networks.push_back(ssid);
    if (networks.size() >= 20) break;
  }
  WiFi.scanDelete();
  Serial.printf("Found %u networks\n", (unsigned)networks.size());
}

static String page(const String& body) {
  String html = F(
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Weather display setup</title><style>"
      "body{font-family:-apple-system,system-ui,sans-serif;margin:0;padding:16px;background:#f2f2ef;color:#111}"
      ".card{max-width:420px;margin:0 auto;background:#fff;border-radius:14px;padding:20px;box-shadow:0 1px 4px rgba(0,0,0,.12)}"
      "h1{font-size:1.3em;margin:0 0 4px}p.sub{margin:0 0 12px;color:#666;font-size:.9em}"
      "label{display:block;font-weight:600;margin:16px 0 6px;font-size:.9em}"
      "input,select{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #bbb;border-radius:8px;background:#fff;color:#111}"
      ".hint{color:#777;font-size:.8em;margin-top:5px}"
      ".err{background:#fde8e8;color:#8a1c1c;padding:10px 12px;border-radius:8px;margin:12px 0}"
      ".row{display:flex;gap:10px}.row>div{flex:1}"
      ".chk{display:flex;align-items:center;gap:6px;font-weight:400;margin:8px 0 0;font-size:.85em}.chk input{width:auto}"
      "button{margin-top:22px;width:100%;padding:13px;font-size:16px;font-weight:600;border:0;border-radius:8px;background:#111;color:#fff}"
      "</style></head><body><div class='card'>");
  html += body;
  html += F("</div></body></html>");
  return html;
}

static void handleRoot() {
  lastActivity = millis();
  if (!fromAp()) return;
  const Settings& s = *cfg;
  String b;
  b.reserve(4096);
  b += F("<h1>Weather display setup</h1><p class='sub'>Connect the display to your Wi-Fi and choose what to show.</p>");
  if (s.lastError.length()) b += "<div class='err'>" + esc(s.lastError) + "</div>";

  b += F("<form method='POST' action='/save'><label for='net'>Wi-Fi network</label><select id='net' onchange='pick(this)'>"
         "<option value=''>Choose a network&hellip;</option>");
  for (const String& n : networks)
    b += "<option" + String(n == s.ssid ? " selected" : "") + ">" + esc(n) + "</option>";
  b += F("<option value='__other'>Other / hidden network&hellip;</option></select>");
  b += "<input id='ssid' name='ssid' required placeholder='Network name' style='margin-top:8px' value='" + esc(s.ssid) + "'>";

  b += F("<label for='pass'>Wi-Fi password</label><input id='pass' name='pass' type='password' autocomplete='off'");
  if (s.ssid.length()) b += F(" placeholder='Leave blank to keep the saved password'");
  b += F("><label class='chk'><input type='checkbox' onchange=\"pass.type=this.checked?'text':'password'\"> Show password</label>");

  b += "<label for='loc'>Location</label><input id='loc' name='loc' required placeholder='e.g. Lisbon' value='" +
       esc(s.location) + "'>";
  b += F("<div class='hint'>A city, optionally with its country (<i>Porto, Portugal</i>), or coordinates (<i>38.72,-9.14</i>).</div>");

  b += F("<div class='row'><div><label for='units'>Units</label><select id='units' name='units'>");
  b += "<option value='metric'" + String(s.imperial ? "" : " selected") + ">&deg;C, km/h</option>";
  b += "<option value='imperial'" + String(s.imperial ? " selected" : "") + ">&deg;F, mph</option>";
  b += F("</select></div><div><label for='refresh'>Update every</label><select id='refresh' name='refresh'>");
  for (uint16_t m : REFRESH_OPTIONS) {
    String label = m < 60 ? String(m) + " min" : String(m / 60) + (m == 60 ? " hour" : " hours");
    b += "<option value='" + String(m) + "'" + (m == s.refreshMin ? " selected" : "") + ">" + label + "</option>";
  }
  b += F("</select></div></div><div class='hint'>Longer intervals make the battery last longer.</div>"
         "<button type='submit'>Save and connect</button></form>"
         "<p class='hint' style='text-align:center;margin-top:18px'><a href='/update'>Firmware update</a></p>"
         "<script>function pick(e){var s=document.getElementById('ssid');"
         "if(e.value=='__other'){s.value='';s.focus();}else if(e.value){s.value=e.value;document.getElementById('pass').focus();}}</script>");
  server.send(200, "text/html", page(b));
}

static void handleSave() {
  lastActivity = millis();
  if (!fromAp()) return;
  Settings& s = *cfg;
  String ssid = server.arg("ssid"), pass = server.arg("pass"), loc = server.arg("loc");
  ssid.trim();
  loc.trim();
  uint16_t refresh = server.arg("refresh").toInt();

  bool refreshOk = std::find(std::begin(REFRESH_OPTIONS), std::end(REFRESH_OPTIONS), refresh) != std::end(REFRESH_OPTIONS);
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63 || loc.isEmpty() || !refreshOk) {
    s.lastError = "Please fill in the network name and location.";
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
    return;
  }

  if (pass.isEmpty() && ssid == s.ssid) pass = s.pass;  // keep saved password
  if (loc != s.location) {                               // re-resolve on next boot
    s.lat = NAN;
    s.lon = NAN;
    s.place = "";
  }
  s.ssid = ssid;
  s.pass = pass;
  s.location = loc;
  s.imperial = server.arg("units") == "imperial";
  s.refreshMin = refresh;
  s.verified = false;  // new credentials must prove themselves
  s.needsSetup = false;
  s.lastError = "";
  config::save(s);
  Serial.printf("Saved settings: ssid='%s' location='%s' refresh=%u\n", ssid.c_str(), loc.c_str(), refresh);

  server.send(200, "text/html",
              page("<h1>Saved</h1><p>The display is now connecting to <b>" + esc(ssid) +
                   "</b>. This setup network will disappear and your phone should go back to its usual Wi-Fi.</p>"
                   "<p>If the connection fails, the display shows the setup QR code again with the reason.</p>"));
  restartAt = millis() + 1500;  // let the response reach the phone first
}

// ---------- firmware update from the browser ----------

static void handleUpdatePage() {
  lastActivity = millis();
  if (!updateAllowed()) return;
  server.send(200, "text/html",
              page(F("<h1>Firmware update</h1><p class='sub'>Choose the <b>firmware.bin</b> built by PlatformIO "
                     "(<i>.pio/build/weather/firmware.bin</i>). Settings are kept.</p>"
                     "<form method='POST' action='/update' enctype='multipart/form-data'>"
                     "<input type='file' name='fw' accept='.bin' required>"
                     "<button type='submit'>Upload and install</button></form>"
                     "<div class='hint'>Takes about 30 s. Keep this page open until it reports back.</div>"
                     "<p class='hint' style='text-align:center;margin-top:18px'><a href='/'>Back to settings</a></p>")));
}

static void handleUpdateUpload() {
  lastActivity = millis();
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      uploadOk = false;
      uploadError = "";
      if (server.client().localIP() != AP_IP && !(otaPass.length() && server.authenticate("ota", otaPass.c_str()))) {
        uploadError = "Wrong or missing password.";
        return;
      }
      Serial.printf("Web update: %s\n", up.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) uploadError = Update.errorString();
      break;
    case UPLOAD_FILE_WRITE:
      if (uploadError.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize)
        uploadError = Update.errorString();
      break;
    case UPLOAD_FILE_END:
      if (uploadError.isEmpty()) {
        if (Update.end(true)) uploadOk = true;
        else uploadError = Update.errorString();
      }
      Serial.printf("Web update: %u bytes, %s\n", (unsigned)up.totalSize, uploadOk ? "ok" : uploadError.c_str());
      break;
    case UPLOAD_FILE_ABORTED:
      Update.abort();
      uploadError = "Upload interrupted.";
      break;
  }
}

static void handleUpdateDone() {
  lastActivity = millis();
  if (!updateAllowed()) {
    if (Update.isRunning()) Update.abort();
    return;
  }
  if (!uploadOk) {
    if (Update.isRunning()) Update.abort();
    server.send(400, "text/html",
                page("<h1>Update failed</h1><div class='err'>" + esc(uploadError.length() ? uploadError : "No file received.") +
                     "</div><p>The current firmware is unchanged. <a href='/update'>Try again</a></p>"));
    return;
  }
  server.send(200, "text/html",
              page(F("<h1>Update installed</h1><p>The display is restarting with the new firmware.</p>")));
  finishSetupAfterUpdate();
  uiMessage("Update installed", "Restarting...");
  restartAt = millis() + 500;
}

// Joins the saved home Wi-Fi alongside the setup access point, so the board
// can also be updated over the home network. Only with settings that worked.
static String joinHomeWifi(const Settings& s, const String& host) {
  if (!s.verified || s.ssid.isEmpty()) return "";
  WiFi.setHostname(host.c_str());
  WiFi.begin(s.ssid.c_str(), s.pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < HOME_WIFI_TIMEOUT_MS) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Home Wi-Fi not reachable, setup network only");
    WiFi.disconnect();
    return "";
  }
  String ip = WiFi.localIP().toString();
  Serial.printf("Home Wi-Fi %s, IP %s\n", s.ssid.c_str(), ip.c_str());
  return ip;
}

// Any unknown URL (including the OS captive-portal probes such as
// /generate_204 and /hotspot-detect.html) is sent to the form.
static void handleRedirect() {
  lastActivity = millis();
  if (server.client().localIP() != AP_IP) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  server.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
  server.send(302, "text/plain", "");
}

static String randomPassword() {
  const char* chars = "abcdefghjkmnpqrstuvwxyz23456789";  // no look-alikes (l/1, o/0)
  String p;
  for (int i = 0; i < 8; i++) p += chars[esp_random() % strlen(chars)];
  return p;
}

void runPortal(Settings& s) {
  cfg = &s;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  String apSsid = String("T5-Weather-") + suffix;
  String host = String("t5-weather-") + suffix;  // DHCP host name on the home Wi-Fi
  host.toLowerCase();
  String apPass = randomPassword();  // also the OTA password

  Serial.println("Entering setup mode");
  scanNetworks();
  // Home Wi-Fi first: the access point then starts on the same channel.
  WiFi.mode(WIFI_AP_STA);
  String homeIp = joinHomeWifi(s, host);
  uiSetupScreen(apSsid, apPass, s.lastError, homeIp);

  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleRedirect);
  server.begin();
  Serial.printf("AP '%s' password '%s' at %s\n", apSsid.c_str(), apPass.c_str(), AP_IP.toString().c_str());
  otaPass = apPass;
  Serial.printf("OTA: http://%s/update, password '%s'\n", homeIp.length() ? homeIp.c_str() : AP_IP.toString().c_str(),
                apPass.c_str());

  lastActivity = millis();
  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    if (restartAt && millis() > restartAt) ESP.restart();

    if (millis() - lastActivity > PORTAL_TIMEOUT_MS) {
      Serial.println("Setup timed out");
      if (s.verified) {
        // Was entered on purpose with working settings: go back to normal.
        s.needsSetup = false;
        config::save(s);
        ESP.restart();
      }
      uiMessage("Setup paused", "Nobody finished the setup.", "Press the button to", "start it again.");
      uiSleep();
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
      esp_deep_sleep_start();
    }
    delay(2);
  }
}
