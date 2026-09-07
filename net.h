#pragma once

/*
  Network layer -- station WiFi + MQTT telemetry.

  Three rules shape this file.

  1. Network settings live in their OWN NVS namespace ("psnet"), not in the
     Settings blob. That blob is a raw byte copy keyed on a magic number, so
     adding a field to it resets every pump tuning value on the next boot.
     Broker moves and SSID changes are routine; cap tables and gains are not.
     Different lifetimes, different storage.

  2. Nothing here runs in the control tick. MQTT connects block -- a broker
     that is unreachable is normal on a customer network, and a 100 ms tick
     cannot absorb a socket timeout. All of it lives on its own FreeRTOS task
     pinned to core 0, which is also where ROADMAP Phase 2 wants Modbus.

  3. WiFi calls happen on that task only. The web handlers run in the AsyncTCP
     task; calling WiFi.begin() from there while the net task is mid-reconnect
     is a real hazard. Handlers stage a change and raise a flag; the task
     applies it.

  Telemetry is publish-only. PumpSaver does not subscribe, so nothing on a
  customer's network can start, stop, or re-tune a pump. Adding a command
  topic later is a deliberate decision, not an accident of this file.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>

// ---------------------------------------------------------------- config
struct NetCfg {
  uint32_t magic   = 0x50534E54;      // 'PSNT' -- bump if this layout changes
  char     ssid[33]  = "";
  char     pass[65]  = "";
  bool     mqttOn    = false;
  char     host[64]  = "";
  uint16_t port      = 1883;
  char     user[33]  = "";
  char     mpass[65] = "";
  char     topic[40] = "fcw/pumpsaver";
  uint16_t pubMs     = 5000;

  /* ---- appended in 0.10.0 -------------------------------------------------
     APPEND ONLY, and do not reorder what is above.  The blob is a raw byte
     copy, and Preferences::getBytes fills only as many bytes as were stored,
     leaving the rest at the defaults written here.  So new fields at the END
     read as their defaults on a board that was configured by an older build,
     and the magic does not have to change -- which means nobody has to retype
     an SSID to take this update.  Insert a field in the middle instead and
     every value after it is silently garbage.                              */
  bool     useStatic = false;          // false = DHCP
  uint8_t  ip[4]     = {0, 0, 0, 0};   // no opinionated default: a guessed
  uint8_t  gw[4]     = {0, 0, 0, 0};   // subnet that half-matches the site
  uint8_t  mask[4]   = {255, 255, 255, 0};  // is worse than an empty box
  uint8_t  dns[4]    = {0, 0, 0, 0};

  char     apSsid[33] = "FCW-PUMP";    // was compiled in before 0.10.0
  char     apPass[65] = "fullcircle";
  char     host_name[24] = "pumpsaver";     // mDNS: pumpsaver.local
  char     label[24]     = "";              // free text, shown in the header
};

// ---------------------------------------------------------------- telemetry
// Written by the control tick, read by the net task. Values are single 32-bit
// words; a torn read would cost one stale sample on a telemetry topic, so the
// copy is taken under a spinlock rather than a mutex to keep the tick cheap.
struct NetTelem {
  float    psi = 0, spAct = 0, hzCmd = 0, cap = 0, shutoff = 0, flow = 0;
  int      state = 0, sleepStage = 0;
  bool     enable = false, runLead = false, runLag = false;
  bool     commsOK = false, psiValid = false;
  uint32_t upSec = 0;
};

// ---------------------------------------------------------------- state
extern const char *FW_VERSION_STR;          // set by the .ino

static NetCfg      gNet;
static NetCfg      gNetPending;
static volatile bool gNetApply = false;     // handler -> task
static volatile bool gScanReq  = false;

static NetTelem    gTelem;
static portMUX_TYPE gTelemMux = portMUX_INITIALIZER_UNLOCKED;

static WiFiClient   gWifiClient;
static PubSubClient gMqtt(gWifiClient);
static Preferences  gNetPrefs;

static char     gDevId[16]   = "";
static uint32_t gMqttFails   = 0;
static uint32_t gLastPubMs   = 0;
static String   gNetLog      = "";
static bool     gMdnsUp      = false;

