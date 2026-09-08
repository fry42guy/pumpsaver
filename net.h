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
#include <stdarg.h>
#include <esp_mac.h>          // esp_read_mac -- see netDeviceId

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

  /* ---- appended in 0.14.0 -- still APPEND ONLY ---------------------------
     pubMask picks which telemetry groups go out.  Default all-on, because a
     board that quietly stopped publishing a field a dashboard depends on is
     worse than one that publishes a field nobody charted.  Turning groups OFF
     is the deliberate act.

     tbMode switches the topic to ThingsBoard's fixed `v1/devices/me/telemetry`
     and ignores the base topic.  ThingsBoard does not accept arbitrary topics
     -- it routes purely on the access token in the MQTT username -- so without
     this the broker connects, accepts every publish, and shows no data, with
     nothing anywhere reporting an error.                                   */
  uint32_t pubMask = 0xFFFFFFFFu;
  bool     tbMode  = false;
};

// ---- telemetry groups.  Bit numbers are part of the saved config: append
// new ones at the end, never renumber, or a saved mask selects the wrong set.
enum : uint32_t {
  PUB_PRESSURE = 1u << 0,   // psi, sp, psiValid
  PUB_SPEED    = 1u << 1,   // hz, cap, shutoff
  PUB_FLOW     = 1u << 2,   // flowEst  (estimate, not a measurement)
  PUB_STATE    = 1u << 3,   // state, sleep, enable
  PUB_PUMPS    = 1u << 4,   // lead, lag, comms
  PUB_AMPS     = 1u << 5,   // a1, a2
  PUB_TEMPS    = 1u << 6,   // t1, t2  (drive heatsink)
  PUB_AI       = 1u << 7,   // ai1 raw counts
  PUB_TRIPS    = 1u << 8,   // st1/st2 status words, trip1/trip2
  PUB_HEALTH   = 1u << 9,   // up, fw, rssi, cycles
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
  // ---- 0.14.0: what the drives report, so a dashboard can trend the motor
  // and not just the loop.  amps is the one that predicts a failure.
  float    a1 = 0, a2 = 0;          // motor current per drive, reg 8
  float    t1 = 0, t2 = 0;          // heatsink degC per drive, reg 24
  uint16_t ai1 = 0;                 // lead drive AI1 counts, reg 20
  uint16_t st1 = 0, st2 = 0;        // raw status words, reg 6
  uint8_t  trip1 = 0, trip2 = 0;    // decoded trip codes
  uint32_t cycles = 0;              // sleep cycles -- short-cycling shows here
  char     stName[12] = "";         // "Regulating" reads better on a dashboard
};                                  // than "2", and costs 20 bytes of payload

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
// Read from efuse, NOT via WiFi.macAddress().  netBegin() calls this before
// WiFi.softAP(), and until the WiFi stack is up macAddress() hands back six
// zero bytes -- so every board in the field came up as "000000".  That is the
// MQTT client id AND the telemetry topic AND the mDNS unit TXT, so two boards
// on one broker would take turns evicting each other forever, each looking
// perfectly healthy on its own screen.  esp_read_mac() works before init.
static void netDeviceId() {
  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);
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
  // Read-back, not decoration: the form POSTs these, so it has to be able to
  // load them or saving writes a zero mask and the board stops publishing.
  j += ",\"pubMask\":" + String(gNet.pubMask);
  j += ",\"tbMode\":" + String(gNet.tbMode ? "true" : "false");
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
// Bounded append.  snprintf returns what it WOULD have written, so adding its
// return value straight onto an offset walks past the end of the buffer on
// truncation and every later write lands outside it.  Clamp instead.
static int jput(char *b, int cap, int n, const char *fmt, ...) {
  if (n >= cap - 1) return n;
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(b + n, cap - n, fmt, ap);
  va_end(ap);
  if (w < 0) return n;
  n += w;
  return (n > cap - 1) ? cap - 1 : n;
}

#define JB(x) ((x) ? "true" : "false")

static void netPublish() {
  NetTelem t;
  portENTER_CRITICAL(&gTelemMux);
  t = gTelem;
  portEXIT_CRITICAL(&gTelemMux);

  const uint32_t m = gNet.pubMask;
  char topic[80], p[720];
  const int cap = sizeof(p);
  int n = 0;

  // ThingsBoard routes on the access token in the MQTT username and ignores
  // the topic entirely -- but only accepts this one.  Publish elsewhere and it
  // connects, ACKs every message and charts nothing, silently.
  if (gNet.tbMode) snprintf(topic, sizeof(topic), "v1/devices/me/telemetry");
  else             snprintf(topic, sizeof(topic), "%s/%s/telemetry", gNet.topic, gDevId);

  p[n++] = '{';
  #define SEP (n > 1 ? "," : "")

  if (m & PUB_PRESSURE)
    n = jput(p, cap, n, "%s\"psi\":%.2f,\"sp\":%.2f,\"psiValid\":%s",
             SEP, t.psi, t.spAct, JB(t.psiValid));
  if (m & PUB_SPEED)
    n = jput(p, cap, n, "%s\"hz\":%.2f,\"cap\":%.2f,\"shutoff\":%.2f",
             SEP, t.hzCmd, t.cap, t.shutoff);
  // flowEst, not flow.  There is no meter: this is the affinity-law curve
  // solved backwards, and naming it as a measurement invites someone to bill
  // off it.  See the note in netStatusJSON's caller.
  if (m & PUB_FLOW)
    n = jput(p, cap, n, "%s\"flowEst\":%.1f", SEP, t.flow);
  if (m & PUB_STATE)
    n = jput(p, cap, n, "%s\"state\":%d,\"stateName\":\"%s\",\"sleep\":%d,\"enable\":%s",
             SEP, t.state, t.stName, t.sleepStage, JB(t.enable));
  if (m & PUB_PUMPS)
    n = jput(p, cap, n, "%s\"lead\":%s,\"lag\":%s,\"comms\":%s",
             SEP, JB(t.runLead), JB(t.runLag), JB(t.commsOK));
  if (m & PUB_AMPS)
    n = jput(p, cap, n, "%s\"a1\":%.1f,\"a2\":%.1f", SEP, t.a1, t.a2);
  if (m & PUB_TEMPS)
    n = jput(p, cap, n, "%s\"t1\":%.0f,\"t2\":%.0f", SEP, t.t1, t.t2);
  if (m & PUB_AI)
    n = jput(p, cap, n, "%s\"ai1\":%u", SEP, t.ai1);
  if (m & PUB_TRIPS)
    n = jput(p, cap, n, "%s\"st1\":%u,\"st2\":%u,\"trip1\":%u,\"trip2\":%u",
             SEP, t.st1, t.st2, t.trip1, t.trip2);
  // RSSI is read here rather than carried through NetTelem: this runs on the
  // net task, which is the only place WiFi calls are allowed.
  if (m & PUB_HEALTH)
    n = jput(p, cap, n, "%s\"up\":%u,\"cycles\":%u,\"rssi\":%d,\"fw\":\"%s\"",
             SEP, t.upSec, t.cycles,
             (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0, FW_VERSION_STR);

  n = jput(p, cap, n, "}");
  #undef SEP
  gMqtt.publish(topic, p);
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
        gMqtt.setBufferSize(1024);   // selective payload can reach ~700 B
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
