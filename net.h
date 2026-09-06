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

  for (;;) {
    uint32_t now = millis();

    // ---- a staged change from the web handler ----
    if (gNetApply) {
      gNetApply = false;
      gNet = gNetPending;
      netSave();
      WiFi.disconnect(false, false);
      if (gNet.ssid[0]) WiFi.begin(gNet.ssid, gNet.pass);
      if (gMqtt.connected()) gMqtt.disconnect();
      gMqttFails = 0;
      nextWifiTry = now + 8000;
      nextMqttTry = 0;
      gNetLog = "Network settings applied.";
    }

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
// Call after WiFi.mode(WIFI_AP_STA) and softAP() are up.
void netBegin() {
  netDeviceId();
  netLoad();
  if (gNet.ssid[0]) WiFi.begin(gNet.ssid, gNet.pass);
  WiFi.setAutoReconnect(true);
  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 1, nullptr, 0);
}

// Called from a web handler. Stages only -- the task applies it.
void netStage(const NetCfg &c) {
  gNetPending = c;
  gNetPending.magic = NetCfg().magic;
  gNetApply = true;
}

NetCfg netCurrent() { return gNet; }