// ---------------------------------------------------------------- addresses
static String netQuad(const uint8_t *a) {
  return String(a[0]) + "." + String(a[1]) + "." + String(a[2]) + "." + String(a[3]);
}

// Leaves the target untouched unless all four octets parse and are in range.
// A half-applied address is worse than the old one, and "10.79.262.4" must be
// refused rather than truncated into something that looks plausible.
static bool netParseQuad(const String &s, uint8_t *out) {
  int v[4];
  if (sscanf(s.c_str(), "%d.%d.%d.%d", &v[0], &v[1], &v[2], &v[3]) != 4) return false;
  for (int i = 0; i < 4; i++) if (v[i] < 0 || v[i] > 255) return false;
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)v[i];
  return true;
}

// mDNS labels are hostnames: fold to [a-z0-9-] or it resolves for nobody.
static String netSanitizeHost(const String &in) {
  String o;
  for (size_t i = 0; i < in.length() && o.length() < 22; i++) {
    char c = in[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c;
    else if ((c == '-' || c == ' ' || c == '_') && o.length() && o[o.length() - 1] != '-') o += '-';
  }
  while (o.length() && o[o.length() - 1] == '-') o.remove(o.length() - 1);
  return o.length() ? o : String("pumpsaver");
}

static void netStartMdns() {
  if (gMdnsUp) MDNS.end();
  gMdnsUp = MDNS.begin(gNet.host_name);
  if (!gMdnsUp) return;
  MDNS.addService("http", "tcp", 80);
  // Advertised so a future hub can enumerate units without a hard-coded
  // address list.  The casts are required -- these are char[], which makes
  // the char* and const char* overloads equally good matches.
  MDNS.addServiceTxt("http", "tcp", "unit",  (const char *)gDevId);
  MDNS.addServiceTxt("http", "tcp", "label", (const char *)gNet.label);
}

// Static addressing is applied on the net task, never from a web handler --
// same rule as WiFi.begin().  All-zero address means "not configured", which
// falls back to DHCP rather than trying to claim 0.0.0.0.
static void netApplyIp() {
  if (gNet.useStatic && (gNet.ip[0] || gNet.ip[1] || gNet.ip[2] || gNet.ip[3])) {
    IPAddress ip(gNet.ip), gw(gNet.gw), mask(gNet.mask), dns(gNet.dns);
    if (!WiFi.config(ip, gw, mask, dns))
      gNetLog = "Static IP rejected -- falling back to DHCP.";
  } else {
    WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
  }
}

// ---------------------------------------------------------------- helpers
static void netDeviceId() {
  uint8_t m[6];
  WiFi.macAddress(m);
  snprintf(gDevId, sizeof(gDevId), "%02X%02X%02X", m[3], m[4], m[5]);
}

void netLoad() {
  NetCfg d;
  gNetPrefs.begin("psnet", true);
  if (gNetPrefs.isKey("blob")) gNetPrefs.getBytes("blob", &gNet, sizeof(gNet));
  gNetPrefs.end();
  if (gNet.magic != d.magic) gNet = d;
}

static void netSave() {
  gNetPrefs.begin("psnet", false);
  gNetPrefs.putBytes("blob", &gNet, sizeof(gNet));
  gNetPrefs.end();
}

// Called from the control tick.
void netPushTelem(const NetTelem &t) {
  portENTER_CRITICAL(&gTelemMux);
  gTelem = t;
  portEXIT_CRITICAL(&gTelemMux);
}

// ---------------------------------------------------------------- status json
String netStatusJSON() {
  NetTelem t;
  portENTER_CRITICAL(&gTelemMux);
  t = gTelem;
  portEXIT_CRITICAL(&gTelemMux);

  bool sta = (WiFi.status() == WL_CONNECTED);
  String j = "{";
  j += "\"id\":\"" + String(gDevId) + "\"";
  j += ",\"ssid\":\"" + String(gNet.ssid) + "\"";
  j += ",\"sta\":" + String(sta ? "true" : "false");
  j += ",\"ip\":\"" + (sta ? WiFi.localIP().toString() : String("")) + "\"";
  j += ",\"rssi\":" + String(sta ? WiFi.RSSI() : 0);
  j += ",\"apip\":\"" + WiFi.softAPIP().toString() + "\"";
  j += ",\"mqttOn\":" + String(gNet.mqttOn ? "true" : "false");
  j += ",\"mqtt\":" + String(gMqtt.connected() ? "true" : "false");
  j += ",\"mqttFails\":" + String(gMqttFails);
  j += ",\"host\":\"" + String(gNet.host) + "\"";
  j += ",\"port\":" + String(gNet.port);
  j += ",\"user\":\"" + String(gNet.user) + "\"";
  j += ",\"topic\":\"" + String(gNet.topic) + "\"";
  j += ",\"pubMs\":" + String(gNet.pubMs);
  j += ",\"useStatic\":" + String(gNet.useStatic ? "true" : "false");
  j += ",\"sip\":\""  + netQuad(gNet.ip)   + "\"";
  j += ",\"gw\":\""   + netQuad(gNet.gw)   + "\"";
  j += ",\"mask\":\"" + netQuad(gNet.mask) + "\"";
  j += ",\"dns\":\""  + netQuad(gNet.dns)  + "\"";
  j += ",\"apSsid\":\"" + String(gNet.apSsid) + "\"";
  j += ",\"apClients\":" + String(WiFi.softAPgetStationNum());
  j += ",\"hostName\":\"" + String(gNet.host_name) + "\"";
  j += ",\"mdns\":\"" + String(gNet.host_name) + ".local\"";
  j += ",\"mdnsUp\":" + String(gMdnsUp ? "true" : "false");
  j += ",\"label\":\"" + String(gNet.label) + "\"";
  j += ",\"mac\":\"" + WiFi.macAddress() + "\"";
  j += ",\"ch\":" + String(WiFi.channel());
  j += ",\"passSet\":"  + String(gNet.pass[0]  ? "true" : "false");
  j += ",\"mpassSet\":" + String(gNet.mpass[0] ? "true" : "false");
  j += ",\"log\":\"" + gNetLog + "\"";
  j += "}";                                  // passwords are never sent out
  return j;
}

// ---------------------------------------------------------------- scan
// Scanning briefly interrupts the softAP: one radio, and it has to leave the
// AP channel to sweep. Expected, and why the scan is on demand rather than
// on a timer.
String netScanJSON() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true, false); return "{\"state\":\"scanning\"}"; }
  if (n == WIFI_SCAN_RUNNING) return "{\"state\":\"scanning\"}";

  String j = "{\"state\":\"done\",\"nets\":[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i) j += ",";
    String s = WiFi.SSID(i);
    s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
    j += "{\"ssid\":\"" + s + "\"";
    j += ",\"rssi\":" + String(WiFi.RSSI(i));
    j += ",\"lock\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
    j += "}";
  }
  j += "]}";
  WiFi.scanDelete();
  return j;
}

