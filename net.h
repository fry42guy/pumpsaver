#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>

/*
  Network identity and Wi-Fi configuration.

  Kept in its OWN NVS namespace ("net"), not in the pump Settings blob, so
  that bumping the control-settings magic never wipes the credentials that
  put the board on the network -- and so that editing a network field never
  risks resetting a tuned control loop.

  Two rules, for the same reason gEnable is not persisted:

  1. THE AP ALWAYS COMES UP.  A wrong SSID must never strand a board.  If a
     station connection is configured and fails, the AP is running anyway,
     so a technician can walk up to the skid and join FCW-PUMP.

  2. NO CONTROL DECISION DEPENDS ON THE RADIO.  loop() runs whether or not
     Wi-Fi is up.  Networking is for humans and for telemetry; it is never
     in the feedback path.  See ECOSYSTEM.md.

  Radio note: the ESP32 has one radio.  In AP+STA the softAP is forced onto
  the channel of the router the station joins, so AP clients get bumped and
  reconnect when the station associates.  That is normal, not a fault.
*/

#define NET_MAGIC 0x4E455431      // "NET1"

struct NetCfg {
  uint32_t magic = NET_MAGIC;

  // ---- identity.  Used for mDNS, the page header, and node-to-node
  // addressing once there is more than one of these on a site.
  char    node[24] = "pump-1";      // mDNS name -> pump-1.local
  char    role[16] = "pump";        // pump | tank | flow | dosing | gateway
  uint8_t id       = 1;             // 1..250, the last octet when static

  // ---- access point (service access; on by default and meant to stay on)
  bool    apOn       = true;
  char    apSsid[33] = "FCW-PUMP";
  char    apPass[65] = "fullcircle";

  // ---- station (join the site network, or the skid own router)
  bool    staOn     = false;
  char    ssid[33]  = "";
  char    pass[65]  = "";

  bool    useStatic = false;
  uint8_t ip[4]     = {10, 10, 42, 1};
  uint8_t gw[4]     = {10, 10, 42, 254};
  uint8_t mask[4]   = {255, 255, 255, 0};
  uint8_t dns[4]    = {10, 10, 42, 254};
};

NetCfg      N;
Preferences netPrefs;
String      netLog;
bool        mdnsUp = false;

// ---------------------------------------------------------------- helpers
static String quad(const uint8_t *a) {
  return String(a[0]) + "." + String(a[1]) + "." + String(a[2]) + "." + String(a[3]);
}

// Returns false and leaves the target untouched unless all four octets parse
// and are in range -- a half-applied address is worse than the old one.
static bool parseQuad(const String &s, uint8_t *out) {
  int v[4];
  if (sscanf(s.c_str(), "%d.%d.%d.%d", &v[0], &v[1], &v[2], &v[3]) != 4) return false;
  for (int i = 0; i < 4; i++) if (v[i] < 0 || v[i] > 255) return false;
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)v[i];
  return true;
}

static void copyStr(char *dst, size_t cap, const String &s) {
  strncpy(dst, s.c_str(), cap - 1);
  dst[cap - 1] = 0;
}

static String jstr(const char *s) {          // minimal JSON string escaping
  String o = "\"";
  for (const char *p = s; *p; p++) {
    if (*p == '"' || *p == '\\') { o += '\\'; o += *p; }
    else if ((uint8_t)*p < 0x20)  o += ' ';
    else                          o += *p;
  }
  o += "\"";
  return o;
}

// ---------------------------------------------------------------- nvs
void netLoad() {
  NetCfg d;
  netPrefs.begin("net", true);
  if (netPrefs.isKey("blob")) netPrefs.getBytes("blob", &N, sizeof(N));
  netPrefs.end();
  if (N.magic != NET_MAGIC) N = d;
}

void netSave() {
  netPrefs.begin("net", false);
  netPrefs.putBytes("blob", &N, sizeof(N));
  netPrefs.end();
}

// ---------------------------------------------------------------- bring-up
void netStartMdns() {
  if (mdnsUp) MDNS.end();
  mdnsUp = MDNS.begin(N.node);
  if (!mdnsUp) return;
  MDNS.addService("http", "tcp", 80);
  // Advertised so a future hub can enumerate every node and learn what each
  // one is, instead of carrying a hard-coded address list.  See ECOSYSTEM.md.
  // The casts are required: N.role is char[], which makes the char* and the
  // const char* overloads of addServiceTxt equally good matches.
  MDNS.addServiceTxt("http", "tcp", "role", (const char *)N.role);
  MDNS.addServiceTxt("http", "tcp", "node", (const char *)N.node);
  MDNS.addServiceTxt("http", "tcp", "id",   String(N.id).c_str());
}