// ---------------------------------------------------------------- publish
static void netPublish() {
  NetTelem t;
  portENTER_CRITICAL(&gTelemMux);
  t = gTelem;
  portEXIT_CRITICAL(&gTelemMux);

  char topic[80], payload[420];
  snprintf(topic, sizeof(topic), "%s/%s/telemetry", gNet.topic, gDevId);
  snprintf(payload, sizeof(payload),
    "{\"psi\":%.2f,\"sp\":%.2f,\"hz\":%.2f,\"cap\":%.2f,\"shutoff\":%.2f,"
    "\"flow\":%.1f,\"state\":%d,\"sleep\":%d,\"enable\":%s,\"lead\":%s,"
    "\"lag\":%s,\"comms\":%s,\"psiValid\":%s,\"up\":%u,\"fw\":\"%s\"}",
    t.psi, t.spAct, t.hzCmd, t.cap, t.shutoff, t.flow, t.state, t.sleepStage,
    t.enable ? "true" : "false", t.runLead ? "true" : "false",
    t.runLag ? "true" : "false", t.commsOK ? "true" : "false",
    t.psiValid ? "true" : "false", t.upSec, FW_VERSION_STR);

  gMqtt.publish(topic, payload);
}

// ---------------------------------------------------------------- the task
static void netTask(void *) {
  uint32_t nextWifiTry = 0, nextMqttTry = 0;
  bool     staWasUp = false;

  for (;;) {
    uint32_t now = millis();

    // ---- a staged change from the web handler ----
    if (gNetApply) {
      gNetApply = false;
      gNet = gNetPending;
      netSave();
      WiFi.disconnect(false, false);
      netApplyIp();                       // must precede begin() to take effect
      if (gNet.ssid[0]) WiFi.begin(gNet.ssid, gNet.pass);
      if (gMqtt.connected()) gMqtt.disconnect();
      gMqttFails = 0;
      nextWifiTry = now + 8000;
      nextMqttTry = 0;
      netStartMdns();                     // the host name may have changed
      gNetLog = "Network settings applied.";
    }

    // mDNS binds to an interface, so it has to be restarted once the station
    // actually has an address -- otherwise the name only answers on the AP.
    bool up = (WiFi.status() == WL_CONNECTED);
    if (up != staWasUp) { staWasUp = up; if (up) netStartMdns(); }

    // ---- station: reconnect with a slow retry, never a blocking wait ----
    if (gNet.ssid[0] && WiFi.status() != WL_CONNECTED && now >= nextWifiTry) {
      WiFi.begin(gNet.ssid, gNet.pass);
      nextWifiTry = now + 15000;
    }

    // ---- mqtt ----
    if (gNet.mqttOn && gNet.host[0] && WiFi.status() == WL_CONNECTED) {
      if (!gMqtt.connected() && now >= nextMqttTry) {
        gMqtt.setServer(gNet.host, gNet.port);
        gMqtt.setBufferSize(512);
        gMqtt.setSocketTimeout(4);

        char willTopic[80];
        snprintf(willTopic, sizeof(willTopic), "%s/%s/status", gNet.topic, gDevId);

        bool ok = gNet.user[0]
          ? gMqtt.connect(gDevId, gNet.user, gNet.mpass, willTopic, 0, true, "offline")
          : gMqtt.connect(gDevId, willTopic, 0, true, "offline");

        if (ok) {
          gMqtt.publish(willTopic, "online", true);
          gMqttFails = 0;
          gNetLog = "MQTT connected.";
        } else {
          gMqttFails++;
          // back off 2 s, 4 s, 8 s ... capped at 60 s
          uint32_t wait = 2000u << (gMqttFails > 5 ? 5 : gMqttFails - 1);
          if (wait > 60000u) wait = 60000u;
          nextMqttTry = now + wait;
          gNetLog = "MQTT connect failed (rc " + String(gMqtt.state()) + ").";
        }
      }

      if (gMqtt.connected()) {
        gMqtt.loop();
        if (now - gLastPubMs >= gNet.pubMs) { gLastPubMs = now; netPublish(); }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ---------------------------------------------------------------- api
// Owns radio bring-up as of 0.10.0: the AP SSID and password come out of NVS
// now rather than being compiled in, so the .ino cannot raise the AP before
// this has loaded the config.  Call it with the mode already set to AP_STA.
//
// The AP always comes up, even with a station configured.  A wrong SSID must
// never strand a board -- the same reasoning that keeps gEnable out of NVS.
// One radio means the softAP is dragged onto the station's channel when it
// associates, so AP clients get bumped once and reconnect.  Expected.
void netBegin() {
  netDeviceId();
  netLoad();

  WiFi.softAP(gNet.apSsid, strlen(gNet.apPass) >= 8 ? gNet.apPass : nullptr);
  gNetLog = "AP " + String(gNet.apSsid) + " at " + WiFi.softAPIP().toString();

  netApplyIp();
  if (gNet.ssid[0]) WiFi.begin(gNet.ssid, gNet.pass);
  WiFi.setAutoReconnect(true);
  netStartMdns();
  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 1, nullptr, 0);
}

// Called from a web handler. Stages only -- the task applies it.
void netStage(const NetCfg &c) {
  gNetPending = c;
  gNetPending.magic = NetCfg().magic;
  gNetApply = true;
}

NetCfg netCurrent() { return gNet; }