void netStart() {
  bool wantSta = N.staOn && N.ssid[0];

  // The AP stays up even when a station is configured.  A board reachable
  // only over a network it has failed to join is a board someone has to
  // visit with a laptop and a USB cable.
  if (wantSta && N.apOn)  WiFi.mode(WIFI_AP_STA);
  else if (wantSta)       WiFi.mode(WIFI_STA);
  else                    WiFi.mode(WIFI_AP);

  if (N.apOn || !wantSta) {
    WiFi.softAP(N.apSsid, strlen(N.apPass) >= 8 ? N.apPass : nullptr);
    netLog += "AP " + String(N.apSsid) + " at " + WiFi.softAPIP().toString() + "\n";
  }

  if (wantSta) {
    if (N.useStatic) {
      IPAddress ip(N.ip), gw(N.gw), mask(N.mask), dns(N.dns);
      if (!WiFi.config(ip, gw, mask, dns))
        netLog += "static IP rejected -- falling back to DHCP\n";
    }
    WiFi.setAutoReconnect(true);
    WiFi.begin(N.ssid, N.pass);
    netLog += "joining " + String(N.ssid) + " ...\n";
  }

  netStartMdns();
}

const char *staStateName() {
  switch (WiFi.status()) {
    case WL_CONNECTED:       return "connected";
    case WL_NO_SSID_AVAIL:   return "SSID not found";
    case WL_CONNECT_FAILED:  return "rejected -- check the password";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "connecting";
    case WL_IDLE_STATUS:     return "idle";
    default:                 return "off";
  }
}

// ---------------------------------------------------------------- json
String netJSON() {
  bool sta = N.staOn && N.ssid[0];
  bool up  = WiFi.status() == WL_CONNECTED;
  String j = "{";
  j += "\"node\":"  + jstr(N.node);
  j += ",\"role\":" + jstr(N.role);
  j += ",\"id\":"   + String(N.id);
  j += ",\"mdns\":" + jstr((String(N.node) + ".local").c_str());
  j += ",\"mdnsUp\":"    + String(mdnsUp ? "true" : "false");
  j += ",\"apOn\":"      + String(N.apOn ? "true" : "false");
  j += ",\"apSsid\":"    + jstr(N.apSsid);
  j += ",\"apPassSet\":" + String(strlen(N.apPass) >= 8 ? "true" : "false");
  j += ",\"apIp\":\""    + WiFi.softAPIP().toString() + "\"";
  j += ",\"apClients\":" + String(WiFi.softAPgetStationNum());
  j += ",\"staOn\":"     + String(N.staOn ? "true" : "false");
  j += ",\"ssid\":"      + jstr(N.ssid);
  j += ",\"passSet\":"   + String(N.pass[0] ? "true" : "false");
  j += ",\"staState\":"  + jstr(sta ? staStateName() : "off");
  j += ",\"staUp\":"     + String(up ? "true" : "false");
  j += ",\"staIp\":\""   + (up ? WiFi.localIP().toString() : String("--")) + "\"";
  j += ",\"rssi\":"      + String(up ? WiFi.RSSI() : 0);
  j += ",\"ch\":"        + String(WiFi.channel());
  j += ",\"useStatic\":" + String(N.useStatic ? "true" : "false");
  j += ",\"ip\":\""      + quad(N.ip)   + "\"";
  j += ",\"gw\":\""      + quad(N.gw)   + "\"";
  j += ",\"mask\":\""    + quad(N.mask) + "\"";
  j += ",\"dns\":\""     + quad(N.dns)  + "\"";
  j += ",\"mac\":\""     + WiFi.macAddress() + "\"";
  j += ",\"log\":"       + jstr(netLog.c_str());
  j += "}";
  return j;
}

// Async scan: /wifi/scan?start=1 kicks one off, bare /wifi/scan collects it.
// A blocking scan takes 2-4 s, which would stall the control tick.
String netScanJSON(bool start) {
  if (start) {
    if (WiFi.scanComplete() != -1) { WiFi.scanDelete(); WiFi.scanNetworks(true); }
    return "{\"scanning\":true,\"nets\":[]}";
  }
  int n = WiFi.scanComplete();
  if (n == -1) return "{\"scanning\":true,\"nets\":[]}";
  if (n <  0)  return "{\"scanning\":false,\"nets\":[]}";
  String j = "{\"scanning\":false,\"nets\":[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i) j += ",";
    j += "{\"ssid\":" + jstr(WiFi.SSID(i).c_str());
    j += ",\"rssi\":" + String(WiFi.RSSI(i));
    j += ",\"ch\":"   + String(WiFi.channel(i));
    j += ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]}";
  return j;
}
